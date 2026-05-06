# Macro System and Closure Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add flat-closure capture and `syntax-rules`-based hygienic macros in two phases.

**Architecture:** Phase 1 adds free-variable detection + env threading to the C bootstrap compiler and creates the Scheme compiler (`compiler.scm`). Phase 2 adds `gensym`/`eval` C primitives plus a full `syntax-rules` macro system in Scheme.

**Tech Stack:** C11, Scheme (compiler.scm, lib.scm), meson build, VM bytecode

**Spec:** `docs/superpowers/specs/2026-04-26-macro-closure-implementation.md`

---

## File Structure

| File | Phase | Action | Purpose |
|------|-------|--------|---------|
| `src/bootstrap/compiler.c` | 1 | Modify | Env threading, free var detection, real nfree |
| `src/scheme/compiler.scm` | 1 | Create | Full Scheme compiler with closures |
| `tests/c/test_closure.c` | 1 | Create | C-level closure tests |
| `tests/scheme/test-closure.ss` | 1 | Create | Scheme-level closure tests |
| `src/primitives/macro.c` | 2 | Create | gensym, eval primitives |
| `src/include/vm.h` | 2 | Modify | gensym_counter in vm_state_t |
| `src/vm/builtins.c` | 2 | Modify | Register gensym, eval primitives |
| `src/vm/vm.c` | 2 | Modify | Init gensym_counter in vm_init |
| `src/scheme/lib.scm` | 2 | Modify | *macro-table*, syntax-rules |
| `tests/c/test_macro.c` | 2 | Create | C-level macro primitive tests |
| `tests/scheme/test-macro.ss` | 2 | Create | syntax-rules integration tests |
| `tests/c/meson.build` | 1-2 | Modify | Add test_closure, test_macro |
| `tests/meson.build` | 1-2 | Modify | Add Scheme test entries |
| `src/meson.build` | 2 | Modify | Add macro.c to sources |

---

## Phase 1 — Closures

### Task 1: Add env threading to C compiler signatures

**Files:** Modify `src/bootstrap/compiler.c`

Add `word env` parameter to `compile_expr_to_buf`, `compile_list`, `compile_lambda`. Thread `word_nil()` everywhere. No behavior change — pure plumbing.

- [ ] **Step 1: Update forward declarations and signatures**

Change lines 65-67:
```c
static void compile_lambda(code_buf_t*, vm_state_t*, word args, word body, word env);
static void compile_list(code_buf_t*, vm_state_t*, word expr, local_scope_t*, word env);
static void compile_expr_to_buf(code_buf_t*, vm_state_t*, word expr, local_scope_t*, word env);
```

Change line 69 definition:
```c
static void compile_lambda(code_buf_t* buf, vm_state_t* vm, word args, word body, word env) {
    (void)env;
```

Change line 123 definition:
```c
static void compile_list(code_buf_t* buf, vm_state_t* vm, word expr, local_scope_t* scope, word env) {
    (void)env;
```

Change line 448 definition:
```c
static void compile_expr_to_buf(code_buf_t* buf, vm_state_t* vm, word expr, local_scope_t* scope, word env) {
    (void)env;
```

- [ ] **Step 2: Thread env through all internal call sites**

- `compile_lambda` call at line ~90 (body compilation): pass `word_nil()` for now
- `compile_lambda` call at line ~165 (chain define): pass `env`
- `compile_lambda` call at line ~260 (lambda form): pass `env`
- `compile_expr_to_buf` call at line ~187 (define val): pass `env`
- `compile_expr_to_buf` calls in if/begin/cond/let/call sections: pass `env`
- `compile_expr_to_buf` call at line ~396 (let desugar): pass `env`
- `compile_expr_to_buf` call at line ~462 (pair case): pass `env`
- `compile_expr_to_buf` call at line ~501: pass `word_nil()`

- [ ] **Step 3: Build and verify no regressions**

```bash
cd /data/data/com.termux/files/home/scheme
rtk meson setup build --wipe
rtk meson compile -C build
rtk meson test -C build
```

- [ ] **Step 4: Commit**

```bash
git add src/bootstrap/compiler.c
git commit -m "refactor: thread env parameter through C compiler pipeline"
```

---

### Task 2: Add env_find helper and free-var detection

**Files:** Modify `src/bootstrap/compiler.c`

Add `env_find` to look up symbols in the env alist. Add `collect_free_vars` to walk body S-exprs and identify symbols that are in the enclosing env but not params/primitives.

- [ ] **Step 1: Add env_find function**

Insert before `compile_lambda`:
```c
// env alist: ((sym . slot) ...). Returns slot (>=1) or -1.
static int env_find(word env, word sym) {
    word cur = env;
    while (is_ptr(cur) && obj_type(ptr_from_word(cur)) == OBJ_TYPE_PAIR) {
        word* hdr = ptr_from_word(cur);
        word entry = pair_car(hdr);
        if (is_ptr(entry) && obj_type(ptr_from_word(entry)) == OBJ_TYPE_PAIR) {
            word* eh = ptr_from_word(entry);
            word entry_sym = pair_car(eh);
            if (is_ptr(entry_sym) && is_ptr(sym) &&
                obj_type(ptr_from_word(entry_sym)) == OBJ_TYPE_SYMBOL &&
                obj_type(ptr_from_word(sym)) == OBJ_TYPE_SYMBOL) {
                word* s1 = ptr_from_word(entry_sym);
                word* s2 = ptr_from_word(sym);
                int l1 = (int)string_length(s1);
                int l2 = (int)string_length(s2);
                if (l1 == l2) {
                    int match = 1;
                    for (int i = 0; i < l1; i++)
                        if (string_ref(s1, i) != string_ref(s2, i)) { match = 0; break; }
                    if (match) {
                        word sw = pair_cdr(eh);
                        if (is_fixnum(sw)) return (int)word_to_fixnum(sw);
                    }
                }
            }
        }
        cur = pair_cdr(hdr);
    }
    return -1;
}
```

- [ ] **Step 2: Add sym_in_list helper**

```c
// Returns true if sym is eq?-equivalent to any symbol in the list
static bool sym_in_list(word sym, word list) {
    word cur = list;
    while (is_ptr(cur) && obj_type(ptr_from_word(cur)) == OBJ_TYPE_PAIR) {
        word item = pair_car(ptr_from_word(cur));
        if (is_ptr(item) && is_ptr(sym) &&
            obj_type(ptr_from_word(item)) == OBJ_TYPE_SYMBOL &&
            obj_type(ptr_from_word(sym)) == OBJ_TYPE_SYMBOL) {
            word* s1 = ptr_from_word(item);
            word* s2 = ptr_from_word(sym);
            int l1 = (int)string_length(s1), l2 = (int)string_length(s2);
            if (l1 == l2) {
                int match = 1;
                for (int i = 0; i < l1; i++)
                    if (string_ref(s1, i) != string_ref(s2, i)) { match = 0; break; }
                if (match) return true;
            }
        }
        cur = pair_cdr(ptr_from_word(cur));
    }
    return false;
}
```

- [ ] **Step 3: Add collect_free_vars_inner (recursive walker)**

