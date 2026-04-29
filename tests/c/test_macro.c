#include "types.h"
#include "vm.h"
#include "prim.h"
#include "gc.h"
#include "pal.h"
#include <stdio.h>

extern word prim_gensym(vm_state_t* vm, int nargs);
extern word prim_eval(vm_state_t* vm, int nargs);

static int n_failures = 0;
pal_interface* pal;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); n_failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while(0)

int main(void) {
    pal = pal_init();
    gc_interface* gc = gc_init();
    vm_state_t* vm = vm_init(gc, pal);
    prim_init_all(vm);

    /* gensym: returns a symbol */
    word g1 = prim_gensym(vm, 0);
    CHECK(is_ptr(g1) && obj_type(ptr_from_word(g1)) == OBJ_TYPE_SYMBOL,
          "gensym returns a symbol");

    /* gensym: two calls return different symbols */
    word g2 = prim_gensym(vm, 0);
    CHECK(g1 != g2, "gensym: two calls return different symbols");

    /* prim_eval with fixnum */
    *++vm->sp = word_from_fixnum(42);
    word r1 = prim_eval(vm, 1);
    vm->sp--;
    CHECK(is_fixnum(r1) && word_to_fixnum(r1) == 42,
          "eval 42 = 42");

    printf("\n%d failures\n", n_failures);
    return n_failures;
}
