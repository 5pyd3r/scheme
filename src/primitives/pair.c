#include "prim.h"
#include "types.h"

word prim_cons(vm_state_t* vm, int nargs) {
    if (nargs != 2) { vm->error_code = 1; return word_nil(); }
    word car = vm->sp[0];
    word cdr = vm->sp[1];
    word* pair = vm->gc->alloc_words(4);
    obj_set_type(pair, OBJ_TYPE_PAIR);
    pair_car(pair) = car;
    pair_cdr(pair) = cdr;
    return ptr_to_word(pair);
}

word prim_car(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_code = 1; return word_nil(); }
    word* pair = ptr_from_word(vm->sp[0]);
    return pair_car(pair);
}

word prim_cdr(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_code = 1; return word_nil(); }
    word* pair = ptr_from_word(vm->sp[0]);
    return pair_cdr(pair);
}

word prim_null(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_code = 1; return word_nil(); }
    return is_nil(vm->sp[0]) ? word_true() : word_false();
}

word prim_pair(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_code = 1; return word_nil(); }
    return is_ptr(vm->sp[0]) && obj_type(ptr_from_word(vm->sp[0])) == OBJ_TYPE_PAIR
           ? word_true() : word_false();
}

word prim_eqv(vm_state_t* vm, int nargs) {
    if (nargs != 2) { vm->error_code = 1; return word_nil(); }
    return vm->sp[0] == vm->sp[1] ? word_true() : word_false();
}

word prim_eq(vm_state_t* vm, int nargs) { return prim_eqv(vm, nargs); }

word prim_set_car(vm_state_t* vm, int nargs) {
    if (nargs != 2) { vm->error_code = 1; return word_nil(); }
    word* pair = ptr_from_word(vm->sp[0]);
    pair_car(pair) = vm->sp[1];
    return word_nil();
}

word prim_set_cdr(vm_state_t* vm, int nargs) {
    if (nargs != 2) { vm->error_code = 1; return word_nil(); }
    word* pair = ptr_from_word(vm->sp[0]);
    pair_cdr(pair) = vm->sp[1];
    return word_nil();
}
