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

word prim_call_cc(vm_state_t* vm, int nargs) {
    if (nargs != 1) {
        vm->error_kind = ERR_ARITY;
        vm->error_msg = "call/cc: expected 1 argument";
        return word_nil();
    }

    word proc = vm->sp[0];

    // Allocate continuation object: header(3) + 5 words of state
    word* cont = vm->gc->alloc_words(3 + 5);
    obj_set_type(cont, OBJ_TYPE_CONTINUATION);

    cont[DATA_START_INDEX + 0] = (word)(uintptr_t)(vm->sp - vm->stack);
    cont[DATA_START_INDEX + 1] = (word)(uintptr_t)(vm->fp - vm->stack);
    cont[DATA_START_INDEX + 2] = (word)(uintptr_t)vm->ip;
    cont[DATA_START_INDEX + 3] = (word)(uintptr_t)vm->current_code;
    cont[DATA_START_INDEX + 4] = (word)(uintptr_t)vm->env;

    word cont_word = ptr_to_word(cont);

    // Set up call: (proc cont)
    // Pop the proc arg from sp (it was pushed by PRIM_CALL setup)
    // Then push cont as arg and proc as closure
    vm->sp--;               // discard proc from PRIM_CALL stack
    *++vm->sp = cont_word;  // arg: continuation
    *++vm->sp = proc;       // closure on top

    // Manually set up a frame for calling (proc cont)
    // This is simplified version of OP_CALL frame setup
    word* clo = ptr_from_word(proc);
    word nfree_word = clo[DATA_START_INDEX + 2];
    uint8_t nfree_val = (uint8_t)(nfree_word & 0xFF);

    word* base = vm->sp - 1;  // base[0] = cont (arg), base[1] = proc (closure)
    word old_sp = (word)(uintptr_t)(base - 1);

    // Shift arg up FIRST (before writing frame header)
    for (int i = 0; i >= 0; i--)
        base[i + 4 + nfree_val] = base[i];

    // Write frame header
    base[0] = old_sp;
    base[1] = (word)(uintptr_t)vm->ip;
    base[2] = (word)(uintptr_t)vm->env;
    base[3] = (word)(uintptr_t)vm->fp;

    // Unpack captured vars
    if (nfree_val > 0) {
        word* env_vec = (word*)(uintptr_t)closure_env(clo);
        if (env_vec) {
            for (int i = 0; i < nfree_val; i++)
                base[4 + i] = env_vec[DATA_START_INDEX + 1 + i];
        }
    }

    // Set new frame
    vm->fp = base + 3;
    vm->env = (word*)(uintptr_t)closure_env(clo);
    vm->sp = base + 4 + 1 + nfree_val;  // 1 arg
    vm->current_code = (word*)(uintptr_t)closure_code(clo);
    vm->ip = (uint8_t*)(vm->current_code + 3); // jump to first bytecode

    // The PRIM_CALL handler checks if ip changed and skips cleanup
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
    // The args are on the current stack at sp[-nargs+1..0]
    // We only use the first value
    word value;
    if (nargs_val >= 1) {
        value = vm->sp[-nargs_val + 1]; // first arg
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
