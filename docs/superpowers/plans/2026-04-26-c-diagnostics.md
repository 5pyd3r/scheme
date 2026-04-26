# C Diagnostics Infrastructure Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add debug build, assertions, enriched error reporting, and crash signal handlers to the Stage 0 C core.

**Architecture:** A new `debug.h` header provides macros (`DASSERT`, `DASSERT_TYPE`, `VM_ERROR`, `DEBUG_LOG`) gated by `SCHEME_DEBUG`. A new `debug.c` implements `SIGSEGV`/`SIGABRT`/`SIGFPE`/`SIGILL` handlers with backtrace and VM state dump. The meson build system gains a `build_type` option to toggle debug mode with ASAN+UBSAN. In release builds all diagnostic code compiles to nothing.

**Tech Stack:** C11, Meson, ASAN/UBSAN

**Target worktree:** `.worktrees/stage0-c-core`

---

### Task 1: Build System — Debug Mode

**Files:**
- Modify: `meson_options.txt`
- Modify: `meson.build`

- [ ] **Step 1: Add `build_type` option to meson_options.txt**

Read `meson_options.txt` (currently only has `gc_impl` option), then add the new option:

```c
option('build_type', type: 'string', value: 'release', description: 'Build type: debug or release')
```

Run: `cat meson_options.txt`
Expected: shows both `gc_impl` and `build_type` options

- [ ] **Step 2: Update meson.build with debug/release flags**

Current `meson.build`:
```python
project('scheme', 'c',
  default_options: [
    'c_std=c11',
    'warning_level=2',
  ]
)

subdir('src')
subdir('tests')
```

Replace with:
```python
project('scheme', 'c',
  default_options: [
    'c_std=c11',
    'warning_level=2',
  ]
)

build_type = get_option('build_type')

if build_type == 'debug'
  add_project_arguments('-O0', '-g3', '-ggdb', language: 'c')
  add_project_arguments('-fsanitize=address,undefined', language: 'c')
  add_project_link_arguments('-fsanitize=address,undefined', language: 'c')
  add_project_arguments('-fno-omit-frame-pointer', language: 'c')
  add_project_arguments('-DSCHEME_DEBUG', language: 'c')
else
  add_project_arguments('-O2', language: 'c')
  add_project_arguments('-D_FORTIFY_SOURCE=2', language: 'c')
endif

add_project_arguments('-Wall', '-Wextra', '-Wno-unused-parameter', language: 'c')

subdir('src')
subdir('tests')
```

Run: `meson setup build -Dbuild_type=debug && ls build/build.ninja`
Expected: build directory configures successfully

- [ ] **Step 3: Verify debug define is active**

