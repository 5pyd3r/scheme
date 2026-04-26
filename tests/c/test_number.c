#include "types.h"
#include "vm.h"
#include "prim.h"
#include "gc.h"
#include "pal.h"
#include <stdio.h>

extern word prim_add(vm_state_t* vm, int nargs);
extern word prim_sub(vm_state_t* vm, int nargs);
extern word prim_mul(vm_state_t* vm, int nargs);
extern word prim_lt(vm_state_t* vm, int nargs);
extern word prim_gt(vm_state_t* vm, int nargs);
extern word prim_eq_num(vm_state_t* vm, int nargs);
extern word word_from_double(vm_state_t* vm, double d);
extern double word_to_double(word w);

#define INT62_MAX 0x1FFFFFFFFFFFFFFFLL

static int n_failures = 0;
pal_interface* pal;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); n_failures++; } \
} while(0)

int main(void) {
    pal = pal_init();
    gc_interface* gc = gc_init();
    vm_state_t* vm = vm_init(gc, pal);

    // === fixnum add ===
    vm->sp[0] = word_from_fixnum(10);
    vm->sp[1] = word_from_fixnum(20);
    word r = prim_add(vm, 2);
    CHECK(is_fixnum(r) && word_to_fixnum(r) == 30, "fixnum 10+20=30");

    vm->sp[0] = word_from_fixnum(-5);
    vm->sp[1] = word_from_fixnum(3);
    r = prim_add(vm, 2);
    CHECK(is_fixnum(r) && word_to_fixnum(r) == -2, "fixnum -5+3=-2");

    vm->sp[0] = word_from_fixnum(0);
    vm->sp[1] = word_from_fixnum(0);
    r = prim_add(vm, 2);
    CHECK(is_fixnum(r) && word_to_fixnum(r) == 0, "fixnum 0+0=0");

    // === fixnum sub ===
    vm->sp[0] = word_from_fixnum(100);
    vm->sp[1] = word_from_fixnum(1);
    r = prim_sub(vm, 2);
    CHECK(is_fixnum(r) && word_to_fixnum(r) == 99, "fixnum 100-1=99");

    vm->sp[0] = word_from_fixnum(5);
    vm->sp[1] = word_from_fixnum(10);
    r = prim_sub(vm, 2);
    CHECK(is_fixnum(r) && word_to_fixnum(r) == -5, "fixnum 5-10=-5");

    vm->sp[0] = word_from_fixnum(0);
    vm->sp[1] = word_from_fixnum(5);
    r = prim_sub(vm, 2);
    CHECK(is_fixnum(r) && word_to_fixnum(r) == -5, "fixnum 0-5=-5");

    // === fixnum mul ===
    vm->sp[0] = word_from_fixnum(6);
    vm->sp[1] = word_from_fixnum(7);
    r = prim_mul(vm, 2);
    CHECK(is_fixnum(r) && word_to_fixnum(r) == 42, "fixnum 6*7=42");

    // === fixnum compare ===
    vm->sp[0] = word_from_fixnum(1);
    vm->sp[1] = word_from_fixnum(2);
    r = prim_lt(vm, 2);
    CHECK(is_true(r), "1 < 2");

    vm->sp[0] = word_from_fixnum(3);
    vm->sp[1] = word_from_fixnum(2);
    r = prim_lt(vm, 2);
    CHECK(is_false(r), "3 < 2 = #f");

    vm->sp[0] = word_from_fixnum(3);
    vm->sp[1] = word_from_fixnum(3);
    r = prim_lt(vm, 2);
    CHECK(is_false(r), "3 < 3 = #f");

    vm->sp[0] = word_from_fixnum(5);
    vm->sp[1] = word_from_fixnum(5);
    r = prim_eq_num(vm, 2);
    CHECK(is_true(r), "5 = 5");

    vm->sp[0] = word_from_fixnum(5);
    vm->sp[1] = word_from_fixnum(6);
    r = prim_eq_num(vm, 2);
    CHECK(is_false(r), "5 = 6 = #f");

    // === 3-arg add ===
    vm->sp[0] = word_from_fixnum(1);
    vm->sp[1] = word_from_fixnum(2);
    vm->sp[2] = word_from_fixnum(3);
    r = prim_add(vm, 3);
    CHECK(is_fixnum(r) && word_to_fixnum(r) == 6, "fixnum 1+2+3=6");

    // === fixnum boundary ===
    vm->sp[0] = word_from_fixnum(INT62_MAX);
    vm->sp[1] = word_from_fixnum(0);
    r = prim_add(vm, 2);
    CHECK(is_fixnum(r) && word_to_fixnum(r) == INT62_MAX, "max fixnum + 0");

    // === bignum from overflow (INT62_MAX + 1) ===
    vm->sp[0] = word_from_fixnum(INT62_MAX);
    vm->sp[1] = word_from_fixnum(1);
    r = prim_add(vm, 2);
    CHECK(!is_fixnum(r), "INT62_MAX + 1 → not fixnum");

    // === bignum from overflow (INT62_MAX + 2) ===
    vm->sp[0] = word_from_fixnum(INT62_MAX);
    vm->sp[1] = word_from_fixnum(2);
    r = prim_add(vm, 2);
    CHECK(!is_fixnum(r), "INT62_MAX + 2 → not fixnum");

    // === bignum multiplication via overflow ===
    // (2^31)^2 = 2^62 -> overflows fixnum -> bignum
    int64_t v = (int64_t)1 << 31;
    vm->sp[0] = word_from_fixnum(v);
    vm->sp[1] = word_from_fixnum(v);
    r = prim_mul(vm, 2);
    CHECK(!is_fixnum(r), "2^31 * 2^31 overflows to bignum");

    // 1000000 * 1000000 = 10^12, fits in fixnum
    vm->sp[0] = word_from_fixnum(1000000);
    vm->sp[1] = word_from_fixnum(1000000);
    r = prim_mul(vm, 2);
    CHECK(is_fixnum(r) && word_to_fixnum(r) == 1000000000000LL, "1M * 1M = 1T");

    // bignum multiplication chain: 2^31 * 2^31 * 2^31 = 2^93
    vm->sp[0] = word_from_fixnum(v);
    vm->sp[1] = word_from_fixnum(v);
    vm->sp[2] = word_from_fixnum(v);
    r = prim_mul(vm, 3);
    CHECK(!is_fixnum(r), "2^31 * 2^31 * 2^31 overflows to bignum");

    // no overflow tests: basic fixnum mul
    vm->sp[0] = word_from_fixnum(-6);
    vm->sp[1] = word_from_fixnum(7);
    r = prim_mul(vm, 2);
    CHECK(is_fixnum(r) && word_to_fixnum(r) == -42, "fixnum -6*7=-42");

    // fixnum * 0 = 0
    vm->sp[0] = word_from_fixnum(1000000);
    vm->sp[1] = word_from_fixnum(0);
    r = prim_mul(vm, 2);
    CHECK(is_fixnum(r) && word_to_fixnum(r) == 0, "fixnum 1M*0=0");

    // === flonum creation and round-trip ===
    word fl = word_from_double(vm, 3.14);
    CHECK(is_ptr(fl) && !is_fixnum(fl), "flonum created");
    CHECK(word_to_double(fl) == 3.14, "flonum roundtrip");

    fl = word_from_double(vm, -2.5);
    CHECK(word_to_double(fl) == -2.5, "flonum negative roundtrip");

    fl = word_from_double(vm, 0.0);
    CHECK(word_to_double(fl) == 0.0, "flonum zero roundtrip");

    if (n_failures == 0)
        printf("ALL number tests PASSED\n");
    return n_failures;
}
