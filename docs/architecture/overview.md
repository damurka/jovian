# Datasuite-R Architecture Overview

## Introduction

Datasuite-R is a unified C++ and TypeScript project that implements a Jupyter kernel for R, designed to be embedded in Node.js applications and Electron environments.

## High-Level Architecture

```
┌─────────────────────────────────────────────────────────┐
│                   TypeScript Layer (lib/)                │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐│
│  │  Engine  │  │Messaging │  │Execution │  │Middleware││
│  │  API     │  │ Routing  │  │  Queue   │  │ Plugins  ││
│  └──────────┘  └──────────┘  └──────────┘  └──────────┘│
└──────────────────────┬──────────────────────────────────┘
                       │ N-API Bridge
┌──────────────────────┴──────────────────────────────────┐
│                  C++ Native Layer (native/)              │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐│
│  │  Kernel  │  │ ZMQ      │  │ R        │  │ Platform ││
│  │  Core    │  │Transport │  │Interpreter│  │ Specific ││
│  └──────────┘  └──────────┘  └──────────┘  └──────────┘│
└──────────────────────┬──────────────────────────────────┘
                       │ R C API
┌──────────────────────┴──────────────────────────────────┐
│                   R Package (packages/hera/)             │
│         Execution │ Completion │ Inspection              │
└─────────────────────────────────────────────────────────┘
```

## Directory Structure

### `native/` - C++ Native Code
```
native/
├── include/datasuite/    # Public API headers
├── src/
│   ├── core/             # Kernel functionality
│   │   ├── kernel/       # Kernel lifecycle
│   │   ├── execution/    # Code execution, request handling
│   │   ├── messaging/    # Jupyter messages
│   │   └── history/      # Command history
│   ├── transport/        # Communication layer
│   │   ├── server/       # ZMQ server (ROUTER/PUB sockets)
│   │   ├── client/       # ZMQ client (DEALER/SUB sockets)
│   │   └── common/       # Serialization, authentication
│   ├── bridge/           # Node.js N-API integration
│   ├── platform/         # OS-specific code
│   ├── r/                # R integration
│   └── utils/            # Utilities
└── test/                 # C++ tests
```

### `lib/` - TypeScript Code
```
lib/
├── core/                 # Engine core
├── messaging/            # Message parsing/routing
├── handlers/             # Message type handlers
├── execution/            # Execution queue
├── middleware/           # Plugin system
├── types/                # Type definitions
└── api/                  # Public API
```

### `packages/` - R Packages
```
packages/
└── hera/                 # R kernel package
    └── R/
        ├── execute.R     # Code execution
        ├── completion.R  # Code completion
        └── inspect.R     # Object inspection
```

## Component Details

### 1. C++ Kernel Core

**Responsibilities:**
- Manage kernel lifecycle (startup, shutdown)
- Process Jupyter protocol messages
- Coordinate between R interpreter and ZMQ transport
- Handle multiple requests with priority queue

**Key Classes:**
- `Kernel` - Main kernel orchestrator
- `KernelCore` - Message dispatch and handling
- `RequestQueue` - Priority queue for requests
- `ExecutionState` - Track execution state

### 2. R Interpreter Integration

**Responsibilities:**
- Embed R runtime (`Rf_initEmbeddedR`)
- Execute R code via hera package
- Capture stdout/stderr streams
- Handle R console callbacks

**Key Classes:**
- `RInterpreter` - R embedding and execution
- `InterpreterBase` - Abstract interpreter interface

### 3. ZMQ Transport Layer

**Responsibilities:**
- Implement Jupyter protocol over ZMQ
- Manage multiple channels (shell, control, stdin, iopub, heartbeat)
- Serialize/deserialize Jupyter messages
- HMAC authentication

**Key Classes:**
- `ServerZmq` - Server-side ZMQ implementation
- `ClientZmq` - Client-side ZMQ implementation (custom)
- `ZmqSerializer` - Message serialization
- `Authentication` - HMAC signing/verification

### 4. N-API Bridge

**Responsibilities:**
- Expose C++ engine to Node.js/TypeScript
- Convert between C++ and JavaScript types
- Manage JavaScript callbacks from C++
- Handle async message passing

**Key Components:**
- `addon.cpp` - N-API entry point
- `EngineWrapper` - Wrap `DatasuiteEngine` for JS
- `CallbackManager` - Manage JS callbacks

### 5. TypeScript Engine

