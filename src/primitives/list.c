#include "types.h"
#include "vm.h"

word prim_listp(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_kind = ERR_ARITY; return word_false(); }
    word cur = vm->sp[0];
    while (is_ptr(cur) && obj_type(ptr_from_word(cur)) == OBJ_TYPE_PAIR)
        cur = pair_cdr(ptr_from_word(cur));
    return is_nil(cur) ? word_true() : word_false();
}

word prim_length(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_kind = ERR_ARITY; return word_nil(); }
    word cur = vm->sp[0];
    int64_t len = 0;
    while (is_ptr(cur) && obj_type(ptr_from_word(cur)) == OBJ_TYPE_PAIR) {
        len++;
        cur = pair_cdr(ptr_from_word(cur));
    }
    return word_from_fixnum((int64_t)len);
}

word prim_list_ref(vm_state_t* vm, int nargs) {
    if (nargs != 2) { vm->error_kind = ERR_ARITY; return word_nil(); }
    word cur = vm->sp[0];
    int64_t idx = word_to_fixnum(vm->sp[1]);
    for (int64_t i = 0; i < idx; i++) {
        if (!is_ptr(cur) || obj_type(ptr_from_word(cur)) != OBJ_TYPE_PAIR)
            { vm->error_kind = ERR_TYPE; return word_nil(); }
        cur = pair_cdr(ptr_from_word(cur));
    }
    return pair_car(ptr_from_word(cur));
}

word prim_list_tail(vm_state_t* vm, int nargs) {
    if (nargs != 2) { vm->error_kind = ERR_ARITY; return word_nil(); }
    word cur = vm->sp[0];
    int64_t idx = word_to_fixnum(vm->sp[1]);
    for (int64_t i = 0; i < idx; i++) {
        if (!is_ptr(cur) || obj_type(ptr_from_word(cur)) != OBJ_TYPE_PAIR)
            { vm->error_kind = ERR_TYPE; return word_nil(); }
        cur = pair_cdr(ptr_from_word(cur));
    }
    return cur;
}

word prim_reverse(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_kind = ERR_ARITY; return word_nil(); }
    word cur = vm->sp[0];
    word result = word_nil();
    while (is_ptr(cur) && obj_type(ptr_from_word(cur)) == OBJ_TYPE_PAIR) {
        word* p = vm->gc->alloc_words(4);
        obj_set_type(p, OBJ_TYPE_PAIR);
        pair_car(p) = pair_car(ptr_from_word(cur));
        pair_cdr(p) = result;
        result = ptr_to_word(p);
        cur = pair_cdr(ptr_from_word(cur));
    }
    return result;
}

word prim_append(vm_state_t* vm, int nargs) {
    if (nargs == 0) return word_nil();
    // Validate all args are proper lists
    for (int i = 0; i < nargs; i++) {
        word cur = vm->sp[i];
        while (is_ptr(cur) && obj_type(ptr_from_word(cur)) == OBJ_TYPE_PAIR)
            cur = pair_cdr(ptr_from_word(cur));
        if (!is_nil(cur)) { vm->error_kind = ERR_TYPE; return word_nil(); }
    }
    // Build result from end
    word result = word_nil();
    for (int i = nargs - 1; i >= 0; i--) {
        // Build reversed list for this segment, then cons onto result
        word seg = word_nil();
        word cur = vm->sp[i];
        while (is_ptr(cur) && obj_type(ptr_from_word(cur)) == OBJ_TYPE_PAIR) {
            word* p = vm->gc->alloc_words(4);
            obj_set_type(p, OBJ_TYPE_PAIR);
            pair_car(p) = pair_car(ptr_from_word(cur));
            pair_cdr(p) = seg;
            seg = ptr_to_word(p);
            cur = pair_cdr(ptr_from_word(cur));
        }
        // seg is reversed; now prepend to result
        while (is_ptr(seg) && obj_type(ptr_from_word(seg)) == OBJ_TYPE_PAIR) {
            word* p = vm->gc->alloc_words(4);
            obj_set_type(p, OBJ_TYPE_PAIR);
            pair_car(p) = pair_car(ptr_from_word(seg));
            pair_cdr(p) = result;
            result = ptr_to_word(p);
            seg = pair_cdr(ptr_from_word(seg));
        }
    }
    return result;
}

word prim_memq(vm_state_t* vm, int nargs) {
    if (nargs != 2) { vm->error_kind = ERR_ARITY; return word_false(); }
    word key = vm->sp[0], cur = vm->sp[1];
    while (is_ptr(cur) && obj_type(ptr_from_word(cur)) == OBJ_TYPE_PAIR) {
        if (pair_car(ptr_from_word(cur)) == key) return cur;
        cur = pair_cdr(ptr_from_word(cur));
    }
    return word_false();
}

