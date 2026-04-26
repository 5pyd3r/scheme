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
        word name_or_form = pair_car(ahdr);

        if (is_ptr(name_or_form) && obj_type(ptr_from_word(name_or_form)) == OBJ_TYPE_PAIR) {
            // Chain define: (define (f x) body) -> (define f (lambda (x) body))
            word form_args = pair_cdr(ptr_from_word(name_or_form));
            word body_list = pair_cdr(ahdr);
            word* body_list_hdr = ptr_from_word(body_list);
            word form_body;
            if (is_ptr(pair_cdr(body_list_hdr)) &&
                obj_type(ptr_from_word(pair_cdr(body_list_hdr))) == OBJ_TYPE_PAIR) {
                // Multiple body expressions — wrap in (begin ...)
                word* bsym = vm->gc->alloc_words(3 + 5);
                obj_set_type(bsym, OBJ_TYPE_SYMBOL);
                bsym[DATA_START_INDEX] = (word)5;
                const char* bname = "begin";
                for (int bi = 0; bi < 5; bi++)
                    string_set(bsym, bi, word_from_char((unsigned char)bname[bi]));
                word* bp = vm->gc->alloc_words(4); obj_set_type(bp, OBJ_TYPE_PAIR);
                pair_car(bp) = ptr_to_word(bsym);
                pair_cdr(bp) = body_list;
                form_body = ptr_to_word(bp);
            } else {
                form_body = pair_car(body_list_hdr);
            }

            // Compile lambda
            compile_lambda(buf, vm, form_args, form_body, scope);

            // Store to global slot for fn_sym
            word fn_sym = pair_car(ptr_from_word(name_or_form));
            int slot = vm_find_global_slot(vm, fn_sym);
            if (slot < 0) {
                slot = vm->next_global_slot++;
                if (slot >= (int)vm->global_count) {
                    size_t new_count = vm->global_count * 2;
                    vm->globals = realloc(vm->globals, new_count * sizeof(word));
                    vm->global_names = realloc(vm->global_names, new_count * sizeof(word));
                    vm->global_count = new_count;
                }
                vm->global_names[slot] = fn_sym;
            }
            emit_byte(buf, OP_GSET);
            emit_byte(buf, (uint8_t)slot);
            return;
        }

        // Simple define: (define x val)
        word val_expr = pair_car(ptr_from_word(pair_cdr(ahdr)));
        compile_expr_to_buf(buf, vm, val_expr, scope);
        word name_sym = name_or_form;
        int slot = vm_find_global_slot(vm, name_sym);
        if (slot < 0) {
            slot = vm->next_global_slot++;
            if (slot >= (int)vm->global_count) {
                size_t new_count = vm->global_count * 2;
                vm->globals = realloc(vm->globals, new_count * sizeof(word));
                vm->global_names = realloc(vm->global_names, new_count * sizeof(word));
                vm->global_count = new_count;
            }
            vm->global_names[slot] = name_sym;
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
        word body_list = pair_cdr(ahdr);
        word* body_list_hdr = ptr_from_word(body_list);
        word body;
        if (is_ptr(pair_cdr(body_list_hdr)) &&
            obj_type(ptr_from_word(pair_cdr(body_list_hdr))) == OBJ_TYPE_PAIR) {
            // Multiple body expressions — wrap in (begin ...)
            word* bsym = vm->gc->alloc_words(3 + 5);
            obj_set_type(bsym, OBJ_TYPE_SYMBOL);
            bsym[DATA_START_INDEX] = (word)5;
            const char* bname = "begin";
            for (int bi = 0; bi < 5; bi++)
                string_set(bsym, bi, word_from_char((unsigned char)bname[bi]));
            word* bp = vm->gc->alloc_words(4); obj_set_type(bp, OBJ_TYPE_PAIR);
            pair_car(bp) = ptr_to_word(bsym);
            pair_cdr(bp) = body_list;
            body = ptr_to_word(bp);
        } else {
            body = pair_car(body_list_hdr);
        }
        compile_lambda(buf, vm, params, body, scope);
        return;
    }

    if (is_symbol(fn, "begin")) {
        int count = 0;
        word cur = args;
        while (is_ptr(cur) && obj_type(ptr_from_word(cur)) == OBJ_TYPE_PAIR) {
            count++;
            cur = pair_cdr(ptr_from_word(cur));
        }
        if (count == 0) {
            emit_byte(buf, OP_PUSH_NIL);
            return;
        }
        // Compile all but last with POP
        cur = args;
        word last_val = word_nil();
        for (int i = 0; i < count - 1; i++) {
            word expr = pair_car(ptr_from_word(cur));
            compile_expr_to_buf(buf, vm, expr, scope);
            emit_byte(buf, OP_POP);
            cur = pair_cdr(ptr_from_word(cur));
        }
        // Compile last expression (value stays on stack)
        last_val = pair_car(ptr_from_word(cur));
        compile_expr_to_buf(buf, vm, last_val, scope);
        return;
    }

    if (is_symbol(fn, "cond")) {
        word* is = vm->gc->alloc_words(3 + 2);
        obj_set_type(is, OBJ_TYPE_SYMBOL);
        is[DATA_START_INDEX] = (word)2;
        string_set(is, 0, word_from_char('i'));
        string_set(is, 1, word_from_char('f'));
        word if_sym = ptr_to_word(is);

        word clauses[32];
        int n = 0;
        word cur = args;
        while (is_ptr(cur) && obj_type(ptr_from_word(cur)) == OBJ_TYPE_PAIR && n < 32) {
            clauses[n++] = pair_car(ptr_from_word(cur));
            cur = pair_cdr(ptr_from_word(cur));
        }
        if (is_ptr(cur) && obj_type(ptr_from_word(cur)) == OBJ_TYPE_PAIR) {
            vm->error_code = 1;
            return;
        }

        // Build nested if from last clause backwards
        word result = word_nil();
        for (int i = n - 1; i >= 0; i--) {
            word* chdr = ptr_from_word(clauses[i]);
            word test = pair_car(chdr);
            word body = pair_car(ptr_from_word(pair_cdr(chdr)));
            int is_else = (is_ptr(test) && obj_type(ptr_from_word(test)) == OBJ_TYPE_SYMBOL
                           && is_symbol(test, "else"));

            if (is_else) {
                result = body;
            } else {
                // Build (if test body result) as S-expression
                word* rp = vm->gc->alloc_words(4); obj_set_type(rp, OBJ_TYPE_PAIR);
                pair_car(rp) = result; pair_cdr(rp) = word_nil();

                word* bp = vm->gc->alloc_words(4); obj_set_type(bp, OBJ_TYPE_PAIR);
                pair_car(bp) = body; pair_cdr(bp) = ptr_to_word(rp);

                word* tp = vm->gc->alloc_words(4); obj_set_type(tp, OBJ_TYPE_PAIR);
                pair_car(tp) = test; pair_cdr(tp) = ptr_to_word(bp);

                word* ip = vm->gc->alloc_words(4); obj_set_type(ip, OBJ_TYPE_PAIR);
                pair_car(ip) = if_sym; pair_cdr(ip) = ptr_to_word(tp);

                result = ptr_to_word(ip);
            }
        }

        compile_expr_to_buf(buf, vm, result, scope);
        return;
    }

    if (is_symbol(fn, "let")) {
        word* ahdr = ptr_from_word(args);
        word bindings = pair_car(ahdr);
        word body = pair_car(ptr_from_word(pair_cdr(ahdr)));

        // Create "lambda" symbol
        word* ls = vm->gc->alloc_words(3 + 6);
        obj_set_type(ls, OBJ_TYPE_SYMBOL);
        ls[DATA_START_INDEX] = (word)6;
        const char* lsrc = "lambda";
        for (int i = 0; i < 6; i++)
            string_set(ls, i, word_from_char((unsigned char)lsrc[i]));
        word lambda_sym = ptr_to_word(ls);

        // Collect params and values from bindings
        word param_list = word_nil();
        word val_list = word_nil();
        word* prev_param = NULL;
        word* prev_val = NULL;
        word cur = bindings;
        while (is_ptr(cur) && obj_type(ptr_from_word(cur)) == OBJ_TYPE_PAIR) {
            word* bhdr = ptr_from_word(pair_car(ptr_from_word(cur)));
            word param = pair_car(bhdr);
            word val = pair_car(ptr_from_word(pair_cdr(bhdr)));

            word* pp = vm->gc->alloc_words(4); obj_set_type(pp, OBJ_TYPE_PAIR);
            pair_car(pp) = param; pair_cdr(pp) = word_nil();
            if (prev_param) pair_cdr(prev_param) = ptr_to_word(pp);
            else param_list = ptr_to_word(pp);
            prev_param = pp;

            word* vp = vm->gc->alloc_words(4); obj_set_type(vp, OBJ_TYPE_PAIR);
            pair_car(vp) = val; pair_cdr(vp) = word_nil();
            if (prev_val) pair_cdr(prev_val) = ptr_to_word(vp);
            else val_list = ptr_to_word(vp);
            prev_val = vp;

            cur = pair_cdr(ptr_from_word(cur));
        }

        // Build: ((lambda (params) body) val1 val2 ...)
        word* body_pair = vm->gc->alloc_words(4); obj_set_type(body_pair, OBJ_TYPE_PAIR);
        pair_car(body_pair) = body; pair_cdr(body_pair) = word_nil();

        word* args_pair = vm->gc->alloc_words(4); obj_set_type(args_pair, OBJ_TYPE_PAIR);
        pair_car(args_pair) = param_list; pair_cdr(args_pair) = ptr_to_word(body_pair);

        word* lambda_pair = vm->gc->alloc_words(4); obj_set_type(lambda_pair, OBJ_TYPE_PAIR);
        pair_car(lambda_pair) = lambda_sym; pair_cdr(lambda_pair) = ptr_to_word(args_pair);

        word* call_pair = vm->gc->alloc_words(4); obj_set_type(call_pair, OBJ_TYPE_PAIR);
        pair_car(call_pair) = ptr_to_word(lambda_pair); pair_cdr(call_pair) = val_list;

        compile_expr_to_buf(buf, vm, ptr_to_word(call_pair), scope);
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
