# C Diagnostics Infrastructure Design

**Date**: 2026-04-26
**Scope**: Debug build, assertions, enriched errors, crash handlers

## Goal

Add comprehensive diagnostic infrastructure to the Stage 0 C core so that crashes, type errors, and VM bugs produce actionable information instead of "read error" or a raw segfault.

## Non-goals

- Runtime trace levels, per-module logging categories
- Heap canaries or heap validation on every alloc
- Debug REPL commands (`:heap`, `:stack`, `:trace`)

---

## 1. Build System

### Meson options (`meson_options.txt`)

```
option('build_type', type: 'string', value: 'release', description: 'Build type: debug or release')
```

### Build rules (`meson.build`)

Debug mode (`-Dbuild_type=debug`):
- Compiler flags: `-O0 -g3 -ggdb -fsanitize=address,undefined -fno-omit-frame-pointer`
- Define: `-DSCHEME_DEBUG`

Release mode (default):
- Compiler flags: `-O2 -D_FORTIFY_SOURCE=2`

Both modes: `-Wall -Wextra -Wno-unused-parameter`

Usage:
```
meson setup build -Dbuild_type=debug
```

---

## 2. Debug Header

New file: `src/include/debug.h`

Central header for all debug infrastructure. In release builds (`!SCHEME_DEBUG`), all macros compile to `((void)0)`.

### DASSERT

```c
DASSERT(cond, fmt, ...)
```

If `SCHEME_DEBUG` and `cond` is false, prints to stderr:
```
ASSERT at file:line: function: cond
<fmt message>
```
Then calls `abort()`.

### DASSERT_TYPE

```c
DASSERT_TYPE(word, expected_obj_type)
```

Convenience wrapper — checks `obj_type(ptr_from_word(word)) == expected_obj_type`, prints the actual type tag on mismatch.

### VM_ERROR

```c
VM_ERROR(vm, kind, msg, arg)
```

Sets enriched error state on the VM (see Section 3).

### DEBUG_LOG

```c
DEBUG_LOG(fmt, ...)
```

Compile-time gated `fprintf(stderr, ...)`. For temporary printf-style debugging.

### debug_install_handlers

```c
void debug_install_handlers(vm_state_t* vm);
```

Registers crash signal handlers. No-op in release builds.

---

## 3. Enriched Error Reporting

### vm_state_t changes (`src/include/types.h`)

Replace `int error_code; word error_arg;` with:

```c
typedef enum {
    ERR_NONE = 0,
    ERR_READ,       // read / syntax error
    ERR_TYPE,       // type mismatch
    ERR_UNBOUND,    // unbound variable
    ERR_ARITY,      // wrong number of arguments
    ERR_INTERNAL,   // compiler / VM internal error
    ERR_IO,         // file or port error
    ERR_GC,         // out of memory
} vm_error_kind_t;

// In vm_state_t:
vm_error_kind_t error_kind;
const char*     error_msg;
word            error_arg;   // offending value for context
```

### Usage pattern

All sites that set `vm->error_code` switch to:
```c
VM_ERROR(vm, ERR_TYPE, "car: expected pair", obj);
```

### main.c error output

Instead of `"read error"`:
```
Error: [type] car: expected pair (got #<fixnum 42>)
```

---

## 4. Crash Signal Handlers

New file: `src/debug.c`

### Signals caught

`SIGSEGV`, `SIGABRT`, `SIGFPE`, `SIGILL`

### Handler output (to stderr via `write()`)

```
=== SCHEME CRASH [SIGSEGV: address boundary error] ===
Fault address: 0x7f... (from siginfo_t)
VM state:
  IP offset: 0x0042 of 0x01a0 (in code object #3)
  SP: 0x0018 / 0x2000 words (stack 0.1% used)
  ACC: #<fixnum 42>
  error: ERR_TYPE "car: expected pair"
Stack trace:
  #0 scheme(+0x3a2f)
  #1 scheme(+0x2e10)
  #2 scheme(+0x1a80)
  #3 libc.so(+0x2e...) __libc_start_main
Aborted
```

### Backtrace method

- **glibc (Linux)**: `backtrace()` + `backtrace_symbols()`
- **Termux (bionic)**: `_Unwind_Backtrace()` with IP-only output
- Fallback: if neither is available, skip backtrace and print only VM state

The handler uses only async-signal-safe operations (no `malloc`, no `fprintf`). All output via `write()` to `STDERR_FILENO`.

### VM state dump

- Current IP relative to `current_code` bytecode start
- SP position relative to stack start and capacity
- ACC register value (with type tag decode)
- Current error kind + message

### Registration

Called once in `main()` before the REPL/exec loop:
```c
debug_install_handlers(vm);
```

---

## 5. Assertion Placement

Add `DASSERT` calls to hot paths:

| File | Location | Check |
|---|---|---|
| `vm/vm.c` | `vm_execute` switch default | always (unknown opcode) |
| `vm/vm.c` | `OP_CAR` / `OP_CDR` | `DASSERT_TYPE(val, OBJ_TYPE_PAIR)` |
| `vm/vm.c` | `OP_PRIM_CALL` | prim index bounds |
| `gc/gc.c` | `gc_alloc_words` | non-null on OOM retry |
| `gc/gc.c` | `mark_word` switch | valid type tag |
| `reader/reader.c` | entry points | non-null parse output |
| `bootstrap/compiler.c` | `compile_list` | valid pair structure |

## 6. Source Build Config

`debug.c` is always compiled. Content is gated with `#ifdef SCHEME_DEBUG` so the file compiles to an empty object in release builds. No changes needed to `src/meson.build` — `debug.c` is added unconditionally to `scheme_core_sources`.

## Files Summary

| File | Action |
|---|---|
| `meson_options.txt` | Add `build_type` option |
| `meson.build` | Debug/release compiler flags, `SCHEME_DEBUG` define |
| `src/meson.build` | Conditional `debug.c` |
| `src/include/debug.h` | New — all debug macros |
| `src/debug.c` | New — signal handler implementation |
| `src/include/types.h` | Replace `error_code` with `error_kind`/`error_msg`/`error_arg` |
| `src/vm/vm.c` | Use `VM_ERROR`, add `DASSERT` |
| `src/gc/gc.c` | Add `DASSERT` |
| `src/reader/reader.c` | Use `VM_ERROR`, add `DASSERT` |
| `src/bootstrap/compiler.c` | Add `DASSERT` |
| `src/main.c` | Register signal handlers, richer error display |
