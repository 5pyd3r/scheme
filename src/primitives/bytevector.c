#include "prim.h"
#include "types.h"
#include <string.h>

word prim_bytevectorp(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_kind = 1; return word_nil(); }
    word w = vm->sp[0];
    return (is_ptr(w) && obj_type(ptr_from_word(w)) == OBJ_TYPE_BYTEVECTOR)
           ? word_true() : word_false();
}

word prim_make_bytevector(vm_state_t* vm, int nargs) {
    if (nargs < 1 || nargs > 2) { vm->error_kind = 1; return word_nil(); }
    word kw = vm->sp[0];
    if (!is_fixnum(kw)) { vm->error_kind = 1; return word_nil(); }
    int64_t k = word_to_fixnum(kw);
    if (k < 0) { vm->error_kind = 1; return word_nil(); }
    uint8_t fill = (nargs == 2) ? (uint8_t)(word_to_fixnum(vm->sp[1]) & 0xFF) : 0;
    size_t data_slots = ((size_t)k + sizeof(word) - 1) / sizeof(word);
    size_t nwords = 3 + data_slots;
    word* bv = vm->gc->alloc_words(nwords);
    obj_set_type(bv, OBJ_TYPE_BYTEVECTOR);
    bv[DATA_START_INDEX] = (word)k;
    memset(bytevector_data(bv), fill, (size_t)k);
    return ptr_to_word(bv);
}

word prim_bytevector(vm_state_t* vm, int nargs) {
    size_t data_slots = ((size_t)nargs + sizeof(word) - 1) / sizeof(word);
    size_t nwords = 3 + data_slots;
    word* bv = vm->gc->alloc_words(nwords);
    obj_set_type(bv, OBJ_TYPE_BYTEVECTOR);
    bv[DATA_START_INDEX] = (word)(int64_t)nargs;
    uint8_t* data = bytevector_data(bv);
    for (int i = 0; i < nargs; i++)
        data[i] = (uint8_t)(word_to_fixnum(vm->sp[i]) & 0xFF);
    return ptr_to_word(bv);
}

word prim_bytevector_length(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_kind = 1; return word_nil(); }
    word w = vm->sp[0];
    if (!is_ptr(w) || obj_type(ptr_from_word(w)) != OBJ_TYPE_BYTEVECTOR) {
        vm->error_kind = 1; return word_nil();
    }
    return word_from_fixnum((int64_t)bytevector_length(ptr_from_word(w)));
}

word prim_bytevector_u8_ref(vm_state_t* vm, int nargs) {
    if (nargs != 2) { vm->error_kind = 1; return word_nil(); }
    word bvw = vm->sp[0], iw = vm->sp[1];
    if (!is_ptr(bvw) || obj_type(ptr_from_word(bvw)) != OBJ_TYPE_BYTEVECTOR || !is_fixnum(iw)) {
        vm->error_kind = 1; return word_nil();
    }
    int64_t idx = word_to_fixnum(iw);
    word* hdr = ptr_from_word(bvw);
    if (idx < 0 || (size_t)idx >= bytevector_length(hdr)) {
        vm->error_kind = 1; return word_nil();
    }
    return word_from_fixnum((int64_t)bytevector_data(hdr)[idx]);
}

word prim_bytevector_u8_set(vm_state_t* vm, int nargs) {
    if (nargs != 3) { vm->error_kind = 1; return word_nil(); }
    word bvw = vm->sp[0], iw = vm->sp[1], valw = vm->sp[2];
    if (!is_ptr(bvw) || obj_type(ptr_from_word(bvw)) != OBJ_TYPE_BYTEVECTOR || !is_fixnum(iw) || !is_fixnum(valw)) {
        vm->error_kind = 1; return word_nil();
    }
    int64_t idx = word_to_fixnum(iw);
    word* hdr = ptr_from_word(bvw);
    if (idx < 0 || (size_t)idx >= bytevector_length(hdr)) {
        vm->error_kind = 1; return word_nil();
    }
    bytevector_data(hdr)[idx] = (uint8_t)(word_to_fixnum(valw) & 0xFF);
    return word_nil();
}

word prim_bytevector_to_u8_list(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_kind = 1; return word_nil(); }
    word bvw = vm->sp[0];
    if (!is_ptr(bvw) || obj_type(ptr_from_word(bvw)) != OBJ_TYPE_BYTEVECTOR) {
        vm->error_kind = 1; return word_nil();
    }
    word* hdr = ptr_from_word(bvw);
    size_t len = bytevector_length(hdr);
    uint8_t* data = bytevector_data(hdr);
    word result = word_nil();
    for (size_t i = len; i > 0; i--) {
        word* p = vm->gc->alloc_words(4);
        obj_set_type(p, OBJ_TYPE_PAIR);
        pair_car(p) = word_from_fixnum((int64_t)data[i - 1]);
        pair_cdr(p) = result;
        result = ptr_to_word(p);
    }
    return result;
}

word prim_u8_list_to_bytevector(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_kind = 1; return word_nil(); }
    word lst = vm->sp[0];
    size_t count = 0;
    word cur = lst;
    while (is_ptr(cur) && obj_type(ptr_from_word(cur)) == OBJ_TYPE_PAIR) {
        count++;
        cur = pair_cdr(ptr_from_word(cur));
    }
    size_t data_slots = (count + sizeof(word) - 1) / sizeof(word);
    word* bv = vm->gc->alloc_words(3 + data_slots);
    obj_set_type(bv, OBJ_TYPE_BYTEVECTOR);
    bv[DATA_START_INDEX] = (word)count;
    uint8_t* data = bytevector_data(bv);
    cur = lst;
    for (size_t i = 0; i < count; i++) {
        word* p = ptr_from_word(cur);
        data[i] = (uint8_t)(word_to_fixnum(pair_car(p)) & 0xFF);
        cur = pair_cdr(p);
    }
    return ptr_to_word(bv);
}

word prim_bytevector_copy(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_kind = ERR_ARITY; return word_nil(); }
    word bw = vm->sp[0];
    if (!is_ptr(bw) || obj_type(ptr_from_word(bw)) != OBJ_TYPE_BYTEVECTOR)
        { vm->error_kind = ERR_TYPE; return word_nil(); }
    word* hdr = ptr_from_word(bw);
    size_t len = (size_t)hdr[DATA_START_INDEX];
    size_t nwords = 3 + (len + sizeof(word) - 1) / sizeof(word);
    word* nv = vm->gc->alloc_words(nwords);
    obj_set_type(nv, OBJ_TYPE_BYTEVECTOR);
    nv[DATA_START_INDEX] = (word)len;
    memcpy(bytevector_data(nv), bytevector_data(hdr), len);
    return ptr_to_word(nv);
}