```bash
meson configure build | grep SCHEME
```
Expected: no output (it's a `-D` flag, not a meson option), but verify via compile check:
```bash
echo '#ifndef SCHEME_DEBUG
#error "SCHEME_DEBUG not defined"
#endif
int main(){return 0;}' > /tmp/test_debug.c
cc -DSCHEME_DEBUG /tmp/test_debug.c -o /tmp/test_debug && echo "DEBUG defined"
rm /tmp/test_debug.c /tmp/test_debug
```
Expected: "DEBUG defined"

- [ ] **Step 4: Verify release build omits debug define**

```bash
meson setup build-release --wipe > /dev/null 2>&1
grep -c 'SCHEME_DEBUG' build-release/build.ninja || echo "SCHEME_DEBUG not in release ninja (expected)"
```
Expected: "SCHEME_DEBUG not in release ninja" or count 0

- [ ] **Step 5: Commit**

```bash
git add meson.build meson_options.txt
git commit -m "feat: add debug build type with ASAN/UBSAN support"
```

---

### Task 2: Debug Header — Macros

**Files:**
- Create: `src/include/debug.h`

- [ ] **Step 1: Create debug.h**

Create `src/include/debug.h`:

```c
#ifndef SCHEME_DEBUG_H
#define SCHEME_DEBUG_H

#include "types.h"
#include <stdio.h>
#include <stdlib.h>

#ifdef SCHEME_DEBUG

// ---- Assertion ----
#define DASSERT(cond, fmt, ...) do { \
    if (!(cond)) { \
        fprintf(stderr, "ASSERT at %s:%d: %s: (%s)\n", __FILE__, __LINE__, __func__, #cond); \
        fprintf(stderr, "  " fmt "\n", ##__VA_ARGS__); \
        abort(); \
    } \
} while(0)

// ---- Type assertion ----
#define DASSERT_TYPE(w, expected_type) do { \
    if (is_ptr(w)) { \
        word* _hdr = ptr_from_word(w); \
        word _actual = obj_type(_hdr); \
        if (_actual != (expected_type)) { \
            fprintf(stderr, "ASSERT at %s:%d: %s: expected type %d, got %ld\n", \
                    __FILE__, __LINE__, __func__, (int)(expected_type), (long)_actual); \
            abort(); \
        } \
    } else { \
        fprintf(stderr, "ASSERT at %s:%d: %s: expected ptr, got immediate word 0x%lx\n", \
                __FILE__, __LINE__, __func__, (unsigned long)(w)); \
        abort(); \
    } \
} while(0)

// ---- Debug log ----
#define DEBUG_LOG(fmt, ...) \
    fprintf(stderr, "DEBUG %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)

// ---- VM error setter ----
#define VM_ERROR(vm, kind, msg, arg) do { \
    (vm)->error_kind = (kind); \
    (vm)->error_msg = (msg); \
    (vm)->error_arg = (arg); \
} while(0)

// ---- Signal handler registration ----
void debug_install_handlers(vm_state_t* vm);

#else // !SCHEME_DEBUG

#define DASSERT(cond, fmt, ...)         ((void)0)
#define DASSERT_TYPE(w, expected_type)  ((void)0)
#define DEBUG_LOG(fmt, ...)             ((void)0)
#define VM_ERROR(vm, kind, msg, arg) do { \
    (vm)->error_kind = (kind); \
    (vm)->error_msg = (msg); \
    (vm)->error_arg = (arg); \
} while(0)

static inline void debug_install_handlers(vm_state_t* vm) { (void)vm; }

#endif // SCHEME_DEBUG

#endif // SCHEME_DEBUG_H
```

Note: `VM_ERROR` always sets error state (even in release) — only the assertion/logging is gated. `debug_install_handlers` is a no-op inline in release.

- [ ] **Step 2: Verify header compiles standalone**

```bash
cd build && ninja 2>&1 | head -5
```
(we haven't included debug.h anywhere yet, so this just verifies the build still works)

Expected: build succeeds with no new errors

- [ ] **Step 3: Commit**

```bash
git add src/include/debug.h
git commit -m "feat: add debug header with DASSERT, DASSERT_TYPE, VM_ERROR, DEBUG_LOG macros"
```

---

### Task 3: Enriched Error Types

**Files:**
- Modify: `src/include/types.h`
- Currently `error_code` and `error_arg` are at lines 33-34 of `vm_state_t` in `vm.h`
- Error enum goes in `types.h` before `vm.h` includes it, and `vm_state_t` fields change in `vm.h`

Actually: `error_code` and `error_arg` are in `vm.h` (the `vm_state_t` struct). The error enum goes in `types.h` because it's a foundational type. `vm.h` already includes `types.h`.

- [ ] **Step 1: Add error kind enum to types.h**

Add after the existing enums in `types.h` (after `OBJ_TYPE_RECORD = 12`):

```c
// ---- Error kinds ----
typedef enum {
    ERR_NONE = 0,
    ERR_READ,
    ERR_TYPE,
    ERR_UNBOUND,
    ERR_ARITY,
    ERR_INTERNAL,
    ERR_IO,
    ERR_GC,
} vm_error_kind_t;
```

- [ ] **Step 2: Update vm_state_t in vm.h**

Change lines 33-34 of `vm.h` from:
```c
    int     error_code;
    word    error_arg;
```

To:
```c
    vm_error_kind_t error_kind;
    const char*     error_msg;
    word            error_arg;
```

- [ ] **Step 3: Build to catch any compile errors from the type change**

```bash
cd build && ninja 2>&1
```

Expected: compile errors where `error_code` was referenced (we'll fix those in the source file tasks).

Note: this will show errors about `error_code` being referenced in `vm/vm.c`, `reader/reader.c`, and `main.c`. This is expected — the next tasks fix them.

- [ ] **Step 4: Commit**

```bash
git add src/include/types.h src/include/vm.h
git commit -m "feat: replace generic error_code with enriched error kind/message/arg"
```

---

### Task 4: Crash Signal Handler Implementation

**Files:**
- Create: `src/debug.c`
- Modify: `src/meson.build`

- [ ] **Step 1: Update src/meson.build to include debug.c**

Current `src/meson.build` sources:
```python
scheme_core_sources = files(
  'pal/pal_posix.c',
  'gc/gc.c',
  'vm/vm.c',
  'vm/builtins.c',
  'reader/reader.c',
  'bootstrap/compiler.c',
  'primitives/arith.c',
  'primitives/pair.c',
  'primitives/port.c',
)
```

Add `'debug.c',` to the list (before `'primitives/arith.c'` or anywhere — order doesn't matter):
```python
scheme_core_sources = files(
  'pal/pal_posix.c',
  'gc/gc.c',
  'vm/vm.c',
  'vm/builtins.c',
  'reader/reader.c',
  'bootstrap/compiler.c',
  'debug.c',
  'primitives/arith.c',
  'primitives/pair.c',
  'primitives/port.c',
)
```

- [ ] **Step 2: Create src/debug.c**

```c
#include "debug.h"
#include "vm.h"
#include <signal.h>
#include <string.h>
#include <unistd.h>

#ifdef SCHEME_DEBUG

// ---- Globals set during handler registration ----
static vm_state_t* crash_vm = NULL;

// ---- Async-signal-safe write of a string ----
static void safe_write(const char* s) {
    size_t len = 0;
    while (s[len]) len++;
    write(STDERR_FILENO, s, len);
}

// ---- Write unsigned integer to stderr ----
static void safe_write_hex(unsigned long n) {
    static const char hex[] = "0123456789abcdef";
    char buf[20];
    int i = 18;
    buf[19] = '\0';
    if (n == 0) {
        buf[i--] = '0';
    } else {
        while (n > 0) {
            buf[i--] = hex[n & 0xf];
            n >>= 4;
        }
        buf[i--] = 'x';
        buf[i--] = '0';
    }
    safe_write(&buf[i + 1]);
}

// ---- Write decimal unsigned integer ----
static void safe_write_dec(unsigned long n) {
    char buf[24];
    int i = 22;
    buf[23] = '\0';
    if (n == 0) {
        buf[i--] = '0';
    } else {
        while (n > 0) {
            buf[i--] = (char)('0' + (n % 10));
            n /= 10;
        }
    }
    safe_write(&buf[i + 1]);
}

// ---- Decode a word for display ----
static void safe_write_word(word w) {
    if (is_fixnum(w)) {
        safe_write("#<fixnum ");
        safe_write_dec((unsigned long)word_to_fixnum(w));
        safe_write(">");
    } else if (is_char(w)) {
        safe_write("#\\");
        char c = (char)word_to_char(w);
        write(STDERR_FILENO, &c, 1);
    } else if (w == word_true()) {
        safe_write("#t");
    } else if (w == word_false()) {
        safe_write("#f");
    } else if (w == word_nil()) {
        safe_write("()");
    } else if (w == word_eof()) {
        safe_write("#<eof>");
    } else if (is_ptr(w)) {
        word* hdr = ptr_from_word(w);
        safe_write("#<ptr type=");
        safe_write_dec((unsigned long)obj_type(hdr));
        safe_write(" addr=");
        safe_write_hex((unsigned long)(uintptr_t)hdr);
        safe_write(">");
    } else {
        safe_write("#<unknown 0x");
        safe_write_hex((unsigned long)w);
        safe_write(">");
    }
}

// ---- Dump VM state ----
static void dump_vm_state(void) {
    if (!crash_vm) return;

    safe_write("VM state:\n");

    // IP offset
    if (crash_vm->current_code && crash_vm->ip) {
        uint8_t* code_start = (uint8_t*)(crash_vm->current_code + 3);
        unsigned long ip_off = (unsigned long)(crash_vm->ip - code_start);
        unsigned long bc_len = (unsigned long)crash_vm->current_code[2];
        safe_write("  IP offset: 0x");
        safe_write_hex(ip_off);
        safe_write(" of 0x");
        safe_write_hex(bc_len);
        safe_write(" bytes");
        // Find which code object
        for (size_t i = 0; i < crash_vm->code_count; i++) {
            if (crash_vm->code_objects[i] == crash_vm->current_code) {
                safe_write(" (in code object #");
                safe_write_dec((unsigned long)i);
                safe_write(")");
                break;
            }
        }
        safe_write("\n");
    }

    // SP
    if (crash_vm->stack) {
        unsigned long sp_off = (unsigned long)(crash_vm->sp - crash_vm->stack);
        safe_write("  SP: 0x");
        safe_write_hex(sp_off);
        safe_write(" / 0x");
        safe_write_hex((unsigned long)crash_vm->stack_cap);
        safe_write(" words (stack ");
        if (crash_vm->stack_cap > 0) {
            safe_write_dec((unsigned long)(sp_off * 100 / crash_vm->stack_cap));
        } else {
            safe_write("0");
        }
        safe_write("% used)\n");
    }

    // ACC
    safe_write("  ACC: ");
    safe_write_word(crash_vm->acc);
    safe_write("\n");

    // Active error
    if (crash_vm->error_kind != ERR_NONE) {
        safe_write("  Error: kind=");
        safe_write_dec((unsigned long)crash_vm->error_kind);
        safe_write(" \"");
        if (crash_vm->error_msg) {
            safe_write(crash_vm->error_msg);
        }
        safe_write("\"");
        if (crash_vm->error_kind != ERR_NONE) {
            safe_write(" arg=");
            safe_write_word(crash_vm->error_arg);
        }
        safe_write("\n");
    }
}

// ---- Signal name from signum ----
static const char* signal_name(int sig) {
    switch (sig) {
    case SIGSEGV: return "SIGSEGV: address boundary error";
    case SIGABRT: return "SIGABRT: abort called";
    case SIGFPE:  return "SIGFPE: arithmetic exception";
    case SIGILL:  return "SIGILL: illegal instruction";
    default:      return "UNKNOWN SIGNAL";
    }
}

// ---- Backtrace via glibc backtrace() ----
#if defined(__GLIBC__)
#include <execinfo.h>

static void print_backtrace(void) {
    void* frames[32];
    int n = backtrace(frames, 32);
    char** symbols = backtrace_symbols(frames, n);
    safe_write("Backtrace:\n");
    for (int i = 0; i < n; i++) {
        safe_write("  #");
        safe_write_dec((unsigned long)i);
        safe_write(" ");
        safe_write(symbols[i] ? symbols[i] : "???");
        safe_write("\n");
    }
    // backtrace_symbols uses malloc, but we're crashing anyway so it's acceptable
}

// ---- Backtrace via _Unwind_Backtrace (bionic/Android) ----
#elif defined(__ANDROID__) || defined(__BIONIC__)
#define _GNU_SOURCE
#include <unwind.h>

struct bt_state {
    void** frames;
    int    count;
    int    max;
};

static _Unwind_Reason_Code bt_callback(struct _Unwind_Context* ctx, void* arg) {
    struct bt_state* state = (struct bt_state*)arg;
    if (state->count >= state->max)
        return _URC_END_OF_STACK;
    state->frames[state->count++] = (void*)_Unwind_GetIP(ctx);
    return _URC_NO_REASON;
}

static void print_backtrace(void) {
    void* frames[32];
    struct bt_state state = { frames, 0, 32 };
    _Unwind_Backtrace(bt_callback, &state);

    safe_write("Backtrace (IPs):\n");
    for (int i = 0; i < state.count; i++) {
        safe_write("  #");
        safe_write_dec((unsigned long)i);
        safe_write(" 0x");
        safe_write_hex((unsigned long)(uintptr_t)frames[i]);
        safe_write("\n");
    }
}

// ---- No backtrace available ----
#else
static void print_backtrace(void) {
    safe_write("Backtrace: not available on this platform\n");
}
#endif

// ---- Signal handler ----
static void crash_handler(int sig, siginfo_t* info, void* ctx) {
    (void)ctx;

    safe_write("\n=== SCHEME CRASH [");
    safe_write(signal_name(sig));
    safe_write("] ===\n");

    if (info && (sig == SIGSEGV || sig == SIGFPE)) {
        safe_write("Fault address: 0x");
        safe_write_hex((unsigned long)(uintptr_t)info->si_addr);
        safe_write("\n");
    }

    dump_vm_state();
    print_backtrace();

    safe_write("Aborted\n");

    // Restore default handler and re-raise so the OS can write a core dump
    signal(sig, SIG_DFL);
    raise(sig);
}

void debug_install_handlers(vm_state_t* vm) {
    crash_vm = vm;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crash_handler;
    sa.sa_flags = SA_SIGINFO;

    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGFPE,  &sa, NULL);
    sigaction(SIGILL,  &sa, NULL);
}

#endif // SCHEME_DEBUG
```

- [ ] **Step 3: Build in debug mode**

```bash
cd build && ninja 2>&1
```

Expected: build succeeds (still may have `error_code` errors from Task 3 — those get fixed next). If there are linker errors about missing `debug_install_handlers`, verify the inline definition in `debug.h` is correct.

- [ ] **Step 4: Commit**

```bash
git add src/debug.c src/meson.build
git commit -m "feat: add crash signal handlers with backtrace and VM state dump"
```

---

### Task 5: Update vm.c — DASSERT and VM_ERROR

**Files:**
- Modify: `src/vm/vm.c`

- [ ] **Step 1: Add debug.h include**

Add `#include "debug.h"` after existing includes at the top of `vm.c` (after `#include "vm.h"`):

```c
#include "vm.h"
#include "debug.h"
#include "opcodes.h"
```

- [ ] **Step 2: Fix error_code → error_kind references**

In `vm.c` there are currently references to `vm->error_code`:
- Line 198: `vm->error_code = 1;` in the `default:` case of the VM switch

Replace lines 196-199:
```c
        default:
            fprintf(stderr, "unknown opcode: 0x%02x\n", op);
            vm->error_code = 1;
            return word_nil();
```

With:
```c
        default:
            DASSERT(false, "unknown opcode: 0x%02x at IP offset %ld",
                    op, (long)(vm->ip - 1 - (uint8_t*)(vm->current_code + 3)));
            VM_ERROR(vm, ERR_INTERNAL, "unknown opcode", word_from_fixnum(op));
            return word_nil();
```

- [ ] **Step 3: Add DASSERT_TYPE to OP_CAR and OP_CDR**

Replace lines 144-154:
```c
        case OP_CAR: {
            word* pair = ptr_from_word(*vm->sp);
            *vm->sp = pair_car(pair);
            break;
        }

        case OP_CDR: {
            word* pair = ptr_from_word(*vm->sp);
            *vm->sp = pair_cdr(pair);
            break;
        }
```

With:
```c
        case OP_CAR: {
            DASSERT_TYPE(*vm->sp, OBJ_TYPE_PAIR);
            word* pair = ptr_from_word(*vm->sp);
            *vm->sp = pair_car(pair);
            break;
        }

        case OP_CDR: {
            DASSERT_TYPE(*vm->sp, OBJ_TYPE_PAIR);
            word* pair = ptr_from_word(*vm->sp);
            *vm->sp = pair_cdr(pair);
            break;
        }
```

- [ ] **Step 4: OP_PRIM_CALL — no DASSERT (no prim count field available)**

`vm.h` has `word* primitives;` but no count field, so we can't bounds-check the prim index. The unknown opcode handler already catches bogus dispatch. OP_PRIM_CALL is left as-is.

- [ ] **Step 5: Build**

```bash
cd build && ninja 2>&1
```

Expected: any remaining `error_code` compilation errors. Fix any stragglers in `vm.c` (search for `error_code` and replace with `error_kind`).

- [ ] **Step 6: Commit**

```bash
git add src/vm/vm.c
git commit -m "fix: use VM_ERROR and DASSERT_TYPE in VM execute loop"
```

---

### Task 6: Update gc.c — DASSERT

**Files:**
- Modify: `src/gc/gc.c`

- [ ] **Step 1: Add debug.h include and add DASSERTs**

Add `#include "debug.h"` after existing includes. Then add assertions:

In `gc_alloc_words`, after the NULL check on OOM:
```c
    word* block = (word*)pal->mmap_alloc(nwords * sizeof(word));
    if (!block) {
        // Retry after collection
        gc_collect();
        block = (word*)pal->mmap_alloc(nwords * sizeof(word));
        DASSERT(block != NULL, "OOM: gc_alloc_words(%zu) failed after collection", nwords);
        if (!block) return NULL;
    }
```

In `mark_word`, in the `default:` of the switch (add a default case):
```c
    switch (obj_type(hdr)) {
    // ... existing cases ...
    default:
        DASSERT(false, "mark_word: unknown object type %ld", (long)obj_type(hdr));
        break;
    }
```

- [ ] **Step 2: Build**

```bash
cd build && ninja 2>&1
```

Expected: compiles clean

- [ ] **Step 3: Commit**

```bash
git add src/gc/gc.c
git commit -m "fix: add DASSERT checks to GC alloc and mark"
```

---

### Task 7: Update reader.c — VM_ERROR

**Files:**
- Modify: `src/reader/reader.c`

`reader.c` has two references to the old `error_code` field:
- Line 84: `vm->error_code = 1;` in `read_atom()` (unrecognized token)
- Line 97: `if (vm->error_code)` in `read_list_tail()` (error propagation)

- [ ] **Step 1: Add debug.h include**

Add after `#include "reader.h"` (line 1):

```c
#include "reader.h"
#include "debug.h"
#include <ctype.h>
```

- [ ] **Step 2: Replace error_code set in read_atom (line 84)**

Change:
```c
    vm->error_code = 1;
    return word_nil();
```

To:
```c
    VM_ERROR(vm, ERR_READ, "unrecognized token", word_from_char((unsigned char)s[*pos]));
    return word_nil();
```

- [ ] **Step 3: Replace error_code check in read_list_tail (line 97)**

Change:
```c
    if (vm->error_code) return word_nil();
```

To:
```c
    if (vm->error_kind != ERR_NONE) return word_nil();
```

- [ ] **Step 4: Build**

```bash
cd build && ninja 2>&1
```

Expected: compiles clean

- [ ] **Step 5: Commit**

```bash
git add src/reader/reader.c
git commit -m "fix: use VM_ERROR for reader parse errors"
```

---

### Task 8: Update compiler.c — DASSERT

**Files:**
- Modify: `src/bootstrap/compiler.c`

- [ ] **Step 1: Add debug.h include**

Add after `#include "compiler.h"` (line 1):

```c
#include "compiler.h"
#include "debug.h"
#include "prim.h"
```

- [ ] **Step 2: Add DASSERT_TYPE to compile_list entry**

At the top of `compile_list` (after local variable declarations at line 41-42), add a DASSERT that the head of the list is a pair (the expression being compiled should always be a proper list):

```c
static void compile_list(code_buf_t* buf, vm_state_t* vm, word expr, int* next_local) {
    word* hdr = ptr_from_word(expr);
    DASSERT_TYPE(expr, OBJ_TYPE_PAIR);
    word fn = pair_car(hdr);
```

- [ ] **Step 3: Build**

```bash
cd build && ninja 2>&1
```

Expected: compiles clean

- [ ] **Step 4: Commit**

```bash
git add src/bootstrap/compiler.c
git commit -m "fix: add DASSERT_TYPE check to compiler list entry"
```

---

### Task 9: Update main.c — Handler Registration and Error Display

**Files:**
- Modify: `src/main.c`

- [ ] **Step 1: Add debug.h include**

Add `#include "debug.h"` after existing includes.

- [ ] **Step 2: Register crash handlers in main()**

After `prim_init_all(vm);`, add:
```c
    debug_install_handlers(vm);
```

- [ ] **Step 3: Replace error_code checks with error_kind**

In `repl()` function, change:
```c
        if (vm->error_code) {
            vm->error_code = 0;
            printf("read error\n");
            continue;
        }
```

To:
```c
        if (vm->error_kind != ERR_NONE) {
            fprintf(stderr, "Error: [%d] %s\n", (int)vm->error_kind,
                    vm->error_msg ? vm->error_msg : "unknown");
            vm->error_kind = ERR_NONE;
            continue;
        }
```

And similarly in `exec_file()`:
```c
        if (vm->error_code) {
            vm->error_code = 0;
            fprintf(stderr, "read error at position %d\n", pos);
            break;
        }
```

To:
```c
        if (vm->error_kind != ERR_NONE) {
            fprintf(stderr, "Error at position %d: [%d] %s\n", pos,
                    (int)vm->error_kind,
                    vm->error_msg ? vm->error_msg : "unknown");
            vm->error_kind = ERR_NONE;
            break;
        }
```

- [ ] **Step 4: Build**

```bash
cd build && ninja 2>&1
```

Expected: build succeeds clean (this is the last of the error_code fixes)

- [ ] **Step 5: Commit**

```bash
git add src/main.c
git commit -m "fix: register crash handlers, use enriched error display in REPL"
```

---

### Task 10: Integration Verification

**Files:** none (verification only)

- [ ] **Step 1: Full debug build**

```bash
cd /data/data/com.termux/files/home/scheme/.worktrees/stage0-c-core
meson setup build --reconfigure -Dbuild_type=debug 2>&1 | tail -5
cd build && ninja 2>&1
```

Expected: build succeeds with all sanitizer flags, no errors

- [ ] **Step 2: Run existing tests**

```bash
cd build && meson test -v 2>&1
```

Expected: both tests (types, gc) pass. ASAN/UBSAN flags should be active and not flag any issues.

- [ ] **Step 3: Verify release build still works**

```bash
meson setup build-release --reconfigure -Dbuild_type=release 2>&1 | tail -3
cd build-release && ninja 2>&1
cd build-release && meson test -v 2>&1
```

Expected: release build compiles and tests pass. No sanitizer overhead.

- [ ] **Step 4: Quick smoke test — crash handler**

Create a test program that triggers a crash:
```bash
echo '(car 42)' | ./build/scheme 2>&1
```

Expected for debug build: should show DASSERT_TYPE failure with file/line info and abort, or show runtime error about type mismatch (depends on whether the `car` primitive or the DASSERT fires first).
Expected for release build: should show "Error: [1] car: expected pair" (clean error, no crash).

- [ ] **Step 5: Commit (if any test fixes were needed)**

No commit expected — verification only. All prior tasks should have commits.

---

### Task 11: Final Clean Build Verification

**Files:** none

- [ ] **Step 1: Clean debug build from scratch**

```bash
cd /data/data/com.termux/files/home/scheme/.worktrees/stage0-c-core
rm -rf build
meson setup build -Dbuild_type=debug
cd build && ninja
```

Expected: zero warnings, zero errors

- [ ] **Step 2: Run full test suite**

```bash
cd build && meson test -v
```

Expected: all tests pass

- [ ] **Step 3: Verify all commits are present**

```bash
git log --oneline -10
```

Expected: ~8 commits covering the implementation
