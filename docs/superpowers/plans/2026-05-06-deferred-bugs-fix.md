# Deferred Bugs Fix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix all 4 deferred bugs on the `feature/macro-closure` branch in priority order, with re-test of downstream issues after each fix.

**Architecture:** Root-cause cascade. Issue 1 (GC root missing) is fundamental — fix it first then re-test Issues 2-4. Issues 3-4 are independent implementation gaps. Each issue has its own task group with build/verify steps.

**Tech Stack:** C99, Meson build system, Scheme (R7RS)

**Key insight:** `gc_collect()` at `src/gc/gc.c:145` sweeps with zero roots marked. The design has `gc_mark_root()`/`gc_mark_stack()` but no mechanism for the VM to provide roots before `gc_alloc_words()` internally triggers collection. We add a root-marker callback to the GC interface.

---

## File Map

| File | Purpose | Bugs touched |
|------|---------|-------------|
| `src/gc/gc.c` | GC: add root-marker callback, call before sweep | Issue 1 |
| `src/include/gc.h` | GC interface: add `set_root_marker` to struct | Issue 1 |
| `src/vm/vm.c` | VM: register root marker, implement OP_CALL_CC | Issue 1, 3 |
| `src/scheme/lib.scm` | Add `do` macro (syntax-rules desugaring) | Issue 4 |

---

### Task 1: Add root-marker callback to GC interface

**Files:**
- Modify: `src/include/gc.h`
- Modify: `src/gc/gc.c`

- [ ] **Step 1: Add `set_root_marker` to gc_interface in gc.h**

Add the `set_root_marker` function pointer and `root_marker` fields to the interface struct:

```c
// src/include/gc.h — modify gc_interface struct:
typedef struct {
    word* (*alloc_words)(size_t nwords);
    void  (*collect)(void);
    void  (*mark_root)(word w);
    void  (*mark_stack)(word* stack, size_t count);
    size_t (*heap_used)(void);
    void  (*set_root_marker)(void (*fn)(void*), void* state);
    void* (*state_ref)(void);
    void*  state;
} gc_interface;
```

- [ ] **Step 2: Implement root-marker storage and setter in gc.c**

In `src/gc/gc.c`, add static variables and wire the setter:

```c
// Add at top of gc.c (after existing static globals):
static void (*gc_root_marker)(void*) = NULL;
static void* gc_root_marker_state = NULL;

// Add setter function:
static void gc_set_root_marker(void (*fn)(void*), void* state) {
    gc_root_marker = fn;
    gc_root_marker_state = state;
}

// Add state_ref function (for VM to get at GC internals if needed):
static void* gc_state_ref(void) {
    return NULL;
}
```

- [ ] **Step 3: Call root marker in gc_collect before sweep**

Modify `gc_collect()` to call the root marker before sweeping:

```c
static void gc_collect(void) {
    if (collecting) return;
    collecting = true;
    if (gc_root_marker) {
        gc_root_marker(gc_root_marker_state);
    }
    gc_sweep();
    collecting = false;
}
```

- [ ] **Step 4: Wire new functions into gc_interface in gc_init()**

In `gc_init()`, add the new function pointers:

```c
gc_interface* gc_init(void) {
    static gc_interface gc;
    gc.alloc_words      = gc_alloc_words;
    gc.collect          = gc_collect;
    gc.mark_root        = gc_mark_root;
    gc.mark_stack       = gc_mark_stack;
    gc.heap_used        = gc_heap_used;
    gc.set_root_marker  = gc_set_root_marker;
    gc.state_ref        = gc_state_ref;
    return &gc;
}
```

- [ ] **Step 5: Build and verify compilation**

```bash
cd /app/scheme && meson compile -C build
```

Expected: Compiles cleanly (no functional change yet — root marker is NULL).

- [ ] **Step 6: Commit**

