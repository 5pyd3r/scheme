#include "compiler.h"
#include "debug.h"
#include "prim.h"
#include "vm/opcodes.h"
#include <stdio.h>

extern word prim_eval(vm_state_t* vm, int nargs);
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

// env alist: ((sym . slot) ...). Returns slot (>=1) or -1.
static int env_find(word env, word sym) {
    word cur = env;
    while (is_ptr(cur) && obj_type(ptr_from_word(cur)) == OBJ_TYPE_PAIR) {
        word* hdr = ptr_from_word(cur);
        word entry = pair_car(hdr);
        if (is_ptr(entry) && obj_type(ptr_from_word(entry)) == OBJ_TYPE_PAIR) {
            word* eh = ptr_from_word(entry);
            word entry_sym = pair_car(eh);
            if (is_ptr(entry_sym) && is_ptr(sym) &&
                obj_type(ptr_from_word(entry_sym)) == OBJ_TYPE_SYMBOL &&
                obj_type(ptr_from_word(sym)) == OBJ_TYPE_SYMBOL) {
                word* s1 = ptr_from_word(entry_sym);
                word* s2 = ptr_from_word(sym);
                int l1 = (int)string_length(s1);
                int l2 = (int)string_length(s2);
                if (l1 == l2) {
                    int match = 1;
                    for (int i = 0; i < l1; i++)
                        if (string_ref(s1, i) != string_ref(s2, i)) { match = 0; break; }
                    if (match) {
                        word sw = pair_cdr(eh);
                        if (is_fixnum(sw)) return (int)word_to_fixnum(sw);
                    }
                }
            }
        }
        cur = pair_cdr(hdr);
    }
    return -1;
}

// Returns true if sym is eq?-equivalent to any symbol in the list
static bool sym_in_list(word sym, word list) {
    word cur = list;
    while (is_ptr(cur) && obj_type(ptr_from_word(cur)) == OBJ_TYPE_PAIR) {
        word item = pair_car(ptr_from_word(cur));
        if (is_ptr(item) && is_ptr(sym) &&
            obj_type(ptr_from_word(item)) == OBJ_TYPE_SYMBOL &&
            obj_type(ptr_from_word(sym)) == OBJ_TYPE_SYMBOL) {
            word* s1 = ptr_from_word(item);
            word* s2 = ptr_from_word(sym);
            int l1 = (int)string_length(s1), l2 = (int)string_length(s2);
            if (l1 == l2) {
                int match = 1;
                for (int i = 0; i < l1; i++)
                    if (string_ref(s1, i) != string_ref(s2, i)) { match = 0; break; }
                if (match) return true;
            }
        }
        cur = pair_cdr(ptr_from_word(cur));
    }
    return false;
}

// Recursively collects free symbols from expr. Skips params, primitives, duplicates.
// collected is a reversed list of symbols (built with cons).
static void collect_free_vars_inner(vm_state_t* vm, word expr, word params, word env, word* collected) {
    if (is_fixnum(expr) || is_char(expr) || is_nil(expr) || is_true(expr) || is_false(expr))
        return;
    if (!is_ptr(expr)) return;
    word* hdr = ptr_from_word(expr);
    int type = obj_type(hdr);

    if (type == OBJ_TYPE_SYMBOL) {
        if (sym_in_list(expr, params)) return;
        if (sym_in_list(expr, *collected)) return;
        char name[64]; int nlen = (int)string_length(hdr);
        if (nlen < 63) {
            for (int i = 0; i < nlen; i++) name[i] = (char)word_to_char(string_ref(hdr, i));
            name[nlen] = '\0';
            if (prim_lookup(name) >= 0) return;
        }
        if (env_find(env, expr) < 0) return;
        word* pp = vm->gc->alloc_words(4);
        obj_set_type(pp, OBJ_TYPE_PAIR);
        pair_car(pp) = expr; pair_cdr(pp) = *collected;
        *collected = ptr_to_word(pp);
        return;
    }

    if (type == OBJ_TYPE_PAIR) {
        word head = pair_car(hdr);
        // Handle inner lambda/let: extract inner params, only walk body
        if (is_ptr(head) && obj_type(ptr_from_word(head)) == OBJ_TYPE_SYMBOL) {
            if (is_symbol(head, "lambda") || is_symbol(head, "let")) {
                word rest = pair_cdr(hdr);  // ((params) body ...)
                if (!is_ptr(rest) || obj_type(ptr_from_word(rest)) != OBJ_TYPE_PAIR) return;
                word* rhdr = ptr_from_word(rest);
                word inner_formals = pair_car(rhdr);
                word body_pair = pair_cdr(rhdr);  // (body ...) — we walk this list

                // Build merged params list (inner params + outer params)
                word merged = params;
                if (is_symbol(head, "lambda")) {
                    // inner_formals is a list of symbols
                    word pcur = inner_formals;
                    while (is_ptr(pcur) && obj_type(ptr_from_word(pcur)) == OBJ_TYPE_PAIR) {
                        word* ph = ptr_from_word(pcur);
                        word* pp = vm->gc->alloc_words(4);
                        obj_set_type(pp, OBJ_TYPE_PAIR);
                        pair_car(pp) = pair_car(ph); pair_cdr(pp) = merged;
                        merged = ptr_to_word(pp);
                        pcur = pair_cdr(ph);
                    }
                } else {
                    // let: extract param names from ((param val) ...)
                    word bcur = inner_formals;
                    while (is_ptr(bcur) && obj_type(ptr_from_word(bcur)) == OBJ_TYPE_PAIR) {
                        word* bh = ptr_from_word(bcur);
                        word binding = pair_car(bh);
                        if (is_ptr(binding) && obj_type(ptr_from_word(binding)) == OBJ_TYPE_PAIR) {
                            word* bih = ptr_from_word(binding);
                            word* pp = vm->gc->alloc_words(4);
                            obj_set_type(pp, OBJ_TYPE_PAIR);
                            pair_car(pp) = pair_car(bih); pair_cdr(pp) = merged;
                            merged = ptr_to_word(pp);
                        }
                        bcur = pair_cdr(bh);
                    }
                }
                // Walk only body with merged params filter
                collect_free_vars_inner(vm, body_pair, merged, env, collected);
                return;
            }
        }
        collect_free_vars_inner(vm, head, params, env, collected);
        collect_free_vars_inner(vm, pair_cdr(hdr), params, env, collected);
    }
}

