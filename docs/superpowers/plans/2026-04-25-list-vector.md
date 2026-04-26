# List and Vector Procedures Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement R7RS-small list and vector procedures — list procedures in Scheme, vector primitives and equal? in C, all loaded during bootstrap.

**Architecture:** C primitives (list, vector?, make-vector, vector-length, vector-ref, vector-set!, list->vector, vector->list, equal?) in new src/primitives/vector.c and modified pair.c. VM bytecodes for vector ops. Scheme library (lib.scm) with ~20 procedures loaded after compiler.scm during Phase 1.

**Tech Stack:** C11, Scheme (note: C bootstrap compiler supports `let`, `cond`, `begin`, `if`, `lambda`, `define`, `quote`, `set!`, function calls but NOT variadic `. args` or `let*`), Meson build, R7RS-small API

---

### Task 1: Vector C Primitives + list

**Files:**
- Create: `src/primitives/vector.c`
- Modify: `src/vm/builtins.c` (forward decls + prim_table entries)
- Modify: `src/meson.build` (add vector.c to sources)
- Create: `tests/c/test_listvec.c`
- Modify: `tests/c/meson.build`

- [ ] **Step 1: Write failing C test**

Create `tests/c/test_listvec.c`:

```c
#include "types.h"
#include "vm.h"
#include "prim.h"
#include "gc.h"
#include "pal.h"
#include <stdio.h>

extern word prim_vectorp(vm_state_t* vm, int nargs);
extern word prim_make_vector(vm_state_t* vm, int nargs);
extern word prim_vector(vm_state_t* vm, int nargs);
extern word prim_vector_length(vm_state_t* vm, int nargs);
extern word prim_vector_ref(vm_state_t* vm, int nargs);
extern word prim_vector_set(vm_state_t* vm, int nargs);
extern word prim_list_to_vector(vm_state_t* vm, int nargs);
extern word prim_vector_to_list(vm_state_t* vm, int nargs);
extern word prim_list(vm_state_t* vm, int nargs);
extern word prim_equal(vm_state_t* vm, int nargs);

static int n_failures = 0;
pal_interface* pal;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); n_failures++; } \
} while(0)

int main(void) {
    pal = pal_init();
    gc_interface* gc = gc_init();
    vm_state_t* vm = vm_init(gc, pal);

    // === list ===
    vm->sp[0] = word_from_fixnum(1);
    vm->sp[1] = word_from_fixnum(2);
    vm->sp[2] = word_from_fixnum(3);
    word lst = prim_list(vm, 3);
    CHECK(is_ptr(lst), "list returns ptr");
    word* cur = ptr_from_word(lst);
    CHECK(pair_car(cur) == word_from_fixnum(1), "list[0]=1");
    cur = ptr_from_word(pair_cdr(cur));
    CHECK(pair_car(cur) == word_from_fixnum(2), "list[1]=2");
    cur = ptr_from_word(pair_cdr(cur));
    CHECK(pair_car(cur) == word_from_fixnum(3), "list[2]=3");
    CHECK(is_nil(pair_cdr(cur)), "list ends with nil");

    word empty = prim_list(vm, 0);
    CHECK(is_nil(empty), "list with 0 args = nil");

    // === vector? ===
    CHECK(is_false(prim_vectorp(vm, 1)), "vector? on nil is #f");

    // === make-vector ===
    vm->sp[0] = word_from_fixnum(5);
    vm->sp[1] = word_from_fixnum(42);
    word v = prim_make_vector(vm, 2);
    CHECK(is_ptr(v), "make-vector returns ptr");
    vm->sp[0] = v;
    CHECK(is_true(prim_vectorp(vm, 1)), "vector? on vector is #t");

    // === vector ===
    vm->sp[0] = word_from_fixnum(10);
    vm->sp[1] = word_from_fixnum(20);
    vm->sp[2] = word_from_fixnum(30);
    word v2 = prim_vector(vm, 3);
    CHECK(is_ptr(v2), "vector returns ptr");
    vm->sp[0] = v2;
    CHECK(is_true(prim_vectorp(vm, 1)), "vector? on vector from vector");

    // === vector-length ===
    vm->sp[0] = v;
    word len = prim_vector_length(vm, 1);
    CHECK(is_fixnum(len) && word_to_fixnum(len) == 5, "vector-length of 5");

    // === vector-ref ===
    vm->sp[0] = v;
    vm->sp[1] = word_from_fixnum(0);
    word elem = prim_vector_ref(vm, 2);
    CHECK(is_fixnum(elem) && word_to_fixnum(elem) == 42, "vector-ref index 0 is 42");

    vm->sp[0] = v;
    vm->sp[1] = word_from_fixnum(4);
    elem = prim_vector_ref(vm, 2);
    CHECK(is_fixnum(elem) && word_to_fixnum(elem) == 42, "vector-ref index 4 is 42");

    // === vector-set! ===
    vm->sp[0] = v;
    vm->sp[1] = word_from_fixnum(2);
    vm->sp[2] = word_from_fixnum(99);
    word result = prim_vector_set(vm, 3);
    CHECK(is_nil(result), "vector-set! returns nil");
    vm->sp[0] = v;
    vm->sp[1] = word_from_fixnum(2);
    elem = prim_vector_ref(vm, 2);
    CHECK(is_fixnum(elem) && word_to_fixnum(elem) == 99, "vector-ref after set! is 99");

    // === list->vector ===
    // Build list (10 20 30) using prim_list
    vm->sp[0] = word_from_fixnum(10);
    vm->sp[1] = word_from_fixnum(20);
    vm->sp[2] = word_from_fixnum(30);
    lst = prim_list(vm, 3);
    vm->sp[0] = lst;
    word lv = prim_list_to_vector(vm, 1);
    CHECK(is_ptr(lv), "list->vector returns ptr");
    word* lv_hdr = ptr_from_word(lv);
    CHECK(vector_length(lv_hdr) == 3, "list->vector length = 3");
    CHECK(vector_elem(lv_hdr, 0) == word_from_fixnum(10), "list->vector[0] = 10");
    CHECK(vector_elem(lv_hdr, 1) == word_from_fixnum(20), "list->vector[1] = 20");

    // === vector->list ===
    vm->sp[0] = lv;
    lst = prim_vector_to_list(vm, 1);
    CHECK(is_ptr(lst), "vector->list returns ptr");
    cur = ptr_from_word(lst);
    CHECK(pair_car(cur) == word_from_fixnum(10), "vector->list[0]=10");
    cur = ptr_from_word(pair_cdr(cur));
    CHECK(pair_car(cur) == word_from_fixnum(20), "vector->list[1]=20");
    cur = ptr_from_word(pair_cdr(cur));
    CHECK(pair_car(cur) == word_from_fixnum(30), "vector->list[2]=30");
    CHECK(is_nil(pair_cdr(cur)), "vector->list ends with nil");

    // === equal? on same fixnums ===
    vm->sp[0] = word_from_fixnum(42);
    vm->sp[1] = word_from_fixnum(42);
    CHECK(is_true(prim_equal(vm, 2)), "equal? 42 42");
    vm->sp[0] = word_from_fixnum(42);
    vm->sp[1] = word_from_fixnum(43);
    CHECK(is_false(prim_equal(vm, 2)), "equal? 42 43");

    // equal? on pairs (structural)
    // Build (1 . 2) and (1 . 2)
    word* pa = vm->gc->alloc_words(4); obj_set_type(pa, OBJ_TYPE_PAIR);
    pair_car(pa) = word_from_fixnum(1); pair_cdr(pa) = word_from_fixnum(2);
    word* pb = vm->gc->alloc_words(4); obj_set_type(pb, OBJ_TYPE_PAIR);
    pair_car(pb) = word_from_fixnum(1); pair_cdr(pb) = word_from_fixnum(2);
    vm->sp[0] = ptr_to_word(pa);
    vm->sp[1] = ptr_to_word(pb);
    CHECK(is_true(prim_equal(vm, 2)), "equal? (1.2) (1.2)");
    vm->sp[0] = ptr_to_word(pa);
    vm->sp[1] = word_from_fixnum(99);
    CHECK(is_false(prim_equal(vm, 2)), "equal? pair vs fixnum");

    // equal? on vectors
    vm->sp[0] = word_from_fixnum(3);
    vm->sp[1] = word_from_fixnum(0);
    word v1 = prim_make_vector(vm, 2);
    vm->sp[0] = word_from_fixnum(3);
    vm->sp[1] = word_from_fixnum(0);
    word v3 = prim_make_vector(vm, 2);
    vm->sp[0] = v1;
    vm->sp[1] = v3;
    CHECK(is_true(prim_equal(vm, 2)), "equal? zero-vectors");

    // equal? nested pairs: ((1) (2)) vs ((1) (2))
    vm->sp[0] = word_from_fixnum(1);
    vm->sp[1] = word_from_fixnum(2);
    lst = prim_list(vm, 2);  // (1 2)
    vm->sp[0] = word_from_fixnum(1);
    vm->sp[1] = word_from_fixnum(2);
    word lst2 = prim_list(vm, 2);  // (1 2)
    vm->sp[0] = lst;
    vm->sp[1] = lst2;
    CHECK(is_true(prim_equal(vm, 2)), "equal? (1 2) (1 2)");

    if (n_failures == 0)
        printf("ALL listvec tests PASSED\n");
    return n_failures;
}
```

