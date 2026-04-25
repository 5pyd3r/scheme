#ifndef SCHEME_PRIM_H
#define SCHEME_PRIM_H

#include "types.h"
#include "vm.h"

typedef word (*prim_fn_t)(vm_state_t* vm, int nargs);

void prim_init_all(vm_state_t* vm);

word vm_dispatch_prim(vm_state_t* vm, int prim_index, int nargs);

int prim_lookup(const char* name);

#endif
