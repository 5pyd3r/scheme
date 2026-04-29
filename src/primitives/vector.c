#include "prim.h"
#include "types.h"

/* list -- return all arguments as a freshly allocated list */
word prim_list(vm_state_t* vm, int nargs) {
    word result = word_nil();
    for (int i = nargs - 1; i >= 0; i--) {
        word* p = vm->gc->alloc_words(4);
        obj_set_type(p, OBJ_TYPE_PAIR);
        pair_car(p) = vm->sp[i];
        pair_cdr(p) = result;
        result = ptr_to_word(p);
    }
    return result;
}

word prim_vectorp(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_kind = 1; return word_nil(); }
    word w = vm->sp[0];
    return (is_ptr(w) && obj_type(ptr_from_word(w)) == OBJ_TYPE_VECTOR)
           ? word_true() : word_false();
}

word prim_make_vector(vm_state_t* vm, int nargs) {
    if (nargs != 2) { vm->error_kind = 1; return word_nil(); }
    word kw = vm->sp[0];
    word vw = vm->sp[1];
    if (!is_fixnum(kw)) { vm->error_kind = 1; return word_nil(); }
    int64_t k = word_to_fixnum(kw);
    if (k < 0) { vm->error_kind = 1; return word_nil(); }
    size_t nwords = 3 + (size_t)k;
    word* vec = vm->gc->alloc_words(nwords);
    obj_set_type(vec, OBJ_TYPE_VECTOR);
    vec[DATA_START_INDEX] = (word)k;
    for (int64_t i = 0; i < k; i++)
        vector_set(vec, i, vw);
    return ptr_to_word(vec);
}

word prim_vector(vm_state_t* vm, int nargs) {
    size_t nwords = 3 + (size_t)nargs;
    word* vec = vm->gc->alloc_words(nwords);
    obj_set_type(vec, OBJ_TYPE_VECTOR);
    vec[DATA_START_INDEX] = (word)(int64_t)nargs;
    for (int i = 0; i < nargs; i++)
        vector_set(vec, i, vm->sp[i]);
    return ptr_to_word(vec);
}

word prim_vector_length(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_kind = 1; return word_nil(); }
    word w = vm->sp[0];
    if (!is_ptr(w) || obj_type(ptr_from_word(w)) != OBJ_TYPE_VECTOR) {
        vm->error_kind = 1; return word_nil();
    }
    return word_from_fixnum((int64_t)vector_length(ptr_from_word(w)));
}

word prim_vector_ref(vm_state_t* vm, int nargs) {
    if (nargs != 2) { vm->error_kind = 1; return word_nil(); }
    word vw = vm->sp[0];
    word iw = vm->sp[1];
    if (!is_ptr(vw) || obj_type(ptr_from_word(vw)) != OBJ_TYPE_VECTOR || !is_fixnum(iw)) {
        vm->error_kind = 1; return word_nil();
    }
    int64_t idx = word_to_fixnum(iw);
    word* hdr = ptr_from_word(vw);
    if (idx < 0 || (size_t)idx >= vector_length(hdr)) {
        vm->error_kind = 1; return word_nil();
    }
    return vector_elem(hdr, idx);
}

word prim_vector_set(vm_state_t* vm, int nargs) {
    if (nargs != 3) { vm->error_kind = 1; return word_nil(); }
    word vw = vm->sp[0];
    word iw = vm->sp[1];
    word val = vm->sp[2];
    if (!is_ptr(vw) || obj_type(ptr_from_word(vw)) != OBJ_TYPE_VECTOR || !is_fixnum(iw)) {
        vm->error_kind = 1; return word_nil();
    }
    int64_t idx = word_to_fixnum(iw);
    word* hdr = ptr_from_word(vw);
    if (idx < 0 || (size_t)idx >= vector_length(hdr)) {
        vm->error_kind = 1; return word_nil();
    }
    vector_set(hdr, idx, val);
    return word_nil();
}

