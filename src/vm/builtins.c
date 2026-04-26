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
