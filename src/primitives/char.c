#include "prim.h"
#include "types.h"

word prim_charp(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_kind = 1; return word_nil(); }
    return is_char(vm->sp[0]) ? word_true() : word_false();
}

word prim_char_to_integer(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_kind = 1; return word_nil(); }
    word w = vm->sp[0];
    if (!is_char(w)) { vm->error_kind = 1; return word_nil(); }
    return word_from_fixnum((int64_t)word_to_char(w));
}

word prim_integer_to_char(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_kind = 1; return word_nil(); }
    word w = vm->sp[0];
    if (!is_fixnum(w)) { vm->error_kind = 1; return word_nil(); }
    int64_t n = word_to_fixnum(w);
    if (n < 0 || n > 0x10FFFF) { vm->error_kind = 1; return word_nil(); }
    return word_from_char((uint32_t)n);
}

word prim_char_eq(vm_state_t* vm, int nargs) {
    if (nargs != 2) { vm->error_kind = 1; return word_nil(); }
    word a = vm->sp[0], b = vm->sp[1];
    if (!is_char(a) || !is_char(b)) { vm->error_kind = 1; return word_nil(); }
    return word_to_char(a) == word_to_char(b) ? word_true() : word_false();
}

word prim_char_lt(vm_state_t* vm, int nargs) {
    if (nargs != 2) { vm->error_kind = 1; return word_nil(); }
    word a = vm->sp[0], b = vm->sp[1];
    if (!is_char(a) || !is_char(b)) { vm->error_kind = 1; return word_nil(); }
    return word_to_char(a) < word_to_char(b) ? word_true() : word_false();
}

word prim_char_gt(vm_state_t* vm, int nargs) {
    if (nargs != 2) { vm->error_kind = 1; return word_nil(); }
    word a = vm->sp[0], b = vm->sp[1];
    if (!is_char(a) || !is_char(b)) { vm->error_kind = 1; return word_nil(); }
    return word_to_char(a) > word_to_char(b) ? word_true() : word_false();
}

word prim_char_le(vm_state_t* vm, int nargs) {
    if (nargs != 2) { vm->error_kind = 1; return word_nil(); }
    word a = vm->sp[0], b = vm->sp[1];
    if (!is_char(a) || !is_char(b)) { vm->error_kind = 1; return word_nil(); }
    return word_to_char(a) <= word_to_char(b) ? word_true() : word_false();
}

word prim_char_ge(vm_state_t* vm, int nargs) {
    if (nargs != 2) { vm->error_kind = 1; return word_nil(); }
    word a = vm->sp[0], b = vm->sp[1];
    if (!is_char(a) || !is_char(b)) { vm->error_kind = 1; return word_nil(); }
    return word_to_char(a) >= word_to_char(b) ? word_true() : word_false();
}

word prim_char_upcase(vm_state_t* vm, int nargs) {
    if (nargs != 1 || !is_char(vm->sp[0])) { vm->error_kind = ERR_ARITY; return word_nil(); }
    char c = (char)word_to_char(vm->sp[0]);
    if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
    return word_from_char((unsigned char)c);
}

word prim_char_downcase(vm_state_t* vm, int nargs) {
    if (nargs != 1 || !is_char(vm->sp[0])) { vm->error_kind = ERR_ARITY; return word_nil(); }
    char c = (char)word_to_char(vm->sp[0]);
    if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
    return word_from_char((unsigned char)c);
}

word prim_digit_value(vm_state_t* vm, int nargs) {
    if (nargs != 1 || !is_char(vm->sp[0])) { vm->error_kind = ERR_ARITY; return word_false(); }
    char c = (char)word_to_char(vm->sp[0]);
    if (c >= '0' && c <= '9') return word_from_fixnum(c - '0');
    if (c >= 'a' && c <= 'f') return word_from_fixnum(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return word_from_fixnum(c - 'A' + 10);
    return word_false();
}