- [ ] **Step 2: Add test to meson.build**

Append to `tests/c/meson.build`:

```meson
test_listvec = executable('test_listvec',
  'test_listvec.c',
  include_directories: test_scheme_inc,
  link_with: scheme_core,
  link_args: ['-lm'],
)
test('listvec', test_listvec)
```

- [ ] **Step 3: Build to verify link failure**

Run: `ninja -C build 2>&1 | tail -10`
Expected: undefined reference errors for `prim_vectorp`, `prim_make_vector`, etc.

- [ ] **Step 4: Create src/primitives/vector.c**

```c
#include "prim.h"
#include "types.h"

/* list — return all arguments as a freshly allocated list */
word prim_list(vm_state_t* vm, int nargs) {
    word result = word_nil();
    for (int i = nargs - 1; i >= 0; i--) {
        word* p = vm->gc->alloc_words(4);
        obj_set_type(p, OBJ_TYPE_PAIR);
        pair_car(p) = vm->sp[i];
        pair_cdr(p) = result;
        result = ptr_to_word(p);
    }
    return result;
}

word prim_vectorp(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_code = 1; return word_nil(); }
    word w = vm->sp[0];
    return (is_ptr(w) && obj_type(ptr_from_word(w)) == OBJ_TYPE_VECTOR)
           ? word_true() : word_false();
}

word prim_make_vector(vm_state_t* vm, int nargs) {
    if (nargs != 2) { vm->error_code = 1; return word_nil(); }
    word kw = vm->sp[0];
    word vw = vm->sp[1];
    if (!is_fixnum(kw)) { vm->error_code = 1; return word_nil(); }
    int64_t k = word_to_fixnum(kw);
    if (k < 0) { vm->error_code = 1; return word_nil(); }
    size_t nwords = 3 + (size_t)k;
    word* vec = vm->gc->alloc_words(nwords);
    obj_set_type(vec, OBJ_TYPE_VECTOR);
    vec[DATA_START_INDEX] = (word)k;
    for (int64_t i = 0; i < k; i++)
        vector_set(vec, i, vw);
    return ptr_to_word(vec);
}

word prim_vector(vm_state_t* vm, int nargs) {
    size_t nwords = 3 + (size_t)nargs;
    word* vec = vm->gc->alloc_words(nwords);
    obj_set_type(vec, OBJ_TYPE_VECTOR);
    vec[DATA_START_INDEX] = (word)(int64_t)nargs;
    for (int i = 0; i < nargs; i++)
        vector_set(vec, i, vm->sp[i]);
    return ptr_to_word(vec);
}

word prim_vector_length(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_code = 1; return word_nil(); }
    word w = vm->sp[0];
    if (!is_ptr(w) || obj_type(ptr_from_word(w)) != OBJ_TYPE_VECTOR) {
        vm->error_code = 1; return word_nil();
    }
    return word_from_fixnum((int64_t)vector_length(ptr_from_word(w)));
}

word prim_vector_ref(vm_state_t* vm, int nargs) {
    if (nargs != 2) { vm->error_code = 1; return word_nil(); }
    word vw = vm->sp[0];
    word iw = vm->sp[1];
    if (!is_ptr(vw) || obj_type(ptr_from_word(vw)) != OBJ_TYPE_VECTOR || !is_fixnum(iw)) {
        vm->error_code = 1; return word_nil();
    }
    int64_t idx = word_to_fixnum(iw);
    word* hdr = ptr_from_word(vw);
    if (idx < 0 || (size_t)idx >= vector_length(hdr)) {
        vm->error_code = 1; return word_nil();
    }
    return vector_elem(hdr, idx);
}

word prim_vector_set(vm_state_t* vm, int nargs) {
    if (nargs != 3) { vm->error_code = 1; return word_nil(); }
    word vw = vm->sp[0];
    word iw = vm->sp[1];
    word val = vm->sp[2];
    if (!is_ptr(vw) || obj_type(ptr_from_word(vw)) != OBJ_TYPE_VECTOR || !is_fixnum(iw)) {
        vm->error_code = 1; return word_nil();
    }
    int64_t idx = word_to_fixnum(iw);
    word* hdr = ptr_from_word(vw);
    if (idx < 0 || (size_t)idx >= vector_length(hdr)) {
        vm->error_code = 1; return word_nil();
    }
    vector_set(hdr, idx, val);
    return word_nil();
}

word prim_list_to_vector(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_code = 1; return word_nil(); }
    word lst = vm->sp[0];
    size_t count = 0;
    word cur = lst;
    while (is_ptr(cur) && obj_type(ptr_from_word(cur)) == OBJ_TYPE_PAIR) {
        count++;
        cur = pair_cdr(ptr_from_word(cur));
    }
    word* vec = vm->gc->alloc_words(3 + count);
    obj_set_type(vec, OBJ_TYPE_VECTOR);
    vec[DATA_START_INDEX] = (word)count;
    cur = lst;
    for (size_t i = 0; i < count; i++) {
        word* p = ptr_from_word(cur);
        vector_set(vec, i, pair_car(p));
        cur = pair_cdr(p);
    }
    return ptr_to_word(vec);
}

word prim_vector_to_list(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_code = 1; return word_nil(); }
    word vw = vm->sp[0];
    if (!is_ptr(vw) || obj_type(ptr_from_word(vw)) != OBJ_TYPE_VECTOR) {
        vm->error_code = 1; return word_nil();
    }
    word* hdr = ptr_from_word(vw);
    size_t len = vector_length(hdr);
    word result = word_nil();
    for (size_t i = len; i > 0; i--) {
        word* p = vm->gc->alloc_words(4);
        obj_set_type(p, OBJ_TYPE_PAIR);
        pair_car(p) = vector_elem(hdr, i - 1);
        pair_cdr(p) = result;
        result = ptr_to_word(p);
    }
    return result;
}
```

