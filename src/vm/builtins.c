#include "prim.h"
#include "opcodes.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

word prim_cons(vm_state_t* vm, int nargs);
word prim_car(vm_state_t* vm, int nargs);
word prim_cdr(vm_state_t* vm, int nargs);
word prim_set_car(vm_state_t* vm, int nargs);
word prim_set_cdr(vm_state_t* vm, int nargs);
word prim_null(vm_state_t* vm, int nargs);
word prim_pair(vm_state_t* vm, int nargs);
word prim_eq(vm_state_t* vm, int nargs);
word prim_eqv(vm_state_t* vm, int nargs);
word prim_add(vm_state_t* vm, int nargs);
word prim_sub(vm_state_t* vm, int nargs);
word prim_mul(vm_state_t* vm, int nargs);
word prim_div(vm_state_t* vm, int nargs);
word prim_lt(vm_state_t* vm, int nargs);
word prim_gt(vm_state_t* vm, int nargs);
word prim_le(vm_state_t* vm, int nargs);
word prim_ge(vm_state_t* vm, int nargs);
word prim_eq_num(vm_state_t* vm, int nargs);
word prim_display(vm_state_t* vm, int nargs);
word prim_newline(vm_state_t* vm, int nargs);
word prim_symbol_to_string(vm_state_t* vm, int nargs);
word prim_prim_index(vm_state_t* vm, int nargs);
word prim_assemble_code(vm_state_t* vm, int nargs);
word prim_read(vm_state_t* vm, int nargs);
word prim_fixnum_pred(vm_state_t* vm, int nargs);
word prim_symbol_pred(vm_state_t* vm, int nargs);
word prim_number_pred(vm_state_t* vm, int nargs);
word prim_integer_pred(vm_state_t* vm, int nargs);
word prim_exact_pred(vm_state_t* vm, int nargs);
word prim_inexact_pred(vm_state_t* vm, int nargs);
word prim_zerop(vm_state_t* vm, int nargs);
word prim_positivep(vm_state_t* vm, int nargs);
word prim_negativep(vm_state_t* vm, int nargs);
word prim_evenp(vm_state_t* vm, int nargs);
word prim_oddp(vm_state_t* vm, int nargs);
word prim_quotient(vm_state_t* vm, int nargs);
word prim_remainder(vm_state_t* vm, int nargs);
word prim_modulo(vm_state_t* vm, int nargs);
word prim_gcd(vm_state_t* vm, int nargs);
word prim_lcm(vm_state_t* vm, int nargs);
word prim_abs(vm_state_t* vm, int nargs);
word prim_max(vm_state_t* vm, int nargs);
word prim_min(vm_state_t* vm, int nargs);
word prim_floor(vm_state_t* vm, int nargs);
word prim_ceiling(vm_state_t* vm, int nargs);
word prim_truncate(vm_state_t* vm, int nargs);
word prim_round(vm_state_t* vm, int nargs);
word prim_number_to_string(vm_state_t* vm, int nargs);
word prim_string_to_number(vm_state_t* vm, int nargs);
word prim_exact_to_inexact(vm_state_t* vm, int nargs);
word prim_inexact_to_exact(vm_state_t* vm, int nargs);
word prim_finitep(vm_state_t* vm, int nargs);
word prim_infinitep(vm_state_t* vm, int nargs);
word prim_nanp(vm_state_t* vm, int nargs);
word prim_sin(vm_state_t* vm, int nargs);
word prim_cos(vm_state_t* vm, int nargs);
word prim_tan(vm_state_t* vm, int nargs);
word prim_asin(vm_state_t* vm, int nargs);
word prim_acos(vm_state_t* vm, int nargs);
word prim_atan(vm_state_t* vm, int nargs);
word prim_sqrt(vm_state_t* vm, int nargs);
word prim_exp(vm_state_t* vm, int nargs);
word prim_log(vm_state_t* vm, int nargs);
word prim_find_global_slot(vm_state_t* vm, int nargs);
word prim_create_global_slot(vm_state_t* vm, int nargs);

