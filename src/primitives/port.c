#include "prim.h"
#include "debug.h"
#include "reader.h"
#include <stdio.h>
#include <stdlib.h>

extern double word_to_double(word w);
extern char* bignum_to_string(vm_state_t* vm, word b, int radix);

word prim_display(vm_state_t* vm, int nargs) {
    if (nargs != 1) { VM_ERROR(vm, ERR_ARITY, "display: expected 1 argument", word_nil()); return word_nil(); }
    word w = vm->sp[0];
    if (is_fixnum(w)) {
        printf("%lld", (long long)word_to_fixnum(w));
    } else if (is_char(w)) {
        putchar(word_to_char(w));
    } else if (is_ptr(w)) {
        word* hdr = ptr_from_word(w);
        switch (obj_type(hdr)) {
        case OBJ_TYPE_STRING: {
            int len = (int)string_length(hdr);
            for (int i = 0; i < len; i++)
                putchar(word_to_char(string_ref(hdr, i)));
            break;
        }
        case OBJ_TYPE_SYMBOL: {
            int len = (int)string_length(hdr);
            for (int i = 0; i < len; i++)
                putchar(word_to_char(string_ref(hdr, i)));
            break;
        }
        case OBJ_TYPE_PAIR:
            printf("#<pair>");
            break;
        case OBJ_TYPE_BIGNUM: {
            char* s = bignum_to_string(vm, w, 10);
            printf("%s", s);
            free(s);
            break;
        }
        case OBJ_TYPE_FLONUM: {
            double d = word_to_double(w);
            if (d == (long long)d && d != -9223372036854775808.0)
                printf("%lld.0", (long long)d);
            else
                printf("%.17g", d);
            break;
        }
        default:
            printf("#<obj>");
            break;
        }
    } else if (is_imm(w)) {
        if (is_true(w)) printf("#t");
        else if (is_false(w)) printf("#f");
        else if (is_nil(w)) printf("()");
    }
    fflush(stdout);
    return word_nil();
}

word prim_newline(vm_state_t* vm, int nargs) {
    (void)vm;
    (void)nargs;
    putchar('\n');
    fflush(stdout);
    return word_nil();
}

word prim_read(vm_state_t* vm, int nargs) {
    if (nargs != 0) { vm->error_kind = 1; return word_eof(); }
    char buf[4096];
    if (!fgets(buf, sizeof(buf), stdin))
        return word_eof();
    int pos = 0;
    word result = read_sexp(vm, buf, &pos);
    if (vm->error_kind) {
        vm->error_kind = 0;
        return word_eof();
    }
    return result;
}