- [ ] **Step 5: Register in builtins.c**

Add forward declarations after existing ones:
```c
word prim_list(vm_state_t* vm, int nargs);
word prim_vectorp(vm_state_t* vm, int nargs);
word prim_make_vector(vm_state_t* vm, int nargs);
word prim_vector(vm_state_t* vm, int nargs);
word prim_vector_length(vm_state_t* vm, int nargs);
word prim_vector_ref(vm_state_t* vm, int nargs);
word prim_vector_set(vm_state_t* vm, int nargs);
word prim_list_to_vector(vm_state_t* vm, int nargs);
word prim_vector_to_list(vm_state_t* vm, int nargs);
```

Add to prim_table (before closing `};`):
```c
    {"list",             prim_list},
    {"vector?",          prim_vectorp},
    {"make-vector",      prim_make_vector},
    {"vector",           prim_vector},
    {"vector-length",    prim_vector_length},
    {"vector-ref",       prim_vector_ref},
    {"vector-set!",      prim_vector_set},
    {"list->vector",     prim_list_to_vector},
    {"vector->list",     prim_vector_to_list},
```

- [ ] **Step 6: Add vector.c to src/meson.build**

Add `'primitives/vector.c',` after `'primitives/symbol.c',` in scheme_core_sources.

- [ ] **Step 7: Build and run test**

