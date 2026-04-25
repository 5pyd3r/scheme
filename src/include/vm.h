#ifndef SCHEME_VM_H
#define SCHEME_VM_H

#include "types.h"
#include "gc.h"
#include "pal.h"

typedef struct vm_state {
    gc_interface*  gc;
    pal_interface* pal;

    uint8_t* ip;
    word*   sp;
    word*   fp;
    word*   env;
    word    acc;

    word*   stack;
    size_t  stack_cap;
    size_t  stack_len;

    word**  code_objects;
    size_t  code_count;

    word*   current_code;

    word*   globals;
    size_t  global_count;
    int     next_global_slot;

    word*   primitives;

    int     error_code;
    word    error_arg;

    bool    gc_active;
} vm_state_t;

vm_state_t* vm_init(gc_interface* gc, pal_interface* pal);
int vm_load_code(vm_state_t* vm, word* code_obj);
word vm_execute(vm_state_t* vm, int entry_point);
int vm_register_prim(vm_state_t* vm, word prim);

#endif
