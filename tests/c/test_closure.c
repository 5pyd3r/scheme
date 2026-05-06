#include "types.h"
#include "vm.h"
#include "prim.h"
#include "gc.h"
#include "pal.h"
#include "compiler.h"
#include "reader.h"
#include <stdio.h>
#include <string.h>

static int n_failures = 0;
pal_interface* pal;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); n_failures++; } \
} while(0)

static word eval_string(vm_state_t* vm, const char* src) {
    int pos = 0;
    word expr = read_sexp(vm, src, &pos);
    if (vm->error_kind != ERR_NONE) {
        vm->error_kind = ERR_NONE;
        return word_nil();
    }
    if (is_eof(expr)) return word_nil();
    word code_obj = compile_expr(vm, expr);
    int ci = vm_load_code(vm, ptr_from_word(code_obj));
    if (ci < 0) return word_nil();
    return vm_execute(vm, ci);
}

int main(void) {
    pal = pal_init();
    gc_interface* gc = gc_init();
    vm_state_t* vm = vm_init(gc, pal);
    prim_init_all(vm);

    /* Simple eval check */
    word result = eval_string(vm, "42");
    CHECK(is_fixnum(result) && word_to_fixnum(result) == 42, "literal 42");

    /* Simple closure */
    result = eval_string(vm, "(((lambda (x) (lambda (y) (+ x y))) 1) 2)");
    CHECK(is_fixnum(result) && word_to_fixnum(result) == 3, "simple closure (1+2=3)");

    /* Multiple captured vars */
    result = eval_string(vm, "(((lambda (x y) (lambda (z) (+ x (+ y z)))) 1 2) 3)");
    CHECK(is_fixnum(result) && word_to_fixnum(result) == 6, "multiple captured vars (1+2+3=6)");

    /* Closure via define */
    eval_string(vm, "(define (make-adder n) (lambda (x) (+ x n)))");
    result = eval_string(vm, "((make-adder 5) 10)");
    CHECK(is_fixnum(result) && word_to_fixnum(result) == 15, "make-adder (5+10=15)");

    /* No capture (nfree=0) */
    result = eval_string(vm, "((lambda (x) x) 42)");
    CHECK(is_fixnum(result) && word_to_fixnum(result) == 42, "no capture (42)");

    /* Shadowing */
    result = eval_string(vm, "(((lambda (x) (lambda (x) x)) 1) 2)");
    CHECK(is_fixnum(result) && word_to_fixnum(result) == 2, "shadowing returns inner 2");

    /* Mutual recursion (even?/odd?) */
    eval_string(vm, "(define (even? n) (if (= n 0) #t (odd? (- n 1))))");
    eval_string(vm, "(define (odd? n) (if (= n 0) #f (even? (- n 1))))");
    result = eval_string(vm, "(even? 4)");
    CHECK(result == word_true(), "mutual recursion even? 4 = #t");
    result = eval_string(vm, "(even? 5)");
    CHECK(result == word_false(), "mutual recursion even? 5 = #f");
    result = eval_string(vm, "(odd? 3)");
    CHECK(result == word_true(), "mutual recursion odd? 3 = #t");

    if (n_failures == 0) {
        printf("ALL closure tests PASSED\n");
        return 0;
    }
    printf("%d closure test(s) FAILED\n", n_failures);
    return 1;
}