Run: `ninja -C build 2>&1 | tail -10 && meson test -C build listvec -v`
Expected: build succeeds, "ALL listvec tests PASSED"

- [ ] **Step 8: Commit**

```bash
git add src/primitives/vector.c src/vm/builtins.c src/meson.build tests/c/test_listvec.c tests/c/meson.build
git commit -m "vector+list: add C primitives (vector? make-vector vector-length ref set! list->vector vector->list list)"
```

---

### Task 2: VM Vector Bytecode Handlers

**Files:**
- Modify: `src/vm/vm.c`

- [ ] **Step 1: Add bytecode handlers in vm.c**

Inside `vm_execute` switch, before `case OP_HALT:`, add:

```c
        case OP_MAKE_VEC: {
            uint8_t len = read_u8(&vm->ip);
            size_t nwords = 3 + (size_t)len;
            word* vec = vm->gc->alloc_words(nwords);
            obj_set_type(vec, OBJ_TYPE_VECTOR);
            vec[DATA_START_INDEX] = (word)len;
            for (int i = 0; i < len; i++)
                vector_set(vec, i, word_nil());
            *++vm->sp = ptr_to_word(vec);
            break;
        }

        case OP_VEC_REF: {
            word idx_w = *vm->sp--;
            word vec_w = *vm->sp;
            word* hdr = ptr_from_word(vec_w);
            size_t idx = (size_t)word_to_fixnum(idx_w);
            *vm->sp = vector_elem(hdr, idx);
            break;
        }

        case OP_VEC_SET: {
            word val = *vm->sp--;
            word idx_w = *vm->sp--;
            word vec_w = *vm->sp;
            word* hdr = ptr_from_word(vec_w);
            size_t idx = (size_t)word_to_fixnum(idx_w);
            vector_set(hdr, idx, val);
            break;
        }
```

