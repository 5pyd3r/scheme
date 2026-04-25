#include "reader.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static word read_expr(vm_state_t* vm, const char* s, int* pos);

static void skip_ws(const char* s, int* pos) {
    while (s[*pos]) {
        char c = s[*pos];
        if (c == ';') {
            while (s[*pos] && s[*pos] != '\n') (*pos)++;
        } else if (isspace(c)) {
            (*pos)++;
        } else {
            break;
        }
    }
}

static word read_atom(vm_state_t* vm, const char* s, int* pos) {
    int start = *pos;

    // Boolean: #t / #f
    if (s[*pos] == '#' && s[*pos + 1] == 't') { *pos += 2; return word_true(); }
    if (s[*pos] == '#' && s[*pos + 1] == 'f') { *pos += 2; return word_false(); }

    // Character: #\name
    if (s[*pos] == '#' && s[*pos + 1] == '\\') {
        *pos += 2;
        char c = s[*pos]; (*pos)++;
        return word_from_char((unsigned char)c);
    }

    // String: "..."
    if (s[*pos] == '"') {
        (*pos)++;
        int len = 0;
        int scan = *pos;
        while (s[scan] && s[scan] != '"') {
            scan++; len++;
        }
        size_t nwords = 3 + len;
        word* str = vm->gc->alloc_words(nwords);
        obj_set_type(str, OBJ_TYPE_STRING);
        string_set(str, 0, (word)len);
        for (int i = 0; i < len; i++)
            string_set(str, i, word_from_char((unsigned char)s[*pos + i]));
        *pos += len + 1;
        return ptr_to_word(str);
    }

    // Number (integer)
    int neg = 0;
    if (s[*pos] == '-') { neg = 1; (*pos)++; }
    if (isdigit(s[*pos])) {
        int64_t val = 0;
        while (isdigit(s[*pos])) {
            val = val * 10 + (s[*pos] - '0');
            (*pos)++;
        }
        if (neg) val = -val;
        return word_from_fixnum(val);
    }
    if (neg) (*pos)--;

    // Symbol
    if (isalpha(s[*pos]) || strchr("!$%&*+-./:<=>?@^_~", s[*pos])) {
        int len = 0;
        int scan = *pos;
        while (s[scan] && !isspace(s[scan]) && s[scan] != '(' && s[scan] != ')' &&
               s[scan] != '"' && s[scan] != ';') {
            scan++; len++;
        }
        size_t nwords = 3 + len;
        word* sym = vm->gc->alloc_words(nwords);
        obj_set_type(sym, OBJ_TYPE_SYMBOL);
        string_set(sym, 0, (word)len);
        for (int i = 0; i < len; i++)
            string_set(sym, i, word_from_char((unsigned char)s[*pos + i]));
        *pos += len;
        return ptr_to_word(sym);
    }

    vm->error_code = 1;
    return word_nil();
}

static word read_list(vm_state_t* vm, const char* s, int* pos) {
    (*pos)++;
    skip_ws(s, pos);

    if (s[*pos] == ')') {
        (*pos)++;
        return word_nil();
    }

    word car = read_expr(vm, s, pos);
    if (vm->error_code) return word_nil();
    skip_ws(s, pos);

    word cdr;
    if (s[*pos] == '.') {
        (*pos)++;
        skip_ws(s, pos);
        cdr = read_expr(vm, s, pos);
        skip_ws(s, pos);
        if (s[*pos] == ')') (*pos)++;
    } else if (s[*pos] == ')') {
        (*pos)++;
        cdr = word_nil();
    } else {
        cdr = read_list(vm, s, pos);
    }

    word* pair = vm->gc->alloc_words(4);
    obj_set_type(pair, OBJ_TYPE_PAIR);
    pair_car(pair) = car;
    pair_cdr(pair) = cdr;
    return ptr_to_word(pair);
}

static word read_expr(vm_state_t* vm, const char* s, int* pos) {
    skip_ws(s, pos);
    if (!s[*pos]) return word_eof();

    char c = s[*pos];
    if (c == '(')  return read_list(vm, s, pos);
    if (c == '\'') {
        (*pos)++;
        word expr = read_expr(vm, s, pos);
        word* pair2 = vm->gc->alloc_words(4);
        obj_set_type(pair2, OBJ_TYPE_PAIR);
        pair_car(pair2) = expr;
        pair_cdr(pair2) = word_nil();
        word* pair1 = vm->gc->alloc_words(4);
        obj_set_type(pair1, OBJ_TYPE_PAIR);
        word* qsym = vm->gc->alloc_words(3 + 5);
        obj_set_type(qsym, OBJ_TYPE_SYMBOL);
        string_set(qsym, 0, (word)5);
        const char* q = "quote";
        for (int i = 0; i < 5; i++)
            string_set(qsym, i, word_from_char((unsigned char)q[i]));
        pair_car(pair1) = ptr_to_word(qsym);
        pair_cdr(pair1) = ptr_to_word(pair2);
        return ptr_to_word(pair1);
    }
    return read_atom(vm, s, pos);
}

word read_sexp(vm_state_t* vm, const char* input, int* end_pos) {
    int pos = 0;
    word result = read_expr(vm, input, &pos);
    if (end_pos) *end_pos = pos;
    return result;
}