word prim_member(vm_state_t* vm, int nargs) {
    if (nargs != 2) { vm->error_kind = ERR_ARITY; return word_false(); }
    word key = vm->sp[0], cur = vm->sp[1];
    while (is_ptr(cur) && obj_type(ptr_from_word(cur)) == OBJ_TYPE_PAIR) {
        word val = pair_car(ptr_from_word(cur));
        // Simple comparison: eq? for now
        if (val == key) return cur;
        cur = pair_cdr(ptr_from_word(cur));
    }
    return word_false();
}

word prim_assq(vm_state_t* vm, int nargs) {
    if (nargs != 2) { vm->error_kind = ERR_ARITY; return word_false(); }
    word key = vm->sp[0], cur = vm->sp[1];
    while (is_ptr(cur) && obj_type(ptr_from_word(cur)) == OBJ_TYPE_PAIR) {
        word entry = pair_car(ptr_from_word(cur));
        if (is_ptr(entry) && obj_type(ptr_from_word(entry)) == OBJ_TYPE_PAIR) {
            if (pair_car(ptr_from_word(entry)) == key) return entry;
        }
        cur = pair_cdr(ptr_from_word(cur));
    }
    return word_false();
}

word prim_make_list(vm_state_t* vm, int nargs) {
    if (nargs < 1 || nargs > 2) { vm->error_kind = ERR_ARITY; return word_nil(); }
    int64_t k = word_to_fixnum(vm->sp[nargs - 1]);
    word fill = (nargs == 2) ? vm->sp[0] : word_nil();
    word result = word_nil();
    for (int64_t i = 0; i < k; i++) {
        word* p = vm->gc->alloc_words(4); obj_set_type(p, OBJ_TYPE_PAIR);
        pair_car(p) = fill; pair_cdr(p) = result;
        result = ptr_to_word(p);
    }
    return result;
}

word prim_list_set(vm_state_t* vm, int nargs) {
    if (nargs != 3) { vm->error_kind = ERR_ARITY; return word_nil(); }
    word lst = vm->sp[0];
    int64_t k = word_to_fixnum(vm->sp[1]);
    word val = vm->sp[2];
    word cur = lst;
    for (int64_t i = 0; i < k; i++) {
        if (!is_ptr(cur) || obj_type(ptr_from_word(cur)) != OBJ_TYPE_PAIR)
            { vm->error_kind = ERR_TYPE; return word_nil(); }
        cur = pair_cdr(ptr_from_word(cur));
    }
    pair_car(ptr_from_word(cur)) = val;
    return word_nil();
}

word prim_list_copy(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_kind = ERR_ARITY; return word_nil(); }
    word cur = vm->sp[0];
    word result = word_nil();
    word* prev = NULL;
    while (is_ptr(cur) && obj_type(ptr_from_word(cur)) == OBJ_TYPE_PAIR) {
        word* p = vm->gc->alloc_words(4); obj_set_type(p, OBJ_TYPE_PAIR);
        pair_car(p) = pair_car(ptr_from_word(cur));
        pair_cdr(p) = word_nil();
        if (prev) pair_cdr(prev) = ptr_to_word(p);
        else result = ptr_to_word(p);
        prev = p;
        cur = pair_cdr(ptr_from_word(cur));
    }
    return result;
}

word prim_memv(vm_state_t* vm, int nargs) {
    if (nargs != 2) { vm->error_kind = ERR_ARITY; return word_false(); }
    word key = vm->sp[0], cur = vm->sp[1];
    while (is_ptr(cur) && obj_type(ptr_from_word(cur)) == OBJ_TYPE_PAIR) {
        word val = pair_car(ptr_from_word(cur));
        if (val == key) return cur;  // eqv? for same immediate values
        cur = pair_cdr(ptr_from_word(cur));
    }
    return word_false();
}

word prim_assv(vm_state_t* vm, int nargs) {
    if (nargs != 2) { vm->error_kind = ERR_ARITY; return word_false(); }
    word key = vm->sp[0], cur = vm->sp[1];
    while (is_ptr(cur) && obj_type(ptr_from_word(cur)) == OBJ_TYPE_PAIR) {
        word entry = pair_car(ptr_from_word(cur));
        if (is_ptr(entry) && obj_type(ptr_from_word(entry)) == OBJ_TYPE_PAIR)
            if (pair_car(ptr_from_word(entry)) == key) return entry;
        cur = pair_cdr(ptr_from_word(cur));
    }
    return word_false();
}

word prim_assoc(vm_state_t* vm, int nargs) {
    // assoc uses equal? but we approximate with eqv? for now
    if (nargs != 2) { vm->error_kind = ERR_ARITY; return word_false(); }
    word key = vm->sp[0], cur = vm->sp[1];
    while (is_ptr(cur) && obj_type(ptr_from_word(cur)) == OBJ_TYPE_PAIR) {
        word entry = pair_car(ptr_from_word(cur));
        if (is_ptr(entry) && obj_type(ptr_from_word(entry)) == OBJ_TYPE_PAIR)
            if (pair_car(ptr_from_word(entry)) == key) return entry;
        cur = pair_cdr(ptr_from_word(cur));
    }
    return word_false();
}
