#include "prim.h"
#include "types.h"

word prim_stringp(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_kind = 1; return word_nil(); }
    word w = vm->sp[0];
    return (is_ptr(w) && obj_type(ptr_from_word(w)) == OBJ_TYPE_STRING)
           ? word_true() : word_false();
}

word prim_make_string(vm_state_t* vm, int nargs) {
    if (nargs < 1 || nargs > 2) { vm->error_kind = 1; return word_nil(); }
    word kw = vm->sp[0];
    if (!is_fixnum(kw)) { vm->error_kind = 1; return word_nil(); }
    int64_t k = word_to_fixnum(kw);
    if (k < 0) { vm->error_kind = 1; return word_nil(); }
    word fill = (nargs == 2) ? vm->sp[1] : word_from_char(' ');
    if (!is_char(fill)) { vm->error_kind = 1; return word_nil(); }
    size_t nwords = 3 + (size_t)k;
    word* str = vm->gc->alloc_words(nwords);
    obj_set_type(str, OBJ_TYPE_STRING);
    str[DATA_START_INDEX] = (word)k;
    for (int64_t i = 0; i < k; i++)
        string_set(str, i, fill);
    return ptr_to_word(str);
}

word prim_string(vm_state_t* vm, int nargs) {
    size_t nwords = 3 + (size_t)nargs;
    word* str = vm->gc->alloc_words(nwords);
    obj_set_type(str, OBJ_TYPE_STRING);
    str[DATA_START_INDEX] = (word)(int64_t)nargs;
    for (int i = 0; i < nargs; i++) {
        if (!is_char(vm->sp[i])) { vm->error_kind = 1; return word_nil(); }
        string_set(str, i, vm->sp[i]);
    }
    return ptr_to_word(str);
}

word prim_string_length(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_kind = 1; return word_nil(); }
    word w = vm->sp[0];
    if (!is_ptr(w) || obj_type(ptr_from_word(w)) != OBJ_TYPE_STRING) {
        vm->error_kind = 1; return word_nil();
    }
    return word_from_fixnum((int64_t)string_length(ptr_from_word(w)));
}

word prim_string_ref(vm_state_t* vm, int nargs) {
    if (nargs != 2) { vm->error_kind = 1; return word_nil(); }
    word sw = vm->sp[0], iw = vm->sp[1];
    if (!is_ptr(sw) || obj_type(ptr_from_word(sw)) != OBJ_TYPE_STRING || !is_fixnum(iw)) {
        vm->error_kind = 1; return word_nil();
    }
    int64_t idx = word_to_fixnum(iw);
    word* hdr = ptr_from_word(sw);
    if (idx < 0 || (size_t)idx >= string_length(hdr)) {
        vm->error_kind = 1; return word_nil();
    }
    return string_ref(hdr, idx);
}

word prim_string_set(vm_state_t* vm, int nargs) {
    if (nargs != 3) { vm->error_kind = 1; return word_nil(); }
    word sw = vm->sp[0], iw = vm->sp[1], cv = vm->sp[2];
    if (!is_ptr(sw) || obj_type(ptr_from_word(sw)) != OBJ_TYPE_STRING || !is_fixnum(iw) || !is_char(cv)) {
        vm->error_kind = 1; return word_nil();
    }
    int64_t idx = word_to_fixnum(iw);
    word* hdr = ptr_from_word(sw);
    if (idx < 0 || (size_t)idx >= string_length(hdr)) {
        vm->error_kind = 1; return word_nil();
    }
    string_set(hdr, idx, cv);
    return word_nil();
}

word prim_string_eq(vm_state_t* vm, int nargs) {
    if (nargs != 2) { vm->error_kind = 1; return word_nil(); }
    word a = vm->sp[0], b = vm->sp[1];
    if (!is_ptr(a) || !is_ptr(b)) { vm->error_kind = 1; return word_nil(); }
    word* ha = ptr_from_word(a), *hb = ptr_from_word(b);
    if (obj_type(ha) != OBJ_TYPE_STRING || obj_type(hb) != OBJ_TYPE_STRING) {
        vm->error_kind = 1; return word_nil();
    }
    size_t la = string_length(ha), lb = string_length(hb);
    if (la != lb) return word_false();
    for (size_t i = 0; i < la; i++)
        if (string_ref(ha, i) != string_ref(hb, i)) return word_false();
    return word_true();
}

word prim_string_lt(vm_state_t* vm, int nargs) {
    if (nargs != 2) { vm->error_kind = 1; return word_nil(); }
    word a = vm->sp[0], b = vm->sp[1];
    if (!is_ptr(a) || !is_ptr(b)) { vm->error_kind = 1; return word_nil(); }
    word* ha = ptr_from_word(a), *hb = ptr_from_word(b);
    if (obj_type(ha) != OBJ_TYPE_STRING || obj_type(hb) != OBJ_TYPE_STRING) {
        vm->error_kind = 1; return word_nil();
    }
    size_t la = string_length(ha), lb = string_length(hb);
    size_t min = la < lb ? la : lb;
    for (size_t i = 0; i < min; i++) {
        uint32_t ca = word_to_char(string_ref(ha, i));
        uint32_t cb = word_to_char(string_ref(hb, i));
        if (ca != cb) return ca < cb ? word_true() : word_false();
    }
    return la < lb ? word_true() : word_false();
}

