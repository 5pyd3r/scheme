#include "prim.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>

// ============================================================
// Bignum representation
// ============================================================

#define BIGNUM_SIGN_INDEX    DATA_START_INDEX
#define BIGNUM_COUNT_INDEX   (DATA_START_INDEX + 1)
#define BIGNUM_LIMBS_INDEX   (DATA_START_INDEX + 2)

#define bignum_sign(hdr)        ((hdr)[BIGNUM_SIGN_INDEX])
#define bignum_set_sign(hdr, s) ((hdr)[BIGNUM_SIGN_INDEX] = (word)(s))
#define bignum_count(hdr)       ((size_t)(hdr)[BIGNUM_COUNT_INDEX])
#define bignum_set_count(hdr, c)((hdr)[BIGNUM_COUNT_INDEX] = (word)(c))
#define bignum_limbs(hdr)       ((uint32_t*)((hdr) + BIGNUM_LIMBS_INDEX))

static inline bool is_bignum(word w) {
    return is_ptr(w) && obj_type(ptr_from_word(w)) == OBJ_TYPE_BIGNUM;
}

// Allocate a bignum with n limbs (zero-initialized)
static word make_bignum(vm_state_t* vm, int sign, size_t n) {
    size_t words = BIGNUM_LIMBS_INDEX + n;
    word* hdr = vm->gc->alloc_words(words);
    obj_set_type(hdr, OBJ_TYPE_BIGNUM);
    bignum_set_sign(hdr, sign ? 1 : 0);
    bignum_set_count(hdr, n);
    for (size_t i = 0; i < n; i++)
        hdr[BIGNUM_LIMBS_INDEX + i] = 0;
    return ptr_to_word(hdr);
}

// Count uint32_t limbs needed to represent a uint64_t value
static int bignum_limbs_needed(uint64_t val) {
    if (val == 0) return 0;
    int n = 0;
    while (val) { n++; val >>= 32; }
    return n;
}

// Create bignum from int64_t
static word bignum_from_int64(vm_state_t* vm, int64_t n) {
    uint64_t abs_val;
    if (n == INT64_MIN) {
        abs_val = (uint64_t)INT64_MAX + 1;
    } else {
        abs_val = (uint64_t)(n < 0 ? -n : n);
    }
    int sign = (n < 0) ? 1 : 0;
    int nl = bignum_limbs_needed(abs_val);
    word* hdr = ptr_from_word(make_bignum(vm, sign, (size_t)nl));
    uint32_t* limbs = bignum_limbs(hdr);
    if (nl > 0) {
        limbs[0] = (uint32_t)(abs_val & 0xFFFFFFFF);
        if (nl > 1) limbs[1] = (uint32_t)((abs_val >> 32) & 0xFFFFFFFF);
    }
    return ptr_to_word(hdr);
}

#define INT62_MAX 0x1FFFFFFFFFFFFFFFLL
#define INT62_MIN (-INT62_MAX - 1)

// Try to extract int64_t from bignum; *ok = 1 on success, 0 on overflow
static int64_t bignum_to_int64(word b, int* ok) {
    word* hdr = ptr_from_word(b);
    size_t nc = bignum_count(hdr);
    uint32_t* limbs = bignum_limbs(hdr);
    if (nc == 0) { *ok = 1; return 0; }
    if (nc > 2) { *ok = 0; return 0; }
    uint64_t val = limbs[0];
    if (nc > 1) val |= ((uint64_t)limbs[1]) << 32;
    // Check it fits in 62-bit signed fixnum range
    if (val > (uint64_t)INT62_MAX) { *ok = 0; return 0; }
    *ok = 1;
    int64_t result = (int64_t)val;
    return bignum_sign(hdr) ? -result : result;
}

// If bignum fits in fixnum, return fixnum; else return bignum as-is
static word bignum_to_fixnum_or_box(word b) {
    int ok;
    int64_t val = bignum_to_int64(b, &ok);
    if (ok && val >= INT62_MIN && val <= INT62_MAX) return word_from_fixnum(val);
    return b;
}

// Remove leading zero limbs (in-place)
static word bignum_normalize(word b) {
    word* hdr = ptr_from_word(b);
    size_t nc = bignum_count(hdr);
    uint32_t* limbs = bignum_limbs(hdr);
    while (nc > 0 && limbs[nc - 1] == 0) nc--;
    if (nc == bignum_count(hdr)) return b;
    if (nc == 0) {
        bignum_set_sign(hdr, 0);
        bignum_set_count(hdr, 0);
        return b;
    }
    bignum_set_count(hdr, nc);
    return b;
}