word prim_list_to_vector(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_kind = 1; return word_nil(); }
    word lst = vm->sp[0];
    size_t count = 0;
    word cur = lst;
    while (is_ptr(cur) && obj_type(ptr_from_word(cur)) == OBJ_TYPE_PAIR) {
        count++;
        cur = pair_cdr(ptr_from_word(cur));
    }
    word* vec = vm->gc->alloc_words(3 + count);
    obj_set_type(vec, OBJ_TYPE_VECTOR);
    vec[DATA_START_INDEX] = (word)count;
    cur = lst;
    for (size_t i = 0; i < count; i++) {
        word* p = ptr_from_word(cur);
        vector_set(vec, i, pair_car(p));
        cur = pair_cdr(p);
    }
    return ptr_to_word(vec);
}

word prim_vector_to_list(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_kind = 1; return word_nil(); }
    word vw = vm->sp[0];
    if (!is_ptr(vw) || obj_type(ptr_from_word(vw)) != OBJ_TYPE_VECTOR) {
        vm->error_kind = 1; return word_nil();
    }
    word* hdr = ptr_from_word(vw);
    size_t len = vector_length(hdr);
    word result = word_nil();
    for (size_t i = len; i > 0; i--) {
        word* p = vm->gc->alloc_words(4);
        obj_set_type(p, OBJ_TYPE_PAIR);
        pair_car(p) = vector_elem(hdr, i - 1);
        pair_cdr(p) = result;
        result = ptr_to_word(p);
    }
    return result;
}

word prim_vector_copy(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_kind = ERR_ARITY; return word_nil(); }
    word vw = vm->sp[0];
    if (!is_ptr(vw) || obj_type(ptr_from_word(vw)) != OBJ_TYPE_VECTOR)
        { vm->error_kind = ERR_TYPE; return word_nil(); }
    word* hdr = ptr_from_word(vw);
    size_t len = (size_t)hdr[DATA_START_INDEX];
    word* nv = vm->gc->alloc_words(3 + len);
    obj_set_type(nv, OBJ_TYPE_VECTOR);
    nv[DATA_START_INDEX] = (word)len;
    for (size_t i = 0; i < len; i++)
        nv[DATA_START_INDEX + 1 + i] = hdr[DATA_START_INDEX + 1 + i];
    return ptr_to_word(nv);
}

word prim_vector_fill(vm_state_t* vm, int nargs) {
    if (nargs != 2) { vm->error_kind = ERR_ARITY; return word_nil(); }
    word vw = vm->sp[0], val = vm->sp[1];
    if (!is_ptr(vw) || obj_type(ptr_from_word(vw)) != OBJ_TYPE_VECTOR)
        { vm->error_kind = ERR_TYPE; return word_nil(); }
    word* hdr = ptr_from_word(vw);
    size_t len = (size_t)hdr[DATA_START_INDEX];
    for (size_t i = 0; i < len; i++)
        hdr[DATA_START_INDEX + 1 + i] = val;
    return word_nil();
}

word prim_vector_append(vm_state_t* vm, int nargs) {
    if (nargs == 0) {
        word* nv = vm->gc->alloc_words(3); obj_set_type(nv, OBJ_TYPE_VECTOR);
        nv[DATA_START_INDEX] = (word)0; return ptr_to_word(nv);
    }
    size_t total = 0;
    for (int i = 0; i < nargs; i++) {
        word vw = vm->sp[i];
        if (!is_ptr(vw) || obj_type(ptr_from_word(vw)) != OBJ_TYPE_VECTOR)
            { vm->error_kind = ERR_TYPE; return word_nil(); }
        total += (size_t)ptr_from_word(vw)[DATA_START_INDEX];
    }
    word* nv = vm->gc->alloc_words(3 + total);
    obj_set_type(nv, OBJ_TYPE_VECTOR);
    nv[DATA_START_INDEX] = (word)total;
    size_t pos = 0;
    for (int i = 0; i < nargs; i++) {
        word* hdr = ptr_from_word(vm->sp[i]);
        size_t len = (size_t)hdr[DATA_START_INDEX];
        for (size_t j = 0; j < len; j++)
            nv[DATA_START_INDEX + 1 + pos++] = hdr[DATA_START_INDEX + 1 + j];
    }
    return ptr_to_word(nv);
}
