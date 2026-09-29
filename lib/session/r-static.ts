// Reading R code without running it: the names it defines and the packages it
// attaches. R's own completer only knows what exists in the session, so this
// covers what it cannot -- names defined earlier in the cell being typed (not
// run yet), and, while a cell runs, what that cell defines and attaches (the
// helper R process of r-helper.ts attaches those packages; completion offers
// those names). It reads the code, so it can be wrong where R's evaluation
// decides (a name assigned only in a branch not taken, eval(parse(...))) --
// completion is the only use, where an extra suggestion is harmless.

type TokenKind = 'name' | 'string' | 'op' | 'open' | 'close' | 'other';

interface Token {
    kind: TokenKind;
    text: string;
}

const NAME_START = /[A-Za-z._À-￿]/;
const NAME_CHAR = /[A-Za-z0-9._À-￿]/;
const RESERVED = new Set([
    'if', 'else', 'repeat', 'while', 'function', 'for', 'in', 'next', 'break',
    'TRUE', 'FALSE', 'NULL', 'Inf', 'NaN', 'NA', 'NA_integer_', 'NA_real_', 'NA_character_', 'NA_complex_'
]);
// Longest first, so "<<-" wins over "<-" and "<".
const OPERATORS = ['<<-', '->>', '|>', '<-', '->', '==', '!=', '<=', '>=', '&&', '||', '::', ':::', '$', '@', '=', ',', ';', '~', '+', '-', '*', '/', '^', '<', '>', '!', '&', '|', ':', '?'];

/** R code as tokens, without comments and whitespace. Never throws. */
function tokenize(code: string): Token[] {
    const tokens: Token[] = [];
    const chars = [...code];
    let i = 0;
    while (i < chars.length) {
        const c = chars[i]!;
        if (c === '#') {
            while (i < chars.length && chars[i] !== '\n') i++;
            continue;
        }
        if (/\s/.test(c)) {
            i++;
            continue;
        }
        // Raw strings: r"(...)", R"[...]", r"---{...}---"
        if ((c === 'r' || c === 'R') && (chars[i + 1] === '"' || chars[i + 1] === "'")) {
            const quote = chars[i + 1]!;
            let j = i + 2;
            let dashes = '';
            while (chars[j] === '-') { dashes += '-'; j++; }
            const open = chars[j];
            const close = open === '(' ? ')' : open === '[' ? ']' : open === '{' ? '}' : undefined;
            if (close) {
                const end = `${close}${dashes}${quote}`;
                const rest = chars.slice(j + 1).join('');
                const at = rest.indexOf(end);
                const body = at < 0 ? rest : rest.slice(0, at);
                tokens.push({ kind: 'string', text: body });
                i = at < 0 ? chars.length : j + 1 + [...rest.slice(0, at)].length + [...end].length;
                continue;
            }
        }
        if (c === '"' || c === "'" || c === '`') {
            let j = i + 1;
            let text = '';
            while (j < chars.length && chars[j] !== c) {
                if (chars[j] === '\\' && j + 1 < chars.length) {
                    text += chars[j + 1];
                    j += 2;
                    continue;
                }
                text += chars[j];
                j++;
            }
            // A backtick-quoted name is a name (`my var`).
            tokens.push({ kind: c === '`' ? 'name' : 'string', text });
            i = j + 1;
            continue;
        }
        if (NAME_START.test(c) && !(c === '.' && /[0-9]/.test(chars[i + 1] ?? ''))) {
            let text = '';
            while (i < chars.length && NAME_CHAR.test(chars[i]!)) text += chars[i++];
            tokens.push({ kind: 'name', text });
            continue;
        }
        if (/[0-9]/.test(c) || (c === '.' && /[0-9]/.test(chars[i + 1] ?? ''))) {
            while (i < chars.length && /[0-9A-Za-z._]/.test(chars[i]!)) i++;
            tokens.push({ kind: 'other', text: '0' });
            continue;
        }
        if ('([{'.includes(c)) { tokens.push({ kind: 'open', text: c }); i++; continue; }
        if (')]}'.includes(c)) { tokens.push({ kind: 'close', text: c }); i++; continue; }
        const rest = chars.slice(i, i + 3).join('');
        const op = OPERATORS.find((o) => rest.startsWith(o));
        if (op) {
            tokens.push({ kind: 'op', text: op });
            i += op.length;
            continue;
        }
        tokens.push({ kind: 'other', text: c });
        i++;
    }
    return tokens;
}