// Compare absolute values: -1, 0, +1
static int bignum_cmp_abs(word a, word b) {
    word* ha = ptr_from_word(a);
    word* hb = ptr_from_word(b);
    size_t na = bignum_count(ha), nb = bignum_count(hb);
    if (na != nb) return na < nb ? -1 : 1;
    uint32_t* la = bignum_limbs(ha);
    uint32_t* lb = bignum_limbs(hb);
    for (size_t i = na; i > 0; i--) {
        if (la[i-1] != lb[i-1])
            return la[i-1] < lb[i-1] ? -1 : 1;
    }
    return 0;
}

// Signed comparison: -1, 0, +1
static int bignum_cmp(word a, word b) {
    word* ha = ptr_from_word(a);
    word* hb = ptr_from_word(b);
    int sa = (int)bignum_sign(ha), sb = (int)bignum_sign(hb);
    if (sa != sb) return sa < sb ? 1 : -1;
    int cmp = bignum_cmp_abs(a, b);
    return sa ? -cmp : cmp;
}

// Pure-functional bignum addition
static word bignum_add(vm_state_t* vm, word a, word b) {
    word* ha = ptr_from_word(a);
    word* hb = ptr_from_word(b);
    int sa = (int)bignum_sign(ha), sb = (int)bignum_sign(hb);

    if (sa == sb) {
        size_t na = bignum_count(ha), nb = bignum_count(hb);
        size_t max_n = (na > nb ? na : nb) + 1;
        word* hr = ptr_from_word(make_bignum(vm, sa, max_n));
        uint32_t* la = bignum_limbs(ha);
        uint32_t* lb = bignum_limbs(hb);
        uint32_t* lr = bignum_limbs(hr);
        uint64_t carry = 0;
        for (size_t i = 0; i < max_n; i++) {
            uint64_t sum = carry;
            if (i < na) sum += la[i];
            if (i < nb) sum += lb[i];
            lr[i] = (uint32_t)sum;
            carry = sum >> 32;
        }
        return bignum_to_fixnum_or_box(bignum_normalize(ptr_to_word(hr)));
    } else {
        int cmp = bignum_cmp_abs(a, b);
        if (cmp == 0) {
            word* hz = ptr_from_word(make_bignum(vm, 0, 0));
            return ptr_to_word(hz);
        }
        word* big = ptr_from_word(cmp > 0 ? a : b);
        word* small = ptr_from_word(cmp > 0 ? b : a);
        int result_sign = (cmp > 0) ? sa : sb;
        size_t nbig = bignum_count(big);
        word* hr = ptr_from_word(make_bignum(vm, result_sign, nbig));
        uint32_t* lbig = bignum_limbs(big);
        uint32_t* lsmall = bignum_limbs(small);
        uint32_t* lr = bignum_limbs(hr);
        uint64_t borrow = 0;
        size_t nsmall = bignum_count(small);
        for (size_t i = 0; i < nbig; i++) {
            uint64_t diff = (uint64_t)lbig[i] - borrow;
            if (i < nsmall) diff -= lsmall[i];
            lr[i] = (uint32_t)diff;
            borrow = (diff >> 32) ? 1 : 0;
        }
        return bignum_to_fixnum_or_box(bignum_normalize(ptr_to_word(hr)));
    }
}

// a - b = a + (-b)
static word bignum_sub(vm_state_t* vm, word a, word b) {
    word* hb = ptr_from_word(b);
    size_t nb = bignum_count(hb);
    word* hneg = ptr_from_word(make_bignum(vm, !bignum_sign(hb) ? 1 : 0, nb));
    memcpy(bignum_limbs(hneg), bignum_limbs(hb), nb * sizeof(uint32_t));
    return bignum_add(vm, a, ptr_to_word(hneg));
}

// Return bignum with opposite sign
static word bignum_negate(vm_state_t* vm, word a) {
    word* ha = ptr_from_word(a);
    size_t na = bignum_count(ha);
    if (na == 0) return a;
    word* hr = ptr_from_word(make_bignum(vm, !bignum_sign(ha) ? 1 : 0, na));
    memcpy(bignum_limbs(hr), bignum_limbs(ha), na * sizeof(uint32_t));
    return ptr_to_word(hr);
}

// ============================================================
// Fixnum overflow detection
// ============================================================

static inline bool fixnum_add_overflows(int64_t a, int64_t b) {
    return (b > 0 && a > INT62_MAX - b) ||
           (b < 0 && a < INT62_MIN - b);
}

static inline bool fixnum_sub_overflows(int64_t a, int64_t b) {
    return (b > 0 && a < INT62_MIN + b) ||
           (b < 0 && a > INT62_MAX + b);
}

static inline bool fixnum_mul_overflows(int64_t a, int64_t b) {
    if (a == 0 || b == 0) return false;
    return (a > INT62_MAX / b) || (a < INT62_MIN / b);
}

// ============================================================
// Fixnum-only arithmetic primitives (temporary — will be replaced
// by type-dispatch in Task 4)
// ============================================================