- [ ] **Step 2: Build**

Run: `ninja -C build 2>&1 | tail -5`
Expected: clean build

- [ ] **Step 3: Commit**

```bash
git add src/vm/vm.c
git commit -m "vm: implement OP_MAKE_VEC/OP_VEC_REF/OP_VEC_SET handlers"
```

---

### Task 3: equal? in pair.c

**Files:**
- Modify: `src/primitives/pair.c`
- Modify: `src/vm/builtins.c`

- [ ] **Step 1: Implement equal? in pair.c**

Append at the end of `src/primitives/pair.c`:

```c
word prim_equal(vm_state_t* vm, int nargs) {
    if (nargs != 2) { vm->error_code = 1; return word_nil(); }
    word a = vm->sp[0];
    word b = vm->sp[1];
    if (a == b) return word_true();
    if (!is_ptr(a) || !is_ptr(b)) return word_false();
    word* ha = ptr_from_word(a);
    word* hb = ptr_from_word(b);
    int ta = (int)obj_type(ha);
    int tb = (int)obj_type(hb);
    if (ta != tb) return word_false();
    switch (ta) {
    case OBJ_TYPE_PAIR: {
        word ca = pair_car(ha), cb = pair_car(hb);
        word da = pair_cdr(ha), db = pair_cdr(hb);
        if (ca != cb) {
            vm->sp[0] = ca; vm->sp[1] = cb;
            if (is_false(prim_equal(vm, 2))) return word_false();
        }
        if (da != db) {
            vm->sp[0] = da; vm->sp[1] = db;
            return prim_equal(vm, 2);
        }
        return word_true();
    }
    case OBJ_TYPE_VECTOR: {
        size_t la = vector_length(ha), lb = vector_length(hb);
        if (la != lb) return word_false();
        for (size_t i = 0; i < la; i++) {
            word ea = vector_elem(ha, i), eb = vector_elem(hb, i);
            if (ea == eb) continue;
            vm->sp[0] = ea; vm->sp[1] = eb;
            if (is_false(prim_equal(vm, 2))) return word_false();
        }
        return word_true();
    }
    default:
        return a == b ? word_true() : word_false();
    }
}
```