```c
// Recursively collects free symbols from expr. Skips params, primitives, duplicates.
// collected is a reversed list of symbols (built with cons).
static void collect_free_vars_inner(vm_state_t* vm, word expr, word params, word env, word* collected) {
    if (is_fixnum(expr) || is_char(expr) || is_nil(expr) || is_true(expr) || is_false(expr))
        return;
    if (!is_ptr(expr)) return;
    word* hdr = ptr_from_word(expr);
    int type = obj_type(hdr);

    if (type == OBJ_TYPE_SYMBOL) {
        if (sym_in_list(expr, params)) return;
        if (sym_in_list(expr, *collected)) return;
        char name[64]; int nlen = (int)string_length(hdr);
        if (nlen < 63) {
            for (int i = 0; i < nlen; i++) name[i] = (char)word_to_char(string_ref(hdr, i));
            name[nlen] = '\0';
            if (prim_lookup(name) >= 0) return;
        }
        if (env_find(env, expr) < 0) return;
        word* pp = vm->gc->alloc_words(4);
        obj_set_type(pp, OBJ_TYPE_PAIR);
        pair_car(pp) = expr; pair_cdr(pp) = *collected;
        *collected = ptr_to_word(pp);
        return;
    }

    if (type == OBJ_TYPE_PAIR) {
        word head = pair_car(hdr);
        // Skip walking into inner lambda bodies (new scope)
        if (is_ptr(head) && obj_type(ptr_from_word(head)) == OBJ_TYPE_SYMBOL) {
            if (is_symbol(head, "lambda") || is_symbol(head, "let")) {
                // Only walk the lambda's position in the parent form (e.g., as a value in (define f (lambda ...)))
                collect_free_vars_inner(vm, pair_cdr(hdr), params, env, collected);
                return;
            }
        }
        collect_free_vars_inner(vm, head, params, env, collected);
        collect_free_vars_inner(vm, pair_cdr(hdr), params, env, collected);
    }
}

// Returns alist of (sym . captured_slot) for each free variable found, slots start at nparams+1
static word collect_free_vars(vm_state_t* vm, word body, word params, word env, int nparams) {
    (void)nparams;
    word collected = word_nil();
    collect_free_vars_inner(vm, body, params, env, &collected);
    // collected is reversed source-order list. Reverse to get source-order.
    word ordered = word_nil();
    word cur = collected;
    while (is_ptr(cur) && obj_type(ptr_from_word(cur)) == OBJ_TYPE_PAIR) {
        word* hdr = ptr_from_word(cur);
        word* pp = vm->gc->alloc_words(4); obj_set_type(pp, OBJ_TYPE_PAIR);
        pair_car(pp) = pair_car(hdr); pair_cdr(pp) = ordered;
        ordered = ptr_to_word(pp);
        cur = pair_cdr(hdr);
    }
    // Build bindings alist with slots 1..nfree (captured vars go first in frame)
    int slot = 1;
    word bindings = word_nil();
    cur = ordered;
    while (is_ptr(cur) && obj_type(ptr_from_word(cur)) == OBJ_TYPE_PAIR) {
        word* hdr = ptr_from_word(cur);
        word* entry = vm->gc->alloc_words(4); obj_set_type(entry, OBJ_TYPE_PAIR);
        pair_car(entry) = pair_car(hdr); pair_cdr(entry) = word_from_fixnum(slot);
        word* bp = vm->gc->alloc_words(4); obj_set_type(bp, OBJ_TYPE_PAIR);
        pair_car(bp) = ptr_to_word(entry); pair_cdr(bp) = bindings;
        bindings = ptr_to_word(bp);
        slot++; cur = pair_cdr(hdr);
    }
    return bindings;
}
```

- [ ] **Step 4: Build and verify compiles**

```bash
cd /data/data/com.termux/files/home/scheme
rtk meson compile -C build
```

- [ ] **Step 5: Commit**

```bash
git add src/bootstrap/compiler.c
git commit -m "feat: add env_find and free variable detection to C compiler"
```

---

### Task 3: Implement closure capture in compile_lambda

**Files:** Modify `src/bootstrap/compiler.c`

Replace the `compile_lambda` function to use real capture: detect free vars, build child env, emit LREF for each captured var before OP_CLOSE, set real nfree.

- [ ] **Step 1: Replace compile_lambda**

Replace the entire function (lines 69-121) with:
```c
static void compile_lambda(code_buf_t* buf, vm_state_t* vm, word args, word body, word env) {
    local_scope_t lambda_scope = {0};
    int param_idx = 0;
    word cur = args;
    while (is_ptr(cur) && obj_type(ptr_from_word(cur)) == OBJ_TYPE_PAIR && param_idx < MAX_LOCALS) {
        word param_sym = pair_car(ptr_from_word(cur));
        word* shdr = ptr_from_word(param_sym);
        int plen = (int)string_length(shdr);
        char* pname = (char*)malloc(plen + 1);
        for (int i = 0; i < plen; i++)
            pname[i] = (char)word_to_char(string_ref(shdr, i));
        pname[plen] = '\0';
        lambda_scope.names[param_idx++] = pname;
        cur = pair_cdr(ptr_from_word(cur));
    }
    lambda_scope.count = param_idx;

    // Detect captured free variables
    word captured_bindings = collect_free_vars(vm, body, args, env, param_idx);
    int nfree = 0;
    word cb = captured_bindings;
    while (is_ptr(cb) && obj_type(ptr_from_word(cb)) == OBJ_TYPE_PAIR) {
        nfree++;
        cb = pair_cdr(ptr_from_word(cb));
    }

    // Build child env: captured bindings (slots 1..nfree) + param bindings (slots nfree+1..)
    // VM frame layout: fp[1..nfree]=captured, fp[nfree+1..]=params
    word child_env = captured_bindings;
    cur = args; int pslot = nfree + 1;
    while (is_ptr(cur) && obj_type(ptr_from_word(cur)) == OBJ_TYPE_PAIR) {
        word param_sym = pair_car(ptr_from_word(cur));
        word* entry = vm->gc->alloc_words(4); obj_set_type(entry, OBJ_TYPE_PAIR);
        pair_car(entry) = param_sym; pair_cdr(entry) = word_from_fixnum(pslot);
        word* bp = vm->gc->alloc_words(4); obj_set_type(bp, OBJ_TYPE_PAIR);
        pair_car(bp) = ptr_to_word(entry); pair_cdr(bp) = child_env;
        child_env = ptr_to_word(bp);
        pslot++; cur = pair_cdr(ptr_from_word(cur));
    }

    // Compile body to child code buffer
    code_buf_t child_buf = {0};
    compile_expr_to_buf(&child_buf, vm, body, &lambda_scope, child_env);
    emit_byte(&child_buf, OP_RETURN);

    // Create child code object
    size_t c_bc_words = (child_buf.len + sizeof(word) - 1) / sizeof(word);
    size_t c_total_words = 3 + c_bc_words + child_buf.nconsts;
    word* child_code = vm->gc->alloc_words(c_total_words);
    obj_set_type(child_code, OBJ_TYPE_CODE);
    child_code[2] = (word)child_buf.len;
    memcpy(child_code + 3, child_buf.bytes, child_buf.len);
    for (int i = 0; i < child_buf.nconsts; i++)
        child_code[3 + c_bc_words + i] = child_buf.consts[i];

    int code_idx = vm_load_code(vm, child_code);
    if (code_idx < 0) {
        emit_byte(buf, OP_PUSH_NIL);
        for (int i = 0; i < param_idx; i++) free((void*)lambda_scope.names[i]);
        return;
    }

    // Emit LREF for each captured var (push parent-slot value onto stack)
    word cap = captured_bindings;
    while (is_ptr(cap) && obj_type(ptr_from_word(cap)) == OBJ_TYPE_PAIR) {
        word* eh = ptr_from_word(pair_car(ptr_from_word(cap)));
        word sym = pair_car(eh);
        int parent_slot = env_find(env, sym);
        if (parent_slot > 0) {
            emit_byte(buf, OP_LREF);
            emit_byte(buf, (uint8_t)parent_slot);
        }
        cap = pair_cdr(ptr_from_word(cap));
    }

    // Emit CLOSE
    emit_byte(buf, OP_CLOSE);
    emit_byte(buf, (uint8_t)(code_idx & 0xFF));
    emit_byte(buf, (uint8_t)((code_idx >> 8) & 0xFF));
    emit_byte(buf, (uint8_t)nfree);

    for (int i = 0; i < param_idx; i++) free((void*)lambda_scope.names[i]);
}
```

- [ ] **Step 2: Build and verify**

```bash
cd /data/data/com.termux/files/home/scheme
rtk meson compile -C build
rtk meson test -C build
```

- [ ] **Step 3: Commit**