word prim_add(vm_state_t* vm, int nargs) {
    int64_t sum = 0;
    for (int i = 0; i < nargs; i++) {
        word w = vm->sp[i];
        if (!is_fixnum(w)) { vm->error_code = 1; return word_nil(); }
        if (i == 0) { sum = word_to_fixnum(w); continue; }
        if (fixnum_add_overflows(sum, word_to_fixnum(w))) {
            word acc = bignum_from_int64(vm, sum);
            for (; i < nargs; i++) {
                w = vm->sp[i];
                if (!is_fixnum(w)) { vm->error_code = 1; return word_nil(); }
                acc = bignum_add(vm, acc, bignum_from_int64(vm, word_to_fixnum(w)));
            }
            return bignum_to_fixnum_or_box(acc);
        }
        sum += word_to_fixnum(w);
    }
    return word_from_fixnum(sum);
}

word prim_sub(vm_state_t* vm, int nargs) {
    if (nargs == 0) { vm->error_code = 1; return word_nil(); }
    if (nargs == 1) {
        word w = vm->sp[0];
        if (!is_fixnum(w)) { vm->error_code = 1; return word_nil(); }
        int64_t v = word_to_fixnum(w);
        if (v == INT62_MIN) {
            word zero = bignum_from_int64(vm, 0);
            word neg = bignum_from_int64(vm, v);
            return bignum_to_fixnum_or_box(bignum_sub(vm, zero, neg));
        }
        return word_from_fixnum(-v);
    }
    word w0 = vm->sp[0];
    if (!is_fixnum(w0)) { vm->error_code = 1; return word_nil(); }
    int64_t result = word_to_fixnum(w0);
    for (int i = 1; i < nargs; i++) {
        word w = vm->sp[i];
        if (!is_fixnum(w)) { vm->error_code = 1; return word_nil(); }
        if (fixnum_sub_overflows(result, word_to_fixnum(w))) {
            word acc = bignum_from_int64(vm, result);
            for (; i < nargs; i++) {
                w = vm->sp[i];
                if (!is_fixnum(w)) { vm->error_code = 1; return word_nil(); }
                acc = bignum_sub(vm, acc, bignum_from_int64(vm, word_to_fixnum(w)));
            }
            return bignum_to_fixnum_or_box(acc);
        }
        result -= word_to_fixnum(w);
    }
    return word_from_fixnum(result);
}

word prim_mul(vm_state_t* vm, int nargs) {
    int64_t product = 1;
    for (int i = 0; i < nargs; i++) {
        word w = vm->sp[i];
        if (!is_fixnum(w)) { vm->error_code = 1; return word_nil(); }
        if (i == 0) { product = word_to_fixnum(w); continue; }
        if (fixnum_mul_overflows(product, word_to_fixnum(w))) {
            // For now, just fall back to raw overflow (bignum_mul in Task 2)
            product *= word_to_fixnum(w);
        } else {
            product *= word_to_fixnum(w);
        }
    }
    return word_from_fixnum(product);
}

word prim_div(vm_state_t* vm, int nargs) {
    if (nargs < 1) { vm->error_code = 1; return word_nil(); }
    word w = vm->sp[0];
    if (!is_fixnum(w)) { vm->error_code = 1; return word_nil(); }
    int64_t result = word_to_fixnum(w);
    for (int i = 1; i < nargs; i++) {
        w = vm->sp[i];
        if (!is_fixnum(w)) { vm->error_code = 1; return word_nil(); }
        result /= word_to_fixnum(w);
    }
    return word_from_fixnum(result);
}

word prim_lt(vm_state_t* vm, int nargs) {
    for (int i = 1; i < nargs; i++) {
        word a = vm->sp[i-1], b = vm->sp[i];
        if (!is_fixnum(a) || !is_fixnum(b)) { vm->error_code = 1; return word_nil(); }
        if (word_to_fixnum(a) >= word_to_fixnum(b))
            return word_false();
    }
    return word_true();
}

word prim_gt(vm_state_t* vm, int nargs) {
    for (int i = 1; i < nargs; i++) {
        word a = vm->sp[i-1], b = vm->sp[i];
        if (!is_fixnum(a) || !is_fixnum(b)) { vm->error_code = 1; return word_nil(); }
        if (word_to_fixnum(a) <= word_to_fixnum(b))
            return word_false();
    }
    return word_true();
}

word prim_eq_num(vm_state_t* vm, int nargs) {
    if (nargs < 2) { vm->error_code = 1; return word_nil(); }
    word w0 = vm->sp[0];
    if (!is_fixnum(w0)) { vm->error_code = 1; return word_nil(); }
    int64_t first = word_to_fixnum(w0);
    for (int i = 1; i < nargs; i++) {
        word w = vm->sp[i];
        if (!is_fixnum(w)) { vm->error_code = 1; return word_nil(); }
        if (word_to_fixnum(w) != first)
            return word_false();
    }
    return word_true();
}