// Returns alist of (sym . captured_slot) for each free variable found, slots start at 1 (VM: fp[1..nfree]=captured)
static word collect_free_vars(vm_state_t* vm, word body, word params, word env, int nparams) {
    (void)nparams;
    word collected = word_nil();
    collect_free_vars_inner(vm, body, params, env, &collected);
    // collected is reversed source-order list. Reverse to get source-order.
    word ordered = word_nil();
    word cur = collected;
    while (is_ptr(cur) && obj_type(ptr_from_word(cur)) == OBJ_TYPE_PAIR) {
        word* hdr = ptr_from_word(cur);
        word* pp = vm->gc->alloc_words(4); obj_set_type(pp, OBJ_TYPE_PAIR);
        pair_car(pp) = pair_car(hdr); pair_cdr(pp) = ordered;
        ordered = ptr_to_word(pp);
        cur = pair_cdr(hdr);
    }
    // Build bindings alist with slots 1..nfree (captured vars go first in frame)
    int slot = 1;
    word bindings = word_nil();
    cur = ordered;
    while (is_ptr(cur) && obj_type(ptr_from_word(cur)) == OBJ_TYPE_PAIR) {
        word* hdr = ptr_from_word(cur);
        word* entry = vm->gc->alloc_words(4); obj_set_type(entry, OBJ_TYPE_PAIR);
        pair_car(entry) = pair_car(hdr); pair_cdr(entry) = word_from_fixnum(slot);
        word* bp = vm->gc->alloc_words(4); obj_set_type(bp, OBJ_TYPE_PAIR);
        pair_car(bp) = ptr_to_word(entry); pair_cdr(bp) = bindings;
        bindings = ptr_to_word(bp);
        slot++; cur = pair_cdr(hdr);
    }
    return bindings;
}

static void compile_lambda(code_buf_t* buf, vm_state_t* vm, word args, word body, word env);

static void compile_expr_to_buf(code_buf_t* buf, vm_state_t* vm, word expr, local_scope_t* scope, word env);

// quasiquote expansion: recursively transform template into constructor calls
static word qq_expand(vm_state_t* vm, word tmpl) {
    // Non-pair: wrap non-self-evaluating values in (quote ...)
    if (!is_ptr(tmpl) || obj_type(ptr_from_word(tmpl)) != OBJ_TYPE_PAIR) {
        // Self-evaluating: fixnum, boolean, char, nil
        if (is_fixnum(tmpl) || is_true(tmpl) || is_false(tmpl) || is_char(tmpl) || is_nil(tmpl))
            return tmpl;
        // Otherwise wrap in (quote tmpl)
        word* qsym = vm->gc->alloc_words(3 + 5); obj_set_type(qsym, OBJ_TYPE_SYMBOL);
        qsym[DATA_START_INDEX] = (word)5;
        for (int i = 0; i < 5; i++) string_set(qsym, i, word_from_char((unsigned char)"quote"[i]));
        word* qp = vm->gc->alloc_words(4); obj_set_type(qp, OBJ_TYPE_PAIR);
        pair_car(qp) = tmpl; pair_cdr(qp) = word_nil();
        word* qform = vm->gc->alloc_words(4); obj_set_type(qform, OBJ_TYPE_PAIR);
        pair_car(qform) = ptr_to_word(qsym); pair_cdr(qform) = ptr_to_word(qp);
        return ptr_to_word(qform);
    }

    // Pair — check for unquote/unquote-splicing
    word* hdr = ptr_from_word(tmpl);
    word head = pair_car(hdr);
    word tail = pair_cdr(hdr);

    // head is the car — check if it's the symbol unquote/unquote-splicing
    if (is_ptr(head) && obj_type(ptr_from_word(head)) == OBJ_TYPE_SYMBOL) {
        word* ohdr = ptr_from_word(head);
        int olen = (int)string_length(ohdr);
        if (olen == 7) {
            char oname[8]; for (int i = 0; i < 7; i++) oname[i] = (char)word_to_char(string_ref(ohdr, i));
            oname[7] = '\0';
            if (strcmp(oname, "unquote") == 0)
                return pair_car(ptr_from_word(tail));
        }
        if (olen == 16) {
            char oname[17]; for (int i = 0; i < 16; i++) oname[i] = (char)word_to_char(string_ref(ohdr, i));
            oname[16] = '\0';
            if (strcmp(oname, "unquote-splicing") == 0)
                return pair_car(ptr_from_word(tail));
        }
    }

    // Regular pair — build (cons (qq a) (qq b))
    word expanded_a = qq_expand(vm, head);
    word expanded_b = qq_expand(vm, tail);

    word* csym = vm->gc->alloc_words(3 + 4); obj_set_type(csym, OBJ_TYPE_SYMBOL);
    csym[DATA_START_INDEX] = (word)4;
    for (int i = 0; i < 4; i++) string_set(csym, i, word_from_char((unsigned char)"cons"[i]));
    word* c_rest = vm->gc->alloc_words(4); obj_set_type(c_rest, OBJ_TYPE_PAIR);
    pair_car(c_rest) = expanded_b; pair_cdr(c_rest) = word_nil();
    word* c_args = vm->gc->alloc_words(4); obj_set_type(c_args, OBJ_TYPE_PAIR);
    pair_car(c_args) = expanded_a; pair_cdr(c_args) = ptr_to_word(c_rest);
    word* c_form = vm->gc->alloc_words(4); obj_set_type(c_form, OBJ_TYPE_PAIR);
    pair_car(c_form) = ptr_to_word(csym); pair_cdr(c_form) = ptr_to_word(c_args);
    return ptr_to_word(c_form);
}

