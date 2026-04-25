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

    // equal? nested pairs
    vm->sp[0] = word_from_fixnum(1);
    vm->sp[1] = word_from_fixnum(2);
    lst = prim_list(vm, 2);
    vm->sp[0] = word_from_fixnum(1);
    vm->sp[1] = word_from_fixnum(2);
    word lst2 = prim_list(vm, 2);
    vm->sp[0] = lst;
    vm->sp[1] = lst2;
    CHECK(is_true(prim_equal(vm, 2)), "equal? (1 2) (1 2)");

    if (n_failures == 0)
        printf("ALL listvec tests PASSED\n");
    return n_failures;
}
