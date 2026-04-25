#include "types.h"
#include "pal.h"
#include "gc.h"
#include "vm.h"
#include "reader.h"
#include "compiler.h"
#include "prim.h"
#include <stdio.h>
#include <string.h>

pal_interface* pal;
gc_interface*  gc;
vm_state_t*    vm;

static void repl(void) {
    char buf[4096];
    printf("Scheme v0.1\n");

    while (1) {
        printf("> ");
        fflush(stdout);

        if (!fgets(buf, sizeof(buf), stdin)) {
            printf("\n");
            break;
        }

        if (buf[0] == '\n') continue;

        int pos = 0;
        word expr = read_sexp(vm, buf, &pos);
        if (vm->error_code) {
            vm->error_code = 0;
            printf("read error\n");
            continue;
        }
        if (is_eof(expr)) break;

        word code_obj = compile_expr(vm, expr);
        int code_idx = vm_load_code(vm, ptr_from_word(code_obj));

        vm->error_code = 0;
        word result = vm_execute(vm, code_idx);

        vm->sp[0] = result;
        prim_display(vm, 1);
        printf("\n");
    }
}

static int exec_file(const char* path) {
    int fd = pal->file_open(path, PAL_O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "cannot open: %s\n", path);
        return 1;
    }

    char buf[65536];
    int64_t n = pal->file_read(fd, buf, sizeof(buf) - 1);
    pal->file_close(fd);
    if (n < 0) {
        fprintf(stderr, "read error: %s\n", path);
        return 1;
    }
    buf[n] = '\0';

    int pos = 0;
    while (pos < n) {
        word expr = read_sexp(vm, buf, &pos);
        if (vm->error_code) {
            vm->error_code = 0;
            fprintf(stderr, "read error at position %d\n", pos);
            break;
        }
        if (is_eof(expr)) break;

        word code_obj = compile_expr(vm, expr);
        int code_idx = vm_load_code(vm, ptr_from_word(code_obj));
        vm_execute(vm, code_idx);
    }

    return 0;
}

int main(int argc, char** argv) {
    pal = pal_init();
    gc = gc_init();
    vm = vm_init(gc, pal);
    prim_init_all(vm);

    if (argc > 1) {
        return exec_file(argv[1]);
    }

    repl();
    return 0;
}