static void compile_lambda(code_buf_t* buf, vm_state_t* vm, word args, word body, word env) {
    local_scope_t lambda_scope = {0};
    // Extract params, building both scope names and a Scheme list
    int param_idx = 0;
    int nfixed = 0;  // number of fixed params (non-rest), 0 for non-dotted
    word rest_param = 0;  // rest param symbol (or 0 if non-dotted)
    word cur = args;
    word param_list = word_nil();  // reversed list of param symbols
    while (is_ptr(cur) && obj_type(ptr_from_word(cur)) == OBJ_TYPE_PAIR && param_idx < MAX_LOCALS) {
        word param_sym = pair_car(ptr_from_word(cur));
        word* shdr = ptr_from_word(param_sym);
        int plen = (int)string_length(shdr);
        char* pname = (char*)malloc(plen + 1);
        for (int i = 0; i < plen; i++)
            pname[i] = (char)word_to_char(string_ref(shdr, i));
        pname[plen] = '\0';
        lambda_scope.names[param_idx++] = pname;
        // Build param_list (reversed — will be reversed back in collect_free_vars)
        word* pp = vm->gc->alloc_words(4); obj_set_type(pp, OBJ_TYPE_PAIR);
        pair_car(pp) = param_sym; pair_cdr(pp) = param_list;
        param_list = ptr_to_word(pp);
        cur = pair_cdr(ptr_from_word(cur));
    }
    // Check for dotted-tail: if cur is a symbol, it's the rest param
    if (is_ptr(cur) && obj_type(ptr_from_word(cur)) == OBJ_TYPE_SYMBOL && param_idx < MAX_LOCALS) {
        nfixed = param_idx;
        rest_param = cur;
        word* shdr2 = ptr_from_word(rest_param);
        int plen2 = (int)string_length(shdr2);
        char* pname2 = (char*)malloc(plen2 + 1);
        for (int i = 0; i < plen2; i++)
            pname2[i] = (char)word_to_char(string_ref(shdr2, i));
        pname2[plen2] = '\0';
        lambda_scope.names[param_idx++] = pname2;
        word* pp2 = vm->gc->alloc_words(4); obj_set_type(pp2, OBJ_TYPE_PAIR);
        pair_car(pp2) = rest_param; pair_cdr(pp2) = param_list;
        param_list = ptr_to_word(pp2);
    }
    lambda_scope.count = param_idx;

    // Detect free variables (symbols in body that are in env, not params, not primitives)
    word captured = collect_free_vars(vm, body, param_list, env, param_idx);

    // Count captured vars
    int nfree = 0;
    cur = captured;
    while (is_ptr(cur) && obj_type(ptr_from_word(cur)) == OBJ_TYPE_PAIR) {
        nfree++;
        cur = pair_cdr(ptr_from_word(cur));
    }

    // Build child env: params first (they shadow captured), then captured
    // Param slots: nfree+1 .. nfree+param_idx  (fp[nfree+1..nfree+n] = params)
    word child_env = captured;
    {
        int slot = nfree + param_idx;  // params in forward source order at highest slots
        cur = param_list;
        while (is_ptr(cur) && obj_type(ptr_from_word(cur)) == OBJ_TYPE_PAIR) {
            word* ph = ptr_from_word(cur);
            word* entry = vm->gc->alloc_words(4); obj_set_type(entry, OBJ_TYPE_PAIR);
            pair_car(entry) = pair_car(ph); pair_cdr(entry) = word_from_fixnum(slot);
            word* bp = vm->gc->alloc_words(4); obj_set_type(bp, OBJ_TYPE_PAIR);
            pair_car(bp) = ptr_to_word(entry); pair_cdr(bp) = child_env;
            child_env = ptr_to_word(bp);
            slot--; cur = pair_cdr(ph);
        }
    }

    // Emit LREF for each captured var (pushes parent-frame values onto stack, becomes fp[1..nfree])
    {
        // captured is in source order, slots are 1..nfree
        // Reverse to get correct emission order (slot 1 first)
        word rev = word_nil();
        cur = captured;
        while (is_ptr(cur) && obj_type(ptr_from_word(cur)) == OBJ_TYPE_PAIR) {
            word* hdr = ptr_from_word(cur);
            word* pp = vm->gc->alloc_words(4); obj_set_type(pp, OBJ_TYPE_PAIR);
            pair_car(pp) = pair_cdr(ptr_from_word(pair_car(hdr)));  // slot (fixnum)
            pair_cdr(pp) = rev;
            rev = ptr_to_word(pp);
            cur = pair_cdr(hdr);
        }
        cur = rev;
        while (is_ptr(cur) && obj_type(ptr_from_word(cur)) == OBJ_TYPE_PAIR) {
            word* hdr = ptr_from_word(cur);
            int slot = (int)word_to_fixnum(pair_car(hdr));
            emit_byte(buf, OP_LREF);
            emit_byte(buf, (uint8_t)slot);
            cur = pair_cdr(hdr);
        }
    }

    // Compile body to child code buffer with child_env
    code_buf_t child_buf = {0};
    compile_expr_to_buf(&child_buf, vm, body, &lambda_scope, child_env);
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
        emit_byte(buf, OP_PUSH_NIL);
        for (int i = 0; i < param_idx; i++) free((void*)lambda_scope.names[i]);
        return;
    }

    // Emit CLOSE: code_idx (2B little-endian), nfree (1B), nfixed (1B, bit7=dotted)
    emit_byte(buf, OP_CLOSE);
    emit_byte(buf, (uint8_t)(code_idx & 0xFF));
    emit_byte(buf, (uint8_t)((code_idx >> 8) & 0xFF));
    emit_byte(buf, (uint8_t)nfree);
    if (nfixed > 0 || rest_param) {
        // dotted-tail: set bit 7 as dotted flag
        emit_byte(buf, (uint8_t)(0x80 | (nfixed & 0x7F)));
    } else {
        emit_byte(buf, 0);
    }

    // Clean up param names
    for (int i = 0; i < param_idx; i++) free((void*)lambda_scope.names[i]);
}

