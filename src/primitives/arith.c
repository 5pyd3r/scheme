#include "prim.h"
#include <stdio.h>

word prim_add(vm_state_t* vm, int nargs) {
    int64_t sum = 0;
    for (int i = 0; i < nargs; i++) {
        word w = vm->sp[i];
        if (!is_fixnum(w)) { vm->error_code = 1; return word_nil(); }
        sum += word_to_fixnum(w);
    }
    return word_from_fixnum(sum);
}

word prim_sub(vm_state_t* vm, int nargs) {
    if (nargs == 0) { vm->error_code = 1; return word_nil(); }
    if (nargs == 1) return word_from_fixnum(-word_to_fixnum(vm->sp[0]));
    word w0 = vm->sp[0];
    if (!is_fixnum(w0)) { vm->error_code = 1; return word_nil(); }
    int64_t result = word_to_fixnum(w0);
    for (int i = 1; i < nargs; i++) {
        word w = vm->sp[i];
        if (!is_fixnum(w)) { vm->error_code = 1; return word_nil(); }
        result -= word_to_fixnum(w);
    }
    return word_from_fixnum(result);
}

word prim_mul(vm_state_t* vm, int nargs) {
    int64_t product = 1;
    for (int i = 0; i < nargs; i++) {
        word w = vm->sp[i];
        if (!is_fixnum(w)) { vm->error_code = 1; return word_nil(); }
        product *= word_to_fixnum(w);
    }
    return word_from_fixnum(product);
}

word prim_div(vm_state_t* vm, int nargs) {
    if (nargs < 1) { vm->error_code = 1; return word_nil(); }
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
