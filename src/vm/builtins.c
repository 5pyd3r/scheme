#include "prim.h"
#include "opcodes.h"
#include <stdio.h>
#include <string.h>

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
word prim_display(vm_state_t* vm, int nargs);
word prim_newline(vm_state_t* vm, int nargs);
word prim_symbol_to_string(vm_state_t* vm, int nargs);
word prim_prim_index(vm_state_t* vm, int nargs);
word prim_assemble_code(vm_state_t* vm, int nargs);
word prim_read(vm_state_t* vm, int nargs);

word prim_assemble_code(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_code = 1; return word_from_fixnum(-1); }
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
    {"display",  prim_display},
    {"newline",  prim_newline},
    {"symbol->string", prim_symbol_to_string},
    {"prim-index",     prim_prim_index},
    {"assemble-code",  prim_assemble_code},
    {"read",           prim_read},
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