static void compile_list(code_buf_t* buf, vm_state_t* vm, word expr, local_scope_t* scope, word env) {
    (void)env;
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

    if (is_symbol(fn, "quasiquote")) {
        // Expand (quasiquote template) to constructor calls, then compile
        word tmpl = pair_car(ptr_from_word(args));
        word expanded = qq_expand(vm, tmpl);
        compile_expr_to_buf(buf, vm, expanded, scope, env);
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
            compile_lambda(buf, vm, form_args, form_body, env);

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
        compile_expr_to_buf(buf, vm, val_expr, scope, env);
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

        compile_expr_to_buf(buf, vm, test, scope, env);
        int jmp_false_pos = buf->len;
        emit_byte(buf, OP_JMP_IF_NOT);
        emit_byte(buf, 0); emit_byte(buf, 0);

        compile_expr_to_buf(buf, vm, then_expr, scope, env);
        int jmp_end_pos = buf->len;
        emit_byte(buf, OP_JMP);
        emit_byte(buf, 0); emit_byte(buf, 0);

        int false_start = buf->len;
        int false_offset = false_start - (jmp_false_pos + 3);
        buf->bytes[jmp_false_pos + 1] = (uint8_t)(false_offset & 0xFF);
        buf->bytes[jmp_false_pos + 2] = (uint8_t)((false_offset >> 8) & 0xFF);

        if (!is_nil(else_expr))
            compile_expr_to_buf(buf, vm, else_expr, scope, env);

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
        compile_lambda(buf, vm, params, body, env);
        return;
    }

    if (is_symbol(fn, "set!")) {
        word name_sym = pair_car(ptr_from_word(args));
        word val_expr = pair_car(ptr_from_word(pair_cdr(ptr_from_word(args))));
        // Compile value
        compile_expr_to_buf(buf, vm, val_expr, scope, env);
        // Check scope first
        if (scope) {
            int local_idx1 = local_find(scope, name_sym);
            if (local_idx1 > 0) {
                emit_byte(buf, OP_LSET);
                emit_byte(buf, (uint8_t)local_idx1);
                return;
            }
        }
        // Check env
        int env_slot1 = env_find(env, name_sym);
        if (env_slot1 > 0) {
            emit_byte(buf, OP_LSET);
            emit_byte(buf, (uint8_t)env_slot1);
            return;
        }
        // Global
        int slot1 = vm_find_global_slot(vm, name_sym);
        if (slot1 < 0) {
            slot1 = vm->next_global_slot++;
            if (slot1 >= (int)vm->global_count) {
                size_t new_count = vm->global_count * 2;
                vm->globals = realloc(vm->globals, new_count * sizeof(word));
                vm->global_names = realloc(vm->global_names, new_count * sizeof(word));
                vm->global_count = new_count;
            }
            vm->global_names[slot1] = name_sym;
        }
        emit_byte(buf, OP_GSET);
        emit_byte(buf, (uint8_t)slot1);
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
            compile_expr_to_buf(buf, vm, expr, scope, env);
            emit_byte(buf, OP_POP);
            cur = pair_cdr(ptr_from_word(cur));
        }
        // Compile last expression (value stays on stack)
        last_val = pair_car(ptr_from_word(cur));
        compile_expr_to_buf(buf, vm, last_val, scope, env);
        return;
    }

    if (is_symbol(fn, "and")) {
        int count = 0;
        word cur = args;
        while (is_ptr(cur) && obj_type(ptr_from_word(cur)) == OBJ_TYPE_PAIR) {
            count++;
            cur = pair_cdr(ptr_from_word(cur));
        }
        if (count == 0) {
            compile_expr_to_buf(buf, vm, word_true(), scope, env);
            return;
        }
        if (count == 1) {
            compile_expr_to_buf(buf, vm, pair_car(ptr_from_word(args)), scope, env);
            return;
        }
        // Desugar (and e1 e2 ... en) → (if e1 (and e2 ... en) #f)
        // Build rest: (and e2 ... en)
        word* rest = vm->gc->alloc_words(4); obj_set_type(rest, OBJ_TYPE_PAIR);
        pair_car(rest) = fn;
        pair_cdr(rest) = pair_cdr(ptr_from_word(args));
        // Build (if e1 (and e2 ... en) #f)
        word* nf = vm->gc->alloc_words(4); obj_set_type(nf, OBJ_TYPE_PAIR);
        pair_car(nf) = word_false(); pair_cdr(nf) = word_nil();
        word* then_and_false = vm->gc->alloc_words(4); obj_set_type(then_and_false, OBJ_TYPE_PAIR);
        pair_car(then_and_false) = ptr_to_word(rest); pair_cdr(then_and_false) = ptr_to_word(nf);
        word* test_and_rest = vm->gc->alloc_words(4); obj_set_type(test_and_rest, OBJ_TYPE_PAIR);
        pair_car(test_and_rest) = pair_car(ptr_from_word(args));
        pair_cdr(test_and_rest) = ptr_to_word(then_and_false);
        word* if_form = vm->gc->alloc_words(4); obj_set_type(if_form, OBJ_TYPE_PAIR);
        word* is1 = vm->gc->alloc_words(3 + 2); obj_set_type(is1, OBJ_TYPE_SYMBOL);
        is1[DATA_START_INDEX] = (word)2; string_set(is1,0,word_from_char('i')); string_set(is1,1,word_from_char('f'));
        pair_car(if_form) = ptr_to_word(is1);
        pair_cdr(if_form) = ptr_to_word(test_and_rest);
        compile_list(buf, vm, ptr_to_word(if_form), scope, env);
        return;
    }

    if (is_symbol(fn, "or")) {
        int count = 0;
        word cur = args;
        while (is_ptr(cur) && obj_type(ptr_from_word(cur)) == OBJ_TYPE_PAIR) {
            count++;
            cur = pair_cdr(ptr_from_word(cur));
        }
        if (count == 0) {
            compile_expr_to_buf(buf, vm, word_false(), scope, env);
            return;
        }
        if (count == 1) {
            compile_expr_to_buf(buf, vm, pair_car(ptr_from_word(args)), scope, env);
            return;
        }
        // Compile (or e1 e2 ... en) using bytecodes:
        //   e1; DUP; JMP_IF end; POP; e2; DUP; JMP_IF end; POP; ...; en; end:
        int* jmp_positions = (int*)malloc(count * sizeof(int));
        cur = args;
        for (int i = 0; i < count - 1; i++) {
            compile_expr_to_buf(buf, vm, pair_car(ptr_from_word(cur)), scope, env);
            emit_byte(buf, OP_DUP);
            jmp_positions[i] = buf->len;
            emit_byte(buf, OP_JMP_IF);
            emit_byte(buf, 0); emit_byte(buf, 0);  // placeholder
            emit_byte(buf, OP_POP);
            cur = pair_cdr(ptr_from_word(cur));
        }
        // Last expression
        compile_expr_to_buf(buf, vm, pair_car(ptr_from_word(cur)), scope, env);
        // Patch all JMP_IF to point here
        int end_pos = buf->len;
        for (int i = 0; i < count - 1; i++) {
            int off = end_pos - (jmp_positions[i] + 3);
            if (off < 0) off += 65536;
            buf->bytes[jmp_positions[i] + 1] = (uint8_t)(off & 0xFF);
            buf->bytes[jmp_positions[i] + 2] = (uint8_t)((off >> 8) & 0xFF);
        }
        free(jmp_positions);
        return;
    }

    if (is_symbol(fn, "case")) {
        // (case key ((datums ...) body ...) ... (else else-body ...))
        // Desugar to: ((lambda (tmp) if-chain) key)
        word key_expr = pair_car(ptr_from_word(args));
        word clauses = pair_cdr(ptr_from_word(args));

        // Collect clauses into array for reverse iteration
        word carr[64];
        int nc = 0;
        word cc = clauses;
        while (is_ptr(cc) && obj_type(ptr_from_word(cc)) == OBJ_TYPE_PAIR && nc < 64) {
            carr[nc++] = pair_car(ptr_from_word(cc));
            cc = pair_cdr(ptr_from_word(cc));
        }

        // Build inner if-chain (last clause first, working backward)
        word inner = word_nil(); // default: nil (no else clause)
        for (int ci = nc - 1; ci >= 0; ci--) {
            word clause = carr[ci];
            word* chdr = ptr_from_word(clause);
            word test_part = pair_car(chdr);
            word body_part = pair_cdr(chdr);

            // Check for else clause
            int is_else = 0;
            if (is_ptr(test_part) && obj_type(ptr_from_word(test_part)) == OBJ_TYPE_SYMBOL) {
                word* thdr = ptr_from_word(test_part);
                if ((int)string_length(thdr) == 4 &&
                    word_to_char(string_ref(thdr,0))=='e' &&
                    word_to_char(string_ref(thdr,1))=='l' &&
                    word_to_char(string_ref(thdr,2))=='s' &&
                    word_to_char(string_ref(thdr,3))=='e')
                    is_else = 1;
            }

            // Build (begin body ...) — wrap in begin if multiple exps
            word body_form;
            if (is_ptr(body_part) && obj_type(ptr_from_word(body_part)) == OBJ_TYPE_PAIR &&
                is_ptr(pair_cdr(ptr_from_word(body_part))) &&
                obj_type(ptr_from_word(pair_cdr(ptr_from_word(body_part)))) == OBJ_TYPE_PAIR) {
                // Multiple body expressions — wrap in (begin ...)
                word* bsym = vm->gc->alloc_words(3 + 5); obj_set_type(bsym, OBJ_TYPE_SYMBOL);
                bsym[DATA_START_INDEX] = (word)5;
                for (int bi = 0; bi < 5; bi++)
                    string_set(bsym, bi, word_from_char((unsigned char)"begin"[bi]));
                word* bp = vm->gc->alloc_words(4); obj_set_type(bp, OBJ_TYPE_PAIR);
                pair_car(bp) = ptr_to_word(bsym); pair_cdr(bp) = body_part;
                body_form = ptr_to_word(bp);
            } else {
                body_form = pair_car(ptr_from_word(body_part));
            }

            if (is_else) {
                inner = body_form;
            } else {
                // Build (if (memv tmp (quote (datums ...))) body_form inner)
                // 1. Build (quote (datums ...))
                word* qsym = vm->gc->alloc_words(3 + 5); obj_set_type(qsym, OBJ_TYPE_SYMBOL);
                qsym[DATA_START_INDEX] = (word)5;
                for (int qi = 0; qi < 5; qi++)
                    string_set(qsym, qi, word_from_char((unsigned char)"quote"[qi]));
                word* qp = vm->gc->alloc_words(4); obj_set_type(qp, OBJ_TYPE_PAIR);
                pair_car(qp) = test_part; pair_cdr(qp) = word_nil();
                word* quoted = vm->gc->alloc_words(4); obj_set_type(quoted, OBJ_TYPE_PAIR);
                pair_car(quoted) = ptr_to_word(qsym); pair_cdr(quoted) = ptr_to_word(qp);

                // 2. Build (memv tmp <quoted>)
                // memv takes (key list) — key first, then list
                word* tmp_sym = vm->gc->alloc_words(3 + 3); obj_set_type(tmp_sym, OBJ_TYPE_SYMBOL);
                tmp_sym[DATA_START_INDEX] = (word)3;
                for (int ti = 0; ti < 3; ti++)
                    string_set(tmp_sym, ti, word_from_char((unsigned char)"tmp"[ti]));
                word* memv_sym = vm->gc->alloc_words(3 + 4); obj_set_type(memv_sym, OBJ_TYPE_SYMBOL);
                memv_sym[DATA_START_INDEX] = (word)4;
                for (int mi = 0; mi < 4; mi++)
                    string_set(memv_sym, mi, word_from_char((unsigned char)"memv"[mi]));
                // Build args list: (tmp quoted) — tmp first (key), quoted second (list)
                word* mv_rest = vm->gc->alloc_words(4); obj_set_type(mv_rest, OBJ_TYPE_PAIR);
                pair_car(mv_rest) = ptr_to_word(quoted); pair_cdr(mv_rest) = word_nil();
                word* mv_args = vm->gc->alloc_words(4); obj_set_type(mv_args, OBJ_TYPE_PAIR);
                pair_car(mv_args) = ptr_to_word(tmp_sym); pair_cdr(mv_args) = ptr_to_word(mv_rest);
                word* memv_call = vm->gc->alloc_words(4); obj_set_type(memv_call, OBJ_TYPE_PAIR);
                pair_car(memv_call) = ptr_to_word(memv_sym); pair_cdr(memv_call) = ptr_to_word(mv_args);

                // 3. Build (if <memv_call> body_form inner)
                // Structure: (if . (test . (consequent . (alternate . ()))))
                word* if_sym2 = vm->gc->alloc_words(3 + 2); obj_set_type(if_sym2, OBJ_TYPE_SYMBOL);
                if_sym2[DATA_START_INDEX] = (word)2;
                string_set(if_sym2,0,word_from_char('i'));
                string_set(if_sym2,1,word_from_char('f'));
                word* alt_cell = vm->gc->alloc_words(4); obj_set_type(alt_cell, OBJ_TYPE_PAIR);
                pair_car(alt_cell) = inner; pair_cdr(alt_cell) = word_nil();
                word* cons_cell = vm->gc->alloc_words(4); obj_set_type(cons_cell, OBJ_TYPE_PAIR);
                pair_car(cons_cell) = body_form; pair_cdr(cons_cell) = ptr_to_word(alt_cell);
                word* test_cell = vm->gc->alloc_words(4); obj_set_type(test_cell, OBJ_TYPE_PAIR);
                pair_car(test_cell) = ptr_to_word(memv_call); pair_cdr(test_cell) = ptr_to_word(cons_cell);
                word* if_form = vm->gc->alloc_words(4); obj_set_type(if_form, OBJ_TYPE_PAIR);
                pair_car(if_form) = ptr_to_word(if_sym2); pair_cdr(if_form) = ptr_to_word(test_cell);
                inner = ptr_to_word(if_form);
            }
        }

        // Build (lambda (tmp) inner)
        word* tmp_sym2 = vm->gc->alloc_words(3 + 3); obj_set_type(tmp_sym2, OBJ_TYPE_SYMBOL);
        tmp_sym2[DATA_START_INDEX] = (word)3;
        for (int ti2 = 0; ti2 < 3; ti2++)
            string_set(tmp_sym2, ti2, word_from_char((unsigned char)"tmp"[ti2]));
        word* l_sym = vm->gc->alloc_words(3 + 6); obj_set_type(l_sym, OBJ_TYPE_SYMBOL);
        l_sym[DATA_START_INDEX] = (word)6;
        for (int li = 0; li < 6; li++)
            string_set(l_sym, li, word_from_char((unsigned char)"lambda"[li]));
        word* params_pair = vm->gc->alloc_words(4); obj_set_type(params_pair, OBJ_TYPE_PAIR);
        pair_car(params_pair) = ptr_to_word(tmp_sym2); pair_cdr(params_pair) = word_nil();
        word* body_pair = vm->gc->alloc_words(4); obj_set_type(body_pair, OBJ_TYPE_PAIR);
        pair_car(body_pair) = inner; pair_cdr(body_pair) = word_nil();
        word* lambda_tail = vm->gc->alloc_words(4); obj_set_type(lambda_tail, OBJ_TYPE_PAIR);
        pair_car(lambda_tail) = ptr_to_word(params_pair); pair_cdr(lambda_tail) = ptr_to_word(body_pair);
        word* lambda_form = vm->gc->alloc_words(4); obj_set_type(lambda_form, OBJ_TYPE_PAIR);
        pair_car(lambda_form) = ptr_to_word(l_sym); pair_cdr(lambda_form) = ptr_to_word(lambda_tail);

        // Build ((lambda (tmp) inner) key-expr)
        word* key_pair = vm->gc->alloc_words(4); obj_set_type(key_pair, OBJ_TYPE_PAIR);
        pair_car(key_pair) = key_expr; pair_cdr(key_pair) = word_nil();
        word* call_form = vm->gc->alloc_words(4); obj_set_type(call_form, OBJ_TYPE_PAIR);
        pair_car(call_form) = ptr_to_word(lambda_form); pair_cdr(call_form) = ptr_to_word(key_pair);

        // Compile the call form
        compile_list(buf, vm, ptr_to_word(call_form), scope, env);
        return;
    }

    if (is_symbol(fn, "define-syntax")) {
        // (define-syntax name transformer-expr)
        // Evaluate transformer via prim_eval and store in *macro-table*
        word name_sym = pair_car(ptr_from_word(args));
        word trans_expr = pair_car(ptr_from_word(pair_cdr(ptr_from_word(args))));
        // Push trans_expr onto stack for prim_eval, save/restore all VM state
        word* saved_sp = vm->sp;
        uint8_t* saved_ip = vm->ip;
        word* saved_fp = vm->fp;
        word* saved_env = vm->env;
        word* saved_code = vm->current_code;
        *++vm->sp = trans_expr;
        word transformer = prim_eval(vm, 1);
        vm->sp = saved_sp;
        vm->ip = saved_ip;
        vm->fp = saved_fp;
        vm->env = saved_env;
        vm->current_code = saved_code;
        // Store: (set-car! *macro-table* (cons (cons name transformer) (car *macro-table*)))
        int mt_slot = vm_find_global_by_name(vm, "*macro-table*");
        if (mt_slot >= 0) {
            word mt_val = vm->globals[mt_slot];
            if (is_ptr(mt_val) && obj_type(ptr_from_word(mt_val)) == OBJ_TYPE_PAIR) {
                word* entry = vm->gc->alloc_words(4); obj_set_type(entry, OBJ_TYPE_PAIR);
                pair_car(entry) = name_sym; pair_cdr(entry) = transformer;
                word* entry_node = vm->gc->alloc_words(4); obj_set_type(entry_node, OBJ_TYPE_PAIR);
                pair_car(entry_node) = ptr_to_word(entry);
                pair_cdr(entry_node) = pair_car(ptr_from_word(mt_val));
                pair_car(ptr_from_word(mt_val)) = ptr_to_word(entry_node);
            }
        }
        emit_byte(buf, OP_PUSH_NIL);
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
            vm->error_kind = ERR_INTERNAL;
            return;
        }

        // Build nested if from last clause backwards
        word result = word_nil();
        for (int i = n - 1; i >= 0; i--) {
            word* chdr = ptr_from_word(clauses[i]);
            word test = pair_car(chdr);
            word body_list = pair_cdr(chdr);
            word* body_list_hdr = ptr_from_word(body_list);
            word body;
            if (is_ptr(pair_cdr(body_list_hdr)) &&
                obj_type(ptr_from_word(pair_cdr(body_list_hdr))) == OBJ_TYPE_PAIR) {
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

        compile_expr_to_buf(buf, vm, result, scope, env);
        return;
    }

    if (is_symbol(fn, "let*")) {
        word* ahdr = ptr_from_word(args);
        word bindings = pair_car(ahdr);
        word body_list = pair_cdr(ahdr);
        // If no bindings: (let* () body ...) → (begin body ...)
        if (is_nil(bindings)) {
            if (is_ptr(pair_cdr(ptr_from_word(body_list))) &&
                obj_type(ptr_from_word(pair_cdr(ptr_from_word(body_list)))) == OBJ_TYPE_PAIR) {
                word* bsym = vm->gc->alloc_words(3 + 5); obj_set_type(bsym, OBJ_TYPE_SYMBOL);
                bsym[DATA_START_INDEX] = (word)5;
                for (int bi = 0; bi < 5; bi++) string_set(bsym, bi, word_from_char((unsigned char)"begin"[bi]));
                word* bp = vm->gc->alloc_words(4); obj_set_type(bp, OBJ_TYPE_PAIR);
                pair_car(bp) = ptr_to_word(bsym); pair_cdr(bp) = body_list;
                compile_expr_to_buf(buf, vm, ptr_to_word(bp), scope, env);
            } else {
                compile_expr_to_buf(buf, vm, pair_car(ptr_from_word(body_list)), scope, env);
            }
            return;
        }
        // Single binding: (let* ((x v)) body) → (let ((x v)) body)
        if (is_nil(pair_cdr(ptr_from_word(bindings)))) {
            word* new_args = vm->gc->alloc_words(4); obj_set_type(new_args, OBJ_TYPE_PAIR);
            pair_car(new_args) = bindings; pair_cdr(new_args) = body_list;
            word* let_form = vm->gc->alloc_words(4); obj_set_type(let_form, OBJ_TYPE_PAIR);
            pair_car(let_form) = fn; pair_cdr(let_form) = ptr_to_word(new_args);
            // Actually call the 'let' handler — rewrite fn to "let"
            word* ls_sym = vm->gc->alloc_words(3 + 3); obj_set_type(ls_sym, OBJ_TYPE_SYMBOL);
            ls_sym[DATA_START_INDEX] = (word)3;
            for (int li = 0; li < 3; li++) string_set(ls_sym, li, word_from_char((unsigned char)"let"[li]));
            pair_car(let_form) = ptr_to_word(ls_sym);
            compile_list(buf, vm, ptr_to_word(let_form), scope, env);
            return;
        }
        // Multiple bindings: (let* ((x v1) (y v2)) body) → (let ((x v1)) (let* ((y v2)) body))
        word first_binding = pair_car(ptr_from_word(bindings));
        word rest_bindings = pair_cdr(ptr_from_word(bindings));
        word* single_bindings = vm->gc->alloc_words(4); obj_set_type(single_bindings, OBJ_TYPE_PAIR);
        pair_car(single_bindings) = first_binding; pair_cdr(single_bindings) = word_nil();
        // Build (let* (rest) body)
        word* rest_let_star_args = vm->gc->alloc_words(4); obj_set_type(rest_let_star_args, OBJ_TYPE_PAIR);
        pair_car(rest_let_star_args) = rest_bindings; pair_cdr(rest_let_star_args) = body_list;
        word* rest_let_star = vm->gc->alloc_words(4); obj_set_type(rest_let_star, OBJ_TYPE_PAIR);
        pair_car(rest_let_star) = fn; pair_cdr(rest_let_star) = ptr_to_word(rest_let_star_args);
        // Build inner body: ((let* (rest) body))
        word* inner_body = vm->gc->alloc_words(4); obj_set_type(inner_body, OBJ_TYPE_PAIR);
        pair_car(inner_body) = ptr_to_word(rest_let_star); pair_cdr(inner_body) = word_nil();
        // Build: (let ((x v1)) (let* (rest) body))
        word* let_bindings = vm->gc->alloc_words(4); obj_set_type(let_bindings, OBJ_TYPE_PAIR);
        pair_car(let_bindings) = ptr_to_word(single_bindings);
        pair_cdr(let_bindings) = ptr_to_word(inner_body);
        word* let_form2 = vm->gc->alloc_words(4); obj_set_type(let_form2, OBJ_TYPE_PAIR);
        word* ls_sym2 = vm->gc->alloc_words(3 + 3); obj_set_type(ls_sym2, OBJ_TYPE_SYMBOL);
        ls_sym2[DATA_START_INDEX] = (word)3;
        for (int li = 0; li < 3; li++) string_set(ls_sym2, li, word_from_char((unsigned char)"let"[li]));
        pair_car(let_form2) = ptr_to_word(ls_sym2);
        pair_cdr(let_form2) = ptr_to_word(let_bindings);
        compile_list(buf, vm, ptr_to_word(let_form2), scope, env);
        return;
    }

    if (is_symbol(fn, "letrec") || is_symbol(fn, "letrec*")) {
        // Desugar (letrec ((var val) ...) body ...) to (let ((var val) ...) body ...)
        word bindings = pair_car(ptr_from_word(args));
        word body_list = pair_cdr(ptr_from_word(args));
        word* lsym = vm->gc->alloc_words(3 + 3); obj_set_type(lsym, OBJ_TYPE_SYMBOL);
        lsym[DATA_START_INDEX] = (word)3;
        for (int li = 0; li < 3; li++)
            string_set(lsym, li, word_from_char((unsigned char)"let"[li]));
        word* new_args = vm->gc->alloc_words(4); obj_set_type(new_args, OBJ_TYPE_PAIR);
        pair_car(new_args) = bindings; pair_cdr(new_args) = body_list;
        word* let_form = vm->gc->alloc_words(4); obj_set_type(let_form, OBJ_TYPE_PAIR);
        pair_car(let_form) = ptr_to_word(lsym); pair_cdr(let_form) = ptr_to_word(new_args);
        compile_list(buf, vm, ptr_to_word(let_form), scope, env);
        return;
    }

    if (is_symbol(fn, "let")) {
        word* ahdr = ptr_from_word(args);
        word first = pair_car(ahdr);
        if (is_ptr(first) && obj_type(ptr_from_word(first)) == OBJ_TYPE_SYMBOL) {
            // Named let: desugar to a recursive helper that takes itself as first arg
            // (let name ((var val) ...) body ...)
            // → ((lambda (name-helper params ...) body-with-recursion) #f val ...)
            // where body-with-recursion replaces (name e ...) with (name-helper name-helper e ...)
            // Fallback for simplicity: just treat as regular let (name is discarded, recursion
            // works via global define if needed). Let's just collect params and compile.
            word name_sym2 = first;
            (void)name_sym2; // name is available but autorecursion not yet supported
            word* rest_hdr2 = ptr_from_word(pair_cdr(ahdr));
            first = pair_car(rest_hdr2); // the bindings list
            // body_list starts after bindings
            word body_list2 = pair_cdr(rest_hdr2);
            word bindings2 = first;
            // Use same desugaring as regular let but params+vars from bindings2, body from body_list2
            word* body_list_hdr2 = ptr_from_word(body_list2);
            word body2;
            if (is_ptr(pair_cdr(body_list_hdr2)) &&
                obj_type(ptr_from_word(pair_cdr(body_list_hdr2))) == OBJ_TYPE_PAIR) {
                word* bsym2 = vm->gc->alloc_words(3 + 5);
                obj_set_type(bsym2, OBJ_TYPE_SYMBOL); bsym2[DATA_START_INDEX] = (word)5;
                for (int bi2 = 0; bi2 < 5; bi2++)
                    string_set(bsym2, bi2, word_from_char((unsigned char)"begin"[bi2]));
                word* bp2 = vm->gc->alloc_words(4); obj_set_type(bp2, OBJ_TYPE_PAIR);
                pair_car(bp2) = ptr_to_word(bsym2); pair_cdr(bp2) = body_list2;
                body2 = ptr_to_word(bp2);
            } else {
                body2 = pair_car(body_list_hdr2);
            }
            word* ls2 = vm->gc->alloc_words(3 + 6); obj_set_type(ls2, OBJ_TYPE_SYMBOL);
            ls2[DATA_START_INDEX] = (word)6;
            for (int li2 = 0; li2 < 6; li2++)
                string_set(ls2, li2, word_from_char((unsigned char)"lambda"[li2]));
            word lambda_sym3 = ptr_to_word(ls2);
            word param_list2 = word_nil(); word val_list2 = word_nil();
            word* prev_param2 = NULL; word* prev_val2 = NULL;
            word cur3 = bindings2;
            while (is_ptr(cur3) && obj_type(ptr_from_word(cur3)) == OBJ_TYPE_PAIR) {
                word* bhdr3 = ptr_from_word(pair_car(ptr_from_word(cur3)));
                word param3 = pair_car(bhdr3);
                word val3 = pair_car(ptr_from_word(pair_cdr(bhdr3)));
                word* pp3 = vm->gc->alloc_words(4); obj_set_type(pp3, OBJ_TYPE_PAIR);
                pair_car(pp3) = param3; pair_cdr(pp3) = word_nil();
                if (prev_param2) pair_cdr(prev_param2) = ptr_to_word(pp3);
                else param_list2 = ptr_to_word(pp3);
                prev_param2 = pp3;
                word* vp3 = vm->gc->alloc_words(4); obj_set_type(vp3, OBJ_TYPE_PAIR);
                pair_car(vp3) = val3; pair_cdr(vp3) = word_nil();
                if (prev_val2) pair_cdr(prev_val2) = ptr_to_word(vp3);
                else val_list2 = ptr_to_word(vp3);
                prev_val2 = vp3;
                cur3 = pair_cdr(ptr_from_word(cur3));
            }
            word* body_pair3 = vm->gc->alloc_words(4); obj_set_type(body_pair3, OBJ_TYPE_PAIR);
            pair_car(body_pair3) = body2; pair_cdr(body_pair3) = word_nil();
            word* args_pair3 = vm->gc->alloc_words(4); obj_set_type(args_pair3, OBJ_TYPE_PAIR);
            pair_car(args_pair3) = param_list2; pair_cdr(args_pair3) = ptr_to_word(body_pair3);
            word* lp3 = vm->gc->alloc_words(4); obj_set_type(lp3, OBJ_TYPE_PAIR);
            pair_car(lp3) = lambda_sym3; pair_cdr(lp3) = ptr_to_word(args_pair3);
            word* cp3 = vm->gc->alloc_words(4); obj_set_type(cp3, OBJ_TYPE_PAIR);
            pair_car(cp3) = ptr_to_word(lp3); pair_cdr(cp3) = val_list2;
            compile_expr_to_buf(buf, vm, ptr_to_word(cp3), scope, env);
            return;
        }
        word bindings = first;
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

        compile_expr_to_buf(buf, vm, ptr_to_word(call_pair), scope, env);
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
        // Count args
        int nargs = 0;
        word acur = args;
        while (is_ptr(acur) && obj_type(ptr_from_word(acur)) == OBJ_TYPE_PAIR) {
            nargs++;
            acur = pair_cdr(ptr_from_word(acur));
        }
        // Check for apply: use OP_APPLY opcode instead of OP_PRIM_CALL
        if (is_symbol(fn, "apply")) {
            // First arg is the function. If it's a known primitive, push its
            // prim index as a fixnum (instead of GREF which returns nil for primitives).
            word apply_fn = pair_car(ptr_from_word(args));
            if (is_ptr(apply_fn) && obj_type(ptr_from_word(apply_fn)) == OBJ_TYPE_SYMBOL) {
                word* fhdr = ptr_from_word(apply_fn);
                int nlen = (int)string_length(fhdr);
                char fname[64];
                if (nlen < 63) {
                    for (int i = 0; i < nlen; i++)
                        fname[i] = (char)word_to_char(string_ref(fhdr, i));
                    fname[nlen] = '\0';
                    int apply_prim_idx = prim_lookup(fname);
                    if (apply_prim_idx >= 0) {
                        emit_byte(buf, OP_PUSH_INT);
                        int32_t pval = (int32_t)apply_prim_idx;
                        emit_byte(buf, (uint8_t)(pval & 0xFF));
                        emit_byte(buf, (uint8_t)((pval >> 8) & 0xFF));
                        emit_byte(buf, (uint8_t)((pval >> 16) & 0xFF));
                        emit_byte(buf, (uint8_t)((pval >> 24) & 0xFF));
                    } else {
                        compile_expr_to_buf(buf, vm, apply_fn, scope, env);
                    }
                }
            } else {
                compile_expr_to_buf(buf, vm, apply_fn, scope, env);
            }
            // Compile remaining args (individual args + list)
            word acur2 = pair_cdr(ptr_from_word(args));
            while (is_ptr(acur2) && obj_type(ptr_from_word(acur2)) == OBJ_TYPE_PAIR) {
                compile_expr_to_buf(buf, vm, pair_car(ptr_from_word(acur2)), scope, env);
                acur2 = pair_cdr(ptr_from_word(acur2));
            }
            emit_byte(buf, OP_APPLY);
            emit_byte(buf, (uint8_t)nargs);
        } else {
            word acur2 = args;
            while (is_ptr(acur2) && obj_type(ptr_from_word(acur2)) == OBJ_TYPE_PAIR) {
                compile_expr_to_buf(buf, vm, pair_car(ptr_from_word(acur2)), scope, env);
                acur2 = pair_cdr(ptr_from_word(acur2));
            }
            emit_byte(buf, OP_PRIM_CALL);
            emit_byte(buf, (uint8_t)nargs);
            emit_byte(buf, (uint8_t)(known_prim_idx & 0xFF));
            emit_byte(buf, (uint8_t)((known_prim_idx >> 8) & 0xFF));
        }
    } else {
        // User function call — compile args first, then fn (closure on top)
        // This ensures nested calls work: inner calls return, their results are below
        // the closure on the stack.
        word acur = args;
        int nargs = 0;
        while (is_ptr(acur) && obj_type(ptr_from_word(acur)) == OBJ_TYPE_PAIR) {
            compile_expr_to_buf(buf, vm, pair_car(ptr_from_word(acur)), scope, env);
            acur = pair_cdr(ptr_from_word(acur));
            nargs++;
        }
        compile_expr_to_buf(buf, vm, fn, scope, env);
        emit_byte(buf, OP_CALL);
        emit_byte(buf, (uint8_t)nargs);
    }
}