```bash
git add src/bootstrap/compiler.c
git commit -m "feat: implement closure capture with free variable detection"
```

---

### Task 4: Add env-based symbol dispatch

**Files:** Modify `src/bootstrap/compiler.c`

When compiling a symbol, check the env alist between local scope and global lookup.

- [ ] **Step 1: Add env check in symbol compilation**

In `compile_expr_to_buf` (~line 467), add env check after local scope check:
```c
if (is_ptr(expr) && obj_type(ptr_from_word(expr)) == OBJ_TYPE_SYMBOL) {
    if (scope) {
        int local_idx = local_find(scope, expr);
        if (local_idx > 0) {
            emit_byte(buf, OP_LREF);
            emit_byte(buf, (uint8_t)local_idx);
            return;
        }
    }
    // Check captured env
    {
        int env_slot = env_find(env, expr);
        if (env_slot > 0) {
            emit_byte(buf, OP_LREF);
            emit_byte(buf, (uint8_t)env_slot);
            return;
        }
    }
    // Global lookup (unchanged)
    int slot = vm_find_global_slot(vm, expr);
    ...
}
```

- [ ] **Step 2: Build and verify no regressions**

```bash
cd /data/data/com.termux/files/home/scheme
rtk meson compile -C build
rtk meson test -C build
```

- [ ] **Step 3: Commit**

```bash
git add src/bootstrap/compiler.c
git commit -m "feat: resolve captured vars via env in symbol dispatch"
```

---

### Task 5: C closure integration tests

**Files:** Create `tests/c/test_closure.c`, Modify `tests/c/meson.build`

Test closure capture through the full compile+execute pipeline using the C compiler.

- [ ] **Step 1: Write test_closure.c**

```c
#include "types.h"
#include "vm.h"
#include "gc.h"
#include "pal.h"
#include "compiler.h"
#include "reader.h"
#include "prim.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int nf = 0;
#define CHECK(c,m) do { if(!(c)){printf("FAIL: %s\n",m);nf++;}else{printf("PASS: %s\n",m);} }while(0)

static word eval_c(vm_state_t* vm, const char* src) {
    int pos = 0;
    char* buf = strdup(src);
    word expr = read_sexp(vm, buf, &pos);
    free(buf);
    if (vm->error_kind != ERR_NONE) { vm->error_kind = ERR_NONE; return word_nil(); }
    word code = compile_expr(vm, expr);
    int ci = vm_load_code(vm, ptr_from_word(code));
    return vm_execute(vm, ci);
}

int main(void) {
    pal_interface* pal = pal_init();
    gc_interface* gc = gc_init();
    vm_state_t* vm = vm_init(gc, pal);
    prim_init_all(vm);

    // 1. No capture (nfree=0)
    CHECK(word_to_fixnum(eval_c(vm, "((lambda (x) (+ x 1)) 41)")) == 42,
          "no capture: ((lambda (x) (+ x 1)) 41) = 42");

    // 2. Simple closure — single captured var
    CHECK(word_to_fixnum(eval_c(vm, "(((lambda (x) (lambda (y) (+ x y))) 1) 2)")) == 3,
          "simple closure: add1 then add2");

    // 3. Multiple captured vars
    CHECK(word_to_fixnum(eval_c(vm, "(((lambda (a b) (lambda (c) (+ a (+ b c)))) 10 20) 30)")) == 60,
          "multi capture: a=10 b=20 c=30");

    // 4. Shadowing — inner param shadows outer
    CHECK(word_to_fixnum(eval_c(vm, "(((lambda (x) (lambda (x) (+ x 1))) 100) 200)")) == 201,
          "shadowing: inner x shadows outer");

    // 5. Closure via define
    eval_c(vm, "(define (make-adder n) (lambda (x) (+ x n)))");
    CHECK(word_to_fixnum(eval_c(vm, "((make-adder 5) 10)")) == 15,
          "define closure: ((make-adder 5) 10) = 15");

    // 6. No free vars (all params or globals only)
    CHECK(word_to_fixnum(eval_c(vm, "((lambda (x) (+ x 1)) 7)")) == 8,
          "no free vars: simple lambda");

    printf("\n%d failures\n", nf);
    return nf;
}
```

- [ ] **Step 2: Add to tests/c/meson.build**

```python
test_closure = executable('test_closure',
  'test_closure.c',
  include_directories: test_scheme_inc,
  link_with: scheme_core,
  link_args: ['-lm'],
)
test('closure', test_closure)
```

- [ ] **Step 3: Build and run closure tests**

```bash
cd /data/data/com.termux/files/home/scheme
rtk meson setup build --wipe
rtk meson compile -C build
rtk meson test -C build closure
```

Expected: all 6 PASS.

- [ ] **Step 4: Commit**

```bash
git add tests/c/test_closure.c tests/c/meson.build
git commit -m "test: add C-level closure capture tests"
```

---

### Task 6: Scheme compiler — bytecode builder and skeleton

**Files:** Create `src/scheme/compiler.scm`

Create the Scheme-level compiler. Defines global `(compile expr)` returning `(bytecodes . consts)`. Starts with bytecode builder infrastructure and basic expression dispatch.

- [ ] **Step 1: Write compiler.scm — header and opcodes**

```scheme
; src/scheme/compiler.scm — Scheme compiler with closure + macro support
; Provides global (compile expr) → (bytecodes . consts)
; Loaded by C bootstrap compiler (use_scheme=0)

; === Opcodes (must match src/vm/opcodes.h) ===
(define OP-NOP          0)
(define OP-PUSH-NIL     1)
(define OP-PUSH-TRUE    2)
(define OP-PUSH-FALSE   3)
(define OP-PUSH-CONST   4)
(define OP-PUSH-INT     5)
(define OP-POP          6)
(define OP-DUP          7)
(define OP-LREF        16)
(define OP-LSET        17)
(define OP-GREF        20)
(define OP-GSET        21)
(define OP-CLOSE       32)
(define OP-CALL        33)
(define OP-TAIL-CALL   34)
(define OP-RETURN      36)
(define OP-JMP         48)
(define OP-JMP-IF      49)
(define OP-JMP-IF-NOT  50)
(define OP-CONS        64)
(define OP-CAR         65)
(define OP-CDR         66)
(define OP-MAKE-VEC    69)
(define OP-VEC-REF     70)
(define OP-VEC-SET     71)
(define OP-PRIM-CALL   80)
(define OP-HALT       255)
```

- [ ] **Step 2: Add bytecode builder**

```scheme
; === Forward-list bytecode builder with backpatching ===
; cb structure: (head . tail-ptr) — head is a dummy pair, tail-ptr
; tracks the last pair. We append at tail and use list-tail+set-car! to patch.

(define (_make-cb)
  (let ((dummy (cons 'dummy '())))
    (cons dummy dummy)))

(define (_emit-byte! cb b)
  (let ((node (cons b '())))
    (set-cdr! (cdr cb) node)
    (set-cdr! cb node)))

(define (_cb-pos cb)
  (let _loop ((p (cdr (car cb))) (n 0))
    (if (null? p) n (_loop (cdr p) (+ n 1)))))

(define (_cb-patch! cb pos b)
  (set-car! (list-tail (cdr (car cb)) pos) b))

(define (_cb->list cb)
  (cdr (car cb)))

; === Constant table builder ===
(define (_make-consts)
  (let ((dummy (cons 'dummy '())))
    (cons dummy dummy)))

(define (_add-const! cs val)
  (let ((node (cons val '())))
    (set-cdr! (cdr cs) node)
    (set-cdr! cs node))
  (- (_cb-pos cs) 1))

(define (_cs->list cs)
  (cdr (car cs)))
```

- [ ] **Step 3: Add compile entry point and basic dispatch**