- [ ] **Step 2: Add forward declaration to builtins.c**

```c
word prim_equal(vm_state_t* vm, int nargs);
```

- [ ] **Step 3: Add to prim_table**

After `{"eqv?", prim_eqv},`:
```c
    {"equal?",   prim_equal},
```

- [ ] **Step 4: Build and test**

Run: `ninja -C build 2>&1 | tail -5 && meson test -C build listvec -v`
Expected: build succeeds, "ALL listvec tests PASSED"

- [ ] **Step 5: Commit**

```bash
git add src/primitives/pair.c src/vm/builtins.c
git commit -m "pair: add equal? (structural recursive, pairs+vectors)"
```

---

### Task 4: Scheme Library — List and Vector Procedures

**Files:**
- Create: `src/scheme/lib.scm`
- Create: `tests/scheme/test-listvec.ss`
- Modify: `src/main.c`

**Key constraint:** C bootstrap compiler supports `let`, `cond`, `begin`, `if`, `lambda`, `define`, `quote`, `set!`, function calls. Does NOT support variadic `(define (f . args) ...)` or `let*`. `display` shows `#<pair>` for any pair — tests must not use `display` on lists directly.

- [ ] **Step 1: Create lib.scm**

Create `src/scheme/lib.scm`:

```scheme
; lib.scm — R7RS 基本库过程（引导阶段加载）
; C 编译器支持: let, cond, begin, if, lambda, define, quote, set!, 函数调用
; 不支持: 变参 . args, let*
; 已加载: compiler.scm (定义 length, append, reverse)

; ============================================================
; list? — 真列表检测
; ============================================================

(define (list? x)
  (if (pair? x)
      (list? (cdr x))
      (null? x)))

; ============================================================
; list-copy — 复制列表结构
; ============================================================

(define (list-copy lst)
  (if (pair? lst)
      (cons (car lst) (list-copy (cdr lst)))
      lst))

; ============================================================
; list-tail / list-ref
; ============================================================

(define (list-tail lst k)
  (if (= k 0) lst (list-tail (cdr lst) (- k 1))))

(define (list-ref lst k)
  (car (list-tail lst k)))

; ============================================================
; list-set! — 修改第 k 个元素
; ============================================================

(define (list-set! lst k val)
  (if (= k 0)
      (set-car! lst val)
      (list-set! (cdr lst) (- k 1) val)))

; ============================================================
; memq / memv / member
; ============================================================

(define (memq obj lst)
  (cond ((null? lst) ()) ((eq? obj (car lst)) lst)
        (else (memq obj (cdr lst)))))

(define (memv obj lst)
  (cond ((null? lst) ()) ((eqv? obj (car lst)) lst)
        (else (memv obj (cdr lst)))))

(define (member obj lst)
  (cond ((null? lst) ()) ((equal? obj (car lst)) lst)
        (else (member obj (cdr lst)))))

; ============================================================
; assq / assv / assoc
; ============================================================

(define (assq obj alist)
  (cond ((null? alist) ()) ((eq? obj (car (car alist))) (car alist))
        (else (assq obj (cdr alist)))))

(define (assv obj alist)
  (cond ((null? alist) ()) ((eqv? obj (car (car alist))) (car alist))
        (else (assv obj (cdr alist)))))

(define (assoc obj alist)
  (cond ((null? alist) ()) ((equal? obj (car (car alist))) (car alist))
        (else (assoc obj (cdr alist)))))

; ============================================================
; map — 单列表
; ============================================================

(define (map f lst)
  (if (null? lst) ()
      (cons (f (car lst)) (map f (cdr lst)))))

; ============================================================
; for-each — 单列表，副作用
; ============================================================

(define (for-each f lst)
  (if (null? lst) ()
      (begin (f (car lst)) (for-each f (cdr lst)))))

; ============================================================
; filter — 条件过滤
; ============================================================

(define (filter pred lst)
  (cond ((null? lst) ())
        ((pred (car lst)) (cons (car lst) (filter pred (cdr lst))))
        (else (filter pred (cdr lst)))))

; ============================================================
; vector-fill! — 填充向量
; ============================================================

(define (vector-fill! vec fill)
  (vector-fill-loop vec fill 0 (vector-length vec)))

(define (vector-fill-loop vec fill i len)
  (if (= i len) ()
      (begin (vector-set! vec i fill)
             (vector-fill-loop vec fill (+ i 1) len))))

; ============================================================
; vector-copy — 复制向量（通过 list 中转）
; ============================================================

(define (vector-copy vec)
  (list->vector (vector->list vec)))

; ============================================================
; vector-append — 拼接两个向量
; ============================================================

(define (vector-append a b)
  (list->vector (append (vector->list a) (vector->list b))))

; ============================================================
; vector-map — 映射向量
; ============================================================

(define (vector-map f vec)
  (list->vector (map f (vector->list vec))))
```

