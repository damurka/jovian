# Comms

A **comm** is a named, bidirectional message stream between your application and a *target* registered inside the kernel — the mechanism Jupyter widgets are built on. Jovian exposes it as `Comm` objects. Comms are an **R feature today**: Carpo has no API for registering comm targets, so every `comm_open` sent to a Python session is answered with a `comm_close`.

## Client → kernel: `openComm`

First register a target in the kernel (R, via `hera`), then open a comm to it:

```typescript
await session.execute(`
hera::CommManager$register_comm_target("echo", function(comm, message) {
    comm$on_message(function(msg) {
        comm$send(list(echo = msg$content$data$text))
    })
})
`);

const comm = await session.openComm('echo', { hello: 'kernel' });   // data becomes the comm_open payload
comm.on('message', (data) => console.log(data));                    // { echo: 'ping' }
await comm.send({ text: 'ping' });
await comm.close();
```

The callback receives the new `comm` and the `comm_open` `message`; `comm$on_message()` registers a handler for the client's `comm_msg`s, `comm$send()` / `comm$open()` / `comm$close()` send to the client (data is serialised with `jsonlite`).

If the kernel has no such target, it answers with a `comm_close` and the returned `Comm` emits `'close'`. (You can also check with `await session.commInfo('echo')`.)

## Kernel → client: the `'comm'` event

A comm the kernel creates and opens arrives as a `'comm'` event:

```typescript
session.on('comm', (comm, data) => {
    console.log(comm.targetName, 'opened with', data);
    comm.on('message', (msg) => console.log('kernel says', msg));
    comm.send({ text: 'hi' });
});

await session.execute(`
hera::CommManager$register_comm_target("kernel_side")   # a target must exist before new_comm()
comm <- hera::CommManager$new_comm("kernel_side")
comm$on_message(function(msg) comm$send(list(echo = msg$content$data$text)))
comm$open(list(greeting = "from R"))
`);
```

Attach the `'comm'` listener **before** running the code that opens it. `new_comm()` returns `NULL` for an unregistered target, so register the target first.

## The `Comm` object

| | |
|---|---|
| `comm.id`, `comm.targetName`, `comm.closed` | Identity and state. |
| `comm.send(data)` | Sends a `comm_msg`; rejects if the comm is closed. |
| `comm.close(data?)` | Sends `comm_close`; emits `'close'`. |
| `'message'` `(data)` | The kernel sent a `comm_msg`. |
| `'close'` `(data)` | Closed by either side; or the kernel restarted / exited / the session stopped / the connection dropped, in which case `data.reason` says which. |

## Listing comms

`await session.commInfo()` returns `{ comms: { <commId>: { target_name } } }` for every open comm in the kernel; pass a target name to filter.

## Low-level API

`commOpen(targetName, data?, commId?)`, `commMsg(commId, data?)` and `commClose(commId, data?)` send the raw messages and resolve with the message id they were sent under (useful for correlating iopub replies — e.g. the `comm_close` for an unknown target has that id as its `parentMsgId`). The kernel's `comm_open` / `comm_msg` / `comm_close` are also available as plain events of those names (`session.on('comm_msg', (content) => …)` with `content.comm_id`). `openComm()` and the `'comm'` event are built on them.

## Notes

- Comm requests are handled on the kernel's main thread between executions, so a comm message sent while code is running waits for it to finish.
- Comms do not survive a restart; open `Comm`s emit `'close'` with `reason: 'kernel restarted'`.