word prim_list(vm_state_t* vm, int nargs);
word prim_vectorp(vm_state_t* vm, int nargs);
word prim_make_vector(vm_state_t* vm, int nargs);
word prim_vector(vm_state_t* vm, int nargs);
word prim_vector_length(vm_state_t* vm, int nargs);
word prim_vector_ref(vm_state_t* vm, int nargs);
word prim_vector_set(vm_state_t* vm, int nargs);
word prim_list_to_vector(vm_state_t* vm, int nargs);
word prim_vector_to_list(vm_state_t* vm, int nargs);
word prim_equal(vm_state_t* vm, int nargs);
word prim_charp(vm_state_t* vm, int nargs);
word prim_char_to_integer(vm_state_t* vm, int nargs);
word prim_integer_to_char(vm_state_t* vm, int nargs);
word prim_char_eq(vm_state_t* vm, int nargs);
word prim_char_lt(vm_state_t* vm, int nargs);
word prim_char_gt(vm_state_t* vm, int nargs);
word prim_char_le(vm_state_t* vm, int nargs);
word prim_char_ge(vm_state_t* vm, int nargs);
word prim_stringp(vm_state_t* vm, int nargs);
word prim_make_string(vm_state_t* vm, int nargs);
word prim_string(vm_state_t* vm, int nargs);
word prim_string_length(vm_state_t* vm, int nargs);
word prim_string_ref(vm_state_t* vm, int nargs);
word prim_string_set(vm_state_t* vm, int nargs);
word prim_string_eq(vm_state_t* vm, int nargs);
word prim_string_lt(vm_state_t* vm, int nargs);
word prim_string_to_list(vm_state_t* vm, int nargs);
word prim_list_to_string(vm_state_t* vm, int nargs);

word prim_bytevectorp(vm_state_t* vm, int nargs);
word prim_make_bytevector(vm_state_t* vm, int nargs);
word prim_bytevector(vm_state_t* vm, int nargs);
word prim_bytevector_length(vm_state_t* vm, int nargs);
word prim_bytevector_u8_ref(vm_state_t* vm, int nargs);
word prim_bytevector_u8_set(vm_state_t* vm, int nargs);
word prim_bytevector_to_u8_list(vm_state_t* vm, int nargs);
word prim_u8_list_to_bytevector(vm_state_t* vm, int nargs);
word prim_gensym(vm_state_t* vm, int nargs);
word prim_eval(vm_state_t* vm, int nargs);
word prim_string_to_symbol(vm_state_t* vm, int nargs);
word prim_string_append(vm_state_t* vm, int nargs);
word prim_not(vm_state_t* vm, int nargs);
word prim_booleanp(vm_state_t* vm, int nargs);
word prim_procedurep(vm_state_t* vm, int nargs);
word prim_error(vm_state_t* vm, int nargs);
word prim_write(vm_state_t* vm, int nargs);
word prim_substring(vm_state_t* vm, int nargs);
word prim_string_copy(vm_state_t* vm, int nargs);
word prim_apply(vm_state_t* vm, int nargs);
word prim_length(vm_state_t* vm, int nargs);
word prim_listp(vm_state_t* vm, int nargs);
word prim_list_ref(vm_state_t* vm, int nargs);
word prim_list_tail(vm_state_t* vm, int nargs);
word prim_reverse(vm_state_t* vm, int nargs);
word prim_append(vm_state_t* vm, int nargs);
word prim_memq(vm_state_t* vm, int nargs);
word prim_member(vm_state_t* vm, int nargs);
word prim_assq(vm_state_t* vm, int nargs);
word prim_make_list(vm_state_t* vm, int nargs);
word prim_list_set(vm_state_t* vm, int nargs);
word prim_list_copy(vm_state_t* vm, int nargs);
word prim_memv(vm_state_t* vm, int nargs);
word prim_assv(vm_state_t* vm, int nargs);
word prim_assoc(vm_state_t* vm, int nargs);
word prim_expt(vm_state_t* vm, int nargs);
word prim_string_gt(vm_state_t* vm, int nargs);
word prim_string_le(vm_state_t* vm, int nargs);
word prim_string_ge(vm_state_t* vm, int nargs);
word prim_string_fill(vm_state_t* vm, int nargs);
word prim_vector_copy(vm_state_t* vm, int nargs);
word prim_vector_fill(vm_state_t* vm, int nargs);
word prim_vector_append(vm_state_t* vm, int nargs);
word prim_bytevector_copy(vm_state_t* vm, int nargs);
word prim_symbol_eq(vm_state_t* vm, int nargs);
word prim_boolean_eq(vm_state_t* vm, int nargs);
word prim_eof_objectp(vm_state_t* vm, int nargs);
word prim_read_char(vm_state_t* vm, int nargs);
word prim_write_char(vm_state_t* vm, int nargs);
word prim_peek_char(vm_state_t* vm, int nargs);
word prim_char_upcase(vm_state_t* vm, int nargs);
word prim_char_downcase(vm_state_t* vm, int nargs);
word prim_digit_value(vm_state_t* vm, int nargs);