static void compile_expr_to_buf(code_buf_t* buf, vm_state_t* vm, word expr, local_scope_t* scope, word env) {
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
            compile_list(buf, vm, expr, scope, env);
            return;
        }
    }

    if (is_ptr(expr) && obj_type(ptr_from_word(expr)) == OBJ_TYPE_SYMBOL) {
        // Check env first (has correct frame-aware slots for both captured
        // vars and params when nfree > 0)
        if (!is_nil(env)) {
            int env_slot = env_find(env, expr);
            if (env_slot > 0) {
                emit_byte(buf, OP_LREF);
                emit_byte(buf, (uint8_t)env_slot);
                return;
            }
        }
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
    compile_expr_to_buf(&buf, vm, expr, NULL, word_nil());
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

int scheme_compile_and_assemble(vm_state_t* vm, word expr) {
    int compile_slot = vm_find_global_by_name(vm, "compile");
    if (compile_slot < 0) return -1;

    int asm_idx = prim_lookup("assemble-code");
    if (asm_idx < 0) return -1;

    uint8_t bc[32];
    int len = 0;
    bc[len++] = OP_PUSH_CONST; bc[len++] = 0;
    bc[len++] = OP_GREF;       bc[len++] = (uint8_t)compile_slot;
    bc[len++] = OP_CALL;       bc[len++] = 1;
    bc[len++] = OP_PRIM_CALL;  bc[len++] = 1;
    bc[len++] = (uint8_t)(asm_idx & 0xFF);
    bc[len++] = (uint8_t)((asm_idx >> 8) & 0xFF);
    bc[len++] = OP_HALT;

    size_t bc_words = ((size_t)len + sizeof(word) - 1) / sizeof(word);
    size_t total = 3 + bc_words + 1;
    word* obj = vm->gc->alloc_words(total);
    obj_set_type(obj, OBJ_TYPE_CODE);
    obj[2] = (word)len;
    memcpy(obj + 3, bc, (size_t)len);
    obj[3 + bc_words] = expr;

    int idx = vm_load_code(vm, obj);
    if (idx < 0) return -1;

    word result = vm_execute(vm, idx);
    if (is_fixnum(result))
        return (int)word_to_fixnum(result);
    return -1;
}

/* Expand a top-level macro call via _expand-once.
   Returns the expanded expression, or the original if not a macro. */
word scheme_expand_macro(vm_state_t* vm, word expr) {
    int expand_slot = vm_find_global_by_name(vm, "_expand-once");
    if (expand_slot < 0) return expr;

    uint8_t bc[16];
    int len = 0;
    bc[len++] = OP_PUSH_CONST; bc[len++] = 0;
    bc[len++] = OP_GREF;       bc[len++] = (uint8_t)expand_slot;
    bc[len++] = OP_CALL;       bc[len++] = 1;
    bc[len++] = OP_HALT;

    size_t bc_words = ((size_t)len + sizeof(word) - 1) / sizeof(word);
    size_t total = 3 + bc_words + 1;
    word* obj = vm->gc->alloc_words(total);
    obj_set_type(obj, OBJ_TYPE_CODE);
    obj[2] = (word)len;
    memcpy(obj + 3, bc, (size_t)len);
    obj[3 + bc_words] = expr;

    int idx = vm_load_code(vm, obj);
    if (idx < 0) return expr;

    /* Save VM state before executing the expander */
    word* saved_sp = vm->sp;
    uint8_t* saved_ip = vm->ip;
    word* saved_fp = vm->fp;
    word* saved_env = vm->env;
    word* saved_current = vm->current_code;

    word result = vm_execute(vm, idx);

    /* Restore VM state */
    vm->sp = saved_sp;
    vm->ip = saved_ip;
    vm->fp = saved_fp;
    vm->env = saved_env;
    vm->current_code = saved_current;

    if (is_ptr(result)) return result;
    if (is_fixnum(result)) return result;
    return expr;
}
