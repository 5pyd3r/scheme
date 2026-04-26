#include "prim.h"
#include "debug.h"
#include "types.h"

word prim_cons(vm_state_t* vm, int nargs) {
    if (nargs != 2) { VM_ERROR(vm, ERR_ARITY, "cons: expected 2 arguments", word_nil()); return word_nil(); }
    word car = vm->sp[0];
    word cdr = vm->sp[1];
    word* pair = vm->gc->alloc_words(4);
    obj_set_type(pair, OBJ_TYPE_PAIR);
    pair_car(pair) = car;
    pair_cdr(pair) = cdr;
    return ptr_to_word(pair);
}

word prim_car(vm_state_t* vm, int nargs) {
    if (nargs != 1) { VM_ERROR(vm, ERR_ARITY, "car: expected 1 argument", word_nil()); return word_nil(); }
    word* pair = ptr_from_word(vm->sp[0]);
    return pair_car(pair);
}

word prim_cdr(vm_state_t* vm, int nargs) {
    if (nargs != 1) { VM_ERROR(vm, ERR_ARITY, "cdr: expected 1 argument", word_nil()); return word_nil(); }
    word* pair = ptr_from_word(vm->sp[0]);
    return pair_cdr(pair);
}

word prim_null(vm_state_t* vm, int nargs) {
    if (nargs != 1) { VM_ERROR(vm, ERR_ARITY, "null?: expected 1 argument", word_nil()); return word_nil(); }
    return is_nil(vm->sp[0]) ? word_true() : word_false();
}

word prim_pair(vm_state_t* vm, int nargs) {
    if (nargs != 1) { VM_ERROR(vm, ERR_ARITY, "pair?: expected 1 argument", word_nil()); return word_nil(); }
    return is_ptr(vm->sp[0]) && obj_type(ptr_from_word(vm->sp[0])) == OBJ_TYPE_PAIR
           ? word_true() : word_false();
}

word prim_eqv(vm_state_t* vm, int nargs) {
    if (nargs != 2) { VM_ERROR(vm, ERR_ARITY, "eqv?: expected 2 arguments", word_nil()); return word_nil(); }
    return vm->sp[0] == vm->sp[1] ? word_true() : word_false();
}

word prim_eq(vm_state_t* vm, int nargs) { return prim_eqv(vm, nargs); }

word prim_set_car(vm_state_t* vm, int nargs) {
    if (nargs != 2) { VM_ERROR(vm, ERR_ARITY, "set-car!: expected 2 arguments", word_nil()); return word_nil(); }
    word* pair = ptr_from_word(vm->sp[0]);
    pair_car(pair) = vm->sp[1];
    return word_nil();
}

word prim_set_cdr(vm_state_t* vm, int nargs) {
    if (nargs != 2) { VM_ERROR(vm, ERR_ARITY, "set-cdr!: expected 2 arguments", word_nil()); return word_nil(); }
    word* pair = ptr_from_word(vm->sp[0]);
    pair_cdr(pair) = vm->sp[1];
    return word_nil();
}

/* equal? -- structural recursive comparison */
static bool equal_rec(vm_state_t* vm, word a, word b) {
    if (a == b) return true;
    if (!is_ptr(a) || !is_ptr(b)) return false;
    word* ha = ptr_from_word(a);
    word* hb = ptr_from_word(b);
    int ta = (int)obj_type(ha);
    int tb = (int)obj_type(hb);
    if (ta != tb) return false;
    switch (ta) {
    case OBJ_TYPE_PAIR:
        if (!equal_rec(vm, pair_car(ha), pair_car(hb))) return false;
        return equal_rec(vm, pair_cdr(ha), pair_cdr(hb));
    case OBJ_TYPE_VECTOR: {
        size_t la = vector_length(ha), lb = vector_length(hb);
        if (la != lb) return false;
        for (size_t i = 0; i < la; i++) {
            if (!equal_rec(vm, vector_elem(ha, i), vector_elem(hb, i)))
                return false;
        }
        return true;
    }
    default:
        return false;
    }
}

word prim_equal(vm_state_t* vm, int nargs) {
    if (nargs != 2) { vm->error_kind = 1; return word_nil(); }
    return equal_rec(vm, vm->sp[0], vm->sp[1]) ? word_true() : word_false();
}
