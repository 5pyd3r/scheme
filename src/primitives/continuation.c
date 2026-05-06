#include "types.h"
#include "vm.h"
#include <string.h>

/* Continuation object layout (after 3-word header):
   cont[3] = saved sp offset from stack base
   cont[4] = saved fp offset from stack base
   cont[5] = saved ip (as uintptr_t)
   cont[6] = saved current_code (as uintptr_t)
   cont[7] = saved env (as uintptr_t)
*/

/* call/cc via primitive dispatch — used when call/cc is called as a primitive
   rather than via the OP_CALL_CC opcode (which the C compiler emits directly). */
word prim_call_cc(vm_state_t* vm, int nargs) {
    // Not yet fully implemented — use C compiler OP_CALL_CC path instead
    vm->error_kind = ERR_INTERNAL;
    vm->error_msg = "call/cc: use (call/cc proc) syntax for compiler support";
    return word_nil();
}

/* Restore continuation: called from OP_CALL/OP_APPLY when invoked object is continuation.
   cont_word: the continuation object
   nargs: number of values passed (we only use the first) */
void vm_restore_continuation(vm_state_t* vm, word cont_word, int nargs_val) {
    word* cont = ptr_from_word(cont_word);

    size_t sp_off = (size_t)cont[DATA_START_INDEX + 0];
    size_t fp_off = (size_t)cont[DATA_START_INDEX + 1];

    // Get the value to return (first arg passed to continuation)
    // OP_CALL stack layout: sp[-nargs..-1] = args, sp[0] = closure (continuation)
    // First value arg is at sp[-nargs]
    word value;
    if (nargs_val >= 1) {
        value = vm->sp[-nargs_val]; // first value arg
    } else {
        value = word_nil();
    }

    // Restore registers
    vm->sp = vm->stack + sp_off;
    vm->fp = vm->stack + fp_off;
    vm->ip = (uint8_t*)(uintptr_t)cont[DATA_START_INDEX + 2];
    vm->current_code = (word*)(uintptr_t)cont[DATA_START_INDEX + 3];
    vm->env = (word*)(uintptr_t)cont[DATA_START_INDEX + 4];

    // Push the value as the result
    *++vm->sp = value;
}