```scheme
; === Main compile entry ===
(define (compile expr)
  (let ((cb (_make-cb))
        (cs (_make-consts)))
    (_compile-expr expr '() '() cb cs)
    (_emit-byte! cb OP-HALT)
    (cons (_cb->list cb) (_cs->list cs))))

; Forward declarations
(define (_compile-expr expr scope env cb cs)
  (cond
    ((fixnum? expr) (_compile-fixnum expr cb))
    ((pair? expr)   (_compile-list expr scope env cb cs))
    ((symbol? expr)  (_compile-symbol expr scope env cb))
    (#t              (_compile-const expr cb cs))))

(define (_compile-fixnum val cb)
  (_emit-byte! cb OP-PUSH-INT)
  (_emit-byte! cb (remainder val 256))
  (_emit-byte! cb (remainder (quotient val 256) 256))
  (_emit-byte! cb (remainder (quotient val 65536) 256))
  (_emit-byte! cb (remainder (quotient val 16777216) 256)))

(define (_compile-const val cb cs)
  (let ((idx (_add-const! cs val)))
    (_emit-byte! cb OP-PUSH-CONST)
    (_emit-byte! cb idx)))

; alist lookup helper — returns cdr value or #f
(define (_assq-lookup key alist)
  (if (null? alist)
      #f
      (if (eq? key (car (car alist)))
          (cdr (car alist))
          (_assq-lookup key (cdr alist)))))

(define (_compile-symbol sym scope env cb)
  (let ((local (_assq-lookup sym scope)))
    (if local
        (begin
          (_emit-byte! cb OP-LREF)
          (_emit-byte! cb local))
        (let ((env-slot (_assq-lookup sym env)))
          (if env-slot
              (begin
                (_emit-byte! cb OP-LREF)
                (_emit-byte! cb env-slot))
              (let ((slot (find-global-slot sym)))
                (_emit-byte! cb OP-GREF)
                (_emit-byte! cb slot)))))))
```

- [ ] **Step 4: Add _compile-list dispatcher**

```scheme
(define (_compile-list expr scope env cb cs)
  (let ((fn (car expr))
        (args (cdr expr)))
    (cond
      ((eq? fn 'quote)    (_compile-quote args cb cs))
      ((eq? fn 'define)   (_compile-define args scope env cb cs))
      ((eq? fn 'if)       (_compile-if args scope env cb cs))
      ((eq? fn 'begin)    (_compile-begin args scope env cb cs))
      ((eq? fn 'cond)     (_compile-cond args scope env cb cs))
      ((eq? fn 'let)      (_compile-let args scope env cb cs))
      ((eq? fn 'lambda)   (_compile-lambda args scope env cb cs))
      (#t                 (_compile-call fn args scope env cb cs)))))
```

- [ ] **Step 5: Build and verify compiles and loads**

```bash
cd /data/data/com.termux/files/home/scheme
rtk meson compile -C build
echo '(display 42)' | ./build/scheme
# Should print 42 (via C compiler fallback, since compiler.scm is incomplete)
```

- [ ] **Step 6: Commit**

```bash
git add src/scheme/compiler.scm
git commit -m "feat: Scheme compiler skeleton with bytecode builder"
```

---

### Task 7: Scheme compiler — special forms (quote, define, if, begin, cond, let)

**Files:** Modify `src/scheme/compiler.scm`

Implement all special forms. The bytecode builder with backpatching handles JMP offset patching for if/cond.

- [ ] **Step 1: _compile-quote**

```scheme
(define (_compile-quote args cb cs)
  (_compile-const (car args) cb cs))
```

- [ ] **Step 2: _compile-define**

```scheme
(define (_compile-define args scope env cb cs)
  (let ((name-or-form (car args)))
    (if (pair? name-or-form)
        ; (define (f x ...) body ...)
        (let ((fn-name (car name-or-form))
              (fn-args (cdr name-or-form))
              (fn-body (cdr args)))
          (_compile-lambda (list fn-args
                                 (if (null? (cdr fn-body))
                                     (car fn-body)
                                     (cons 'begin fn-body)))
                           scope env cb cs)
          (let ((slot (create-global-slot fn-name)))
            (_emit-byte! cb OP-GSET)
            (_emit-byte! cb slot)))
        ; (define name val)
        (let ((name name-or-form)
              (val (car (cdr args))))
          (_compile-expr val scope env cb cs)
          (let ((slot (create-global-slot name)))
            (_emit-byte! cb OP-GSET)
            (_emit-byte! cb slot))))))
```

- [ ] **Step 3: _compile-if (with backpatching)**

```scheme
(define (_compile-if args scope env cb cs)
  (let ((test (car args))
        (then-expr (car (cdr args)))
        (else-expr (if (null? (cdr (cdr args))) '() (car (cdr (cdr args))))))
    (_compile-expr test scope env cb cs)
    (let ((jmp-false-pos (_cb-pos cb)))
      (_emit-byte! cb OP-JMP-IF-NOT)
      (_emit-byte! cb 0) (_emit-byte! cb 0)
      (_compile-expr then-expr scope env cb cs)
      (let ((jmp-end-pos (_cb-pos cb)))
        (_emit-byte! cb OP-JMP)
        (_emit-byte! cb 0) (_emit-byte! cb 0)
        (let ((false-start (_cb-pos cb)))
          (_patch-jmp cb jmp-false-pos false-start)
          (if (not (null? else-expr))
              (_compile-expr else-expr scope env cb cs))
          (let ((end-pos (_cb-pos cb)))
            (_patch-jmp cb jmp-end-pos end-pos)))))))

; Patch signed 16-bit JMP offset: offset = target - (jmp_pos + 3)
(define (_patch-jmp cb jmp-pos target)
  (let ((off (- target (+ jmp-pos 3))))
    (let ((u (if (< off 0) (+ off 65536) off)))
      (_cb-patch! cb (+ jmp-pos 1) (remainder u 256))
      (_cb-patch! cb (+ jmp-pos 2) (quotient u 256)))))
```

- [ ] **Step 4: _compile-begin**

```scheme
(define (_compile-begin args scope env cb cs)
  (if (null? args)
      (_emit-byte! cb OP-PUSH-NIL)
      (_compile-begin-1 args scope env cb cs)))

(define (_compile-begin-1 args scope env cb cs)
  (if (null? (cdr args))
      (_compile-expr (car args) scope env cb cs)
      (begin
        (_compile-expr (car args) scope env cb cs)
        (_emit-byte! cb OP-POP)
        (_compile-begin-1 (cdr args) scope env cb cs))))
```

- [ ] **Step 5: _compile-cond**

```scheme
(define (_compile-cond args scope env cb cs)
  (if (null? args)
      (_emit-byte! cb OP-PUSH-NIL)
      (let ((clause (car args)))
        (let ((test (car clause))
              (body (cdr clause)))
          (if (eq? test 'else)
              (_compile-begin body scope env cb cs)
              (begin
                (_compile-expr test scope env cb cs)
                (let ((jmp-false-pos (_cb-pos cb)))
                  (_emit-byte! cb OP-JMP-IF-NOT)
                  (_emit-byte! cb 0) (_emit-byte! cb 0)
                  (_compile-begin body scope env cb cs)
                  (let ((jmp-end-pos (_cb-pos cb)))
                    (_emit-byte! cb OP-JMP)
                    (_emit-byte! cb 0) (_emit-byte! cb 0)
                    (let ((next-start (_cb-pos cb)))
                      (_patch-jmp cb jmp-false-pos next-start)
                      (_compile-cond (cdr args) scope env cb cs)
                      (let ((end-pos (_cb-pos cb)))
                        (_patch-jmp cb jmp-end-pos end-pos)))))))))))
```

- [ ] **Step 6: _compile-let (desugar to lambda call)**

