#include "types.h"
#include "pal.h"
#include "gc.h"
#include "vm.h"
#include "debug.h"
#include "reader.h"
#include "compiler.h"
#include "prim.h"
#include <stdio.h>
#include <string.h>

/* Opcodes for Scheme compile trampoline (match src/vm/opcodes.h) */
#define OP_PUSH_CONST   0x04
#define OP_GREF         0x14
#define OP_CALL         0x21
#define OP_PRIM_CALL    0x50
#define OP_HALT         0xFF

pal_interface* pal;
gc_interface*  gc;
vm_state_t*    vm;

/* ============================================================
 * Trampoline: build a tiny code object that calls
 *   (assemble-code (compile expr))
 * and returns the resulting code index (fixnum).
 * ============================================================ */
static int scheme_compile_and_assemble(vm_state_t* vm, word expr) {
    int compile_slot = vm_find_global_by_name(vm, "compile");
    if (compile_slot < 0) return -1;

    int asm_idx = prim_lookup("assemble-code");
    if (asm_idx < 0) return -1;

    uint8_t bc[32];
    int len = 0;
    /* bytecodes: PUSH_CONST 0, GREF compile, CALL 1,
     *            PRIM_CALL 1 <asm_idx>, HALT */
    bc[len++] = OP_PUSH_CONST; bc[len++] = 0;
    bc[len++] = OP_GREF;       bc[len++] = (uint8_t)compile_slot;
    bc[len++] = OP_CALL;       bc[len++] = 1;
    bc[len++] = OP_PRIM_CALL;  bc[len++] = 1;
    bc[len++] = (uint8_t)(asm_idx & 0xFF);
    bc[len++] = (uint8_t)((asm_idx >> 8) & 0xFF);
    bc[len++] = OP_HALT;

    /* build code object: [GC_hdr][type][len][bytes...][consts...] */
    size_t bc_words = ((size_t)len + sizeof(word) - 1) / sizeof(word);
    size_t total = 3 + bc_words + 1;   /* +1 for expr const */
    word* obj = vm->gc->alloc_words(total);
    obj_set_type(obj, OBJ_TYPE_CODE);
    obj[2] = (word)len;
    memcpy(obj + 3, bc, (size_t)len);
    obj[3 + bc_words] = expr;          /* const[0] = expr */

    int idx = vm_load_code(vm, obj);
    if (idx < 0) return -1;

    word result = vm_execute(vm, idx);
    if (is_fixnum(result))
        return (int)word_to_fixnum(result);
    return -1;
}

/* ============================================================
 * REPL — tries Scheme compiler first, falls back to C compiler
 * ============================================================ */
static void repl(void) {
    char buf[4096];
    int use_scheme = (vm_find_global_by_name(vm, "compile") >= 0);
    printf("Scheme v0.1%s\n", use_scheme ? " (bootstrapped)" : "");

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
        if (vm->error_kind != ERR_NONE) {
            fprintf(stderr, "Error: [%d] %s\n", (int)vm->error_kind,
                    vm->error_msg ? vm->error_msg : "unknown");
            vm->error_kind = ERR_NONE;
            continue;
        }
        if (is_eof(expr)) continue;

        vm->error_kind = ERR_NONE;
        word result;
        if (use_scheme) {
            int ci = scheme_compile_and_assemble(vm, expr);
            if (ci >= 0) {
                result = vm_execute(vm, ci);
            } else {
                use_scheme = 0;   /* fall back to C compiler */
            }
        }
        if (!use_scheme) {
            word code_obj = compile_expr(vm, expr);
            int ci = vm_load_code(vm, ptr_from_word(code_obj));
            result = vm_execute(vm, ci);
        }

        vm->sp[0] = result;
        prim_display(vm, 1);
        printf("\n");
    }
}

/* ============================================================
 * Execute a file — when use_scheme=1, try Scheme compile first
 * ============================================================ */
static int exec_file(const char* path, int use_scheme) {
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
        if (vm->error_kind != ERR_NONE) {
            fprintf(stderr, "Error at position %d: [%d] %s\n", pos,
                    (int)vm->error_kind,
                    vm->error_msg ? vm->error_msg : "unknown");
            vm->error_kind = ERR_NONE;
            break;
        }
        if (is_eof(expr)) break;

        if (use_scheme) {
            int ci = scheme_compile_and_assemble(vm, expr);
            if (ci >= 0) {
                vm_execute(vm, ci);
                continue;
            }
            use_scheme = 0;   /* fall back */
        }
        word code_obj = compile_expr(vm, expr);
        int ci = vm_load_code(vm, ptr_from_word(code_obj));
        vm_execute(vm, ci);
    }

    return 0;
}

/* ============================================================
 * Main — Phase 1 loads compiler.scm, Phase 2 runs user code
 * ============================================================ */
int main(int argc, char** argv) {
    pal = pal_init();
    gc = gc_init();
    vm = vm_init(gc, pal);
    prim_init_all(vm);
    debug_install_handlers(vm);

    /* Phase 1: Bootstrap — load compiler.scm with the C compiler */
    if (pal->file_exists("src/scheme/compiler.scm"))
        exec_file("src/scheme/compiler.scm", 0);

    /* Phase 1b: Load standard library (C compiler constraints apply) */
    if (pal->file_exists("src/scheme/lib.scm"))
        exec_file("src/scheme/lib.scm", 0);

    /* Phase 2: Use Scheme compiler (with C fallback) for user code */
    if (argc > 1)
        return exec_file(argv[1], 1);

    repl();
    return 0;
}