// caar..cddddr
word prim_caar(vm_state_t*,int); word prim_cadr(vm_state_t*,int);
word prim_cdar(vm_state_t*,int); word prim_cddr(vm_state_t*,int);
word prim_caaar(vm_state_t*,int); word prim_caadr(vm_state_t*,int);
word prim_cadar(vm_state_t*,int); word prim_caddr(vm_state_t*,int);
word prim_cdaar(vm_state_t*,int); word prim_cdadr(vm_state_t*,int);
word prim_cddar(vm_state_t*,int); word prim_cdddr(vm_state_t*,int);
word prim_caaaar(vm_state_t*,int); word prim_caaadr(vm_state_t*,int);
word prim_caadar(vm_state_t*,int); word prim_caaddr(vm_state_t*,int);
word prim_cadaar(vm_state_t*,int); word prim_cadadr(vm_state_t*,int);
word prim_caddar(vm_state_t*,int); word prim_cadddr(vm_state_t*,int);
word prim_cdaaar(vm_state_t*,int); word prim_cdaadr(vm_state_t*,int);
word prim_cdadar(vm_state_t*,int); word prim_cdaddr(vm_state_t*,int);
word prim_cddaar(vm_state_t*,int); word prim_cddadr(vm_state_t*,int);
word prim_cdddar(vm_state_t*,int); word prim_cddddr(vm_state_t*,int);

word prim_assemble_code(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_kind = 1; return word_from_fixnum(-1); }
    word pair = vm->sp[0];
    if (!is_ptr(pair) || obj_type(ptr_from_word(pair)) != OBJ_TYPE_PAIR)
        return word_from_fixnum(-1);

    word* hdr = ptr_from_word(pair);
    word bytecode_list = pair_car(hdr);
    word const_list = pair_cdr(hdr);

    // Count and collect bytecodes
    uint8_t bytes[4096];
    int len = 0;
    word cur = bytecode_list;
    while (is_ptr(cur) && obj_type(ptr_from_word(cur)) == OBJ_TYPE_PAIR && len < 4096) {
        word val = pair_car(ptr_from_word(cur));
        if (is_fixnum(val))
            bytes[len++] = (uint8_t)(word_to_fixnum(val) & 0xFF);
        cur = pair_cdr(ptr_from_word(cur));
    }

    // Count and collect consts
    word consts[256];
    int nconsts = 0;
    cur = const_list;
    while (is_ptr(cur) && obj_type(ptr_from_word(cur)) == OBJ_TYPE_PAIR && nconsts < 256) {
        consts[nconsts++] = pair_car(ptr_from_word(cur));
        cur = pair_cdr(ptr_from_word(cur));
    }

    // Build code object
    size_t bc_words = ((size_t)len + sizeof(word) - 1) / sizeof(word);
    size_t total_words = 3 + bc_words + nconsts;
    word* code_obj = vm->gc->alloc_words(total_words);
    obj_set_type(code_obj, OBJ_TYPE_CODE);
    code_obj[2] = (word)len;
    memcpy(code_obj + 3, bytes, (size_t)len);
    for (int i = 0; i < nconsts; i++)
        code_obj[3 + bc_words + i] = consts[i];

    int code_idx = vm_load_code(vm, code_obj);
    return word_from_fixnum(code_idx);
}

word prim_fixnum_pred(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_kind = 1; return word_nil(); }
    return is_fixnum(vm->sp[0]) ? word_true() : word_false();
}

word prim_symbol_pred(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_kind = 1; return word_nil(); }
    word w = vm->sp[0];
    return (is_ptr(w) && obj_type(ptr_from_word(w)) == OBJ_TYPE_SYMBOL) ? word_true() : word_false();
}

word prim_find_global_slot(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_kind = 1; return word_from_fixnum(-1); }
    word sym = vm->sp[0];
    int slot = vm_find_global_slot(vm, sym);
    if (slot < 0) {
        // Not found — create new slot
        if (vm->next_global_slot >= (int)vm->global_count) {
            size_t new_count = vm->global_count * 2;
            vm->globals = realloc(vm->globals, new_count * sizeof(word));
            vm->global_names = realloc(vm->global_names, new_count * sizeof(word));
            vm->global_count = new_count;
        }
        slot = vm->next_global_slot++;
        vm->global_names[slot] = sym;
    }
    return word_from_fixnum(slot);
}