```scheme
(define (_compile-let args scope env cb cs)
  (let ((bindings (car args))
        (body (cdr args)))
    (let ((params (_let-params bindings))
          (vals (_let-vals bindings)))
      ; Compile vals as args
      (_compile-let-args vals scope env cb cs)
      ; Compile lambda
      (_compile-lambda (list params
                             (if (null? (cdr body)) (car body) (cons 'begin body)))
                       scope env cb cs)
      (let ((nargs (_count-exprs bindings)))
        (_emit-byte! cb OP-CALL)
        (_emit-byte! cb nargs)))))

(define (_let-params bindings)
  (if (null? bindings) '() (cons (car (car bindings)) (_let-params (cdr bindings)))))

(define (_let-vals bindings)
  (if (null? bindings) '() (cons (car (cdr (car bindings))) (_let-vals (cdr bindings)))))

(define (_compile-let-args vals scope env cb cs)
  (if (not (null? vals))
      (begin
        (_compile-let-args (cdr vals) scope env cb cs)
        (_compile-expr (car vals) scope env cb cs))))

(define (_count-exprs lst)
  (if (null? lst) 0 (+ 1 (_count-exprs (cdr lst)))))
```

- [ ] **Step 7: _compile-call (primitive and user)**

```scheme
(define (_compile-call fn args scope env cb cs)
  (if (_is-primitive? fn)
      (_compile-prim-call fn args scope env cb cs)
      (_compile-user-call fn args scope env cb cs)))

(define (_is-primitive? sym)
  (if (symbol? sym)
      (not (eqv? (prim-index sym) -1))
      #f))

(define (_compile-prim-call fn args scope env cb cs)
  (_compile-prim-args args scope env cb cs)
  (let ((nargs (_count-exprs args))
        (pidx (prim-index fn)))
    (_emit-byte! cb OP-PRIM-CALL)
    (_emit-byte! cb nargs)
    (_emit-byte! cb (remainder pidx 256))
    (_emit-byte! cb (quotient pidx 256))))

(define (_compile-prim-args args scope env cb cs)
  (if (not (null? args))
      (begin
        (_compile-prim-args (cdr args) scope env cb cs)
        (_compile-expr (car args) scope env cb cs))))

(define (_compile-user-call fn args scope env cb cs)
  (_compile-user-args args scope env cb cs)
  (_compile-expr fn scope env cb cs)
  (let ((nargs (_count-exprs args)))
    (_emit-byte! cb OP-CALL)
    (_emit-byte! cb nargs)))

(define (_compile-user-args args scope env cb cs)
  (if (not (null? args))
      (begin
        (_compile-user-args (cdr args) scope env cb cs)
        (_compile-expr (car args) scope env cb cs))))
```

- [ ] **Step 8: Stub _compile-lambda (no closure capture yet)**

```scheme
; Placeholder — closure capture implemented in Task 8
(define (_compile-lambda args scope env cb cs)
  (let ((params (car args))
        (body (car (cdr args))))
    (let ((child-cb (_make-cb))
          (child-cs (_make-consts))
          (child-scope (_make-scope params 1)))
      (_compile-expr body child-scope '() child-cb child-cs)
      (_emit-byte! child-cb OP-RETURN)
      (let ((code-idx (assemble-code (cons (_cb->list child-cb) (_cs->list child-cs)))))
        (_emit-byte! cb OP-CLOSE)
        (_emit-byte! cb (remainder code-idx 256))
        (_emit-byte! cb (quotient code-idx 256))
        (_emit-byte! cb 0)))))

(define (_make-scope params start)
  (if (null? params)
      '()
      (cons (cons (car params) start)
            (_make-scope (cdr params) (+ start 1)))))
```

- [ ] **Step 9: Build and smoke test**

```bash
cd /data/data/com.termux/files/home/scheme
rtk meson compile -C build
echo '(display (+ 1 2))' | ./build/scheme
echo '(display ((lambda (x) (+ x 1)) 41))' | ./build/scheme
```

- [ ] **Step 10: Commit**

```bash
git add src/scheme/compiler.scm
git commit -m "feat: Scheme compiler special forms and calls"
```

---

### Task 8: Scheme compiler — closure capture in _compile-lambda

**Files:** Modify `src/scheme/compiler.scm`

Replace the stub `_compile-lambda` with free variable detection, LREF emission before OP_CLOSE, and real nfree.

- [ ] **Step 1: Add free variable detection helpers**

Add before `_compile-lambda`:
```scheme
; === Free variable detection ===

; Walk expr for free symbols (in env; not in params, not primitive)
(define (_free-syms expr params env)
  (cond
    ((null? expr) '())
    ((symbol? expr)
     (if (or (_assq-lookup expr params)        ; is param?
             (not (_assq-lookup expr env))     ; not in env (global)
             (_is-primitive? expr))            ; is primitive
         '()
         (list expr)))
    ((pair? expr)
     (let ((head (car expr)))
       (if (and (symbol? head) (eq? head 'quote))
           '()
           (if (and (symbol? head) (eq? head 'lambda))
               ; Inner lambda: only walk for our capture context, not inside
               (let ((inner-params (car (cdr expr))))
                 (_free-syms (car (cdr (cdr expr))) inner-params env))
               (append (_free-syms head params env)
                       (_free-syms-list (cdr expr) params env))))))
    (#t '())))

(define (_free-syms-list lst params env)
  (if (null? lst) '()
      (append (_free-syms (car lst) params env)
              (_free-syms-list (cdr lst) params env))))

; Deduplicate a list using memq
(define (_dedup syms)
  (if (null? syms)
      '()
      (let ((s (car syms))
            (rest (_dedup (cdr syms))))
        (if (memq s rest) rest (cons s rest)))))
```

- [ ] **Step 2: Add _assign-slots helper**

```scheme
; Assign sequential slots starting at `start` to each symbol
(define (_assign-slots syms start)
  (if (null? syms)
      '()
      (cons (cons (car syms) start)
            (_assign-slots (cdr syms) (+ start 1)))))
```

- [ ] **Step 3: Replace _compile-lambda with closure-aware version**

```scheme
(define (_compile-lambda args scope env cb cs)
  (let ((params (car args))
        (body (car (cdr args))))
    (let* ((nparams (_count-exprs params))
           ; Find free symbols in body
           (free-syms (_dedup (_free-syms body params env)))
           ; Assign captured slots starting at 1 (VM: fp[1..nfree] = captured vars)
           (captured (_assign-slots free-syms 1))
           (nfree (_count-exprs captured))
           ; Build child env: captured (1..nfree) + params (nfree+1..)
           (child-env (append captured (_assign-slots params (+ nfree 1))))
           (child-scope '()))
      ; Compile child body with child-env
      (let ((child-cb (_make-cb))
            (child-cs (_make-consts)))
        (_compile-expr body child-scope child-env child-cb child-cs)
        (_emit-byte! child-cb OP-RETURN)
        ; Assemble child code at compile time
        (let ((code-idx (assemble-code (cons (_cb->list child-cb) (_cs->list child-cs)))))
          ; Emit LREF for each captured var from parent env
          ; Order: first entry in captured first (→ bottom of stack → env_vec[0] → fp[1])
          (let _emit ((cap captured))
            (if (not (null? cap))
                (let* ((entry (car cap))
                       (sym (car entry))
                       (parent-slot (_assq-lookup sym env)))
                  (_emit-byte! cb OP-LREF)
                  (_emit-byte! cb parent-slot)
                  (_emit (cdr cap)))))
          ; Emit OP_CLOSE with real nfree
          (_emit-byte! cb OP-CLOSE)
          (_emit-byte! cb (remainder code-idx 256))
          (_emit-byte! cb (quotient code-idx 256))
          (_emit-byte! cb nfree))))))
```

- [ ] **Step 4: Build and verify**

```bash
cd /data/data/com.termux/files/home/scheme
rtk meson compile -C build
rtk meson test -C build
```

- [ ] **Step 5: Commit**

```bash
git add src/scheme/compiler.scm
git commit -m "feat: add closure capture to Scheme compiler"
```

---

### Task 9: Scheme closure integration tests

**Files:** Create `tests/scheme/test-closure.ss`, Modify `tests/meson.build`

- [ ] **Step 1: Write test-closure.ss**

