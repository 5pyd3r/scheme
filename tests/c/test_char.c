#include "types.h"
#include "vm.h"
#include "prim.h"
#include "gc.h"
#include "pal.h"
#include <stdio.h>

extern word prim_charp(vm_state_t* vm, int nargs);
extern word prim_char_to_integer(vm_state_t* vm, int nargs);
extern word prim_integer_to_char(vm_state_t* vm, int nargs);
extern word prim_char_eq(vm_state_t* vm, int nargs);
extern word prim_char_lt(vm_state_t* vm, int nargs);
extern word prim_char_gt(vm_state_t* vm, int nargs);
extern word prim_char_le(vm_state_t* vm, int nargs);
extern word prim_char_ge(vm_state_t* vm, int nargs);

static int n_failures = 0;
pal_interface* pal;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); n_failures++; } \
} while(0)

int main(void) {
    pal = pal_init();
    gc_interface* gc = gc_init();
    vm_state_t* vm = vm_init(gc, pal);
    (void)vm;

    // === char? ===
    vm->sp[0] = word_from_char('A');
    CHECK(prim_charp(vm, 1) == word_true(), "char? on char is #t");
    vm->sp[0] = word_from_fixnum(65);
    CHECK(prim_charp(vm, 1) == word_false(), "char? on fixnum is #f");
    vm->sp[0] = word_true();
    CHECK(prim_charp(vm, 1) == word_false(), "char? on bool is #f");

    // === char->integer ===
    vm->sp[0] = word_from_char('A');
    word ci = prim_char_to_integer(vm, 1);
    CHECK(is_fixnum(ci) && word_to_fixnum(ci) == 65, "char->integer A = 65");
    vm->sp[0] = word_from_char(' ');
    ci = prim_char_to_integer(vm, 1);
    CHECK(is_fixnum(ci) && word_to_fixnum(ci) == 32, "char->integer space = 32");

    // === integer->char ===
    vm->sp[0] = word_from_fixnum(65);
    word ic = prim_integer_to_char(vm, 1);
    CHECK(is_char(ic) && word_to_char(ic) == 'A', "integer->char 65 = A");
    vm->sp[0] = word_from_fixnum(0x10FFFF);
    ic = prim_integer_to_char(vm, 1);
    CHECK(is_char(ic), "integer->char 0x10FFFF ok");
    // Out of range
    vm->sp[0] = word_from_fixnum(-1);
    vm->error_kind = 0;
    ic = prim_integer_to_char(vm, 1);
    CHECK(vm->error_kind != 0, "integer->char -1 errors");

    // === char=? ===
    vm->sp[0] = word_from_char('A'); vm->sp[1] = word_from_char('A');
    vm->error_kind = 0;
    CHECK(prim_char_eq(vm, 2) == word_true(), "char=? A A");
    vm->sp[0] = word_from_char('A'); vm->sp[1] = word_from_char('B');
    CHECK(prim_char_eq(vm, 2) == word_false(), "char=? A B");

    // === char<? ===
    vm->sp[0] = word_from_char('A'); vm->sp[1] = word_from_char('B');
    CHECK(prim_char_lt(vm, 2) == word_true(), "char<? A B");
    vm->sp[0] = word_from_char('B'); vm->sp[1] = word_from_char('A');
    CHECK(prim_char_lt(vm, 2) == word_false(), "char<? B A");

    // === char>? ===
    vm->sp[0] = word_from_char('B'); vm->sp[1] = word_from_char('A');
    CHECK(prim_char_gt(vm, 2) == word_true(), "char>? B A");

    // === char<=? ===
    vm->sp[0] = word_from_char('A'); vm->sp[1] = word_from_char('A');
    CHECK(prim_char_le(vm, 2) == word_true(), "char<=? A A");
    vm->sp[0] = word_from_char('A'); vm->sp[1] = word_from_char('B');
    CHECK(prim_char_le(vm, 2) == word_true(), "char<=? A B");
    vm->sp[0] = word_from_char('C'); vm->sp[1] = word_from_char('A');
    CHECK(prim_char_le(vm, 2) == word_false(), "char<=? C A");

    // === char>=? ===
    vm->sp[0] = word_from_char('B'); vm->sp[1] = word_from_char('A');
    CHECK(prim_char_ge(vm, 2) == word_true(), "char>=? B A");
    vm->sp[0] = word_from_char('A'); vm->sp[1] = word_from_char('B');
    CHECK(prim_char_ge(vm, 2) == word_false(), "char>=? A B");

    if (n_failures == 0)
        printf("ALL char tests PASSED\n");
    return n_failures;
}
