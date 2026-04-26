#include "types.h"
#include "vm.h"
#include "prim.h"
#include "gc.h"
#include "pal.h"
#include <stdio.h>

extern word prim_bytevectorp(vm_state_t* vm, int nargs);
extern word prim_make_bytevector(vm_state_t* vm, int nargs);
extern word prim_bytevector(vm_state_t* vm, int nargs);
extern word prim_bytevector_length(vm_state_t* vm, int nargs);
extern word prim_bytevector_u8_ref(vm_state_t* vm, int nargs);
extern word prim_bytevector_u8_set(vm_state_t* vm, int nargs);
extern word prim_bytevector_to_u8_list(vm_state_t* vm, int nargs);
extern word prim_u8_list_to_bytevector(vm_state_t* vm, int nargs);

static int n_failures = 0;
pal_interface* pal;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); n_failures++; } \
} while(0)

int main(void) {
    pal = pal_init();
    gc_interface* gc = gc_init();
    vm_state_t* vm = vm_init(gc, pal);

    // === bytevector? ===
    vm->sp[0] = word_from_fixnum(1);
    vm->sp[1] = word_from_fixnum(2);
    word bv = prim_bytevector(vm, 2);
    vm->sp[0] = bv;
    CHECK(prim_bytevectorp(vm, 1) == word_true(), "bytevector? on bytevector is #t");
    vm->sp[0] = word_from_fixnum(42);
    CHECK(prim_bytevectorp(vm, 1) == word_false(), "bytevector? on fixnum is #f");

    // === make-bytevector ===
    vm->sp[0] = word_from_fixnum(5);
    vm->sp[1] = word_from_fixnum(255);
    word mbv = prim_make_bytevector(vm, 2);
    vm->sp[0] = mbv;
    CHECK(prim_bytevectorp(vm, 1) == word_true(), "make-bytevector returns bytevector");
    vm->sp[0] = mbv;
    word len = prim_bytevector_length(vm, 1);
    CHECK(is_fixnum(len) && word_to_fixnum(len) == 5, "make-bytevector length = 5");

    // make-bytevector with 1 arg
    vm->sp[0] = word_from_fixnum(3);
    word mbv2 = prim_make_bytevector(vm, 1);
    vm->sp[0] = mbv2;
    CHECK(prim_bytevectorp(vm, 1) == word_true(), "make-bytevector 1 arg returns bytevector");

    // === bytevector ===
    vm->sp[0] = word_from_fixnum(10);
    vm->sp[1] = word_from_fixnum(20);
    vm->sp[2] = word_from_fixnum(30);
    word bv3 = prim_bytevector(vm, 3);
    vm->sp[0] = bv3;
    CHECK(prim_bytevectorp(vm, 1) == word_true(), "bytevector returns bytevector");
    vm->sp[0] = bv3;
    len = prim_bytevector_length(vm, 1);
    CHECK(is_fixnum(len) && word_to_fixnum(len) == 3, "bytevector length = 3");

    // === bytevector-u8-ref ===
    vm->sp[0] = bv3;
    vm->sp[1] = word_from_fixnum(1);
    word ref = prim_bytevector_u8_ref(vm, 2);
    CHECK(is_fixnum(ref) && word_to_fixnum(ref) == 20, "bytevector-u8-ref index 1 = 20");

    // === bytevector-u8-set! ===
    vm->sp[0] = mbv;
    vm->sp[1] = word_from_fixnum(0);
    vm->sp[2] = word_from_fixnum(42);
    word result = prim_bytevector_u8_set(vm, 3);
    CHECK(is_nil(result), "bytevector-u8-set! returns nil");
    vm->sp[0] = mbv;
    vm->sp[1] = word_from_fixnum(0);
    ref = prim_bytevector_u8_ref(vm, 2);
    CHECK(is_fixnum(ref) && word_to_fixnum(ref) == 42, "bytevector-u8-ref after set! = 42");

    // === bytevector->u8-list ===
    vm->sp[0] = bv3;
    word lst = prim_bytevector_to_u8_list(vm, 1);
    CHECK(is_ptr(lst), "bytevector->u8-list returns pair");
    word* cur = ptr_from_word(lst);
    CHECK(pair_car(cur) == word_from_fixnum(10), "bytevector->u8-list[0] = 10");
    cur = ptr_from_word(pair_cdr(cur));
    CHECK(pair_car(cur) == word_from_fixnum(20), "bytevector->u8-list[1] = 20");
    cur = ptr_from_word(pair_cdr(cur));
    CHECK(pair_car(cur) == word_from_fixnum(30), "bytevector->u8-list[2] = 30");
    CHECK(is_nil(pair_cdr(cur)), "bytevector->u8-list ends with nil");

    // === u8-list->bytevector ===
    word* p = vm->gc->alloc_words(4); obj_set_type(p, OBJ_TYPE_PAIR);
    pair_car(p) = word_from_fixnum(100);
    word* q = vm->gc->alloc_words(4); obj_set_type(q, OBJ_TYPE_PAIR);
    pair_car(q) = word_from_fixnum(200);
    pair_cdr(q) = word_nil();
    pair_cdr(p) = ptr_to_word(q);
    vm->sp[0] = ptr_to_word(p);
    word lbv = prim_u8_list_to_bytevector(vm, 1);
    vm->sp[0] = lbv;
    CHECK(prim_bytevectorp(vm, 1) == word_true(), "u8-list->bytevector returns bytevector");
    vm->sp[0] = lbv;
    len = prim_bytevector_length(vm, 1);
    CHECK(is_fixnum(len) && word_to_fixnum(len) == 2, "u8-list->bytevector length = 2");
    vm->sp[0] = lbv;
    vm->sp[1] = word_from_fixnum(0);
    ref = prim_bytevector_u8_ref(vm, 2);
    CHECK(is_fixnum(ref) && word_to_fixnum(ref) == 100, "u8-list->bytevector[0] = 100");
    vm->sp[0] = lbv;
    vm->sp[1] = word_from_fixnum(1);
    ref = prim_bytevector_u8_ref(vm, 2);
    CHECK(is_fixnum(ref) && word_to_fixnum(ref) == 200, "u8-list->bytevector[1] = 200");

    if (n_failures == 0)
        printf("ALL bytevector tests PASSED\n");
    return n_failures;
}