```scheme
; tests/scheme/test-closure.ss — closure integration tests

; 1. Simple closure
(display (((lambda (x) (lambda (y) (+ x y))) 1) 2))
(newline)

; 2. Multiple captured vars
(display (((lambda (a b) (lambda (c) (+ a (+ b c)))) 10 20) 30))
(newline)

; 3. Shadowing
(display (((lambda (x) (lambda (x) (+ x 1))) 100) 200))
(newline)

; 4. Closure via define
(define (make-adder n) (lambda (x) (+ x n)))
(display ((make-adder 5) 10))
(newline)

; 5. Nested closures
(define (make-counter init)
  (lambda ()
    (lambda (x)
      (+ init x))))
(display (((make-counter 100) 50)))
(newline)

; 6. No capture (nfree=0)
(display ((lambda (x) (+ x 1)) 99))
(newline)
```

- [ ] **Step 2: Add to tests/meson.build**

```python
test_ss_files = files(
  'scheme/test-char.ss',
  'scheme/test-string.ss',
  'scheme/test-bytevector.ss',
  'scheme/test-closure.ss',
)
# ... add test('closure-ss', scheme_exe, args: ['tests/scheme/test-closure.ss'], ...)
```

- [ ] **Step 3: Run tests**

```bash
cd /data/data/com.termux/files/home/scheme
rtk meson setup build --wipe
rtk meson compile -C build
rtk meson test -C build closure-ss
```

Expected output: `3`, `60`, `201`, `15`, `150`, `100`

- [ ] **Step 4: Commit**

```bash
git add tests/scheme/test-closure.ss tests/meson.build
git commit -m "test: add Scheme-level closure integration tests"
```

---

## Phase 2 — Macros

### Task 10: gensym and eval C primitives

**Files:** Create `src/primitives/macro.c`, Modify `src/include/vm.h`, Modify `src/vm/vm.c`, Modify `src/vm/builtins.c`, Modify `src/meson.build`

- [ ] **Step 1: Add gensym_counter to vm_state_t**

In `src/include/vm.h`, add after `gc_active`:
```c
    int gensym_counter;
```

- [ ] **Step 2: Initialize in vm_init**

In `src/vm/vm.c`, add after `vm->gc_active = false;` (in vm_init):
```c
    vm->gensym_counter = 0;
```

- [ ] **Step 3: Create src/primitives/macro.c**

```c
#include "types.h"
#include "vm.h"
#include <stdio.h>
#include <string.h>

word prim_gensym(vm_state_t* vm, int nargs) {
    (void)nargs;
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "{g%d}", vm->gensym_counter++);
    word sym = vm_intern(vm, buf, n);
    return sym;
}

word prim_eval(vm_state_t* vm, int nargs) {
    if (nargs != 1) {
        vm->error_kind = ERR_ARITY;
        vm->error_msg = "eval requires 1 argument";
        return word_nil();
    }
    word expr = vm->sp[0];
    // Use the same compile+assemble+execute trampoline as main.c
    // Forward declaration from main.c — we define this via header
    // Actually, we inline the same logic: look up 'compile' global, call it, assemble, execute
    int compile_slot = vm_find_global_by_name(vm, "compile");
    if (compile_slot < 0) {
        vm->error_kind = ERR_UNBOUND;
        vm->error_msg = "compile not defined";
        return word_nil();
    }
    // Build tiny bytecode: PUSH_CONST expr, GREF compile, CALL 1, PRIM_CALL 1 assemble-code, HALT
    // But we need assemble-code index...
    // For now: use the scheme_compile_and_assemble pattern from main.c
    // We'll declare it as extern
    extern int scheme_compile_and_assemble(vm_state_t*, word);
    int ci = scheme_compile_and_assemble(vm, expr);
    if (ci < 0) {
        vm->error_kind = ERR_INTERNAL;
        vm->error_msg = "eval: compilation failed";
        return word_nil();
    }
    return vm_execute(vm, ci);
}
```

Wait — `scheme_compile_and_assemble` is static in main.c. We need to either make it non-static or duplicate the logic.

Alternative approach for `prim_eval`: since the Scheme compiler is already loaded when macros are used, we can call `(compile expr)` via the VM, then `assemble-code` on the result, then execute. We do this by leveraging the existing `prim_assemble_code`.

Actually, the simplest approach: make `scheme_compile_and_assemble` non-static by adding a declaration to `compiler.h`.

- [ ] **Step 3: Move scheme_compile_and_assemble to compiler.c**

To avoid linker circularity (macro.c in scheme_core can't call main.c's static function):

In `src/main.c`, remove the entire `scheme_compile_and_assemble` function definition.

In `src/bootstrap/compiler.c`, add the function (non-static):
```c
int scheme_compile_and_assemble(vm_state_t* vm, word expr) {
    int compile_slot = vm_find_global_by_name(vm, "compile");
    if (compile_slot < 0) return -1;
    int asm_idx = prim_lookup("assemble-code");
    if (asm_idx < 0) return -1;

    uint8_t bc[32]; int len = 0;
    bc[len++] = OP_PUSH_CONST; bc[len++] = 0;
    bc[len++] = OP_GREF;       bc[len++] = (uint8_t)compile_slot;
    bc[len++] = OP_CALL;       bc[len++] = 1;
    bc[len++] = OP_PRIM_CALL;  bc[len++] = 1;
    bc[len++] = (uint8_t)(asm_idx & 0xFF);
    bc[len++] = (uint8_t)((asm_idx >> 8) & 0xFF);
    bc[len++] = OP_HALT;

    size_t bc_words = ((size_t)len + sizeof(word) - 1) / sizeof(word);
    size_t total = 3 + bc_words + 1;
    word* obj = vm->gc->alloc_words(total);
    obj_set_type(obj, OBJ_TYPE_CODE);
    obj[2] = (word)len;
    memcpy(obj + 3, bc, (size_t)len);
    obj[3 + bc_words] = expr;

    int idx = vm_load_code(vm, obj);
    if (idx < 0) return -1;
    word result = vm_execute(vm, idx);
    if (is_fixnum(result)) return (int)word_to_fixnum(result);
    return -1;
}
```

In `src/include/compiler.h`, add declaration:
```c
int scheme_compile_and_assemble(vm_state_t* vm, word expr);
```

In `src/main.c`, add `#include "compiler.h"` (should already be there). Function calls remain unchanged.

- [ ] **Step 4: Write src/primitives/macro.c**

```c
#include "types.h"
#include "vm.h"
#include "compiler.h"
#include <stdio.h>

word prim_gensym(vm_state_t* vm, int nargs) {
    (void)nargs;
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "{g%d}", vm->gensym_counter++);
    return vm_intern(vm, buf, n);
}

word prim_eval(vm_state_t* vm, int nargs) {
    if (nargs != 1) {
        VM_ERROR(vm, ERR_ARITY, "eval requires 1 argument", word_from_fixnum(nargs));
        return word_nil();
    }
    int ci = scheme_compile_and_assemble(vm, vm->sp[0]);
    if (ci < 0) return word_nil();
    return vm_execute(vm, ci);
}
```

- [ ] **Step 5: Register in builtins.c**

Add to `prim_table[]`:
```c
{"gensym", prim_gensym},
{"eval",   prim_eval},
```

Add declarations at top:
```c
word prim_gensym(vm_state_t* vm, int nargs);
word prim_eval(vm_state_t* vm, int nargs);
```

- [ ] **Step 6: Add macro.c to meson.build**

In `src/meson.build`, add to `scheme_core_sources`:
```python
'primitives/macro.c',
```

- [ ] **Step 7: Build and test**

```bash
cd /data/data/com.termux/files/home/scheme
rtk meson setup build --wipe
rtk meson compile -C build
# Quick smoke test of gensym
echo '(display (gensym))' | ./build/scheme
```

- [ ] **Step 8: Commit**

```bash
git add src/primitives/macro.c src/include/vm.h src/vm/vm.c src/vm/builtins.c src/include/compiler.h src/main.c src/meson.build
git commit -m "feat: add gensym and eval C primitives"
```