export interface RCodeFacts {
    /** Names the code assigns or defines, in order of first appearance. */
    defines: string[];
    /** Packages it attaches with library() / require() / requireNamespace-free forms. */
    packages: string[];
}

/** What `code` defines and attaches, read without running it. */
export function analyzeR(code: string): RCodeFacts {
    const tokens = tokenize(code);
    const defines: string[] = [];
    const packages: string[] = [];
    const define = (name: string | undefined) => {
        // Not `...` and `..1`: a function's dots, not names to complete.
        if (name && !RESERVED.has(name) && !/^\.\.(\.|\d+)$/.test(name) && !defines.includes(name)) defines.push(name);
    };

    // Bracket depth per position: `=` assigns only outside parentheses and
    // square brackets (inside them it names an argument); braces are blocks.
    const stack: string[] = [];
    for (let i = 0; i < tokens.length; i++) {
        const t = tokens[i]!;
        const next = tokens[i + 1];
        const prev = tokens[i - 1];
        const inCall = stack.some((open) => open === '(' || open === '[');

        if (t.kind === 'open') stack.push(t.text);
        else if (t.kind === 'close') stack.pop();

        if (t.kind === 'name' || (t.kind === 'string' && next?.kind === 'op')) {
            // x <- ...   x <<- ...   x = ... (not an argument)   "x" <- ...
            const isTarget = next?.kind === 'op' && (next.text === '<-' || next.text === '<<-' || (next.text === '=' && !inCall));
            // Not the `b` of a$b <- ..., a@b <- ..., or pkg::b <- ...
            const isMember = prev?.kind === 'op' && ['$', '@', '::', ':::'].includes(prev.text);
            if (isTarget && !isMember) define(t.text);
        }
        // ... -> x   ... ->> x
        if (t.kind === 'op' && (t.text === '->' || t.text === '->>') && next?.kind === 'name') {
            define(next.text);
        }
        // function(a, b = 1, ...)
        if (t.kind === 'name' && t.text === 'function' && next?.text === '(') {
            let depth = 0;
            for (let j = i + 1; j < tokens.length; j++) {
                const u = tokens[j]!;
                if (u.kind === 'open') depth++;
                else if (u.kind === 'close' && --depth === 0) break;
                else if (depth === 1 && u.kind === 'name') {
                    const before = tokens[j - 1];
                    if (before?.text === '(' || before?.text === ',') define(u.text);
                }
            }
        }
        // for (i in ...)
        if (t.kind === 'name' && t.text === 'for' && next?.text === '(' && tokens[i + 2]?.kind === 'name' && tokens[i + 3]?.text === 'in') {
            define(tokens[i + 2]!.text);
        }
        // assign("x", ...)
        if (t.kind === 'name' && t.text === 'assign' && next?.text === '(' && tokens[i + 2]?.kind === 'string') {
            define(tokens[i + 2]!.text);
        }
        // library(pkg), library("pkg"), require(pkg), library(package = pkg)
        if (t.kind === 'name' && (t.text === 'library' || t.text === 'require') && next?.text === '(' && prev?.text !== '$') {
            let arg = tokens[i + 2];
            if (arg?.kind === 'name' && arg.text === 'package' && tokens[i + 3]?.text === '=') arg = tokens[i + 4];
            if (arg && (arg.kind === 'name' || arg.kind === 'string') && /^[A-Za-z][A-Za-z0-9.]*$/.test(arg.text) && !packages.includes(arg.text)) {
                packages.push(arg.text);
            }
        }
    }
    return { defines, packages };
}