```bash
git add src/include/gc.h src/gc/gc.c
git commit -m "fix(gc): add root-marker callback to GC interface

Add set_root_marker() to gc_interface so the VM can register a callback
that marks all VM roots before gc_collect() sweeps. Currently the
callback is NULL so behavior is unchanged.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

### Task 2: Register VM root marker for all live references

**Files:**
- Modify: `src/vm/vm.c`

- [ ] **Step 1: Implement `vm_mark_roots()` function in vm.c**

Add this function before `vm_init()`:

```c
static void vm_mark_roots(void* state) {
    vm_state_t* vm = (vm_state_t*)state;
    gc_interface* gc = vm->gc;
    if (!gc) return;

    // Mark all words on the active stack (sp through sp_max)
    for (word* p = vm->stack; p <= vm->sp; p++)
        gc->mark_root(*p);

    // Mark frame register chain — walk fp values stored in frames
    // Each frame has fp[0] = saved_fp, so walking is automatic if
    // the stack is marked. We also mark raw pointers.
    if (vm->env)   gc->mark_root(ptr_to_word(vm->env));
    if (vm->current_code) gc->mark_root(ptr_to_word(vm->current_code));
    gc->mark_root(vm->acc);

    // Mark all loaded code objects
    for (size_t i = 0; i < vm->code_count; i++) {
        if (vm->code_objects[i])
            gc->mark_root(ptr_to_word(vm->code_objects[i]));
    }

    // Mark all globals and their names
    for (int i = 0; i < vm->next_global_slot; i++) {
        gc->mark_root(vm->globals[i]);
        gc->mark_root(vm->global_names[i]);
    }

    // Mark symbol table
    for (size_t i = 0; i < vm->symbol_count; i++)
        gc->mark_root(vm->symbol_table[i]);

    // Mark error arg
    gc->mark_root(vm->error_arg);
}
```

- [ ] **Step 2: Register the root marker at end of vm_init()**

At the end of `vm_init()`, before `return vm;`, add:

```c
    gc->set_root_marker(vm_mark_roots, vm);
