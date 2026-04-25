#include "compiler.h"
#include "prim.h"
#include "vm/opcodes.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MAX_BYTECODE 4096

typedef struct {
    uint8_t bytes[MAX_BYTECODE];
    int     len;
    word    consts[256];
    int     nconsts;
} code_buf_t;

static void emit_byte(code_buf_t* buf, uint8_t b) {
    buf->bytes[buf->len++] = b;
}

static int add_const(code_buf_t* buf, word val) {
    buf->consts[buf->nconsts] = val;
    return buf->nconsts++;
}

static bool is_symbol(word w, const char* name) {
    if (!is_ptr(w)) return false;
    word* hdr = ptr_from_word(w);
    if (obj_type(hdr) != OBJ_TYPE_SYMBOL) return false;
    int len = (int)string_length(hdr);
    for (int i = 0; i < len; i++) {
        if (word_to_char(string_ref(hdr, i)) != (unsigned char)name[i])
            return false;
    }
    return name[len] == '\0';
}

#define MAX_LOCALS 64

typedef struct {
    const char* names[MAX_LOCALS];
    int         count;
} local_scope_t;

static int local_find(local_scope_t* scope, word sym) {
    if (!is_ptr(sym)) return -1;
    word* hdr = ptr_from_word(sym);
    int slen = (int)string_length(hdr);
    for (int i = scope->count - 1; i >= 0; i--) {
        const char* name = scope->names[i];
        if (!name) continue;
        int match = 1;
        for (int j = 0; j < slen; j++) {
            if (!name[j] || word_to_char(string_ref(hdr, j)) != (unsigned char)name[j]) {
                match = 0; break;
            }
        }
        if (match && name[slen] == '\0')
            return i + 1;  // LREF indices start at 1
    }
    return -1;
}

static void compile_lambda(code_buf_t* buf, vm_state_t* vm, word args, word body, local_scope_t* parent_scope);

static void compile_expr_to_buf(code_buf_t* buf, vm_state_t* vm, word expr, local_scope_t* scope);

static void compile_lambda(code_buf_t* buf, vm_state_t* vm, word args, word body, local_scope_t* parent_scope) {
    (void)parent_scope;  // free variables not yet supported in stage0
    local_scope_t lambda_scope = {0};
    // Fill parameters
    int param_idx = 0;
    word cur = args;
    while (is_ptr(cur) && obj_type(ptr_from_word(cur)) == OBJ_TYPE_PAIR && param_idx < MAX_LOCALS) {
        word param_sym = pair_car(ptr_from_word(cur));
        word* shdr = ptr_from_word(param_sym);
        int plen = (int)string_length(shdr);
        char* pname = (char*)malloc(plen + 1);
        for (int i = 0; i < plen; i++)
            pname[i] = (char)word_to_char(string_ref(shdr, i));
        pname[plen] = '\0';
        lambda_scope.names[param_idx++] = pname;
        cur = pair_cdr(ptr_from_word(cur));
    }
    lambda_scope.count = param_idx;

    // Compile body to child code buffer
    code_buf_t child_buf = {0};
    compile_expr_to_buf(&child_buf, vm, body, &lambda_scope);
    emit_byte(&child_buf, OP_RETURN);

    // Create child code object
    size_t c_bc_words = (child_buf.len + sizeof(word) - 1) / sizeof(word);
    size_t c_total_words = 3 + c_bc_words + child_buf.nconsts;
    word* child_code = vm->gc->alloc_words(c_total_words);
    obj_set_type(child_code, OBJ_TYPE_CODE);
    child_code[2] = (word)child_buf.len;
    memcpy(child_code + 3, child_buf.bytes, child_buf.len);
    for (int i = 0; i < child_buf.nconsts; i++)
        child_code[3 + c_bc_words + i] = child_buf.consts[i];

    // Register child code object with VM
    int code_idx = vm_load_code(vm, child_code);
    if (code_idx < 0) {
        // Failed to register - push nil as fallback
        emit_byte(buf, OP_PUSH_NIL);
        // Clean up param names
        for (int i = 0; i < param_idx; i++) free((void*)lambda_scope.names[i]);
        return;
    }

    // Emit CLOSE: code_idx (2B little-endian), nfree (1B)
    emit_byte(buf, OP_CLOSE);
    emit_byte(buf, (uint8_t)(code_idx & 0xFF));
    emit_byte(buf, (uint8_t)((code_idx >> 8) & 0xFF));
    emit_byte(buf, 0);  // nfree = 0 for stage0

    // Clean up param names
    for (int i = 0; i < param_idx; i++) free((void*)lambda_scope.names[i]);
}

