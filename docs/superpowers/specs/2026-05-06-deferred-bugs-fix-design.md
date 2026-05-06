# Fix Deferred Bugs — feature/macro-closure

Fix the 4 deferred issues documented in `docs/deferred-issues.md` on the
`feature/macro-closure` branch, in priority order. After each fix, re-test
downstream issues since they may share root causes.

## Issue 1 (P0): Multi-macro SIGSEGV — GC root for code_objects

**Root cause:** `gc_collect()` does not scan `vm->code_objects` as a GC root.
When GC fires during the second macro expansion's allocation, code objects
(including the currently executing one) are freed.

**Fix:** Add `vm->code_objects[0..vm->code_count-1]` to GC root scanning in
`gc_mark_roots()`. Also mark each CodeObject's bytecode buffer (`code->code`)
and constants table (`code->literals`) as roots.

**Verification:** The two-macro file repro (`when` + `unless`) no longer
SIGSEGVs. Add printf in gc_collect to confirm GC fires and code objects
survive.

**Files:** `src/gc/gc.c`, `src/vm/vm.h`, `src/include/types.h`

## Issue 2 (P1): Compound ellipsis symbol corruption

**Re-test first** after Issue 1 fix — the GREF corruption may be a symptom of
GC freeing live code objects mid-expansion.

If still broken after Issue 1:
- Compare `vm->globals[slot]` for helper functions before and after
  transformer trampoline invocation in `scheme_expand_macro`
- Check that GREF slot indices computed during compilation match those at
  execution time
- Likely fix: ensure global table is stable across the expand/compile/execute
  cycle, or recompute GREF slots after each compilation

**Files:** `src/bootstrap/compiler.c`, `src/scheme/compiler.scm`

## Issue 3 (P2): call/cc frame setup

**Root cause:** Stack pointer off-by-one in OP_CALL_CC frame setup. The proc's
bytecode runs with sp pointing to the wrong slot.

**Fix:** Compare OP_CALL_CC frame layout with a normal `((lambda (k) 42) 'ignored)`
call. Align sp so the first PUSH-INT in the lambda body writes to the correct
slot. Likely a one-line sp adjustment in `src/vm/vm.c`.

**Files:** `src/vm/vm.c`

## Issue 4 (P2): do special form S-expression construction

**Fix path:** The `do` desugaring produces a wrong S-expression. Build and
validate each sub-expression step-by-step:
1. `(do ((i 0 (+ i 1))) ((>= i 5) i))` should produce
   `(let loop ((i 0)) (if (>= i 5) i (loop (+ i 1))))`
2. Add debug prints for each cons/append in the do handler
3. Compare against the expected structure

**Files:** `src/bootstrap/compiler.c`

## Verification

After all fixes:
- `echo '(when #t 42) (unless #f 99)' | ./build/src/scheme` works
- `./build/src/scheme test.ss` with multi-macro file works
- `(letrec ((fact (lambda (n) (if (= n 0) 1 (* n (fact (- n 1))))))) (fact 5))` → 120
- `(call/cc (lambda (k) 42))` → 42
- `(do ((i 0 (+ i 1))) ((>= i 5) i))` → 5
- `meson test -C build` passes
