// Kernels that ask questions: R's readline() and Python's input() are answered at
// your terminal. Run it (Node >= 22.13, in a project with "type": "module"):
//   npx tsx ask-for-input.ts
import { createInterface } from 'node:readline';
import { SessionManager, type Session } from '@damurka/jovian';

const manager = new SessionManager();

// One line at a time from the terminal. (Not readline's question(): lines that
// arrive before it is called are lost, which matters when input is piped in.)
const lines = createInterface({ input: process.stdin })[Symbol.asyncIterator]();
async function ask(prompt: string): Promise<string> {
    process.stdout.write(prompt);
    const next = await lines.next();
    return next.done ? '' : next.value;
}

// Output arrives in pieces, not lines: label only the start of each line.
function labelled(label: string, stream: NodeJS.WriteStream): (text: string) => void {
    let atLineStart = true;
    return (text) => {
        for (const part of text.split(/(?<=\n)/)) {
            stream.write(atLineStart ? `[${label}] ${part}` : part);
            atLineStart = part.endsWith('\n');
        }
    };
}

// Wire a session to the terminal: what it prints, and every question it asks.
function attach(session: Session, label: string): void {
    session.on('stdout', labelled(label, process.stdout));
    session.on('stderr', labelled(label, process.stderr));
    session.on('error', () => {}); // the failure is also in the execute() result; without a listener Node would throw
    session.on('input_request', async ({ prompt }: { prompt: string }) => {
        session.sendInputReply(await ask(`[${label}] ${prompt}`));
    });
}

// What the last expression evaluated to, as text.
function valueOf(result: Awaited<ReturnType<Session['execute']>>): string | undefined {
    return result.output.find((message) => message.msgType === 'execute_result')?.content?.data?.['text/plain'];
}

try {
    // --- R: readline() -------------------------------------------------------
    const r = await manager.createSession({ kernelType: 'r' });
    attach(r, 'R');

    const rResult = await r.execute(
        [
            'name <- readline("Your name? ")',
            'n <- as.numeric(readline("A number? "))',
            'print(paste("Hello,", name, "- the square root of", n, "is", round(sqrt(n), 3)))',
            'mean(seq_len(n))'
        ].join('\n'),
        { allowStdin: true } // required for every execute() that may ask a question
    );
    console.log('R finished:', rResult.success, '- mean of 1..n =', valueOf(rResult));

    // --- Python: input(), several questions, state kept between calls --------
    const py = await manager.createSession({ kernelType: 'python' });
    attach(py, 'Python');

    await py.execute('history = []');
    for (let round = 1; round <= 2; round++) {
        const pyResult = await py.execute(
            [
                'numbers = [int(x) for x in input("Some whole numbers, separated by spaces: ").split()]',
                'history.append(sum(numbers))',
                'print("sum:", sum(numbers), "- so far:", history)',
                'sum(history)'
            ].join('\n'),
            { allowStdin: true }
        );
        console.log(`Python round ${round}:`, pyResult.success, '- total so far =', valueOf(pyResult));
    }

    // --- Without allowStdin a question fails at once instead of hanging ------
    const refused = await py.execute('input("this cannot be answered: ")');
    console.log('without allowStdin:', refused.success ? 'ran' : 'failed fast, as it should');
} finally {
    await manager.stopAll();
    process.stdin.destroy();
}
