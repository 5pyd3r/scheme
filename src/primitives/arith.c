#include "prim.h"
#include "debug.h"

word prim_add(vm_state_t* vm, int nargs) {
    int64_t sum = 0;
    for (int i = 0; i < nargs; i++) {
        word w = vm->sp[i];
        if (!is_fixnum(w)) { VM_ERROR(vm, ERR_TYPE, "+: expected fixnum", w); return word_nil(); }
        sum += word_to_fixnum(w);
    }
    return word_from_fixnum(sum);
}

word prim_sub(vm_state_t* vm, int nargs) {
    if (nargs == 0) { VM_ERROR(vm, ERR_ARITY, "-: expected at least 1 argument", word_nil()); return word_nil(); }
    if (nargs == 1) return word_from_fixnum(-word_to_fixnum(vm->sp[0]));
    word w0 = vm->sp[0];
    if (!is_fixnum(w0)) { VM_ERROR(vm, ERR_TYPE, "-: expected fixnum", w0); return word_nil(); }
    int64_t result = word_to_fixnum(w0);
    for (int i = 1; i < nargs; i++) {
        word w = vm->sp[i];
        if (!is_fixnum(w)) { VM_ERROR(vm, ERR_TYPE, "-: expected fixnum", w); return word_nil(); }
        result -= word_to_fixnum(w);
    }
    return word_from_fixnum(result);
}

word prim_mul(vm_state_t* vm, int nargs) {
    int64_t product = 1;
    for (int i = 0; i < nargs; i++) {
        word w = vm->sp[i];
        if (!is_fixnum(w)) { VM_ERROR(vm, ERR_TYPE, "*: expected fixnum", w); return word_nil(); }
        product *= word_to_fixnum(w);
    }
    return word_from_fixnum(product);
}

word prim_div(vm_state_t* vm, int nargs) {
    if (nargs < 1) { VM_ERROR(vm, ERR_ARITY, "/: expected at least 1 argument", word_nil()); return word_nil(); }
    if (nargs == 1) return word_from_fixnum(word_to_fixnum(vm->sp[0]));
    int64_t result = word_to_fixnum(vm->sp[0]);
    for (int i = 1; i < nargs; i++) {
        result /= word_to_fixnum(vm->sp[i]);
    }
    return word_from_fixnum(result);
}

word prim_lt(vm_state_t* vm, int nargs) {
    for (int i = 1; i < nargs; i++) {
        if (word_to_fixnum(vm->sp[i-1]) >= word_to_fixnum(vm->sp[i]))
            return word_false();
    }
    return word_true();
}

word prim_gt(vm_state_t* vm, int nargs) {
    for (int i = 1; i < nargs; i++) {
        if (word_to_fixnum(vm->sp[i-1]) <= word_to_fixnum(vm->sp[i]))
            return word_false();
    }
    return word_true();
}
