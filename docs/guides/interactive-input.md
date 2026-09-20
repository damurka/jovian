# Interactive input

`input()` (Python) and `readline()` / `readLines(stdin())` (R) block the kernel until somebody answers. Jovian carries the question over the Jupyter **stdin channel** so your application can ask its user and reply.

## Opt in per call

```typescript
session.on('error', () => {});
session.on('input_request', ({ prompt, password }) => {
    session.sendInputReply(askTheUser(prompt, password));   // sync or async — reply whenever you have it
});

await session.execute('name = input("name? ")\nprint("hello", name)', { allowStdin: true });
```

- `allowStdin: true` is required **per `execute()` call** (it maps to the request's `allow_stdin`). The kernel then sends an `input_request` on the stdin channel; the session emits it as the `'input_request'` event with `{ prompt, password }` (`password` is `false` for `input()` / `readline()` today).
- `sendInputReply(value)` sends the `input_reply` and unblocks the kernel. It is fire-and-forget; the result of the answer is the still-running `execute()` finishing.
- The `input_request` is also a `'message'` event (`msgType: 'input_request'`, `channel: 'stdin'`).

## Without `allowStdin`

A blocking read fails fast instead of hanging the kernel forever waiting for an answer nobody will give. The kernel raises this error, whose text is:

> This execution didn't allow interactive input (allow_stdin was false) -- the caller needs to opt in (e.g. execute(code, { allowStdin: true })) and be ready to answer an input_request for a blocking read like this to work.

In Python it surfaces as a `RuntimeError` (so `execute()` resolves with `success: false`); in R it is reported on stderr (prefixed `input:`) and the read gets no input; the execution does not hang.

## Timeouts

The execution's `timeout` (default 30 s) is **cleared** the moment an `input_request` arrives — a slow human cannot trip it. Consequences:

- If you never answer, the execution waits forever. Give your own UI a cancel path; if you want to abandon the kernel, `restart()` it.
- While the kernel waits for input, `interrupt()` cannot break the read (the kernel thread is blocked in a ZMQ receive). Answer it, then interrupt if needed.

## R on Windows

R's `readline()` on Windows reaches the stdin channel only because Elara starts R with its own `ReadConsole` callback (R ≥ 4.2). On older R the call cannot be answered — see [Kernels](../kernels.md#elara-r).

## Multiple prompts

Each `input()` / `readline()` produces its own `input_request`; answer them in order:

```typescript
const answers = ['5', '7'];
let i = 0;
session.on('input_request', () => session.sendInputReply(answers[i++]));
await session.execute('a = float(input("first: "))\nb = float(input("second: "))\nprint(a + b)', { allowStdin: true });
```
