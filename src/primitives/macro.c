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
    word expr = vm->sp[0];

    /* Save VM execution state — vm_execute modifies these */
    word* saved_sp = vm->sp;
    uint8_t* saved_ip = vm->ip;
    word* saved_fp = vm->fp;
    word* saved_env = vm->env;
    word* saved_current = vm->current_code;

    int ci = scheme_compile_and_assemble(vm, expr);
    if (ci < 0) {
        word code_obj = compile_expr(vm, expr);
        if (is_ptr(code_obj))
            ci = vm_load_code(vm, ptr_from_word(code_obj));
    }

    word result;
    if (ci >= 0)
        result = vm_execute(vm, ci);
    else
        result = word_nil();

    /* Restore VM state */
    vm->sp = saved_sp;
    vm->ip = saved_ip;
    vm->fp = saved_fp;
    vm->env = saved_env;
    vm->current_code = saved_current;

    return result;
}
