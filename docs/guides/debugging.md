# Debugging R code

R sessions (Elara) have a debugger, on R's own `browser()` — as Ark's is — spoken through the **Jupyter debug protocol** (JEP 47): Debug Adapter Protocol requests in `debug_request` messages, sent with `session.debugRequest(command, args)`, and DAP events in `debug_event` messages, which arrive as the session's `'debug_event'` events. JupyterLab's and VS Code's notebook debuggers speak it; a host can also drive it directly. `kernelInfo()` reports `debugger: true`.

Debug requests go over the control channel, so they are answered while a cell runs, and while it is stopped at a breakpoint.

## The flow

```js
await session.debugRequest('initialize', { adapterID: 'r' });
await session.debugRequest('attach');                       // 'initialized' event
const { body: info } = await session.debugRequest('debugInfo');
// cells are run from files named info.tmpFilePrefix + murmur2(code, info.hashSeed) + info.tmpFileSuffix
const { body: { sourcePath } } = await session.debugRequest('dumpCell', { code });
await session.debugRequest('setBreakpoints', { source: { path: sourcePath }, breakpoints: [{ line: 2 }] });
await session.debugRequest('configurationDone');

session.on('debug_event', async (event) => {
    if (event.event !== 'stopped') return;                  // reason: 'breakpoint', 'step' or 'pause'
    const { body: { stackFrames } } = await session.debugRequest('stackTrace', { threadId: 1 });
    const { body: { scopes } } = await session.debugRequest('scopes', { frameId: stackFrames[0].id });
    const { body: { variables } } = await session.debugRequest('variables', { variablesReference: scopes[0].variablesReference });
    await session.debugRequest('next', { threadId: 1 });    // or continue, stepIn, stepOut
});
session.execute(code);
```

## What is supported

| Request | |
|---|---|
| `initialize`, `attach`, `configurationDone`, `disconnect` | Start and end debugging. `disconnect` clears the breakpoints and lets a stopped cell go on. |
| `debugInfo` | `isStarted`, `hashMethod` (`Murmur2`), `hashSeed`, `tmpFilePrefix`, `tmpFileSuffix` (`.r`), `breakpoints`, `stoppedThreads`. |
| `dumpCell` | Writes a cell's code to the file it is run from while debugging; `{ sourcePath }`. |
| `setBreakpoints` | Per file. A breakpoint in a function defined in a cell becomes R's own (`utils::setBreakpoint()`, a `trace()` calling `browser()`), set again after every cell; one on a line of the cell's own code stops before that top-level expression (the cell is then run as one `{ }` block, which R can step through). |
| `threads` | One thread, `R` (id 1). |
| `stackTrace` | The calls being debugged, innermost first, each with its file and line, down to `<cell>`. |
| `scopes`, `variables` | `Locals` (the frame's environment) and `Globals`; lists, environments and long vectors can be opened. |
| `evaluate` | An expression evaluated in a frame (`frameId`) or the global environment; its printed value. |
| `continue`, `next`, `stepIn`, `stepOut` | R's browser commands `c`, `n`, `s`, `f`. |
| `pause` | Interrupts the running cell, which stops in `browser()` there (reason `pause`). |
| `source`, `inspectVariables` | A file's text; the global environment's variables. |

Events: `initialized`, `stopped` (`reason`, `threadId: 1`), `continued`.

While debugging, `browser()`'s own lines (`Called from:`, `debug at …`) are not shown: they tell the debugger where R is. An `interrupt()` while stopped quits the browser, and so the cell. With no debugger attached, a `browser()` prompt is an ordinary `input_request`, as before.