- [ ] **Step 3: Create Scheme test file**

Create `tests/scheme/test-listvec.ss`:

```scheme
;; test-listvec.ss — list/vector procedure tests
;; Pipe: cat tests/scheme/test-listvec.ss | ./build/src/scheme

(display "=== list/vector tests ===") (newline)

;; list
(display (equal? (list 1 2 3) '(1 2 3))) (newline)   ; #t
(display (null? (list))) (newline)                    ; #t

;; length (from compiler.scm)
(display (length '(a b c))) (newline)                 ; 3
(display (length '())) (newline)                      ; 0

;; append (from compiler.scm)
(define ab (append '(1 2) '(3 4)))
(display (equal? ab '(1 2 3 4))) (newline)            ; #t

;; reverse (from compiler.scm)
(define rev (reverse '(1 2 3)))
(display (equal? rev '(3 2 1))) (newline)             ; #t

;; list-ref
(display (list-ref '(a b c d) 2)) (newline)           ; c

;; list-tail
(display (car (list-tail '(a b c d) 2))) (newline)    ; c

;; memq
(display (equal? (memq 'b '(a b c)) '(b c))) (newline) ; #t
(display (memq 'z '(a b c))) (newline)                 ; ()

;; member (uses equal?)
(display (equal? (member '(1) '((1) (2))) '((1) (2)))) (newline) ; #t

;; assq
(define a1 (assq 'b '((a . 1) (b . 2))))
(display (equal? a1 '(b . 2))) (newline)              ; #t
(display (assq 'z '((a . 1)))) (newline)              ; ()

;; assoc (uses equal?)
(define a2 (assoc '(1) '(((1) . a) ((2) . b))))
(display (equal? a2 '((1) . a))) (newline)            ; #t

;; list-copy creates a fresh copy
(define orig '(1 2 3))
(define copy (list-copy orig))
(display (equal? orig copy)) (newline)                ; #t
(display (eq? orig copy)) (newline)                   ; #f (different objects)

;; map
(define mapped (map (lambda (x) (* x 2)) '(1 2 3)))
(display (equal? mapped '(2 4 6))) (newline)          ; #t

;; for-each
(define fe-acc 0)
(for-each (lambda (x) (set! fe-acc (+ fe-acc x))) '(1 2 3 4))
(display fe-acc) (newline)                            ; 10

;; filter
(define filtered (filter (lambda (x) (< x 3)) '(1 2 3 4 5)))
(display (equal? filtered '(1 2))) (newline)          ; #t

;; vector? / make-vector / vector-length
(define v (make-vector 5 'a))
(display (vector? v)) (newline)                       ; #t
(display (vector-length v)) (newline)                 ; 5
(display (eq? (vector-ref v 0) 'a)) (newline)         ; #t

;; vector-set! / vector-ref
(vector-set! v 2 'z)
(display (eq? (vector-ref v 2) 'z)) (newline)         ; #t

;; vector (literal)
(define v3 (vector 10 20 30))
(display (vector? v3)) (newline)                      ; #t
(display (vector-length v3)) (newline)                ; 3
(display (eqv? (vector-ref v3 1) 20)) (newline)      ; #t

;; list->vector / vector->list round-trip
(define lv (list->vector '(10 20 30)))
(display (vector? lv)) (newline)                      ; #t
(display (vector-length lv)) (newline)                ; 3
(define vl (vector->list lv))
(display (equal? vl '(10 20 30))) (newline)           ; #t

;; vector-fill!
(define vf (make-vector 3 0))
(vector-fill! vf 99)
(display (eqv? (vector-ref vf 0) 99)) (newline)      ; #t
(display (eqv? (vector-ref vf 2) 99)) (newline)      ; #t

;; vector-copy
(define vc (vector 1 2 3))
(define vc2 (vector-copy vc))
(display (equal? (vector->list vc) (vector->list vc2))) (newline) ; #t

;; vector-map
(define vmapped (vector-map (lambda (x) (* x 10)) (vector 1 2 3)))
(display (equal? (vector->list vmapped) '(10 20 30))) (newline) ; #t

;; vector-append
(define vapp (vector-append (vector 1 2) (vector 3 4)))
(display (equal? (vector->list vapp) '(1 2 3 4))) (newline) ; #t

(display "=== all list/vector tests passed ===") (newline)
```

