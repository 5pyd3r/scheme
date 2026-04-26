#include "prim.h"
#include <string.h>

word prim_symbol_to_string(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_kind = 1; return word_nil(); }
    word sym = vm->sp[0];
    if (!is_ptr(sym)) return word_nil();
    word* hdr = ptr_from_word(sym);
    if (obj_type(hdr) != OBJ_TYPE_SYMBOL) return word_nil();
    int len = (int)string_length(hdr);
    size_t str_words = 3 + len;
    word* str = vm->gc->alloc_words(str_words);
    obj_set_type(str, OBJ_TYPE_STRING);
    str[DATA_START_INDEX] = (word)len;
    for (int i = 0; i < len; i++)
        str[DATA_START_INDEX + 1 + i] = string_ref(hdr, i);
    return ptr_to_word(str);
}

word prim_prim_index(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_kind = 1; return word_false(); }
    word sym = vm->sp[0];
    if (!is_ptr(sym)) return word_false();
    word* hdr = ptr_from_word(sym);
    if (obj_type(hdr) != OBJ_TYPE_SYMBOL) return word_false();
    int len = (int)string_length(hdr);
    if (len >= 63) return word_false();
    char name[64];
    for (int i = 0; i < len; i++)
        name[i] = (char)word_to_char(string_ref(hdr, i));
    name[len] = '\0';
    int idx = prim_lookup(name);
    if (idx < 0) return word_false();
    return word_from_fixnum(idx);
}
