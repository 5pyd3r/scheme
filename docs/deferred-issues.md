# Deferred Issues — scheme bootstrap

Reproduction environment: `cd /data/data/com.termux/files/home/scheme/.worktrees/macro-closure && meson compile -C build`

## Issue 1: Multi-macro SIGSEGV (P0 — blocks file mode)

**Repro:**
```
echo '(when #t 42)' | ./build/src/scheme     # works
echo '(when #f 42)' | ./build/src/scheme     # works
```
File with two macros:
```
(when #t (display 42)) (newline)
(unless #f (display 99)) (newline)
(display 1) (newline)
```
→ `./build/src/scheme test.ss` → SIGSEGV after "99" prints

**Diagnosis:**
- First macro works, second macro produces correct output then crashes
- Single macro from file works; one macro + non-macro works
- Two non-macro expressions from file works
- REPL single-expression macro works

**Attempted fixes that did NOT resolve:**
- Sp save/restore in scheme_expand_macro, scheme_compile_and_assemble, main.c
- Error state save/restore (error_kind, error_msg)
- Cached trampoline code objects (prevent code_objects accumulation)
- vm_execute internal sp save/restore
- Direct transformer call instead of eval in _expand-once

**Likely root cause:**
`vm->code_objects` array is not scanned as a GC root. When GC fires during
alloc_words in the second expansion/compilation, code objects (including the
currently executing one) are freed. Next instruction fetch → SIGSEGV.
Also possible: vm_execute modifies some VM state not captured by the
save/restore (code_count, code_objects pointer after realloc, stack_cap).

**Debug approach:**
1. Add printf in gc_collect to see if GC fires between expansions
2. Add printf in gc_sweep to see which objects are freed
3. Run under GDB: `gdb --args ./build/src/scheme test.ss`
4. Break on gc_sweep, check all_objects list
5. Verify vm->code_objects entries are still valid after GC

**Mitigation for now:** Use REPL mode (single expression per invocation) or
avoid multiple macros in same file. C-compiler special forms (letrec, case)
don't trigger this path.

---

## Issue 2: Compound ellipsis symbol corruption (P1 — blocks letrec/do macros)

**Repro:**
```scheme
(_fill-compound-ellipsis '(var val)
  (list (cons 'var '(x y)) (cons 'val '(1 2))) 1000)
```
Returns correct `((x 1) (y 2))` standalone.

But through full macro expansion pipeline:
```scheme
(let ((t (_lookup-macro 'letrec)))
  (t '(letrec ((x 1)) x)))
```
Returns `(let ...)` with correct structure (car=let, length=3) but
`(equal? result '(let ((x 1)) x))` → #f.

**Diagnosis:**
- `_match-compound-ellipsis` works standalone ✓
- `_build-compound-bindings` works standalone ✓
- `_fill-template` with compound bindings works standalone ✓
- Full pipeline through `_try-clauses → _match-pat → _fill-template`:
  result crashes when `equal?` traverses it (SIGSEGV)
- `_count-exprs` returns `#<pair>` instead of fixnum when called
  from within `_fill-compound-ellipsis` during macro expansion
  (works standalone, returns 3 for '(a b c))

**Likely root cause:**
GREF/CALL to helper functions inside the transformer execution context
returns wrong values. The transformer runs within `_expand-once → (t form)`,
which is a CALL frame set up by `scheme_expand_macro`'s trampoline. Within
this frame, GREF for `_count-exprs` (or other helpers) may return wrong
values due to global slot corruption or GREF slot mismatch.

**Debug approach:**
1. Add printf in `_fill-compound-ellipsis` to print n value
2. Add printf in `_count-exprs` to print input and output
3. Check if global slot indices for helper functions are consistent
   between standalone calls and calls within transformer context
4. Compare `vm->globals[slot]` before and after transformer invocation

---

## Issue 3: call/cc frame setup (P2)

**Repro:**
```scheme
(call/cc (lambda (k) 42))      → returns 0 (should be 42)
(call/cc (lambda (k) (+ 1 2))) → returns 2 (should be 3)
```

**Status:**
- OP_CALL_CC opcode defined (0x33)
- C compiler emits OP_CALL_CC for `call/cc` calls
- Continuation type (OBJ_TYPE_CONTINUATION = 13) added
- vm_restore_continuation() implemented in continuation.c
- Frame setup in OP_CALL_CC copies OP_CALL pattern but produces wrong results

**Diagnosis:**
Stack positions verified correct via debug printf. The result is always the
second value or 0. Likely the proc's bytecode is executed with sp pointing
one position too high or too low, so the first PUSH-INT writes to the wrong
slot.

**Debug approach:**
1. Add printf for every PUSH-INT in the lambda body during execution
2. Compare sp positions with a normal `((lambda (k) 42) 'ignored)` call
3. Single-step OP_CALL_CC frame setup vs OP_CALL frame setup

---

## Issue 4: do special form (P2 — blocked by named let working but S-expr buggy)

**Status:**
- Named let recursion works via temporary global binding
- do desugaring builds S-expression `(let loop ((var init) ...) (if ...))`
  but the S-expression construction crashes or produces wrong structure

**Fix path:** Build and test S-expression for `(do ((i 0 (+ i 1))) ((>= i 5) i))`
step by step. The desugaring should produce:
```scheme
(let loop ((i 0)) (if (>= i 5) i (loop (+ i 1))))
```
which works when typed directly.

---

## Quick reference: Key files

| File | Lines | Purpose |
|------|-------|---------|
| src/vm/vm.c | ~545 | VM execution loop, OP_CALL, OP_RETURN, OP_CALL_CC |
| src/bootstrap/compiler.c | ~1480 | C compiler, special forms, expand_macro, compile_and_assemble |
| src/scheme/compiler.scm | ~355 | Scheme compiler, pattern matcher, template filler |
| src/scheme/lib.scm | ~405 | Standard library, macros (when, unless, letrec) |
| src/primitives/continuation.c | ~30 | Continuation type, vm_restore_continuation |
| src/main.c | ~165 | REPL, exec_file |
| src/reader/reader.c | ~255 | S-expression reader |
| src/gc/gc.c | ~155 | Garbage collector |
| tests/scheme/test-features.ss | ~55 | Feature integration tests |
