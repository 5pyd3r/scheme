#include "prim.h"
#include "types.h"
#include <stdio.h>

extern word prim_eval(vm_state_t* vm, int nargs);

word prim_not(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_kind = ERR_ARITY; return word_false(); }
    return is_false(vm->sp[0]) ? word_true() : word_false();
}

word prim_booleanp(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_kind = ERR_ARITY; return word_false(); }
    word w = vm->sp[0];
    return (w == word_true() || w == word_false()) ? word_true() : word_false();
}

word prim_procedurep(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_kind = ERR_ARITY; return word_false(); }
    word w = vm->sp[0];
    if (!is_ptr(w)) return word_false();
    return (obj_type(ptr_from_word(w)) == OBJ_TYPE_CODE) ? word_true() : word_false();
}

word prim_error(vm_state_t* vm, int nargs) {
    vm->error_kind = ERR_INTERNAL;
    vm->error_msg = "error";
    return word_nil();
}

word prim_write(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_kind = ERR_ARITY; return word_nil(); }
    word w = vm->sp[0];
    if (is_fixnum(w)) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%lld", (long long)word_to_fixnum(w));
        fputs(buf, stdout);
    } else if (w == word_true()) {
        fputs("#t", stdout);
    } else if (w == word_false()) {
        fputs("#f", stdout);
    } else if (is_nil(w)) {
        fputs("()", stdout);
    } else if (is_ptr(w) && obj_type(ptr_from_word(w)) == OBJ_TYPE_SYMBOL) {
        word* hdr = ptr_from_word(w);
        int len = (int)string_length(hdr);
        for (int i = 0; i < len; i++)
            putchar((char)word_to_char(string_ref(hdr, i)));
    } else if (is_ptr(w) && obj_type(ptr_from_word(w)) == OBJ_TYPE_STRING) {
        fputs("\"", stdout);
        word* hdr = ptr_from_word(w);
        int len = (int)string_length(hdr);
        for (int i = 0; i < len; i++) {
            char c = (char)word_to_char(string_ref(hdr, i));
            if (c == '"' || c == '\\') putchar('\\');
            putchar(c);
        }
        fputs("\"", stdout);
    } else {
        prim_display(vm, nargs);
    }
    return word_nil();
}

word prim_substring(vm_state_t* vm, int nargs) {
    if (nargs != 3) { vm->error_kind = ERR_ARITY; return word_nil(); }
    word sw = vm->sp[0];
    if (!is_ptr(sw) || obj_type(ptr_from_word(sw)) != OBJ_TYPE_STRING)
        { vm->error_kind = ERR_TYPE; return word_nil(); }
    word* hdr = ptr_from_word(sw);
    int len = (int)string_length(hdr);
    int start = (int)word_to_fixnum(vm->sp[1]);
    int end = (int)word_to_fixnum(vm->sp[2]);
    if (start < 0 || end > len || start > end)
        { vm->error_kind = ERR_TYPE; return word_nil(); }
    int newlen = end - start;
    word* ns = vm->gc->alloc_words(3 + newlen);
    obj_set_type(ns, OBJ_TYPE_STRING);
    ns[DATA_START_INDEX] = (word)newlen;
    for (int i = 0; i < newlen; i++)
        string_set(ns, i, string_ref(hdr, start + i));
    return ptr_to_word(ns);
}

word prim_string_copy(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_kind = ERR_ARITY; return word_nil(); }
    word sw = vm->sp[0];
    if (!is_ptr(sw) || obj_type(ptr_from_word(sw)) != OBJ_TYPE_STRING)
        { vm->error_kind = ERR_TYPE; return word_nil(); }
    word* hdr = ptr_from_word(sw);
    int len = (int)string_length(hdr);
    word* ns = vm->gc->alloc_words(3 + len);
    obj_set_type(ns, OBJ_TYPE_STRING);
    ns[DATA_START_INDEX] = (word)len;
    for (int i = 0; i < len; i++)
        string_set(ns, i, string_ref(hdr, i));
    return ptr_to_word(ns);
}

/* apply: (apply proc arg1 ... argN rest-list) */
word prim_apply(vm_state_t* vm, int nargs) {
    if (nargs < 2) { vm->error_kind = ERR_ARITY; return word_nil(); }
    word proc = vm->sp[0];
    word list_arg = vm->sp[nargs - 1];
    // Collect list_arg elements in a temp array (forward order)
    word list_elems[1024]; int nlist = 0;
    word cur2 = list_arg;
    while (is_ptr(cur2) && obj_type(ptr_from_word(cur2)) == OBJ_TYPE_PAIR && nlist < 1024) {
        list_elems[nlist++] = pair_car(ptr_from_word(cur2));
        cur2 = pair_cdr(ptr_from_word(cur2));
    }
    if (!is_nil(cur2)) { vm->error_kind = ERR_TYPE; return word_nil(); }
    // Build call form: (proc arg1 ... argN list-elem1 ... list-elemN)
    word call_expr = word_nil();
    // Add list elements in reverse (last first → first at front)
    for (int i = nlist - 1; i >= 0; i--) {
        word* p = vm->gc->alloc_words(4); obj_set_type(p, OBJ_TYPE_PAIR);
        pair_car(p) = list_elems[i]; pair_cdr(p) = call_expr;
        call_expr = ptr_to_word(p);
    }
    // Add individual args in reverse (sp[nargs-2] down to sp[1])
    for (int i = nargs - 2; i >= 1; i--) {
        word* p = vm->gc->alloc_words(4); obj_set_type(p, OBJ_TYPE_PAIR);
        pair_car(p) = vm->sp[i]; pair_cdr(p) = call_expr;
        call_expr = ptr_to_word(p);
    }
    // Cons proc
    { word* p = vm->gc->alloc_words(4); obj_set_type(p, OBJ_TYPE_PAIR);
      pair_car(p) = proc; pair_cdr(p) = call_expr;
      call_expr = ptr_to_word(p); }
    // Return call_expr for now — apply needs deeper VM integration
    // (prim_eval nesting within PRIM_CALL stack handling needs rework)
    (void)call_expr;
    vm->error_kind = ERR_INTERNAL;
    vm->error_msg = "apply: not yet implemented";
    return word_nil();
}

word prim_symbol_eq(vm_state_t* vm, int nargs) {
    if (nargs < 2) { vm->error_kind = ERR_ARITY; return word_false(); }
    for (int i = 1; i < nargs; i++)
        if (vm->sp[i-1] != vm->sp[i]) return word_false();
    return word_true();
}

word prim_boolean_eq(vm_state_t* vm, int nargs) {
    if (nargs < 2) { vm->error_kind = ERR_ARITY; return word_false(); }
    for (int i = 1; i < nargs; i++)
        if (vm->sp[i-1] != vm->sp[i]) return word_false();
    return word_true();
}

word prim_eof_objectp(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_kind = ERR_ARITY; return word_false(); }
    return is_eof(vm->sp[0]) ? word_true() : word_false();
}

word prim_read_char(vm_state_t* vm, int nargs) {
    int c = getchar();
    if (c == EOF) return word_eof();
    return word_from_char((unsigned char)c);
}

word prim_write_char(vm_state_t* vm, int nargs) {
    if (nargs != 1 || !is_char(vm->sp[0])) { vm->error_kind = ERR_ARITY; return word_nil(); }
    putchar((char)word_to_char(vm->sp[0]));
    return word_nil();
}

word prim_peek_char(vm_state_t* vm, int nargs) {
    int c = getchar();
    if (c == EOF) return word_eof();
    ungetc(c, stdin);
    return word_from_char((unsigned char)c);
}