- [ ] **Step 4: Update main.c to load lib.scm**

In `src/main.c`, Phase 1 bootstrap section:

```c
    if (pal->file_exists("src/scheme/compiler.scm"))
        exec_file("src/scheme/compiler.scm", 0);
    if (pal->file_exists("src/scheme/lib.scm"))
        exec_file("src/scheme/lib.scm", 0);
```

- [ ] **Step 5: Build and run Scheme tests**

Run: `ninja -C build 2>&1 | tail -5 && cat tests/scheme/test-listvec.ss | ./build/src/scheme 2>&1`

Expected: All tests pass, concluding with "=== all list/vector tests passed ==="

- [ ] **Step 6: Commit**

```bash
git add src/scheme/lib.scm src/main.c tests/scheme/test-listvec.ss
git commit -m "scheme: add lib.scm with list/vector procedures (memq, assoc, map, filter, vector-copy, etc.)"
```

---

### Task 5: Smoke Test — Existing Tests Still Pass

- [ ] **Step 1: Run full C test suite**

Run: `meson test -C build -v 2>&1`
Expected all 4 pass: types, gc, number, listvec

- [ ] **Step 2: Verify self-host tests**

Run: `cat tests/scheme/test-selfhost.ss | ./build/src/scheme 2>&1`
Expected: "=== all bootstrap tests passed ==="

- [ ] **Step 3: Verify number tests**

Run: `cat tests/scheme/test-number.ss | ./build/src/scheme 2>&1`
Expected: "=== all number tests passed ==="

- [ ] **Step 4: Final check**

```bash
git status
```
If clean, done.
