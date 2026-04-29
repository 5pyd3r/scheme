#include "prim.h"
#include "types.h"
#include "compiler.h"
#include <stdio.h>

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
    // For now, write = display (no quoting). Full write needs strings/char escaping.
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
    word sw = vm->sp[2];
    if (!is_ptr(sw) || obj_type(ptr_from_word(sw)) != OBJ_TYPE_STRING)
        { vm->error_kind = ERR_TYPE; return word_nil(); }
    word* hdr = ptr_from_word(sw);
    int len = (int)string_length(hdr);
    int start = (int)word_to_fixnum(vm->sp[1]);
    int end = (int)word_to_fixnum(vm->sp[0]);
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
    word proc = vm->sp[nargs - 1];               // first arg
    word list_arg = vm->sp[0];                    // last arg (the list)
    // Count list elements and push them onto the stack
    word cur = list_arg;
    int list_len = 0;
    word list_elems[1024];
    while (is_ptr(cur) && obj_type(ptr_from_word(cur)) == OBJ_TYPE_PAIR && list_len < 1024) {
        list_elems[list_len++] = pair_car(ptr_from_word(cur));
        cur = pair_cdr(ptr_from_word(cur));
    }
    if (!is_nil(cur)) { vm->error_kind = ERR_TYPE; return word_nil(); }
    // We have: proc, (nargs-2) individual args, and list_len list elements
    // We need to call proc with (nargs-2 + list_len) args
    // The individual args are at sp[1] .. sp[nargs-2]
    // Build a trampoline: push list elems, push individual args, push proc, CALL
    // Save stack position
    word* old_sp = vm->sp - (nargs - 1);  // sp before the apply args
    // Push list elements (in order, last will be top)
    for (int i = 0; i < list_len; i++)
        *++vm->sp = list_elems[i];
    // The individual args are already on the stack at old_sp[1..nargs-2],
    // but we need to move them up past the list elements. Actually, after apply's
    // PRIM_CALL returns, the PRIM_CALL handler pops all nargs and pushes the result.
    // So we can't just manipulate the stack here.
    // Instead: build an expression and use scheme_compile_and_assemble + vm_execute.
    // Build (proc arg1 ... argN list-elem1 ... list-elemN)
    // then compile and execute it.
    vm->sp = old_sp;  // restore sp
    // Build the call list starting from the end
    word call_expr = word_nil();
    // Push list elements in reverse
    for (int i = list_len - 1; i >= 0; i--) {
        word* p = vm->gc->alloc_words(4); obj_set_type(p, OBJ_TYPE_PAIR);
        pair_car(p) = list_elems[i]; pair_cdr(p) = call_expr;
        call_expr = ptr_to_word(p);
    }
    // Push individual args (sp[nargs-2] down to sp[1]) in reverse
    for (int i = nargs - 2; i >= 1; i--) {
        word* p = vm->gc->alloc_words(4); obj_set_type(p, OBJ_TYPE_PAIR);
        pair_car(p) = vm->sp[i]; pair_cdr(p) = call_expr;
        call_expr = ptr_to_word(p);
    }
    // Cons proc onto call_expr
    word* p = vm->gc->alloc_words(4); obj_set_type(p, OBJ_TYPE_PAIR);
    pair_car(p) = proc; pair_cdr(p) = call_expr;
    call_expr = ptr_to_word(p);
    // Save VM state, compile and execute
    word* saved_sp = vm->sp;
    uint8_t* saved_ip = vm->ip;
    word* saved_fp = vm->fp;
    word* saved_env = vm->env;
    word* saved_current = vm->current_code;
    word code_obj = compile_expr(vm, call_expr);
    word result = word_nil();
    if (is_ptr(code_obj)) {
        int ci = vm_load_code(vm, ptr_from_word(code_obj));
        if (ci >= 0) result = vm_execute(vm, ci);
    }
    vm->sp = saved_sp;
    vm->ip = saved_ip;
    vm->fp = saved_fp;
    vm->env = saved_env;
    vm->current_code = saved_current;
    return result;
}
