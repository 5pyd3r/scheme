#include "reader.h"
#include "debug.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
extern word bignum_from_string(vm_state_t* vm, const char* s, int radix);
extern word bignum_to_fixnum_or_box(word b);
extern word word_from_double(vm_state_t* vm, double d);

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
    // Boolean: #t / #f
    if (s[*pos] == '#' && s[*pos + 1] == 't') { *pos += 2; return word_true(); }
    if (s[*pos] == '#' && s[*pos + 1] == 'f') { *pos += 2; return word_false(); }

    // Character: #\name
    if (s[*pos] == '#' && s[*pos + 1] == '\\') {
        *pos += 2;
        // Named characters
        if (strncmp(s + *pos, "space", 5) == 0 && !isalnum(s[*pos + 5])) {
            *pos += 5;
            return word_from_char(' ');
        }
        if (strncmp(s + *pos, "newline", 7) == 0 && !isalnum(s[*pos + 7])) {
            *pos += 7;
            return word_from_char('\n');
        }
        if (strncmp(s + *pos, "tab", 3) == 0 && !isalnum(s[*pos + 3])) {
            *pos += 3;
            return word_from_char('\t');
        }
        if (strncmp(s + *pos, "return", 6) == 0 && !isalnum(s[*pos + 6])) {
            *pos += 6;
            return word_from_char('\r');
        }
        // Single character
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
        str[DATA_START_INDEX] = (word)len;
        for (int i = 0; i < len; i++)
            string_set(str, i, word_from_char((unsigned char)s[*pos + i]));
        *pos += len + 1;
        return ptr_to_word(str);
    }

    // Number (integer or flonum)
    int start = *pos;
    if (s[*pos] == '-') { (*pos)++; }
    else if (s[*pos] == '+') { (*pos)++; }
    if (isdigit(s[*pos]) || (s[*pos] == '.' && isdigit(s[*pos+1]))) {
        // Scan the full numeric token
        int is_float = 0;
        int scan = *pos;
        while (isdigit(s[scan])) scan++;
        if (s[scan] == '.') { is_float = 1; scan++; while (isdigit(s[scan])) scan++; }
        if (s[scan] == 'e' || s[scan] == 'E') {
            is_float = 1; scan++;
            if (s[scan] == '+' || s[scan] == '-') scan++;
            while (isdigit(s[scan])) scan++;
        }
        int end = scan;

        if (is_float) {
            // Parse as flonum
            char buf[128];
            int len = end - start;
            if (len >= 127) { vm->error_kind = 1; return word_nil(); }
            memcpy(buf, s + start, (size_t)len);
            buf[len] = '\0';
            *pos = end;
            double d = strtod(buf, NULL);
            return word_from_double(vm, d);
        } else {
            // Parse as integer (fixnum or bignum)
            char buf[64];
            int len = end - start;
            if (len >= 63) { vm->error_kind = 1; return word_nil(); }
            memcpy(buf, s + start, (size_t)len);
            buf[len] = '\0';
            *pos = end;
            word bn = bignum_from_string(vm, buf, 10);
            return bignum_to_fixnum_or_box(bn);
        }
    }
    *pos = start;  // Not a number, rewind

    // Symbol
    if (isalpha(s[*pos]) || strchr("!$%&*+-./:<=>?@^_~", s[*pos])) {
        int len = 0;
        int scan = *pos;
        while (s[scan] && !isspace(s[scan]) && s[scan] != '(' && s[scan] != ')' &&
               s[scan] != '"' && s[scan] != ';') {
            scan++; len++;
        }
        word sym = vm_intern(vm, s + *pos, len);
        *pos += len;
        return sym;
    }

    VM_ERROR(vm, ERR_READ, "unrecognized token", word_from_char((unsigned char)s[*pos]));
    return word_nil();
}

/* Return non-zero if c can be part of a Scheme symbol (after the first char). */
static int is_sym_char(char c) {
    return isalpha((unsigned char)c) || isdigit((unsigned char)c) ||
           (c && strchr("!$%&*+-./:<=>?@^_~", c));
}

static word read_list_tail(vm_state_t* vm, const char* s, int* pos) {
    skip_ws(s, pos);

    if (s[*pos] == ')') {
        (*pos)++;
        return word_nil();
    }

    word car = read_expr(vm, s, pos);
    if (vm->error_kind != ERR_NONE) return word_nil();
    skip_ws(s, pos);

    word cdr;
    /* Only treat "." as dotted-pair notation when it stands alone.
       ". followed by a symbol char (e.g. "...", ".foo") is an identifier. */
    if (s[*pos] == '.' && !is_sym_char(s[*pos + 1])) {
        (*pos)++;
        skip_ws(s, pos);
        cdr = read_expr(vm, s, pos);
        skip_ws(s, pos);
        if (s[*pos] == ')') (*pos)++;
    } else if (s[*pos] == ')') {
        (*pos)++;
        cdr = word_nil();
    } else {
        cdr = read_list_tail(vm, s, pos);
    }

    word* pair = vm->gc->alloc_words(4);
    obj_set_type(pair, OBJ_TYPE_PAIR);
    pair_car(pair) = car;
    pair_cdr(pair) = cdr;
    return ptr_to_word(pair);
}

