#ifndef SCHEME_READER_H
#define SCHEME_READER_H

#include "types.h"
#include "vm.h"

word read_sexp(vm_state_t* vm, const char* input, int* end_pos);

#endif