word prim_create_global_slot(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_kind = 1; return word_from_fixnum(-1); }
    word sym = vm->sp[0];
    if (vm->next_global_slot >= (int)vm->global_count) {
        size_t new_count = vm->global_count * 2;
        vm->globals = realloc(vm->globals, new_count * sizeof(word));
        vm->global_names = realloc(vm->global_names, new_count * sizeof(word));
        vm->global_count = new_count;
    }
    int slot = vm->next_global_slot++;
    vm->global_names[slot] = sym;
    return word_from_fixnum(slot);
}

typedef struct {
    const char* name;
    prim_fn_t   fn;
} prim_entry_t;

static prim_entry_t prim_table[] = {
    {"cons",     prim_cons},
    {"car",      prim_car},
    {"cdr",      prim_cdr},
    {"set-car!", prim_set_car},
    {"set-cdr!", prim_set_cdr},
    {"null?",    prim_null},
    {"pair?",    prim_pair},
    {"eq?",      prim_eq},
    {"eqv?",     prim_eqv},
    {"+",        prim_add},
    {"-",        prim_sub},
    {"*",        prim_mul},
    {"/",        prim_div},
    {"<",        prim_lt},
    {">",        prim_gt},
    {"<=",       prim_le},
    {">=",       prim_ge},
    {"=",        prim_eq_num},
    {"display",  prim_display},
    {"newline",  prim_newline},
    {"symbol->string", prim_symbol_to_string},
    {"string->symbol", prim_string_to_symbol},
    {"prim-index",     prim_prim_index},
    {"assemble-code",  prim_assemble_code},
    {"read",           prim_read},
    {"fixnum?",        prim_fixnum_pred},
    {"symbol?",        prim_symbol_pred},
    {"number?",        prim_number_pred},
    {"integer?",       prim_integer_pred},
    {"exact?",         prim_exact_pred},
    {"inexact?",       prim_inexact_pred},
    {"zero?",          prim_zerop},
    {"positive?",      prim_positivep},
    {"negative?",      prim_negativep},
    {"even?",          prim_evenp},
    {"odd?",           prim_oddp},
    {"quotient",       prim_quotient},
    {"remainder",      prim_remainder},
    {"modulo",         prim_modulo},
    {"gcd",            prim_gcd},
    {"lcm",            prim_lcm},
    {"abs",            prim_abs},
    {"max",            prim_max},
    {"min",            prim_min},
    {"floor",          prim_floor},
    {"ceiling",        prim_ceiling},
    {"truncate",       prim_truncate},
    {"round",          prim_round},
    {"number->string", prim_number_to_string},
    {"string->number", prim_string_to_number},
    {"exact->inexact", prim_exact_to_inexact},
    {"inexact->exact", prim_inexact_to_exact},
    {"finite?",        prim_finitep},
    {"infinite?",      prim_infinitep},
    {"nan?",           prim_nanp},
    {"sin",            prim_sin},
    {"cos",            prim_cos},
    {"tan",            prim_tan},
    {"asin",           prim_asin},
    {"acos",           prim_acos},
    {"atan",           prim_atan},
    {"sqrt",           prim_sqrt},
    {"exp",            prim_exp},
    {"log",            prim_log},
    {"find-global-slot",   prim_find_global_slot},
    {"create-global-slot", prim_create_global_slot},
    {"list",             prim_list},
    {"vector?",          prim_vectorp},
    {"make-vector",      prim_make_vector},
    {"vector",           prim_vector},
    {"vector-length",    prim_vector_length},
    {"vector-ref",       prim_vector_ref},
    {"vector-set!",      prim_vector_set},
    {"list->vector",     prim_list_to_vector},
    {"vector->list",     prim_vector_to_list},
    {"equal?",           prim_equal},
    {"char?",             prim_charp},
    {"char->integer",     prim_char_to_integer},
    {"integer->char",     prim_integer_to_char},
    {"char=?",            prim_char_eq},
    {"char<?",            prim_char_lt},
    {"char>?",            prim_char_gt},
    {"char<=?",           prim_char_le},
    {"char>=?",           prim_char_ge},
    {"string?",           prim_stringp},
    {"make-string",       prim_make_string},
    {"string",            prim_string},
    {"string-length",     prim_string_length},
    {"string-ref",        prim_string_ref},
    {"string-set!",       prim_string_set},
    {"string=?",          prim_string_eq},
    {"string<?",          prim_string_lt},
    {"string->list",      prim_string_to_list},
    {"list->string",      prim_list_to_string},
    {"string-append",     prim_string_append},
    {"bytevector?",           prim_bytevectorp},
    {"make-bytevector",       prim_make_bytevector},
    {"bytevector",            prim_bytevector},
    {"bytevector-length",     prim_bytevector_length},
    {"bytevector-u8-ref",     prim_bytevector_u8_ref},
    {"bytevector-u8-set!",    prim_bytevector_u8_set},
    {"bytevector->u8-list",   prim_bytevector_to_u8_list},
    {"u8-list->bytevector",   prim_u8_list_to_bytevector},
    {"gensym",           prim_gensym},
    {"eval",             prim_eval},
    {"not",              prim_not},
    {"boolean?",         prim_booleanp},
    {"procedure?",       prim_procedurep},
    {"error",            prim_error},
    {"write",            prim_write},
    {"substring",        prim_substring},
    {"string-copy",      prim_string_copy},
    {"apply",            prim_apply},
    {"length",           prim_length},
    {"list?",            prim_listp},
    {"list-ref",         prim_list_ref},
    {"list-tail",        prim_list_tail},
    {"reverse",          prim_reverse},
    {"append",           prim_append},
    {"memq",             prim_memq},
    {"member",           prim_member},
    {"assq",             prim_assq},
    {"assv",             prim_assv},
    {"assoc",            prim_assoc},
    {"memv",             prim_memv},
    {"make-list",        prim_make_list},
    {"list-set!",        prim_list_set},
    {"list-copy",        prim_list_copy},
    {"string>?",         prim_string_gt},
    {"string<=?",        prim_string_le},
    {"string>=?",        prim_string_ge},
    {"string-fill!",     prim_string_fill},
    {"vector-copy",      prim_vector_copy},
    {"vector-fill!",     prim_vector_fill},
    {"vector-append",    prim_vector_append},
    {"bytevector-copy",  prim_bytevector_copy},
    {"expt",             prim_expt},
    {"symbol=?",         prim_symbol_eq},
    {"boolean=?",        prim_boolean_eq},
    {"eof-object?",      prim_eof_objectp},
    {"read-char",        prim_read_char},
    {"write-char",       prim_write_char},
    {"peek-char",        prim_peek_char},
    {"char-upcase",      prim_char_upcase},
    {"char-downcase",    prim_char_downcase},
    {"digit-value",      prim_digit_value},
    {"caar",  prim_caar}, {"cadr", prim_cadr},
    {"cdar",  prim_cdar}, {"cddr", prim_cddr},
    {"caaar", prim_caaar}, {"caadr", prim_caadr},
    {"cadar", prim_cadar}, {"caddr", prim_caddr},
    {"cdaar", prim_cdaar}, {"cdadr", prim_cdadr},
    {"cddar", prim_cddar}, {"cdddr", prim_cdddr},
    {"caaaar",prim_caaaar},{"caaadr",prim_caaadr},
    {"caadar",prim_caadar},{"caaddr",prim_caaddr},
    {"cadaar",prim_cadaar},{"cadadr",prim_cadadr},
    {"caddar",prim_caddar},{"cadddr",prim_cadddr},
    {"cdaaar",prim_cdaaar},{"cdaadr",prim_cdaadr},
    {"cdadar",prim_cdadar},{"cdaddr",prim_cdaddr},
    {"cddaar",prim_cddaar},{"cddadr",prim_cddadr},
    {"cdddar",prim_cdddar},{"cddddr",prim_cddddr},
};
#define NUM_PRIMS (sizeof(prim_table) / sizeof(prim_table[0]))

word vm_dispatch_prim(vm_state_t* vm, int prim_index, int nargs) {
    if (prim_index < 0 || prim_index >= (int)NUM_PRIMS) {
        fprintf(stderr, "invalid prim index: %d\n", prim_index);
        return word_nil();
    }
    return prim_table[prim_index].fn(vm, nargs);
}

void prim_init_all(vm_state_t* vm) {
    (void)vm;
}

int prim_lookup(const char* name) {
    for (size_t i = 0; i < NUM_PRIMS; i++) {
        if (strcmp(prim_table[i].name, name) == 0)
            return (int)i;
    }
    return -1;
}
