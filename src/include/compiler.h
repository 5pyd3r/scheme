#ifndef SCHEME_COMPILER_H
#define SCHEME_COMPILER_H

#include "types.h"
#include "vm.h"

word compile_expr(vm_state_t* vm, word expr);

word compile_program(vm_state_t* vm, word exprs);

#endif