**Responsibilities:**
- Provide high-level API for users
- Parse and route Jupyter messages
- Queue and manage code executions
- Implement middleware/plugin system

**Key Classes:**
- `DatasuiteEngine` - Main user-facing class
- `MessageRouter` - Route messages to handlers
- `ExecutionQueue` - Queue TypeScript-side executions
- `MiddlewareChain` - Plugin architecture

## Message Flow

### Execute Request Flow
```
1. TypeScript: engine.execute(code)
2. → Native: addon.execute(code)
3. → C++ Engine: DatasuiteEngine::execute()
4. → Kernel Core: kernel_core::execute_request()
5. → R Interpreter: r_interpreter::execute_request_impl()
6. → R Package: hera::execute()
7. → R Runtime: eval(parse(code))
8. ← Results flow back through layers
9. ← TypeScript: engine.on('result', ...)
```

### Message Priority
```
Priority Queue:
┌──────────────────────────────────────┐
│ CRITICAL (0) - interrupt, shutdown   │
├──────────────────────────────────────┤
│ HIGH (1)     - kernel_info, comm     │
├──────────────────────────────────────┤
│ NORMAL (2)   - complete, inspect     │
├──────────────────────────────────────┤
│ LOW (3)      - execute_request       │
└──────────────────────────────────────┘
```

## Threading Model

### C++ Threads
1. **Main Thread** - R interpreter (single-threaded)
2. **Server Thread** - Kernel server polling loop
3. **IOPub Thread** - Publish messages to clients
4. **Heartbeat Thread** - Keep-alive monitoring

### Synchronization
- `RequestQueue` with mutex for thread-safe request handling
- `ExecutionState` with atomics for execution tracking
- Message passing between threads via ZMQ inproc sockets

## Build System

### CMake (C++)
- Root `CMakeLists.txt` - Output to `dist/native/`
- `native/CMakeLists.txt` - Source organization
- Dependencies: zeromq, cppzmq, nlohmann_json, OpenSSL, R

### TypeScript
- `tsconfig.json` - Compile `lib/` → `dist/lib/`
- ES modules with Node.js
- Type definitions generated

### Unified Build
```bash
npm run build:all    # Build both C++ and TypeScript
npm run clean        # Clean all artifacts
npm run dev          # Watch mode for TypeScript
```

## Style Guide

- **C++**: Google C++ Style Guide
- **TypeScript**: Standard + Prettier
- **Formatting**: clang-format (C++), Prettier (TS)
- **Linting**: clang-tidy (C++), ESLint (TS)

## Testing Strategy

### Unit Tests
- `test/unit/native/` - C++ unit tests
- `test/unit/lib/` - TypeScript unit tests

### Integration Tests
- `test/integration/` - End-to-end message flow

### E2E Tests
- `test/e2e/` - Full engine lifecycle tests

## Deployment

### Package Contents
```
dist/
├── lib/              # Compiled TypeScript
│   ├── index.js
│   └── index.d.ts
└── native/
    └── Release/
        └── datasuite_addon.node
```

### Usage
```typescript
import { DatasuiteEngine } from 'datasuite-r';

const engine = new DatasuiteEngine({
  rHome: '/path/to/R',
  enableLogging: true
});

await engine.start();
await engine.execute('x <- 1:10; mean(x)');
await engine.stop();
```

## Performance Considerations

1. **Request Queue** - Prevents blocking on single-threaded R
2. **Priority Handling** - Critical requests bypass queue
3. **Message Batching** - Group similar requests
4. **Async Operations** - Non-blocking API on TypeScript side
5. **Zero-Copy** - Minimize data copying between layers

## Security

1. **HMAC Authentication** - All Jupyter messages signed
2. **Input Validation** - Validate all user inputs
3. **R Sandboxing** - (Future) Restrict R capabilities
4. **No Eval** - Don't eval strings in TypeScript layer

## Future Enhancements

1. **Request Cancellation** - Cancel long-running operations
2. **Multi-Session** - Support multiple R sessions
3. **Debugger Integration** - R debugger support
4. **Performance Metrics** - Built-in profiling
5. **Plugin API** - Extensible middleware system

## References

- [Jupyter Kernel Protocol](https://jupyter-client.readthedocs.io/)
- [ZeroMQ Guide](https://zguide.zeromq.org/)
- [R Internals](https://cran.r-project.org/doc/manuals/r-release/R-ints.html)
- [N-API Documentation](https://nodejs.org/api/n-api.html)
- [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)