---

### Task 11: C-level macro primitive tests

**Files:** Create `tests/c/test_macro.c`, Modify `tests/c/meson.build`

- [ ] **Step 1: Write test_macro.c**

```c
#include "types.h"
#include "vm.h"
#include "gc.h"
#include "pal.h"
#include "compiler.h"
#include "reader.h"
#include "prim.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern word prim_gensym(vm_state_t* vm, int nargs);
extern word prim_eval(vm_state_t* vm, int nargs);

static int nf = 0;
#define CHECK(c,m) do { if(!(c)){printf("FAIL: %s\n",m);nf++;}else{printf("PASS: %s\n",m);} }while(0)

int main(void) {
    pal_interface* pal = pal_init();
    gc_interface* gc = gc_init();
    vm_state_t* vm = vm_init(gc, pal);
    prim_init_all(vm);

    // === gensym tests ===
    word g1 = prim_gensym(vm, 0);
    word g2 = prim_gensym(vm, 0);
    CHECK(is_ptr(g1) && obj_type(ptr_from_word(g1)) == OBJ_TYPE_SYMBOL,
          "gensym returns a symbol");
    CHECK(g1 != g2, "two gensym calls return different symbols");

    // === eval tests ===
    // First we need the Scheme compiler loaded to use eval.
    // These tests run after the build, so compiler.scm is available.
    // We simulate by compiling a simple expression:
    // Actually, eval requires compile global. Skip C-level eval test —
    // full eval testing happens in Scheme tests.

    printf("\n%d failures\n", nf);
    return nf;
}
```

- [ ] **Step 2: Add to tests/c/meson.build**

```python
test_macro = executable('test_macro',
  'test_macro.c',
  include_directories: test_scheme_inc,
  link_with: scheme_core,
  link_args: ['-lm'],
)
test('macro', test_macro)
```

- [ ] **Step 3: Build and run**

```bash
cd /data/data/com.termux/files/home/scheme
rtk meson setup build --wipe
rtk meson compile -C build
rtk meson test -C build macro
```

- [ ] **Step 4: Commit**

```bash
git add tests/c/test_macro.c tests/c/meson.build
git commit -m "test: add C-level macro primitive tests"
```

---

### Task 12: Macro table and define-syntax in compiler.scm

**Files:** Modify `src/scheme/lib.scm`, Modify `src/scheme/compiler.scm`

- [ ] **Step 1: Add *macro-table* to lib.scm**

Add at the top of `src/scheme/lib.scm`:
```scheme
; === Macro table ===
(define *macro-table* '())
```

- [ ] **Step 2: Add macro helpers to compiler.scm**

Add before `_compile-list`:
```scheme
; === Macro support ===

(define (_lookup-macro name)
  (_assq-lookup name *macro-table*))

(define (_compile-define-syntax args scope env cb cs)
  (let ((name (car args))
        (transformer-expr (car (cdr args))))
    ; Evaluate the transformer at compile time
    (let ((transformer (eval transformer-expr)))
      ; Add to macro table: push (name . transformer)
      (set! *macro-table* (cons (cons name transformer) *macro-table*))
      ; Emit no-op
      (_emit-byte! cb OP-PUSH-NIL))))
```

- [ ] **Step 3: Add define-syntax to _compile-list dispatch**

In `_compile-list`, add before `((eq? fn 'quote) ...)`:
```scheme
      ((eq? fn 'define-syntax) (_compile-define-syntax args scope env cb cs))
```

- [ ] **Step 4: Build and verify**

```bash
cd /data/data/com.termux/files/home/scheme
rtk meson compile -C build
```

- [ ] **Step 5: Commit**

```bash
git add src/scheme/lib.scm src/scheme/compiler.scm
git commit -m "feat: add *macro-table* and define-syntax to Scheme compiler"
```

---

### Task 13: Macro expansion hook in compiler.scm

**Files:** Modify `src/scheme/compiler.scm`

Insert macro expansion at the top of `_compile-list` — before special form dispatch, check if `fn` is a macro.

- [ ] **Step 1: Add expansion to _compile-list**

Replace `_compile-list`:
```scheme
(define (_compile-list expr scope env cb cs)
  (let ((fn (car expr))
        (args (cdr expr)))
    ; Macro expansion hook — check before special form dispatch
    (let ((transformer (_lookup-macro fn)))
      (if transformer
          ; Expand and recompile the result
          (_compile-expr (transformer (cons fn args)) scope env cb cs)
          ; Standard special form dispatch
          (cond
            ((eq? fn 'define-syntax) (_compile-define-syntax args scope env cb cs))
            ((eq? fn 'quote)    (_compile-quote args cb cs))
            ((eq? fn 'define)   (_compile-define args scope env cb cs))
            ((eq? fn 'if)       (_compile-if args scope env cb cs))
            ((eq? fn 'begin)    (_compile-begin args scope env cb cs))
            ((eq? fn 'cond)     (_compile-cond args scope env cb cs))
            ((eq? fn 'let)      (_compile-let args scope env cb cs))
            ((eq? fn 'lambda)   (_compile-lambda args scope env cb cs))
            (#t                 (_compile-call fn args scope env cb cs)))))))
```

- [ ] **Step 2: Build and verify**

```bash
cd /data/data/com.termux/files/home/scheme
rtk meson compile -C build
```

- [ ] **Step 3: Commit**

```bash
git add src/scheme/compiler.scm
git commit -m "feat: add macro expansion hook to Scheme compiler"
```

---

### Task 14: syntax-rules library

**Files:** Modify `src/scheme/lib.scm`

Implement `syntax-rules` with pattern matching, template filling, and gensym-based hygiene.

- [ ] **Step 1: Add immune-symbol predicate**

```scheme
; Immune symbols: special forms and primitives, never renamed
(define (_immune? sym)
  (if (symbol? sym)
      (if (eq? sym 'lambda) #t
          (if (eq? sym 'if) #t
              (if (eq? sym 'define) #t
                  (if (eq? sym 'set!) #t
                      (if (eq? sym 'begin) #t
                          (if (eq? sym 'quote) #t
                              (if (eq? sym 'cond) #t
                                  (if (eq? sym 'let) #t
                                      (if (eq? sym 'else) #t
                                          (if (eq? sym 'define-syntax) #t
                                              (if (eq? sym 'syntax-rules) #t
                                                  (if (eq? sym '...) #t
                                                      (_is-primitive? sym)))))))))))))
      #f))
```

- [ ] **Step 2: Add pattern matching**

```scheme
; === Pattern matching ===
; Returns alist of (pattern-var . matched-value) or #f on failure

(define (_match-pat pat input literals)
  (cond
    ; Literal identifier
    ((and (symbol? pat) (memq pat literals))
     (if (eq? pat input) '() #f))
    ; Pattern variable
    ((symbol? pat)
     (if (eq? pat '...) '()  ; ellipsis handled specially
         (list (cons pat input))))
    ; Pair pattern
    ((pair? pat)
     (if (pair? input)
         (_match-pair pat input literals)
         #f))
    ; Empty list
    ((null? pat)
     (if (null? input) '() #f))
    (#t (if (eqv? pat input) '() #f))))

(define (_match-pair pat input literals)
  (let ((p-car (car pat))
        (p-cdr (cdr pat)))
    ; Check for ellipsis pattern: (var ...)
    (if (and (pair? p-cdr) (null? (cdr p-cdr)) (eq? (car p-cdr) '...)
             (symbol? p-car) (not (memq p-car literals)))
        (_match-ellipsis p-car input literals)
        ; Normal pair match
        (let ((car-match (_match-pat p-car (car input) literals)))
          (if car-match
              (let ((cdr-match (_match-pat p-cdr (cdr input) literals)))
                (if cdr-match
                    (append car-match cdr-match)
                    #f))
              #f)))))

(define (_match-ellipsis var input literals)
  ; Match zero or more repetitions of var pattern
  ; Try: match as many as possible, but allow fewer
  ; Returns ((var . (v1 v2 ...))) for matched elements
  (let ((elements (_match-repeated input)))
    (list (cons var elements))))

(define (_match-repeated input)
  ; Simply collect all elements (full match). Caller handles backtracking.
  ; For simplicity: match all remaining list elements
  (if (null? input)
      '()
      (if (pair? input)
          (cons (car input) (_match-repeated (cdr input)))
          '())))
```