```

- [ ] **Step 3: Build and verify compilation**

```bash
cd /app/scheme && meson compile -C build
```

Expected: Compiles cleanly.

- [ ] **Step 4: Test Issue 1 repro — two macros from file**

Write the test file and run it:

```bash
cat > /tmp/test-issue1.ss << 'SCHEME'
(when #t (display 42)) (newline)
(unless #f (display 99)) (newline)
(display 1) (newline)
SCHEME
echo '(when #t 42)' | ./build/src/scheme       # should work
echo '(when #f 42)' | ./build/src/scheme       # should work
./build/src/scheme /tmp/test-issue1.ss          # should print 42 99 1, no SIGSEGV
```

Expected: All three work without SIGSEGV.

- [ ] **Step 5: Re-test Issue 2 repro — letrec macro through full pipeline**

```bash
echo '(letrec ((x 1)) x)' | ./build/src/scheme
```

Expected: Returns 1. If still broken, proceed to Task 3.

- [ ] **Step 6: Commit**

```bash
git add src/vm/vm.c
git commit -m "fix(vm): register GC root marker covering stack, code, globals, symbols

Before gc_collect sweeps, vm_mark_roots marks all live references:
stack region (sp), env, current_code, acc, code_objects array, globals,
symbol table, and error_arg. This fixes the multi-macro SIGSEGV caused
by GC freeing code objects still in use.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

### Task 3: Implement OP_CALL_CC in VM execution loop

**Files:**
- Modify: `src/vm/vm.c`

- [ ] **Step 1: Add OP_CALL_CC case to switch in vm_execute()**

Insert after the OP_JMP_IF_NOT case (around line 255), before OP_CONS:

```c
        case OP_CALL_CC: {
            uint8_t nargs = read_u8(&vm->ip); // always 1
            word cc_proc = *vm->sp;           // proc is on top of stack

            DASSERT_TYPE(cc_proc, OBJ_TYPE_CLOSURE);

            // Allocate continuation object: 3 header + 5 data words
            word* cont = vm->gc->alloc_words(3 + 5);
            obj_set_type(cont, OBJ_TYPE_CONTINUATION);
            // Save sp offset from stack base
            cont[DATA_START_INDEX + 0] = (word)(vm->sp - vm->stack);
            // Save fp offset
            cont[DATA_START_INDEX + 1] = (word)(vm->fp - vm->stack);
            // Save ip
            cont[DATA_START_INDEX + 2] = (word)(uintptr_t)vm->ip;
            // Save current_code
            cont[DATA_START_INDEX + 3] = (word)(uintptr_t)vm->current_code;
            // Save env
            cont[DATA_START_INDEX + 4] = (word)(uintptr_t)vm->env;

            // Replace proc on stack with continuation object
            *vm->sp = ptr_to_word(cont);

            // Now perform a CALL with the proc and 1 arg (the continuation)
            word* clo = ptr_from_word(cc_proc);
            word nfree_word = clo[DATA_START_INDEX + 2];
            uint8_t nfree = (uint8_t)(nfree_word & 0xFF);

            // base = sp (points to continuation, which is the single arg)
            word* base = vm->sp;

            // Save caller sp (restore to before the arg)
            word old_sp = (word)(uintptr_t)(base - 1);

            // Shift the single arg (continuation) to make room for frame header
            base[4 + nfree] = base[0];   // shift arg past header + free vars

            // Write frame header
            base[0] = old_sp;
            base[1] = (word)(uintptr_t)vm->ip;
            base[2] = (word)(uintptr_t)vm->env;
            base[3] = (word)(uintptr_t)vm->fp;

            // Unpack captured free variables
            if (nfree > 0) {
                word* env_vec = ptr_from_word(closure_env(clo));
                for (int i = 0; i < nfree; i++)
                    base[4 + i] = env_vec[DATA_START_INDEX + 1 + i];
            }

            // Set new frame
            vm->fp = base + 3;
            vm->env = ptr_from_word(closure_env(clo));
            vm->sp = base + 4 + nargs + nfree;

            vm->current_code = ptr_from_word(closure_code(clo));
            vm->ip = code_bytes(vm->current_code);
            break;
        }
```

- [ ] **Step 2: Build and verify compilation**

```bash
cd /app/scheme && meson compile -C build
```

Expected: Compiles cleanly.

- [ ] **Step 3: Test call/cc basic cases**

```bash
echo '(call/cc (lambda (k) 42))' | ./build/src/scheme
echo '(call/cc (lambda (k) (+ 1 2)))' | ./build/src/scheme
echo '(call/cc (lambda (k) (k 99)))' | ./build/src/scheme
```

Expected: 42, 3, 99 respectively.

- [ ] **Step 4: Commit**

```bash
git add src/vm/vm.c
git commit -m "fix(vm): implement OP_CALL_CC handler in execution loop

Allocate a continuation object saving sp, fp, ip, current_code, env.
Replace proc on stack with continuation, then perform standard CALL
frame setup. This fixes call/cc returning wrong results (was falling
through to default: assert due to missing case).

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

### Task 4: Add `do` macro to lib.scm

**Files:**
- Modify: `src/scheme/lib.scm`

- [ ] **Step 1: Add `do` macro as syntax-rules in lib.scm**

Add at the end of `src/scheme/lib.scm`:

```scheme
;; do: iteration macro — desugars to named let
;; (do ((var init step) ...) (test result ...) command ...)
;;   → (let loop ((var init) ...)
;;       (if test
;;           (begin result ...)
;;           (begin command ... (loop step ...))))
(define-syntax do
  (syntax-rules ()
    ((_ ((var init step) ...) (test result ...) command ...)
     (let loop ((var init) ...)
       (if test
           (begin result ...)
           (begin command ... (loop step ...)))))))
```

- [ ] **Step 2: Build and verify compilation**

```bash
cd /app/scheme && meson compile -C build
```

Expected: Compiles cleanly.

- [ ] **Step 3: Test `do` with simple counting example**

```bash
echo '(do ((i 0 (+ i 1))) ((>= i 5) i) (display i) (newline))' | ./build/src/scheme
```

Expected: Prints 0 1 2 3 4, returns 5.

- [ ] **Step 4: Test `do` with empty body and result**

```bash
echo '(do ((i 0 (+ i 1))) ((= i 3) (* i 10)))' | ./build/src/scheme
```

Expected: 30.

- [ ] **Step 5: Commit**

```bash
git add src/scheme/lib.scm
git commit -m "feat: add do iteration macro (syntax-rules)

Desugars to named let with if: (do ((var init step) ...) (test expr ...)
body ...) → (let loop ((var init) ...) (if test (begin expr ...)
(begin body ... (loop step ...)))).

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

### Task 5: Full integration test

**Files:**
- Create: `/tmp/test-all.ss`

- [ ] **Step 1: Write comprehensive test file**

```bash
cat > /tmp/test-all.ss << 'SCHEME'
;; Test 1: Multi-macro file (Issue 1 — no SIGSEGV)
(when #t (display "1: ok")) (newline)
(unless #f (display "2: ok")) (newline)

;; Test 2: letrec macro (Issue 2 — compound ellipsis)
(letrec ((fact (lambda (n) (if (= n 0) 1 (* n (fact (- n 1)))))))
  (display "fact(5)=") (display (fact 5)) (newline))

;; Test 3: call/cc (Issue 3)
(display "call/cc simple: ") (display (call/cc (lambda (k) 42))) (newline)
(display "call/cc invoke: ")
(display (call/cc (lambda (k) (k 99)))) (newline)

;; Test 4: do loop (Issue 4)
(display "do loop: ")
(display (do ((i 0 (+ i 1))) ((>= i 5) i))) (newline)

;; Test 5: do with body
(do ((i 0 (+ i 1))) ((>= i 3) 'done)
  (display "iter: ") (display i) (newline))
SCHEME
```

- [ ] **Step 2: Run full integration test**

```bash
./build/src/scheme /tmp/test-all.ss
```

Expected output:
```
1: ok
2: ok
fact(5)=120
call/cc simple: 42
call/cc invoke: 99
do loop: 5
iter: 0
iter: 1
iter: 2
```

- [ ] **Step 3: Run existing meson tests**

```bash
cd /app/scheme && meson test -C build
```

Expected: All tests pass, no regressions.

- [ ] **Step 4: Commit**

```bash
git commit --allow-empty -m "test: add full integration test for deferred bugs 1-4

Verifies multi-macro, letrec, call/cc, and do all work correctly.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```
```

<｜｜DSML｜｜parameter name="content" string="true"># Deferred Bugs Fix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix all 4 deferred bugs on the `feature/macro-closure` branch in priority order, with re-test of downstream issues after each fix.

**Architecture:** Root-cause cascade. Issue 1 (GC root missing) is fundamental — fix it first then re-test Issues 2-4. Issues 3-4 are independent implementation gaps. Each issue has its own task group with build/verify steps.

**Tech Stack:** C99, Meson build system, Scheme (R7RS)

**Key insight:** `gc_collect()` at `src/gc/gc.c:145` sweeps with zero roots marked. The design has `gc_mark_root()`/`gc_mark_stack()` but no mechanism for the VM to provide roots before `gc_alloc_words()` internally triggers collection. We add a root-marker callback to the GC interface.

---

## File Map

| File | Purpose | Bugs touched |
|------|---------|-------------|
| `src/gc/gc.c` | GC: add root-marker callback, call before sweep | Issue 1 |
| `src/include/gc.h` | GC interface: add `set_root_marker` to struct | Issue 1 |
| `src/vm/vm.c` | VM: register root marker, implement OP_CALL_CC | Issue 1, 3 |
| `src/scheme/lib.scm` | Add `do` macro (syntax-rules desugaring) | Issue 4 |

---

### Task 1: Add root-marker callback to GC interface

**Files:**
- Modify: `src/include/gc.h`
- Modify: `src/gc/gc.c`

- [ ] **Step 1: Add `set_root_marker` to gc_interface in gc.h**

```c
typedef struct {
    word* (*alloc_words)(size_t nwords);
    void  (*collect)(void);
    void  (*mark_root)(word w);
    void  (*mark_stack)(word* stack, size_t count);
    size_t (*heap_used)(void);
    void  (*set_root_marker)(void (*fn)(void*), void* state);
    void* (*state_ref)(void);
    void*  state;
} gc_interface;
```

- [ ] **Step 2: Implement root-marker storage and setter in gc.c**

After the existing static globals in `src/gc/gc.c`:

```c
static void (*gc_root_marker)(void*) = NULL;
static void* gc_root_marker_state = NULL;

static void gc_set_root_marker(void (*fn)(void*), void* state) {
    gc_root_marker = fn;
    gc_root_marker_state = state;
}

static void* gc_state_ref(void) {
    return NULL;
}
```

- [ ] **Step 3: Call root marker in gc_collect before sweep**

```c
static void gc_collect(void) {
    if (collecting) return;
    collecting = true;
    if (gc_root_marker) {
        gc_root_marker(gc_root_marker_state);
    }
    gc_sweep();
    collecting = false;
}
```

- [ ] **Step 4: Wire new functions into gc_interface in gc_init()**

```c
gc_interface* gc_init(void) {
    static gc_interface gc;
    gc.alloc_words      = gc_alloc_words;
    gc.collect          = gc_collect;
    gc.mark_root        = gc_mark_root;
    gc.mark_stack       = gc_mark_stack;
    gc.heap_used        = gc_heap_used;
    gc.set_root_marker  = gc_set_root_marker;
    gc.state_ref        = gc_state_ref;
    return &gc;
}
```

- [ ] **Step 5: Build and verify compilation**

```bash
cd /app/scheme && meson compile -C build
```

Expected: Compiles cleanly.

- [ ] **Step 6: Commit**

```bash
git add src/include/gc.h src/gc/gc.c
git commit -m "fix(gc): add root-marker callback to GC interface

Add set_root_marker() to gc_interface so the VM can register a callback
that marks all VM roots before gc_collect() sweeps. Currently the
callback is NULL so behavior is unchanged.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

### Task 2: Register VM root marker for all live references

**Files:**
- Modify: `src/vm/vm.c`

- [ ] **Step 1: Implement `vm_mark_roots()` function in vm.c**

Before `vm_init()`:

```c
static void vm_mark_roots(void* state) {
    vm_state_t* vm = (vm_state_t*)state;
    gc_interface* gc = vm->gc;
    if (!gc) return;

    // Mark all words on the active stack
    for (word* p = vm->stack; p <= vm->sp; p++)
        gc->mark_root(*p);

    // Mark raw-pointers held in VM registers
    if (vm->env)         gc->mark_root(ptr_to_word(vm->env));
    if (vm->current_code) gc->mark_root(ptr_to_word(vm->current_code));
    gc->mark_root(vm->acc);

    // Mark all loaded code objects
    for (size_t i = 0; i < vm->code_count; i++) {
        if (vm->code_objects[i])
            gc->mark_root(ptr_to_word(vm->code_objects[i]));
    }

    // Mark all globals and their names
    for (int i = 0; i < vm->next_global_slot; i++) {
        gc->mark_root(vm->globals[i]);
        gc->mark_root(vm->global_names[i]);
    }

    // Mark symbol table
    for (size_t i = 0; i < vm->symbol_count; i++)
        gc->mark_root(vm->symbol_table[i]);

    // Mark error arg
    gc->mark_root(vm->error_arg);
}
```

- [ ] **Step 2: Register the root marker at end of vm_init()**

At the end of `vm_init()`, before `return vm;`:

```c
    gc->set_root_marker(vm_mark_roots, vm);
```

- [ ] **Step 3: Build**

```bash
cd /app/scheme && meson compile -C build
```

- [ ] **Step 4: Test Issue 1 — two macros from file**

```bash
cat > /tmp/test-issue1.ss << 'SCHEME'
(when #t (display 42)) (newline)
(unless #f (display 99)) (newline)
(display 1) (newline)
SCHEME
echo '(when #t 42)' | ./build/src/scheme
echo '(when #f 42)' | ./build/src/scheme
./build/src/scheme /tmp/test-issue1.ss
```

Expected: All three work, no SIGSEGV.

- [ ] **Step 5: Re-test Issue 2 — letrec macro**

```bash
echo '(letrec ((x 1)) x)' | ./build/src/scheme
```

If broken, proceed to deeper diagnostics in Task 6.

- [ ] **Step 6: Commit**

```bash
git add src/vm/vm.c
git commit -m "fix(vm): register GC root marker covering stack, code, globals, symbols

Before gc_collect sweeps, vm_mark_roots marks all live references.
Fixes multi-macro SIGSEGV caused by GC freeing code objects in use.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

### Task 3: Implement OP_CALL_CC in VM execution loop

**Files:**
- Modify: `src/vm/vm.c`

- [ ] **Step 1: Add OP_CALL_CC case to switch in vm_execute()**

Insert after the `OP_JMP_IF_NOT` case, before `OP_CONS`:

```c
        case OP_CALL_CC: {
            uint8_t nargs = read_u8(&vm->ip);
            word cc_proc = *vm->sp;

            DASSERT_TYPE(cc_proc, OBJ_TYPE_CLOSURE);

            // Allocate continuation object: 3 header + 5 data words
            word* cont = vm->gc->alloc_words(3 + 5);
            obj_set_type(cont, OBJ_TYPE_CONTINUATION);
            cont[DATA_START_INDEX + 0] = (word)(vm->sp - vm->stack);
            cont[DATA_START_INDEX + 1] = (word)(vm->fp - vm->stack);
            cont[DATA_START_INDEX + 2] = (word)(uintptr_t)vm->ip;
            cont[DATA_START_INDEX + 3] = (word)(uintptr_t)vm->current_code;
            cont[DATA_START_INDEX + 4] = (word)(uintptr_t)vm->env;

            // Replace proc on stack with continuation object
            *vm->sp = ptr_to_word(cont);

            // Perform CALL with the proc and 1 arg (the continuation)
            word* clo = ptr_from_word(cc_proc);
            word nfree_word = clo[DATA_START_INDEX + 2];
            uint8_t nfree = (uint8_t)(nfree_word & 0xFF);
            word* base = vm->sp;

            word old_sp = (word)(uintptr_t)(base - 1);

            // Shift single arg past frame header + free vars
            base[4 + nfree] = base[0];

            // Frame header
            base[0] = old_sp;
            base[1] = (word)(uintptr_t)vm->ip;
            base[2] = (word)(uintptr_t)vm->env;
            base[3] = (word)(uintptr_t)vm->fp;

            // Unpack free variables
            if (nfree > 0) {
                word* env_vec = ptr_from_word(closure_env(clo));
                for (int i = 0; i < nfree; i++)
                    base[4 + i] = env_vec[DATA_START_INDEX + 1 + i];
            }

            // Set new frame
            vm->fp = base + 3;
            vm->env = ptr_from_word(closure_env(clo));
            vm->sp = base + 4 + nargs + nfree;
            vm->current_code = ptr_from_word(closure_code(clo));
            vm->ip = code_bytes(vm->current_code);
            break;
        }
```

- [ ] **Step 2: Build and test call/cc**

```bash
cd /app/scheme && meson compile -C build
echo '(call/cc (lambda (k) 42))' | ./build/src/scheme      # Expected: 42
echo '(call/cc (lambda (k) (+ 1 2)))' | ./build/src/scheme  # Expected: 3
echo '(call/cc (lambda (k) (k 99)))' | ./build/src/scheme   # Expected: 99
```

- [ ] **Step 3: Commit**

```bash
git add src/vm/vm.c
git commit -m "fix(vm): implement OP_CALL_CC handler in execution loop

Allocate continuation saving sp/fp/ip/code/env, replace proc with
continuation, then CALL. Fixes call/cc returning wrong results.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

### Task 4: Add `do` macro to lib.scm

**Files:**
- Modify: `src/scheme/lib.scm`

- [ ] **Step 1: Add `do` macro at end of lib.scm**

```scheme
(define-syntax do
  (syntax-rules ()
    ((_ ((var init step) ...) (test result ...) command ...)
     (let loop ((var init) ...)
       (if test
           (begin result ...)
           (begin command ... (loop step ...)))))))
```

- [ ] **Step 2: Build and test**

```bash
cd /app/scheme && meson compile -C build
echo '(do ((i 0 (+ i 1))) ((>= i 5) i) (display i) (newline))' | ./build/src/scheme
echo '(do ((i 0 (+ i 1))) ((= i 3) (* i 10)))' | ./build/src/scheme
```

Expected: First prints 0 1 2 3 4 and returns 5. Second returns 30.

- [ ] **Step 3: Commit**

```bash
git add src/scheme/lib.scm
git commit -m "feat: add do iteration macro (syntax-rules)

Desugars to named let with if: (do ((var init step) ...) (test result ...)
body ...) → (let loop ((var init) ...) (if test (begin result ...)
(begin body ... (loop step ...)))).

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

### Task 5: Full integration verification

- [ ] **Step 1: Write comprehensive test file**

```bash
cat > /tmp/test-all.ss << 'SCHEME'
(when #t (display "1: ok")) (newline)
(unless #f (display "2: ok")) (newline)
(letrec ((fact (lambda (n) (if (= n 0) 1 (* n (fact (- n 1)))))))
  (display "fact(5)=") (display (fact 5)) (newline))
(display "call/cc: ") (display (call/cc (lambda (k) 42))) (newline)
(display "call/cc invoke: ") (display (call/cc (lambda (k) (k 99)))) (newline)
(display "do: ") (display (do ((i 0 (+ i 1))) ((>= i 5) i))) (newline)
SCHEME
```

- [ ] **Step 2: Run full test**

```bash
./build/src/scheme /tmp/test-all.ss
```

Expected:
```
1: ok
2: ok
fact(5)=120
call/cc: 42
call/cc invoke: 99
do: 5
```

- [ ] **Step 3: Run meson test suite**

```bash
cd /app/scheme && meson test -C build
```

Expected: All pass.

- [ ] **Step 4: Commit**

```bash
git commit --allow-empty -m "test: integration verification for deferred bugs 1-4"
```

---

### Task 6 (Conditional): Deep-dive on Issue 2 if GC fix doesn't resolve it

Only proceed if `(letrec ((x 1)) x)` still fails after Tasks 1-2.

- [ ] **Step 1: Add diagnostic prints at critical points in compiler.scm**

In `src/scheme/compiler.scm`, modify `_fill-compound-ellipsis` (line 302) to print the value of `n` returned by `_count-exprs`:

```scheme
(define (_fill-compound-ellipsis inner-tmpl bindings rename-id)
  (let ((vars (_template-vars inner-tmpl)))
    (let ((first-vals (cdr (assq (car vars) bindings))))
      (let ((n (_count-exprs first-vals)))
        (display "DEBUG fill-compound-ellipsis n=") (display n) (newline)
        (display "DEBUG first-vals=") (display first-vals) (newline)
        (_fill-compound-iter inner-tmpl vars bindings 0 n rename-id)))))
```

And in `_count-exprs` (line 19):

```scheme
(define (_count-exprs lst)
  (display "DEBUG _count-exprs input=") (display lst) (newline)
  (let ((result (if (null? lst) 0 (+ 1 (_count-exprs (cdr lst))))))
    (display "DEBUG _count-exprs result=") (display result) (newline)
    result))
```

- [ ] **Step 2: Rebuild and test standalone vs transformer context**

```bash
cd /app/scheme && meson compile -C build
echo '(_count-exprs (quote (a b c)))' | ./build/src/scheme    # standalone: expect 3
echo '(letrec ((x 1)) x)' | ./build/src/scheme                # transformer context
```

- [ ] **Step 3: Diagnose**

If `_count-exprs` returns 3 standalone but a pair within transformer context, check:
- Global slot index for `_count-exprs` in the expander's compiled code
- `vm->globals[slot]` before/after `vm_execute` in `scheme_expand_macro`
- Whether the expander's global slots are stable across recompilation

Likely fix if GREF slot mismatch: compute all helper GREF slots once in
`scheme_expand_macro`, pass them as constants to the expander trampoline
instead of relying on the expander to look them up dynamically.

- [ ] **Step 4: Apply fix, remove debug prints, rebuild, test**

- [ ] **Step 5: Commit**