word prim_string_to_list(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_kind = 1; return word_nil(); }
    word sw = vm->sp[0];
    if (!is_ptr(sw) || obj_type(ptr_from_word(sw)) != OBJ_TYPE_STRING) {
        vm->error_kind = 1; return word_nil();
    }
    word* hdr = ptr_from_word(sw);
    size_t len = string_length(hdr);
    word result = word_nil();
    for (size_t i = len; i > 0; i--) {
        word* p = vm->gc->alloc_words(4);
        obj_set_type(p, OBJ_TYPE_PAIR);
        pair_car(p) = string_ref(hdr, i - 1);
        pair_cdr(p) = result;
        result = ptr_to_word(p);
    }
    return result;
}

word prim_list_to_string(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_kind = 1; return word_nil(); }
    word lst = vm->sp[0];
    size_t count = 0;
    word cur = lst;
    while (is_ptr(cur) && obj_type(ptr_from_word(cur)) == OBJ_TYPE_PAIR) {
        count++;
        cur = pair_cdr(ptr_from_word(cur));
    }
    word* str = vm->gc->alloc_words(3 + count);
    obj_set_type(str, OBJ_TYPE_STRING);
    str[DATA_START_INDEX] = (word)count;
    cur = lst;
    for (size_t i = 0; i < count; i++) {
        word* p = ptr_from_word(cur);
        if (!is_char(pair_car(p))) { vm->error_kind = 1; return word_nil(); }
        string_set(str, i, pair_car(p));
        cur = pair_cdr(p);
    }
    return ptr_to_word(str);
}

word prim_string_append(vm_state_t* vm, int nargs) {
    if (nargs == 0) {
        word* s = vm->gc->alloc_words(3);
        obj_set_type(s, OBJ_TYPE_STRING);
        s[DATA_START_INDEX] = (word)0;
        return ptr_to_word(s);
    }
    size_t total = 0;
    for (int i = 0; i < nargs; i++) {
        word sw = vm->sp[i];
        if (!is_ptr(sw) || obj_type(ptr_from_word(sw)) != OBJ_TYPE_STRING)
            { vm->error_kind = 1; return word_nil(); }
        total += string_length(ptr_from_word(sw));
    }
    size_t words = 3 + total;
    word* result = vm->gc->alloc_words(words);
    obj_set_type(result, OBJ_TYPE_STRING);
    result[DATA_START_INDEX] = (word)total;
    size_t pos = 0;
    for (int i = 0; i < nargs; i++) {
        word* hdr = ptr_from_word(vm->sp[i]);
        size_t len = string_length(hdr);
        for (size_t j = 0; j < len; j++)
            string_set(result, pos++, string_ref(hdr, j));
    }
    return ptr_to_word(result);
}

word prim_string_gt(vm_state_t* vm, int nargs) {
    if (nargs < 2) { vm->error_kind = ERR_ARITY; return word_nil(); }
    for (int i = 1; i < nargs; i++) {
        word* ha = ptr_from_word(vm->sp[i-1]), *hb = ptr_from_word(vm->sp[i]);
        size_t la = string_length(ha), lb = string_length(hb);
        size_t n = la < lb ? la : lb;
        int cmp = 0;
        for (size_t j = 0; j < n; j++) {
            uint32_t ca = word_to_char(string_ref(ha, j));
            uint32_t cb = word_to_char(string_ref(hb, j));
            if (ca != cb) { cmp = ca < cb ? -1 : 1; break; }
        }
        if (cmp == 0) cmp = (la < lb) ? -1 : (la > lb) ? 1 : 0;
        if (cmp <= 0) return word_false();
    }
    return word_true();
}

word prim_string_le(vm_state_t* vm, int nargs) {
    if (nargs < 2) { vm->error_kind = ERR_ARITY; return word_nil(); }
    for (int i = 1; i < nargs; i++) {
        word* ha = ptr_from_word(vm->sp[i-1]), *hb = ptr_from_word(vm->sp[i]);
        size_t la = string_length(ha), lb = string_length(hb);
        size_t n = la < lb ? la : lb;
        int cmp = 0;
        for (size_t j = 0; j < n; j++) {
            uint32_t ca = word_to_char(string_ref(ha, j));
            uint32_t cb = word_to_char(string_ref(hb, j));
            if (ca != cb) { cmp = ca < cb ? -1 : 1; break; }
        }
        if (cmp == 0) cmp = (la < lb) ? -1 : (la > lb) ? 1 : 0;
        if (cmp > 0) return word_false();
    }
    return word_true();
}

word prim_string_ge(vm_state_t* vm, int nargs) {
    if (nargs < 2) { vm->error_kind = ERR_ARITY; return word_nil(); }
    for (int i = 1; i < nargs; i++) {
        word* ha = ptr_from_word(vm->sp[i-1]), *hb = ptr_from_word(vm->sp[i]);
        size_t la = string_length(ha), lb = string_length(hb);
        size_t n = la < lb ? la : lb;
        int cmp = 0;
        for (size_t j = 0; j < n; j++) {
            uint32_t ca = word_to_char(string_ref(ha, j));
            uint32_t cb = word_to_char(string_ref(hb, j));
            if (ca != cb) { cmp = ca < cb ? -1 : 1; break; }
        }
        if (cmp == 0) cmp = (la < lb) ? -1 : (la > lb) ? 1 : 0;
        if (cmp < 0) return word_false();
    }
    return word_true();
}

word prim_string_fill(vm_state_t* vm, int nargs) {
    if (nargs != 2) { vm->error_kind = ERR_ARITY; return word_nil(); }
    word sw = vm->sp[1], cv = vm->sp[0];
    if (!is_ptr(sw) || obj_type(ptr_from_word(sw)) != OBJ_TYPE_STRING || !is_char(cv))
        { vm->error_kind = ERR_TYPE; return word_nil(); }
    word* hdr = ptr_from_word(sw);
    size_t len = string_length(hdr);
    for (size_t i = 0; i < len; i++) string_set(hdr, i, cv);
    return word_nil();
}
