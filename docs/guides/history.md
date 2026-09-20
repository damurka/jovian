# History

There are two different records of what a session has run. They answer different questions.

| | `session.getHistory()` | `session.queryKernelHistory()` |
|---|---|---|
| Kept by | the `Session` object (client side) | the kernel (`InMemoryHistoryManager`) |
| Contains | code **and every message it produced** (output, errors, plots) | input code only (the kernel does not record output; `output: true` returns empty strings) |
| Covers | executions made through *this* `Session` object | everything the kernel ran, from any client, since the kernel started |
| Survives `restart()` | yes | no — a new kernel starts empty |
| Survives the client process | no | as long as the kernel process |
| Limit | latest 200 executions; per execution, the newest ~500 000 characters of stdout/stderr (`entry.truncated`) | the kernel's in-memory list |
| Cost | free (a local array) | a round trip to the kernel; queued behind a running execution |

## `getHistory()`

```typescript
await session.execute('1 + 1');
for (const entry of session.getHistory()) {
    console.log(entry.executionCount, entry.code, entry.messages.map((m) => m.msgType));
}
```

Entries are built from the kernel's own `execute_input` message, so they reflect exactly what ran; a silent execution (`silent: true`) publishes no `execute_input` and is not recorded. The messages are the same objects as in `ExecutionResult.output`, plus the trailing `status` and `execute_reply` messages. It is the live array — copy it if you need a snapshot. The playground uses it to rebuild a transcript after a browser refresh.

**Output is bounded.** A cell that prints millions of lines (`for (i in 1:5e6) print(i)`) would otherwise make `getHistory()` — and anything that serializes it — hold and ship hundreds of megabytes. Per entry, only the newest ~500 000 characters of stream text are kept: older `stream` messages are dropped and `entry.truncated` is set to `true`. Results, display data and errors are never dropped. (`ExecutionResult.output` from `execute()` itself is not bounded.)

## `queryKernelHistory()`

```typescript
const last = await session.queryKernelHistory({ n: 20 });      // [[0, 1, '1 + 1'], [0, 2, 'x <- 3'], …]
const found = await session.queryKernelHistory({ histAccessType: 'search', pattern: '*mean*', unique: true });
const some = await session.queryKernelHistory({ histAccessType: 'range', session: 0, start: 1, stop: 5 });
```

Each entry is `[session, lineNumber, input]` (`session` is always `0`; `lineNumber` is the execution count). Access types:

| `histAccessType` | Fields | Kernel-side defaults (if you send nothing) |
|---|---|---|
| `'tail'` (Jovian's default) | `n`, `raw`, `output` | `n`: the library sends **100**; the kernel's own default is 10 |
| `'search'` | `pattern` (glob: `*`, `?`), `n`, `unique`, `raw`, `output` | pattern `*`, `n` 10, `unique` false |
| `'range'` | `session`, `start`, `stop`, `raw`, `output` | session 0, start 1, stop 10 |

Only `store_history: true` (the default), non-silent executions are stored. An `execute()` that failed is stored too.