static void compile_list(code_buf_t* buf, vm_state_t* vm, word expr, local_scope_t* scope) {
    word* hdr = ptr_from_word(expr);
    word fn = pair_car(hdr);
    word args = pair_cdr(hdr);

    if (is_symbol(fn, "quote")) {
        word val = pair_car(ptr_from_word(args));
        int idx = add_const(buf, val);
        emit_byte(buf, OP_PUSH_CONST);
        emit_byte(buf, (uint8_t)idx);
        return;
    }

    if (is_symbol(fn, "define")) {
        word* ahdr = ptr_from_word(args);
        word name_or_form = pair_car(ahdr);

        if (is_ptr(name_or_form) && obj_type(ptr_from_word(name_or_form)) == OBJ_TYPE_PAIR) {
            // Chain define: (define (f x) body) -> (define f (lambda (x) body))
            word form_args = pair_cdr(ptr_from_word(name_or_form));
            word form_body = pair_car(ptr_from_word(pair_cdr(ahdr)));

            // Compile lambda
            compile_lambda(buf, vm, form_args, form_body, scope);

            // Store to global slot for fn_sym
            word fn_sym = pair_car(ptr_from_word(name_or_form));
            int slot = vm->next_global_slot++;
            if (slot >= (int)vm->global_count) {
                size_t new_count = vm->global_count * 2;
                vm->globals = realloc(vm->globals, new_count * sizeof(word));
                vm->global_names = realloc(vm->global_names, new_count * sizeof(word));
                vm->global_count = new_count;
            }
            vm->global_names[slot] = fn_sym;
            emit_byte(buf, OP_GSET);
            emit_byte(buf, (uint8_t)slot);
            return;
        }

        // Simple define: (define x val)
        word val_expr = pair_car(ptr_from_word(pair_cdr(ahdr)));
        compile_expr_to_buf(buf, vm, val_expr, scope);
        word name_sym = name_or_form;
        int slot = vm->next_global_slot++;
        if (slot >= (int)vm->global_count) {
            size_t new_count = vm->global_count * 2;
            vm->globals = realloc(vm->globals, new_count * sizeof(word));
            vm->global_names = realloc(vm->global_names, new_count * sizeof(word));
            vm->global_count = new_count;
        }
        vm->global_names[slot] = name_sym;
        emit_byte(buf, OP_GSET);
        emit_byte(buf, (uint8_t)slot);
        return;
    }

    if (is_symbol(fn, "if")) {
        word* ahdr = ptr_from_word(args);
        word test = pair_car(ahdr);
        word then_expr = pair_car(ptr_from_word(pair_cdr(ahdr)));
        word else_expr = word_nil();
        word rest = pair_cdr(ptr_from_word(pair_cdr(ahdr)));
        if (!is_nil(rest))
            else_expr = pair_car(ptr_from_word(rest));

        compile_expr_to_buf(buf, vm, test, scope);
        int jmp_false_pos = buf->len;
        emit_byte(buf, OP_JMP_IF_NOT);
        emit_byte(buf, 0); emit_byte(buf, 0);

        compile_expr_to_buf(buf, vm, then_expr, scope);
        int jmp_end_pos = buf->len;
        emit_byte(buf, OP_JMP);
        emit_byte(buf, 0); emit_byte(buf, 0);

        int false_start = buf->len;
        int false_offset = false_start - (jmp_false_pos + 3);
        buf->bytes[jmp_false_pos + 1] = (uint8_t)(false_offset & 0xFF);
        buf->bytes[jmp_false_pos + 2] = (uint8_t)((false_offset >> 8) & 0xFF);

        if (!is_nil(else_expr))
            compile_expr_to_buf(buf, vm, else_expr, scope);

        int end_offset = buf->len - (jmp_end_pos + 3);
        buf->bytes[jmp_end_pos + 1] = (uint8_t)(end_offset & 0xFF);
        buf->bytes[jmp_end_pos + 2] = (uint8_t)((end_offset >> 8) & 0xFF);
        return;
    }

    if (is_symbol(fn, "lambda")) {
        word* ahdr = ptr_from_word(args);
        word params = pair_car(ahdr);
        word body = pair_car(ptr_from_word(pair_cdr(ahdr)));
        compile_lambda(buf, vm, params, body, scope);
        return;
    }

    // Check if fn is a known primitive
    int known_prim_idx = -1;
    if (is_ptr(fn) && obj_type(ptr_from_word(fn)) == OBJ_TYPE_SYMBOL) {
        word* fhdr = ptr_from_word(fn);
        int nlen = (int)string_length(fhdr);
        char fname[64];
        if (nlen < 63) {
            for (int i = 0; i < nlen; i++)
                fname[i] = (char)word_to_char(string_ref(fhdr, i));
            fname[nlen] = '\0';
            known_prim_idx = prim_lookup(fname);
        }
    }

    if (known_prim_idx >= 0) {
        // Primitive call -- only compile args, not fn
        word acur = args;
        while (is_ptr(acur) && obj_type(ptr_from_word(acur)) == OBJ_TYPE_PAIR) {
            compile_expr_to_buf(buf, vm, pair_car(ptr_from_word(acur)), scope);
            acur = pair_cdr(ptr_from_word(acur));
        }
        int nargs = 0;
        acur = args;
        while (is_ptr(acur) && obj_type(ptr_from_word(acur)) == OBJ_TYPE_PAIR) {
            nargs++;
            acur = pair_cdr(ptr_from_word(acur));
        }
        emit_byte(buf, OP_PRIM_CALL);
        emit_byte(buf, (uint8_t)nargs);
        emit_byte(buf, (uint8_t)(known_prim_idx & 0xFF));
        emit_byte(buf, (uint8_t)((known_prim_idx >> 8) & 0xFF));
    } else {
        // User function call — compile args first, then fn (closure on top)
        // This ensures nested calls work: inner calls return, their results are below
        // the closure on the stack.
        word acur = args;
        int nargs = 0;
        while (is_ptr(acur) && obj_type(ptr_from_word(acur)) == OBJ_TYPE_PAIR) {
            compile_expr_to_buf(buf, vm, pair_car(ptr_from_word(acur)), scope);
            acur = pair_cdr(ptr_from_word(acur));
            nargs++;
        }
        compile_expr_to_buf(buf, vm, fn, scope);
        emit_byte(buf, OP_CALL);
        emit_byte(buf, (uint8_t)nargs);
    }
}