- [ ] **Step 3: Add template filling with hygiene**

```scheme
; === Template filling ===
; Fills template using bindings from pattern match.
; Renames non-immune free identifiers for hygiene.

(define (_fill-template tmpl bindings rename-id)
  (cond
    ((symbol? tmpl)
     (cond
       ((eq? tmpl '...) tmpl)  ; ellipsis marker, handled specially
       ((_immune? tmpl) tmpl)
       ((_assq-lookup tmpl bindings)
        => (lambda (val) val))
       (#t
        ; Non-immune free identifier → rename
        (_rename-sym tmpl rename-id))))
    ((pair? tmpl)
     (if (and (pair? (cdr tmpl)) (null? (cdr (cdr tmpl)))
              (eq? (car (cdr tmpl)) '...))
         ; Template ellipsis: (template ...)
         (_fill-ellipsis (car tmpl) bindings rename-id)
         ; Normal pair
         (cons (_fill-template (car tmpl) bindings rename-id)
               (_fill-template (cdr tmpl) bindings rename-id))))
    (#t tmpl)))

(define (_rename-sym sym rename-id)
  ; Append {M rename-id} to symbol name for hygiene
  (let* ((name (symbol->string sym))
         (suffix (string-append "{M" (number->string rename-id) "}"))
         (new-name (string-append name suffix)))
    (string->symbol new-name)))

(define (_fill-ellipsis inner-tmpl bindings rename-id)
  ; Find the pattern variable that was bound to a list
  ; and iterate over each element
  (let ((var (_ellipsis-var inner-tmpl)))
    (let ((vals (_assq-lookup var bindings)))
      (if vals
          (_fill-ellipsis-iter inner-tmpl var vals bindings rename-id)
          '()))))

(define (_ellipsis-var tmpl)
  ; Find the pattern variable in an ellipsis template
  (if (symbol? tmpl) tmpl
      (if (pair? tmpl) (_ellipsis-var (car tmpl)) #f)))

(define (_fill-ellipsis-iter tmpl var vals bindings rename-id)
  (if (null? vals)
      '()
      (let ((new-bindings (cons (cons var (car vals)) bindings)))
        (cons (_fill-template tmpl new-bindings rename-id)
              (_fill-ellipsis-iter tmpl var (cdr vals) new-bindings rename-id)))))
```

- [ ] **Step 4: Add syntax-rules**

```scheme
; === syntax-rules ===
; (syntax-rules (<literals>) (<pattern> <template>) ...)

(define _macro-id-counter 0)

(define (syntax-rules literals . clauses)
  (let ((macro-id _macro-id-counter))
    (set! _macro-id-counter (+ _macro-id-counter 1))
    (let ((_call-id 0))
      (lambda (form)
        (let ((call-id _call-id))
          (set! _call-id (+ _call-id 1))
          (let ((rename-id (+ (* macro-id 1000) call-id)))
            (_try-clauses form clauses literals rename-id)))))))

(define (_try-clauses form clauses literals rename-id)
  (if (null? clauses)
      (begin (display "syntax-rules: no matching clause") (newline) form)
      (let* ((clause (car clauses))
             (pattern (car clause))
             (template (car (cdr clause)))
             (bindings (_match-pat pattern form literals)))
        (if bindings
            (_fill-template template bindings rename-id)
            (_try-clauses form (cdr clauses) literals rename-id)))))
```

- [ ] **Step 5: Build and smoke test**

```bash
cd /data/data/com.termux/files/home/scheme
rtk meson compile -C build
echo '(define-syntax my-when (syntax-rules () ((_ test body ...) (if test (begin body ...))))) (my-when #t (display "ok"))' | ./build/scheme
```

- [ ] **Step 6: Commit**

```bash
git add src/scheme/lib.scm
git commit -m "feat: add syntax-rules library with pattern matching and hygiene"
```

---

### Task 15: Macro integration tests

**Files:** Create `tests/scheme/test-macro.ss`, Modify `tests/meson.build`

- [ ] **Step 1: Write test-macro.ss**

```scheme
; tests/scheme/test-macro.ss — macro system integration tests

; 1. Simple macro: my-when
(define-syntax my-when
  (syntax-rules ()
    ((_ test body ...)
     (if test (begin body ...)))))
(display "test1: ")
(my-when #t (display "ok"))
(newline)
(my-when #f (display "no"))
(display " (no output above for false case)")
(newline)

; 2. Macro with two clauses: my-or
(define-syntax my-or
  (syntax-rules ()
    ((_) #f)
    ((_ x) x)
    ((_ x . rest)
     (let ((temp x))
       (if temp temp (my-or . rest))))))
(display "test2: ")
(display (my-or #f #f 42 #f))
(newline)

; 3. gensym test
(display "test3: ")
(display (symbol? (gensym)))
(newline)

; 4. Hygiene — macro-introduced binding shouldn't clash
(define temp 999)
(display "test4: ")
(display (my-or #f #f 42))
(newline)
(display temp)
(newline)
```

- [ ] **Step 2: Add to tests/meson.build**

```python
# Add test-macro.ss to test_ss_files and add test entry
test('macro-ss', scheme_exe,
  args: ['tests/scheme/test-macro.ss'],
  workdir: meson.project_source_root(),
)
```

- [ ] **Step 3: Run tests**

```bash
cd /data/data/com.termux/files/home/scheme
rtk meson setup build --wipe
rtk meson compile -C build
rtk meson test -C build macro-ss
```

- [ ] **Step 4: Commit**

```bash
git add tests/scheme/test-macro.ss tests/meson.build
git commit -m "test: add macro system integration tests"
```

---

### Task 16: Final verification — full test suite

- [ ] **Step 1: Run all tests**

```bash
cd /data/data/com.termux/files/home/scheme
rtk meson setup build --wipe
rtk meson compile -C build
rtk meson test -C build
```

- [ ] **Step 2: Run existing tests to verify no regressions**

```bash
rtk meson test -C build --suite char
rtk meson test -C build --suite string
rtk meson test -C build --suite bytevector
rtk meson test -C build --suite number
rtk meson test -C build --suite types
rtk meson test -C build --suite gc
rtk meson test -C build --suite listvec
```

- [ ] **Step 3: Interactive smoke test**

```bash
echo '
(define (make-counter n)
  (lambda () (lambda (x) (+ n x))))
(display (((make-counter 10) 20)))
(newline)

(define-syntax unless
  (syntax-rules ()
    ((_ test body ...)
     (if test #f (begin body ...)))))
(unless #f (display "macro+closure working"))
(newline)
' | ./build/scheme
```

Expected: `30` then `macro+closure working`.

- [ ] **Step 4: Commit if any fixes needed**

```bash
git add -A
git diff --cached --stat
git commit -m "fix: final adjustments from integration testing"
```

## R7RS Gap Analysis (2026-04-29)

### Quick Wins (simple C primitives)
caar..cddddr, list-set!/list-copy/make-list, memv/assv/assoc,
string>?/string<=?/string>=?, string-fill!, vector-copy/vector-fill/vector-append,
bytevector-copy, expt, char-upcase/downcase/digit-value, symbol=?/boolean=?,
eof-object?, member, read-char/write-char/peek-char

### Design Needed (S-expr construction bugs)
letrec, case, do, quasiquote (reader+compiler), dotted-tail lambda

### Complex Subsystems
call/cc, values/call-with-values, delay/force, dynamic-wind,
let-syntax/letrec-syntax, exceptions, complex numbers, module system,
ports/I/O, string-map/for-each, vector-map/for-each, environments, system interface
