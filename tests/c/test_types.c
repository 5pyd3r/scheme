#include "types.h"
#include <stdio.h>

int main(void) {
    word w = word_from_fixnum(42);
    if (!is_fixnum(w)) return 1;
    if (word_to_fixnum(w) != 42) return 2;
    if (is_ptr(w) || is_char(w) || is_imm(w)) return 3;

    w = word_from_fixnum(-1);
    if (word_to_fixnum(w) != -1) return 4;

    int64_t max_val = (int64_t)1 << 61;
    w = word_from_fixnum(max_val - 1);
    if (word_to_fixnum(w) != max_val - 1) return 5;

    if (!is_true(word_true())) return 6;
    if (!is_false(word_false())) return 7;
    if (!is_nil(word_nil())) return 8;
    if (!is_eof(word_eof())) return 9;
    if (is_bool(word_nil())) return 10;

    w = word_from_char('A');
    if (!is_char(w)) return 11;
    if (word_to_char(w) != 'A') return 12;

    printf("ALL types tests PASSED\n");
    return 0;
}