static void compile_expr_to_buf(code_buf_t* buf, vm_state_t* vm, word expr, local_scope_t* scope) {
    if (is_fixnum(expr)) {
        int32_t val = (int32_t)word_to_fixnum(expr);
        emit_byte(buf, OP_PUSH_INT);
        emit_byte(buf, (uint8_t)(val & 0xFF));
        emit_byte(buf, (uint8_t)((val >> 8) & 0xFF));
        emit_byte(buf, (uint8_t)((val >> 16) & 0xFF));
        emit_byte(buf, (uint8_t)((val >> 24) & 0xFF));
        return;
    }

    if (is_ptr(expr)) {
        word* hdr = ptr_from_word(expr);
        if (obj_type(hdr) == OBJ_TYPE_PAIR) {
            compile_list(buf, vm, expr, scope);
            return;
        }
    }

    if (is_ptr(expr) && obj_type(ptr_from_word(expr)) == OBJ_TYPE_SYMBOL) {
        if (scope) {
            int local_idx = local_find(scope, expr);
            if (local_idx > 0) {
                emit_byte(buf, OP_LREF);
                emit_byte(buf, (uint8_t)local_idx);
                return;
            }
        }
        // Look up global slot by symbol name
        int slot = vm_find_global_slot(vm, expr);
        if (slot < 0) {
            // Undefined — create slot on demand
            slot = vm->next_global_slot++;
            if (slot >= (int)vm->global_count) {
                size_t new_count = vm->global_count * 2;
                vm->globals = realloc(vm->globals, new_count * sizeof(word));
                vm->global_names = realloc(vm->global_names, new_count * sizeof(word));
                vm->global_count = new_count;
            }
            vm->global_names[slot] = expr;
        }
        emit_byte(buf, OP_GREF);
        emit_byte(buf, (uint8_t)slot);
        return;
    }

    int idx = add_const(buf, expr);
    emit_byte(buf, OP_PUSH_CONST);
    emit_byte(buf, (uint8_t)idx);
}

word compile_expr(vm_state_t* vm, word expr) {
    code_buf_t buf = { 0 };
    compile_expr_to_buf(&buf, vm, expr, NULL);
    emit_byte(&buf, OP_HALT);

    size_t bc_words = (buf.len + sizeof(word) - 1) / sizeof(word);
    size_t total_words = 3 + bc_words + buf.nconsts;
    word* code_obj = vm->gc->alloc_words(total_words);
    obj_set_type(code_obj, OBJ_TYPE_CODE);
    code_obj[2] = (word)buf.len;

    memcpy(code_obj + 3, buf.bytes, buf.len);

    for (int i = 0; i < buf.nconsts; i++)
        code_obj[3 + bc_words + i] = buf.consts[i];

    return ptr_to_word(code_obj);
}

word compile_program(vm_state_t* vm, word exprs) {
    if (is_nil(exprs)) {
        code_buf_t buf = { 0 };
        emit_byte(&buf, OP_PUSH_NIL);
        emit_byte(&buf, OP_HALT);

        size_t bc_words = (buf.len + sizeof(word) - 1) / sizeof(word);
        size_t total_words = 3 + bc_words;
        word* code_obj = vm->gc->alloc_words(total_words);
        obj_set_type(code_obj, OBJ_TYPE_CODE);
        code_obj[2] = (word)buf.len;
        memcpy(code_obj + 3, buf.bytes, buf.len);
        return ptr_to_word(code_obj);
    }
    return compile_expr(vm, pair_car(ptr_from_word(exprs)));
}
