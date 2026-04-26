#include "compiler.h"
#include "debug.h"
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

static void compile_expr_to_buf(code_buf_t* buf, vm_state_t* vm, word expr, int* next_local);

static void compile_list(code_buf_t* buf, vm_state_t* vm, word expr, int* next_local) {
    DASSERT_TYPE(expr, OBJ_TYPE_PAIR);
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
        word val_expr = pair_car(ptr_from_word(pair_cdr(ahdr)));
        compile_expr_to_buf(buf, vm, val_expr, next_local);
        int slot = vm->next_global_slot++;
        if (slot >= (int)vm->global_count) {
            size_t new_count = vm->global_count * 2;
            vm->globals = realloc(vm->globals, new_count * sizeof(word));
            vm->global_count = new_count;
        }
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

        compile_expr_to_buf(buf, vm, test, next_local);
        int jmp_false_pos = buf->len;
        emit_byte(buf, OP_JMP_IF_NOT);
        emit_byte(buf, 0); emit_byte(buf, 0);

        compile_expr_to_buf(buf, vm, then_expr, next_local);
        int jmp_end_pos = buf->len;
        emit_byte(buf, OP_JMP);
        emit_byte(buf, 0); emit_byte(buf, 0);

        int false_start = buf->len;
        int false_offset = false_start - (jmp_false_pos + 3);
        buf->bytes[jmp_false_pos + 1] = (uint8_t)((false_offset >> 8) & 0xFF);
        buf->bytes[jmp_false_pos + 2] = (uint8_t)(false_offset & 0xFF);

        if (!is_nil(else_expr))
            compile_expr_to_buf(buf, vm, else_expr, next_local);

        int end_offset = buf->len - (jmp_end_pos + 3);
        buf->bytes[jmp_end_pos + 1] = (uint8_t)((end_offset >> 8) & 0xFF);
        buf->bytes[jmp_end_pos + 2] = (uint8_t)(end_offset & 0xFF);
        return;
    }

    if (is_symbol(fn, "lambda")) {
        emit_byte(buf, OP_PUSH_NIL);
        return;
    }

    int nargs = 0;
    word cur_word = args;
    while (is_ptr(cur_word) && obj_type(ptr_from_word(cur_word)) == OBJ_TYPE_PAIR) {
        compile_expr_to_buf(buf, vm, pair_car(ptr_from_word(cur_word)), next_local);
        cur_word = pair_cdr(ptr_from_word(cur_word));
        nargs++;
    }
    if (is_ptr(fn) && obj_type(ptr_from_word(fn)) == OBJ_TYPE_SYMBOL) {
        word* fhdr = ptr_from_word(fn);
        int nlen = (int)string_length(fhdr);
        char fname[64];
        if (nlen < 63) {
            for (int i = 0; i < nlen; i++)
                fname[i] = (char)word_to_char(string_ref(fhdr, i));
            fname[nlen] = '\0';
            int prim_idx = prim_lookup(fname);
            if (prim_idx >= 0) {
                emit_byte(buf, OP_PRIM_CALL);
                emit_byte(buf, (uint8_t)nargs);
                emit_byte(buf, (uint8_t)(prim_idx & 0xFF));
                emit_byte(buf, (uint8_t)((prim_idx >> 8) & 0xFF));
                return;
            }
        }
    }
    emit_byte(buf, OP_PUSH_NIL);
}

static void compile_expr_to_buf(code_buf_t* buf, vm_state_t* vm, word expr, int* next_local) {
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
            compile_list(buf, vm, expr, next_local);
            return;
        }
    }

    if (is_ptr(expr) && obj_type(ptr_from_word(expr)) == OBJ_TYPE_SYMBOL) {
        int idx = add_const(buf, expr);
        emit_byte(buf, OP_GREF);
        emit_byte(buf, (uint8_t)idx);
        return;
    }

    int idx = add_const(buf, expr);
    emit_byte(buf, OP_PUSH_CONST);
    emit_byte(buf, (uint8_t)idx);
}

word compile_expr(vm_state_t* vm, word expr) {
    code_buf_t buf = { 0 };
    int dummy = 0;
    compile_expr_to_buf(&buf, vm, expr, &dummy);
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
