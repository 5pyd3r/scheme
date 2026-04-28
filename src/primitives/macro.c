#include "types.h"
#include "vm.h"
#include "compiler.h"
#include <stdio.h>

word prim_gensym(vm_state_t* vm, int nargs) {
    (void)nargs;
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "{g%d}", vm->gensym_counter++);
    return vm_intern(vm, buf, n);
}

word prim_eval(vm_state_t* vm, int nargs) {
    if (nargs != 1) {
        vm->error_kind = ERR_ARITY;
        vm->error_msg = "eval requires 1 argument";
        return word_nil();
    }
    int ci = scheme_compile_and_assemble(vm, vm->sp[0]);
    if (ci < 0) return word_nil();
    return vm_execute(vm, ci);
}