static word read_list(vm_state_t* vm, const char* s, int* pos) {
    (*pos)++;
    return read_list_tail(vm, s, pos);
}

static word read_expr(vm_state_t* vm, const char* s, int* pos) {
    skip_ws(s, pos);
    if (!s[*pos]) return word_eof();

    char c = s[*pos];
    if (c == '(')  return read_list(vm, s, pos);
    if (c == '#' && s[*pos + 1] == 'u' && s[*pos + 2] == '8' && s[*pos + 3] == '(') {
        *pos += 4;
        word lst = read_list_tail(vm, s, pos);
        // Convert list to bytevector via u8-list->bytevector
        size_t count = 0;
        word cur = lst;
        while (is_ptr(cur) && obj_type(ptr_from_word(cur)) == OBJ_TYPE_PAIR) {
            count++;
            cur = pair_cdr(ptr_from_word(cur));
        }
        word* bv = vm->gc->alloc_words(3 + (count + 7) / 8);
        obj_set_type(bv, OBJ_TYPE_BYTEVECTOR);
        bv[DATA_START_INDEX] = (word)count;
        cur = lst;
        for (size_t i = 0; i < count; i++) {
            word* p = ptr_from_word(cur);
            bytevector_data(bv)[i] = (uint8_t)(word_to_fixnum(pair_car(p)) & 0xFF);
            cur = pair_cdr(p);
        }
        return ptr_to_word(bv);
    }
    if (c == '\'') {
        (*pos)++;
        word expr = read_expr(vm, s, pos);
        word* pair2 = vm->gc->alloc_words(4);
        obj_set_type(pair2, OBJ_TYPE_PAIR);
        pair_car(pair2) = expr;
        pair_cdr(pair2) = word_nil();
        word* pair1 = vm->gc->alloc_words(4);
        obj_set_type(pair1, OBJ_TYPE_PAIR);
        word qsym = vm_intern(vm, "quote", 5);
        pair_car(pair1) = qsym;
        pair_cdr(pair1) = ptr_to_word(pair2);
        return ptr_to_word(pair1);
    }
    if (c == '`') {
        (*pos)++;
        word expr = read_expr(vm, s, pos);
        word* pair2 = vm->gc->alloc_words(4);
        obj_set_type(pair2, OBJ_TYPE_PAIR);
        pair_car(pair2) = expr; pair_cdr(pair2) = word_nil();
        word* pair1 = vm->gc->alloc_words(4);
        obj_set_type(pair1, OBJ_TYPE_PAIR);
        pair_car(pair1) = vm_intern(vm, "quasiquote", 10);
        pair_cdr(pair1) = ptr_to_word(pair2);
        return ptr_to_word(pair1);
    }
    if (c == ',' && s[*pos + 1] == '@') {
        *pos += 2;
        word expr = read_expr(vm, s, pos);
        word* pair2 = vm->gc->alloc_words(4);
        obj_set_type(pair2, OBJ_TYPE_PAIR);
        pair_car(pair2) = expr; pair_cdr(pair2) = word_nil();
        word* pair1 = vm->gc->alloc_words(4);
        obj_set_type(pair1, OBJ_TYPE_PAIR);
        pair_car(pair1) = vm_intern(vm, "unquote-splicing", 16);
        pair_cdr(pair1) = ptr_to_word(pair2);
        return ptr_to_word(pair1);
    }
    if (c == ',') {
        (*pos)++;
        word expr = read_expr(vm, s, pos);
        word* pair2 = vm->gc->alloc_words(4);
        obj_set_type(pair2, OBJ_TYPE_PAIR);
        pair_car(pair2) = expr; pair_cdr(pair2) = word_nil();
        word* pair1 = vm->gc->alloc_words(4);
        obj_set_type(pair1, OBJ_TYPE_PAIR);
        pair_car(pair1) = vm_intern(vm, "unquote", 7);
        pair_cdr(pair1) = ptr_to_word(pair2);
        return ptr_to_word(pair1);
    }
    return read_atom(vm, s, pos);
}

word read_sexp(vm_state_t* vm, const char* input, int* end_pos) {
    int pos = end_pos ? *end_pos : 0;
    word result = read_expr(vm, input, &pos);
    if (end_pos) *end_pos = pos;
    return result;
}
