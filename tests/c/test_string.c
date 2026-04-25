#include "types.h"
#include "vm.h"
#include "prim.h"
#include "gc.h"
#include "pal.h"
#include <stdio.h>

extern word prim_stringp(vm_state_t* vm, int nargs);
extern word prim_make_string(vm_state_t* vm, int nargs);
extern word prim_string(vm_state_t* vm, int nargs);
extern word prim_string_length(vm_state_t* vm, int nargs);
extern word prim_string_ref(vm_state_t* vm, int nargs);
extern word prim_string_set(vm_state_t* vm, int nargs);
extern word prim_string_eq(vm_state_t* vm, int nargs);
extern word prim_string_lt(vm_state_t* vm, int nargs);
extern word prim_string_to_list(vm_state_t* vm, int nargs);
extern word prim_list_to_string(vm_state_t* vm, int nargs);

static int n_failures = 0;
pal_interface* pal;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); n_failures++; } \
} while(0)

int main(void) {
    pal = pal_init();
    gc_interface* gc = gc_init();
    vm_state_t* vm = vm_init(gc, pal);

    // === string? ===
    vm->sp[0] = word_from_char('a');
    vm->sp[1] = word_from_char('b');
    word s = prim_string(vm, 2);
    vm->sp[0] = s;
    CHECK(prim_stringp(vm, 1) == word_true(), "string? on string is #t");
    vm->sp[0] = word_from_fixnum(42);
    CHECK(prim_stringp(vm, 1) == word_false(), "string? on fixnum is #f");

    // === make-string ===
    vm->sp[0] = word_from_fixnum(5);
    vm->sp[1] = word_from_char('z');
    word ms = prim_make_string(vm, 2);
    vm->sp[0] = ms;
    CHECK(prim_stringp(vm, 1) == word_true(), "make-string returns string");
    vm->sp[0] = ms;
    word len = prim_string_length(vm, 1);
    CHECK(is_fixnum(len) && word_to_fixnum(len) == 5, "make-string length = 5");

    // make-string with 1 arg
    vm->sp[0] = word_from_fixnum(3);
    word ms2 = prim_make_string(vm, 1);
    vm->sp[0] = ms2;
    CHECK(prim_stringp(vm, 1) == word_true(), "make-string 1 arg returns string");
    vm->sp[0] = ms2;
    len = prim_string_length(vm, 1);
    CHECK(is_fixnum(len) && word_to_fixnum(len) == 3, "make-string 1 arg length = 3");

    // === string ===
    vm->sp[0] = word_from_char('x');
    vm->sp[1] = word_from_char('y');
    vm->sp[2] = word_from_char('z');
    word s3 = prim_string(vm, 3);
    vm->sp[0] = s3;
    CHECK(prim_stringp(vm, 1) == word_true(), "string returns string");
    vm->sp[0] = s3;
    len = prim_string_length(vm, 1);
    CHECK(is_fixnum(len) && word_to_fixnum(len) == 3, "string length = 3");

    // === string-ref ===
    vm->sp[0] = s3;
    vm->sp[1] = word_from_fixnum(1);
    word ref = prim_string_ref(vm, 2);
    CHECK(is_char(ref) && word_to_char(ref) == 'y', "string-ref index 1 = 'y'");

    // === string-set! ===
    vm->sp[0] = ms;
    vm->sp[1] = word_from_fixnum(0);
    vm->sp[2] = word_from_char('a');
    word result = prim_string_set(vm, 3);
    CHECK(is_nil(result), "string-set! returns nil");
    vm->sp[0] = ms;
    vm->sp[1] = word_from_fixnum(0);
    ref = prim_string_ref(vm, 2);
    CHECK(is_char(ref) && word_to_char(ref) == 'a', "string-ref after set! = 'a'");

    // === string=? ===
    vm->sp[0] = s;
    vm->sp[1] = s;
    CHECK(prim_string_eq(vm, 2) == word_true(), "string=? same string ref");
    vm->sp[0] = word_from_char('a');
    vm->sp[1] = word_from_char('b');
    word s_ab1 = prim_string(vm, 2);
    vm->sp[0] = word_from_char('a');
    vm->sp[1] = word_from_char('b');
    word s_ab2 = prim_string(vm, 2);
    vm->sp[0] = s_ab1;
    vm->sp[1] = s_ab2;
    CHECK(prim_string_eq(vm, 2) == word_true(), "string=? 'ab' 'ab'");
    vm->sp[0] = s_ab1;
    vm->sp[1] = s3;
    CHECK(prim_string_eq(vm, 2) == word_false(), "string=? 'ab' 'xyz'");

    // === string<? ===
    vm->sp[0] = word_from_char('a');
    word s_a = prim_string(vm, 1);
    vm->sp[0] = word_from_char('b');
    word s_b = prim_string(vm, 1);
    vm->sp[0] = s_a;
    vm->sp[1] = s_b;
    CHECK(prim_string_lt(vm, 2) == word_true(), "string<? 'a' 'b'");
    vm->sp[0] = s_b;
    vm->sp[1] = s_a;
    CHECK(prim_string_lt(vm, 2) == word_false(), "string<? 'b' 'a'");

    // === string->list ===
    vm->sp[0] = s_ab1;
    word lst = prim_string_to_list(vm, 1);
    CHECK(is_ptr(lst), "string->list returns pair");
    word* cur = ptr_from_word(lst);
    CHECK(pair_car(cur) == word_from_char('a'), "string->list[0] = 'a'");
    cur = ptr_from_word(pair_cdr(cur));
    CHECK(pair_car(cur) == word_from_char('b'), "string->list[1] = 'b'");
    CHECK(is_nil(pair_cdr(cur)), "string->list ends with nil");

    // === list->string ===
    word* p = vm->gc->alloc_words(4); obj_set_type(p, OBJ_TYPE_PAIR);
    pair_car(p) = word_from_char('x');
    word* q = vm->gc->alloc_words(4); obj_set_type(q, OBJ_TYPE_PAIR);
    pair_car(q) = word_from_char('y');
    pair_cdr(q) = word_nil();
    pair_cdr(p) = ptr_to_word(q);
    vm->sp[0] = ptr_to_word(p);
    word ls = prim_list_to_string(vm, 1);
    vm->sp[0] = ls;
    CHECK(prim_stringp(vm, 1) == word_true(), "list->string returns string");
    vm->sp[0] = ls;
    len = prim_string_length(vm, 1);
    CHECK(is_fixnum(len) && word_to_fixnum(len) == 2, "list->string length = 2");
    vm->sp[0] = ls;
    vm->sp[1] = word_from_fixnum(0);
    CHECK(prim_string_ref(vm, 2) == word_from_char('x'), "list->string[0] = 'x'");

    if (n_failures == 0)
        printf("ALL string tests PASSED\n");
    return n_failures;
}
