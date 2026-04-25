#include "prim.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

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
    // INT64_MIN special case: absolute value 2^63 = 0x8000000000000000
    if (bignum_sign(hdr) && nc == 2 && limbs[1] == 0x80000000U && limbs[0] == 0) {
        *ok = 1;
        return INT64_MIN;
    }
    // General range check: positive must be <= INT64_MAX, negative abs must be <= INT64_MAX
    if (val > (uint64_t)INT64_MAX) { *ok = 0; return 0; }
    *ok = 1;
    int64_t result = (int64_t)val;
    return bignum_sign(hdr) ? -result : result;
}

// If bignum fits in fixnum, return fixnum; else return bignum as-is
word bignum_to_fixnum_or_box(word b) {
    int ok;
    int64_t val = bignum_to_int64(b, &ok);
    // Only unbox if value fits in 62-bit fixnum range
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

// Pure-functional bignum addition — always returns bignum (no fixnum unboxing)
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
        return bignum_normalize(ptr_to_word(hr));
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
        return bignum_normalize(ptr_to_word(hr));
    }
}

// a - b = a + (-b)
static word bignum_sub(vm_state_t* vm, word a, word b) {
    word* hb = ptr_from_word(b);
    size_t nb = bignum_count(hb);
    if (nb == 0) return a;  // a - 0 = a
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
// Bignum multiplication, GCD, division, string conversion
// ============================================================

static word bignum_mul(vm_state_t* vm, word a, word b) {
    word* ha = ptr_from_word(a);
    word* hb = ptr_from_word(b);
    int sa = (int)bignum_sign(ha), sb = (int)bignum_sign(hb);
    size_t na = bignum_count(ha), nb = bignum_count(hb);
    // Product requires at most na + nb limbs, zero-initialized by make_bignum
    word* hr = ptr_from_word(make_bignum(vm, sa != sb ? 1 : 0, na + nb));
    uint32_t* la = bignum_limbs(ha);
    uint32_t* lb = bignum_limbs(hb);
    uint32_t* lr = bignum_limbs(hr);
    for (size_t i = 0; i < na; i++) {
        uint64_t carry = 0;
        for (size_t j = 0; j < nb; j++) {
            uint64_t product = (uint64_t)la[i] * lb[j] + lr[i + j] + carry;
            lr[i + j] = (uint32_t)product;
            carry = product >> 32;
        }
        lr[i + nb] = (uint32_t)carry;
    }
    return bignum_normalize(ptr_to_word(hr));
}

// GCD helper functions
static inline bool bignum_zerop(word b) {
    return bignum_count(ptr_from_word(b)) == 0;
}

static word bignum_abs(vm_state_t* vm, word b) {
    word* hdr = ptr_from_word(b);
    size_t nc = bignum_count(hdr);
    if (nc == 0 || bignum_sign(hdr) == 0) return b;
    word* hr = ptr_from_word(make_bignum(vm, 0, nc));
    memcpy(bignum_limbs(hr), bignum_limbs(hdr), nc * sizeof(uint32_t));
    return ptr_to_word(hr);
}

static inline bool bignum_is_even(word b) {
    word* hdr = ptr_from_word(b);
    if (bignum_count(hdr) == 0) return true;
    return (bignum_limbs(hdr)[0] & 1) == 0;
}

static word bignum_halve(vm_state_t* vm, word b) {
    word* hdr = ptr_from_word(b);
    size_t nc = bignum_count(hdr);
    if (nc == 0) return b;
    word* hr = ptr_from_word(make_bignum(vm, bignum_sign(hdr), nc));
    uint32_t* limbs = bignum_limbs(hdr);
    uint32_t* lr = bignum_limbs(hr);
    uint32_t carry = 0;
    for (size_t i = nc; i > 0; i--) {
        uint64_t cur = ((uint64_t)carry << 32) | limbs[i - 1];
        lr[i - 1] = (uint32_t)(cur >> 1);
        carry = (uint32_t)(cur & 1);
    }
    return bignum_normalize(ptr_to_word(hr));
}

static word bignum_double(vm_state_t* vm, word b) {
    word* hdr = ptr_from_word(b);
    size_t nc = bignum_count(hdr);
    if (nc == 0) return b;
    word* hr = ptr_from_word(make_bignum(vm, bignum_sign(hdr), nc + 1));
    uint32_t* limbs = bignum_limbs(hdr);
    uint32_t* lr = bignum_limbs(hr);
    uint32_t carry = 0;
    for (size_t i = 0; i < nc; i++) {
        uint64_t cur = ((uint64_t)limbs[i] << 1) | carry;
        lr[i] = (uint32_t)cur;
        carry = (uint32_t)(cur >> 32);
    }
    lr[nc] = carry;
    return bignum_normalize(ptr_to_word(hr));
}

// Binary GCD (Stein's algorithm)
static word bignum_gcd(vm_state_t* vm, word a, word b) {
    a = bignum_abs(vm, a);
    b = bignum_abs(vm, b);

    if (bignum_zerop(a)) return b;
    if (bignum_zerop(b)) return a;

    int shift = 0;
    while (bignum_is_even(a) && bignum_is_even(b)) {
        a = bignum_halve(vm, a);
        b = bignum_halve(vm, b);
        shift++;
    }
    while (bignum_is_even(a)) a = bignum_halve(vm, a);
    while (bignum_is_even(b)) b = bignum_halve(vm, b);

    while (!bignum_zerop(a) && !bignum_zerop(b)) {
        if (bignum_cmp(a, b) > 0) {
            a = bignum_halve(vm, bignum_sub(vm, a, b));
            while (!bignum_zerop(a) && bignum_is_even(a)) a = bignum_halve(vm, a);
        } else if (bignum_cmp(a, b) < 0) {
            b = bignum_halve(vm, bignum_sub(vm, b, a));
            while (!bignum_zerop(b) && bignum_is_even(b)) b = bignum_halve(vm, b);
        } else {
            break;
        }
    }

    word result = b;
    while (shift--) result = bignum_double(vm, result);
    return result;
}

// Bignum division: quotient = a / b, *mod_out = a % b
static word bignum_divmod(vm_state_t* vm, word a, word b, word* mod_out) {
    word* ha = ptr_from_word(a);
    word* hb = ptr_from_word(b);
    int sa = (int)bignum_sign(ha), sb = (int)bignum_sign(hb);
    size_t na = bignum_count(ha), nb = bignum_count(hb);

    if (nb == 0) { vm->error_code = 1; return word_nil(); }

    // |a| < |b|: quotient 0, remainder = a
    if (na < nb || (na == nb && bignum_cmp_abs(a, b) < 0)) {
        if (mod_out) *mod_out = a;
        return bignum_from_int64(vm, 0);
    }

    // |a| == |b|: quotient +/-1, remainder 0
    if (na == nb && bignum_cmp_abs(a, b) == 0) {
        if (mod_out) *mod_out = bignum_from_int64(vm, 0);
        return bignum_from_int64(vm, sa == sb ? 1 : -1);
    }

    // Single-limb divisor fast path
    if (nb == 1) {
        uint64_t rem = 0;
        uint32_t divisor = bignum_limbs(hb)[0];
        size_t nq = na;
        word* hq = ptr_from_word(make_bignum(vm, sa != sb ? 1 : 0, nq));
        uint32_t* la = bignum_limbs(ha);
        uint32_t* lq = bignum_limbs(hq);
        for (size_t i = na; i > 0; i--) {
            uint64_t dividend = (rem << 32) | la[i - 1];
            lq[i - 1] = (uint32_t)(dividend / divisor);
            rem = dividend % divisor;
        }
        word quo = bignum_normalize(ptr_to_word(hq));
        if (mod_out) {
            // Remainder sign follows dividend's sign, matching Knuth path
            word* hrem = ptr_from_word(make_bignum(vm, sa ? 1 : 0, rem ? 1 : 0));
            if (rem) bignum_limbs(hrem)[0] = (uint32_t)rem;
            *mod_out = bignum_normalize(ptr_to_word(hrem));
        }
        return quo;
    }

    // Full Knuth algorithm D (TAOCP Vol 2, 4.3.1) for nb >= 2
    // D1: Normalize -- make top limb of divisor >= 2^31
    uint32_t v_top = bignum_limbs(hb)[nb - 1];
    uint32_t norm = (uint32_t)(((uint64_t)1 << 32) / ((uint64_t)v_top + 1));

    size_t nu = na + 1; // normalized dividend: a * norm, stored in nu limbs
    word* hu = ptr_from_word(make_bignum(vm, 0, nu));
    word* hv = ptr_from_word(make_bignum(vm, 0, nb));
    uint32_t* lu = bignum_limbs(hu);
    uint32_t* lv = bignum_limbs(hv);

    // u = a * norm
    uint64_t carry = 0;
    for (size_t i = 0; i < na; i++) {
        uint64_t prod = (uint64_t)bignum_limbs(ha)[i] * norm + carry;
        lu[i] = (uint32_t)prod;
        carry = prod >> 32;
    }
    lu[na] = (uint32_t)carry;

    // v = b * norm
    carry = 0;
    for (size_t i = 0; i < nb; i++) {
        uint64_t prod = (uint64_t)bignum_limbs(hb)[i] * norm + carry;
        lv[i] = (uint32_t)prod;
        carry = prod >> 32;
    }

    // Quotient has na - nb + 1 limbs
    size_t nq = na - nb + 1;
    word* hq = ptr_from_word(make_bignum(vm, sa != sb ? 1 : 0, nq));
    uint32_t* lq = bignum_limbs(hq);

    // D2: Loop over j = nq down to 1
    for (size_t j = nq; j > 0; j--) {
        size_t jj = j - 1;

        // D3: Estimate quotient digit
        uint64_t u_digit = ((uint64_t)lu[jj + nb] << 32) | lu[jj + nb - 1];
        uint64_t v_digit = lv[nb - 1];
        uint64_t q_hat = u_digit / v_digit;
        if (q_hat > 0xFFFFFFFFULL) q_hat = 0xFFFFFFFFULL;

        // D4: Multiply and subtract
        uint64_t carry_mul = 0;
        uint64_t borrow = 0;
        for (size_t i = 0; i < nb; i++) {
            uint64_t prod = q_hat * lv[i] + carry_mul;
            carry_mul = prod >> 32;
            int64_t diff = (int64_t)lu[jj + i] - (int64_t)(uint32_t)prod - (int64_t)borrow;
            lu[jj + i] = (uint32_t)(uint64_t)diff;
            borrow = (diff < 0) ? 1 : 0;
        }
        int64_t diff = (int64_t)lu[jj + nb] - (int64_t)(uint32_t)carry_mul - (int64_t)borrow;
        lu[jj + nb] = (uint32_t)(uint64_t)diff;

        // D5: Test remainder -- if negative, q_hat was too big
        if ((int64_t)diff < 0) {
            q_hat--;
            uint64_t add_carry = 0;
            for (size_t i = 0; i < nb; i++) {
                uint64_t sum = (uint64_t)lu[jj + i] + lv[i] + add_carry;
                lu[jj + i] = (uint32_t)sum;
                add_carry = sum >> 32;
            }
            lu[jj + nb] += (uint32_t)add_carry;
        }

        lq[jj] = (uint32_t)q_hat;
    }

    word quotient = bignum_normalize(ptr_to_word(hq));

    // D8: Denormalize remainder
    if (mod_out) {
        if (norm > 1) {
            uint64_t rem = 0;
            for (size_t i = nb; i > 0; i--) {
                uint64_t val = (rem << 32) | lu[i - 1];
                lu[i - 1] = (uint32_t)(val / norm);
                rem = val % norm;
            }
        }
        word* hrem = ptr_from_word(make_bignum(vm, sa ? 1 : 0, nb));
        memcpy(bignum_limbs(hrem), lu, nb * sizeof(uint32_t));
        *mod_out = bignum_normalize(ptr_to_word(hrem));
    }

    return quotient;
}

// Convert string to bignum
word bignum_from_string(vm_state_t* vm, const char* s, int radix) {
    while (*s == ' ' || *s == '\t' || *s == '\n') s++;
    int sign = 0;
    if (*s == '-') { sign = 1; s++; }
    else if (*s == '+') s++;

    word result = bignum_from_int64(vm, 0);
    word rad = bignum_from_int64(vm, radix);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else break;
        if (d >= radix) break;
        result = bignum_mul(vm, result, rad);
        result = bignum_add(vm, result, bignum_from_int64(vm, d));
    }

    if (sign) result = bignum_negate(vm, result);
    return result;
}

// Convert bignum to malloc'd string -- caller must free
char* bignum_to_string(vm_state_t* vm, word b, int radix) {
    word* hdr = ptr_from_word(b);
    size_t nc = bignum_count(hdr);
    if (nc == 0) {
        char* s = (char*)malloc(2);
        s[0] = '0'; s[1] = '\0';
        return s;
    }

    // Upper bound: ceil(nc * 32 / log2(10)) + 2 ~= nc * 10 + 2
    int max_digits = (int)nc * 10 + 2;
    char* result = (char*)malloc((size_t)max_digits + 2);
    int pos = max_digits;
    result[pos] = '\0';

    // We need a temporary bignum for repeated division
    word abs_b = bignum_abs(vm, b);
    static const char digits[] = "0123456789abcdef";

    while (bignum_count(ptr_from_word(abs_b)) > 0) {
        word rem;
        abs_b = bignum_divmod(vm, abs_b, bignum_from_int64(vm, radix), &rem);
        word* hrem = ptr_from_word(rem);
        uint32_t digit = bignum_count(hrem) > 0 ? bignum_limbs(hrem)[0] : 0;
        result[--pos] = digits[digit % radix];
    }

    if (bignum_sign(hdr))
        result[--pos] = '-';

    char* out = (char*)malloc((size_t)(max_digits - pos + 1));
    memcpy(out, result + pos, (size_t)(max_digits - pos));
    out[max_digits - pos] = '\0';
    free(result);
    return out;
}

// ============================================================
// Flonum representation and arithmetic
// ============================================================

static inline bool is_flonum(word w) {
    return is_ptr(w) && obj_type(ptr_from_word(w)) == OBJ_TYPE_FLONUM;
}

word word_from_double(vm_state_t* vm, double d) {
    word* hdr = vm->gc->alloc_words(3);
    obj_set_type(hdr, OBJ_TYPE_FLONUM);
    uint64_t bits;
    memcpy(&bits, &d, sizeof(bits));
    hdr[DATA_START_INDEX] = bits;
    return ptr_to_word(hdr);
}

double word_to_double(word w) {
    word* hdr = ptr_from_word(w);
    uint64_t bits = hdr[DATA_START_INDEX];
    double d;
    memcpy(&d, &bits, sizeof(d));
    return d;
}

static word flonum_add(vm_state_t* vm, word a, word b) {
    return word_from_double(vm, word_to_double(a) + word_to_double(b));
}

static word flonum_sub(vm_state_t* vm, word a, word b) {
    return word_from_double(vm, word_to_double(a) - word_to_double(b));
}

static word flonum_mul(vm_state_t* vm, word a, word b) {
    return word_from_double(vm, word_to_double(a) * word_to_double(b));
}

static word flonum_div(vm_state_t* vm, word a, word b) {
    return word_from_double(vm, word_to_double(a) / word_to_double(b));
}

static int flonum_cmp(vm_state_t* vm, word a, word b) {
    (void)vm;
    double da = word_to_double(a), db = word_to_double(b);
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

// Convert any numeric word to double (for type promotion)
static double word_as_double(word w) {
    if (is_fixnum(w)) return (double)word_to_fixnum(w);
    if (is_bignum(w)) {
        word* hdr = ptr_from_word(w);
        size_t nc = bignum_count(hdr);
        uint32_t* limbs = bignum_limbs(hdr);
        double d = 0.0;
        for (size_t i = nc; i > 0; i--)
            d = d * (double)((uint64_t)1 << 32) + limbs[i - 1];
        return bignum_sign(hdr) ? -d : d;
    }
    if (is_flonum(w)) return word_to_double(w);
    return 0.0;
}

// Promote fixnum to bignum, pass through other numeric types unchanged
static word promote_to_bignum(vm_state_t* vm, word w) {
    if (is_fixnum(w)) return bignum_from_int64(vm, word_to_fixnum(w));
    return w;
}

// ============================================================
// Type-dispatch arithmetic primitives
// ============================================================

word prim_add(vm_state_t* vm, int nargs) {
    bool has_flonum = false, has_bignum = false;
    for (int i = 0; i < nargs; i++) {
        word w = vm->sp[i];
        if (is_flonum(w)) has_flonum = true;
        else if (is_bignum(w)) has_bignum = true;
        else if (!is_fixnum(w)) { vm->error_code = 1; return word_nil(); }
    }

    if (has_flonum) {
        double sum = 0.0;
        for (int i = 0; i < nargs; i++)
            sum += word_as_double(vm->sp[i]);
        return word_from_double(vm, sum);
    }

    if (has_bignum) {
        word acc = bignum_from_int64(vm, 0);
        for (int i = 0; i < nargs; i++) {
            word w = vm->sp[i];
            word bn = is_fixnum(w) ? bignum_from_int64(vm, word_to_fixnum(w)) : w;
            acc = bignum_add(vm, acc, bn);
        }
        return bignum_to_fixnum_or_box(acc);
    }

    int64_t sum = 0;
    for (int i = 0; i < nargs; i++) {
        if (i > 0 && fixnum_add_overflows(sum, word_to_fixnum(vm->sp[i]))) {
            word acc = bignum_from_int64(vm, sum);
            for (; i < nargs; i++) {
                acc = bignum_add(vm, acc, bignum_from_int64(vm, word_to_fixnum(vm->sp[i])));
            }
            return bignum_to_fixnum_or_box(acc);
        }
        sum = (i == 0) ? word_to_fixnum(vm->sp[i]) : sum + word_to_fixnum(vm->sp[i]);
    }
    return word_from_fixnum(sum);
}

word prim_sub(vm_state_t* vm, int nargs) {
    if (nargs == 0) { vm->error_code = 1; return word_nil(); }
    bool has_flonum = false, has_bignum = false;
    for (int i = 0; i < nargs; i++) {
        word w = vm->sp[i];
        if (is_flonum(w)) has_flonum = true;
        else if (is_bignum(w)) has_bignum = true;
        else if (!is_fixnum(w)) { vm->error_code = 1; return word_nil(); }
    }

    if (has_flonum) {
        double result = word_as_double(vm->sp[0]);
        if (nargs == 1) { result = -result; }
        else { for (int i = 1; i < nargs; i++) result -= word_as_double(vm->sp[i]); }
        return word_from_double(vm, result);
    }

    if (has_bignum || nargs == 1) {
        word acc = promote_to_bignum(vm, vm->sp[0]);
        if (nargs == 1) {
            if (is_fixnum(vm->sp[0])) {
                int64_t v = word_to_fixnum(vm->sp[0]);
                if (v == INT62_MIN) {
                    word zero = bignum_from_int64(vm, 0);
                    return bignum_to_fixnum_or_box(bignum_sub(vm, zero, acc));
                }
                return word_from_fixnum(-v);
            }
            return bignum_to_fixnum_or_box(bignum_negate(vm, acc));
        }
        for (int i = 1; i < nargs; i++)
            acc = bignum_sub(vm, acc, promote_to_bignum(vm, vm->sp[i]));
        return bignum_to_fixnum_or_box(acc);
    }

    int64_t result = word_to_fixnum(vm->sp[0]);
    if (nargs == 1) {
        if (result == INT62_MIN) {
            return bignum_to_fixnum_or_box(
                bignum_sub(vm, bignum_from_int64(vm, 0), bignum_from_int64(vm, result)));
        }
        return word_from_fixnum(-result);
    }
    for (int i = 1; i < nargs; i++) {
        if (fixnum_sub_overflows(result, word_to_fixnum(vm->sp[i]))) {
            word acc = bignum_from_int64(vm, result);
            for (; i < nargs; i++)
                acc = bignum_sub(vm, acc, bignum_from_int64(vm, word_to_fixnum(vm->sp[i])));
            return bignum_to_fixnum_or_box(acc);
        }
        result -= word_to_fixnum(vm->sp[i]);
    }
    return word_from_fixnum(result);
}

word prim_mul(vm_state_t* vm, int nargs) {
    bool has_flonum = false, has_bignum = false;
    for (int i = 0; i < nargs; i++) {
        word w = vm->sp[i];
        if (is_flonum(w)) has_flonum = true;
        else if (is_bignum(w)) has_bignum = true;
        else if (!is_fixnum(w)) { vm->error_code = 1; return word_nil(); }
    }

    if (has_flonum) {
        double prod = word_as_double(vm->sp[0]);
        for (int i = 1; i < nargs; i++)
            prod *= word_as_double(vm->sp[i]);
        return word_from_double(vm, prod);
    }

    if (has_bignum) {
        word acc = promote_to_bignum(vm, vm->sp[0]);
        for (int i = 1; i < nargs; i++)
            acc = bignum_mul(vm, acc, promote_to_bignum(vm, vm->sp[i]));
        return bignum_to_fixnum_or_box(acc);
    }

    int64_t product = 1;
    for (int i = 0; i < nargs; i++) {
        if (i == 0) { product = word_to_fixnum(vm->sp[i]); continue; }
        if (fixnum_mul_overflows(product, word_to_fixnum(vm->sp[i]))) {
            word acc = bignum_from_int64(vm, product);
            acc = bignum_mul(vm, acc, bignum_from_int64(vm, word_to_fixnum(vm->sp[i])));
            for (i++; i < nargs; i++)
                acc = bignum_mul(vm, acc, bignum_from_int64(vm, word_to_fixnum(vm->sp[i])));
            return bignum_to_fixnum_or_box(acc);
        }
        product = (int64_t)((uint64_t)product * (uint64_t)word_to_fixnum(vm->sp[i]));
    }
    return word_from_fixnum(product);
}

word prim_div(vm_state_t* vm, int nargs) {
    if (nargs < 1) { vm->error_code = 1; return word_nil(); }
    bool has_flonum = false;
    for (int i = 0; i < nargs; i++) {
        word w = vm->sp[i];
        if (is_flonum(w)) has_flonum = true;
        else if (!is_fixnum(w) && !is_bignum(w)) { vm->error_code = 1; return word_nil(); }
    }

    if (has_flonum) {
        double result = word_as_double(vm->sp[0]);
        for (int i = 1; i < nargs; i++) {
            double d = word_as_double(vm->sp[i]);
            if (d == 0.0) { vm->error_code = 1; return word_nil(); }
            result /= d;
        }
        return word_from_double(vm, result);
    }

    // Exact integer division using bignum arithmetic
    word acc = promote_to_bignum(vm, vm->sp[0]);
    for (int i = 1; i < nargs; i++) {
        word divisor = promote_to_bignum(vm, vm->sp[i]);
        if (bignum_count(ptr_from_word(divisor)) == 0) {
            vm->error_code = 1; return word_nil();
        }
        word rem;
        word quotient = bignum_divmod(vm, acc, divisor, &rem);
        if (bignum_count(ptr_from_word(rem)) != 0) {
            // Not exact — fall back to flonum
            double d = word_as_double(acc);
            for (; i < nargs; i++)
                d /= word_as_double(vm->sp[i]);
            return word_from_double(vm, d);
        }
        acc = quotient;
    }
    return bignum_to_fixnum_or_box(acc);
}

word prim_lt(vm_state_t* vm, int nargs) {
    bool has_flonum = false;
    for (int i = 0; i < nargs; i++)
        if (is_flonum(vm->sp[i])) { has_flonum = true; break; }

    if (has_flonum) {
        for (int i = 1; i < nargs; i++)
            if (!(word_as_double(vm->sp[i-1]) < word_as_double(vm->sp[i])))
                return word_false();
        return word_true();
    }

    for (int i = 1; i < nargs; i++) {
        word a = promote_to_bignum(vm, vm->sp[i-1]);
        word b = promote_to_bignum(vm, vm->sp[i]);
        if (bignum_cmp(a, b) >= 0) return word_false();
    }
    return word_true();
}

word prim_gt(vm_state_t* vm, int nargs) {
    bool has_flonum = false;
    for (int i = 0; i < nargs; i++)
        if (is_flonum(vm->sp[i])) { has_flonum = true; break; }

    if (has_flonum) {
        for (int i = 1; i < nargs; i++)
            if (!(word_as_double(vm->sp[i-1]) > word_as_double(vm->sp[i])))
                return word_false();
        return word_true();
    }

    for (int i = 1; i < nargs; i++) {
        word a = promote_to_bignum(vm, vm->sp[i-1]);
        word b = promote_to_bignum(vm, vm->sp[i]);
        if (bignum_cmp(a, b) <= 0) return word_false();
    }
    return word_true();
}

word prim_eq_num(vm_state_t* vm, int nargs) {
    if (nargs < 2) { vm->error_code = 1; return word_nil(); }
    bool has_flonum = false;
    for (int i = 0; i < nargs; i++)
        if (is_flonum(vm->sp[i])) { has_flonum = true; break; }

    if (has_flonum) {
        double first = word_as_double(vm->sp[0]);
        for (int i = 1; i < nargs; i++)
            if (word_as_double(vm->sp[i]) != first) return word_false();
        return word_true();
    }

    word first = promote_to_bignum(vm, vm->sp[0]);
    for (int i = 1; i < nargs; i++)
        if (bignum_cmp(first, promote_to_bignum(vm, vm->sp[i])) != 0)
            return word_false();
    return word_true();
}

// ============================================================
// Numerical predicate primitives
// ============================================================

word prim_number_pred(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_code = 1; return word_nil(); }
    word w = vm->sp[0];
    return (is_fixnum(w) || is_bignum(w) || is_flonum(w)) ? word_true() : word_false();
}

word prim_integer_pred(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_code = 1; return word_nil(); }
    word w = vm->sp[0];
    return (is_fixnum(w) || is_bignum(w)) ? word_true() : word_false();
}

word prim_exact_pred(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_code = 1; return word_nil(); }
    word w = vm->sp[0];
    return (is_fixnum(w) || is_bignum(w)) ? word_true() : word_false();
}

word prim_inexact_pred(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_code = 1; return word_nil(); }
    return is_flonum(vm->sp[0]) ? word_true() : word_false();
}

word prim_zerop(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_code = 1; return word_nil(); }
    word w = vm->sp[0];
    if (is_fixnum(w)) return word_to_fixnum(w) == 0 ? word_true() : word_false();
    if (is_bignum(w)) return bignum_count(ptr_from_word(w)) == 0 ? word_true() : word_false();
    if (is_flonum(w)) return word_to_double(w) == 0.0 ? word_true() : word_false();
    return word_false();
}

word prim_positivep(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_code = 1; return word_nil(); }
    word w = vm->sp[0];
    if (is_fixnum(w)) return word_to_fixnum(w) > 0 ? word_true() : word_false();
    if (is_bignum(w)) {
        word* hdr = ptr_from_word(w);
        return (bignum_count(hdr) > 0 && bignum_sign(hdr) == 0) ? word_true() : word_false();
    }
    if (is_flonum(w)) return word_to_double(w) > 0.0 ? word_true() : word_false();
    return word_false();
}

word prim_negativep(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_code = 1; return word_nil(); }
    word w = vm->sp[0];
    if (is_fixnum(w)) return word_to_fixnum(w) < 0 ? word_true() : word_false();
    if (is_bignum(w)) {
        word* hdr = ptr_from_word(w);
        return (bignum_count(hdr) > 0 && bignum_sign(hdr) != 0) ? word_true() : word_false();
    }
    if (is_flonum(w)) return word_to_double(w) < 0.0 ? word_true() : word_false();
    return word_false();
}

word prim_evenp(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_code = 1; return word_nil(); }
    word w = vm->sp[0];
    if (is_fixnum(w)) return (word_to_fixnum(w) & 1) == 0 ? word_true() : word_false();
    if (is_bignum(w)) {
        word* hdr = ptr_from_word(w);
        if (bignum_count(hdr) == 0) return word_true();
        return (bignum_limbs(hdr)[0] & 1) == 0 ? word_true() : word_false();
    }
    return word_false();
}

word prim_oddp(vm_state_t* vm, int nargs) {
    word r = prim_evenp(vm, nargs);
    if (vm->error_code) return word_nil();
    return is_true(r) ? word_false() : word_true();
}

// ============================================================
// Integer operations
// ============================================================

word prim_quotient(vm_state_t* vm, int nargs) {
    if (nargs != 2) { vm->error_code = 1; return word_nil(); }
    word a = promote_to_bignum(vm, vm->sp[0]);
    word b = promote_to_bignum(vm, vm->sp[1]);
    if (bignum_count(ptr_from_word(b)) == 0) { vm->error_code = 1; return word_nil(); }
    word rem;
    word q = bignum_divmod(vm, a, b, &rem);
    return bignum_to_fixnum_or_box(q);
}

word prim_remainder(vm_state_t* vm, int nargs) {
    if (nargs != 2) { vm->error_code = 1; return word_nil(); }
    word a = promote_to_bignum(vm, vm->sp[0]);
    word b = promote_to_bignum(vm, vm->sp[1]);
    if (bignum_count(ptr_from_word(b)) == 0) { vm->error_code = 1; return word_nil(); }
    word rem;
    bignum_divmod(vm, a, b, &rem);
    return bignum_to_fixnum_or_box(rem);
}

word prim_modulo(vm_state_t* vm, int nargs) {
    if (nargs != 2) { vm->error_code = 1; return word_nil(); }
    word a = promote_to_bignum(vm, vm->sp[0]);
    word b = promote_to_bignum(vm, vm->sp[1]);
    if (bignum_count(ptr_from_word(b)) == 0) { vm->error_code = 1; return word_nil(); }
    word rem;
    bignum_divmod(vm, a, b, &rem);
    // Modulo differs from remainder when signs differ:
    // remainder has same sign as dividend, modulo has same sign as divisor
    word* hb = ptr_from_word(b);
    word* hrem = ptr_from_word(rem);
    if (bignum_count(hrem) != 0 && bignum_sign(hrem) != bignum_sign(hb)) {
        rem = bignum_add(vm, rem, b);
    }
    return bignum_to_fixnum_or_box(rem);
}

word prim_gcd(vm_state_t* vm, int nargs) {
    if (nargs == 0) return word_from_fixnum(0);
    word acc = bignum_from_int64(vm, 0);
    for (int i = 0; i < nargs; i++) {
        word w = promote_to_bignum(vm, vm->sp[i]);
        if (bignum_count(ptr_from_word(acc)) == 0)
            acc = bignum_abs(vm, w);
        else if (bignum_count(ptr_from_word(w)) > 0)
            acc = bignum_gcd(vm, acc, bignum_abs(vm, w));
    }
    return bignum_to_fixnum_or_box(acc);
}

word prim_lcm(vm_state_t* vm, int nargs) {
    if (nargs == 0) return word_from_fixnum(1);
    word acc = bignum_from_int64(vm, 1);
    for (int i = 0; i < nargs; i++) {
        word w = promote_to_bignum(vm, vm->sp[i]);
        if (bignum_count(ptr_from_word(w)) == 0) return word_from_fixnum(0);
        word g = bignum_gcd(vm, acc, bignum_abs(vm, w));
        acc = bignum_mul(vm, acc, bignum_divmod(vm, w, g, NULL));
        acc = bignum_abs(vm, acc);
    }
    return bignum_to_fixnum_or_box(acc);
}

word prim_abs(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_code = 1; return word_nil(); }
    word w = vm->sp[0];
    if (is_fixnum(w)) {
        int64_t v = word_to_fixnum(w);
        return (v < 0) ? word_from_fixnum(-v) : w;
    }
    if (is_bignum(w)) return bignum_to_fixnum_or_box(bignum_abs(vm, w));
    if (is_flonum(w)) return word_from_double(vm, fabs(word_to_double(w)));
    vm->error_code = 1; return word_nil();
}

word prim_max(vm_state_t* vm, int nargs) {
    if (nargs < 1) { vm->error_code = 1; return word_nil(); }
    word best = vm->sp[0];
    for (int i = 1; i < nargs; i++) {
        word w = vm->sp[i];
        bool fl_best = is_flonum(best) || is_flonum(w);
        if (fl_best) {
            double da = word_as_double(best), db = word_as_double(w);
            best = (da >= db) ? best : w;
        } else {
            word ca = promote_to_bignum(vm, best);
            word cb = promote_to_bignum(vm, w);
            best = (bignum_cmp(ca, cb) >= 0) ? best : w;
        }
    }
    return best;
}

word prim_min(vm_state_t* vm, int nargs) {
    if (nargs < 1) { vm->error_code = 1; return word_nil(); }
    word best = vm->sp[0];
    for (int i = 1; i < nargs; i++) {
        word w = vm->sp[i];
        bool fl_best = is_flonum(best) || is_flonum(w);
        if (fl_best) {
            double da = word_as_double(best), db = word_as_double(w);
            best = (da <= db) ? best : w;
        } else {
            word ca = promote_to_bignum(vm, best);
            word cb = promote_to_bignum(vm, w);
            best = (bignum_cmp(ca, cb) <= 0) ? best : w;
        }
    }
    return best;
}

// Rounding primitives — accept flonum or exact integer, return integer
static word floor_divmod(vm_state_t* vm, word a, word b, word* rem_out) {
    word q, r;
    if (is_flonum(a) || is_flonum(b)) {
        double da = word_as_double(a), db = word_as_double(b);
        if (db == 0.0) { vm->error_code = 1; return word_nil(); }
        double dq = floor(da / db);
        if (rem_out) {
            double dr = da - dq * db;
            // Build flonum remainder
            word* hr = vm->gc->alloc_words(2);
            obj_set_type(hr, OBJ_TYPE_FLONUM);
            uint64_t bits; memcpy(&bits, &dr, sizeof(bits));
            hr[DATA_START_INDEX] = bits;
            *rem_out = ptr_to_word(hr);
        }
        uint64_t bits; memcpy(&bits, &dq, sizeof(bits));
        word* hq = vm->gc->alloc_words(2);
        obj_set_type(hq, OBJ_TYPE_FLONUM);
        hq[DATA_START_INDEX] = bits;
        return ptr_to_word(hq);
    }
    word bna = promote_to_bignum(vm, a);
    word bnb = promote_to_bignum(vm, b);
    if (bignum_count(ptr_from_word(bnb)) == 0) { vm->error_code = 1; return word_nil(); }
    q = bignum_divmod(vm, bna, bnb, &r);
    // Floor: if r != 0 and signs differ, q -= 1
    word* hr = ptr_from_word(r);
    if (bignum_count(hr) != 0) {
        word* hb = ptr_from_word(bnb);
        if (bignum_sign(hr) != bignum_sign(hb)) {
            q = bignum_sub(vm, q, bignum_from_int64(vm, 1));
            r = bignum_add(vm, r, bnb);
        }
    }
    if (rem_out) *rem_out = r;
    return bignum_to_fixnum_or_box(q);
}

word prim_floor(vm_state_t* vm, int nargs) {
    if (nargs == 1) {
        word w = vm->sp[0];
        if (is_fixnum(w) || is_bignum(w)) return w;
        if (is_flonum(w)) return word_from_double(vm, floor(word_to_double(w)));
        vm->error_code = 1; return word_nil();
    }
    return floor_divmod(vm, vm->sp[0], vm->sp[1], NULL);
}

word prim_ceiling(vm_state_t* vm, int nargs) {
    if (nargs == 1) {
        word w = vm->sp[0];
        if (is_fixnum(w) || is_bignum(w)) return w;
        if (is_flonum(w)) return word_from_double(vm, ceil(word_to_double(w)));
        vm->error_code = 1; return word_nil();
    }
    // ceiling(a/b) = -floor(-a/b)
    word neg_a, neg_b;
    if (is_flonum(vm->sp[0])) neg_a = word_from_double(vm, -word_as_double(vm->sp[0]));
    else neg_a = bignum_to_fixnum_or_box(bignum_negate(vm, promote_to_bignum(vm, vm->sp[0])));
    if (is_flonum(vm->sp[1])) neg_b = word_from_double(vm, -word_as_double(vm->sp[1]));
    else neg_b = bignum_to_fixnum_or_box(bignum_negate(vm, promote_to_bignum(vm, vm->sp[1])));
    word fq = floor_divmod(vm, neg_a, neg_b, NULL);
    if (vm->error_code) return word_nil();
    if (is_flonum(fq)) return word_from_double(vm, -word_to_double(fq));
    return bignum_to_fixnum_or_box(bignum_negate(vm, promote_to_bignum(vm, fq)));
}

word prim_truncate(vm_state_t* vm, int nargs) {
    if (nargs == 1) {
        word w = vm->sp[0];
        if (is_fixnum(w) || is_bignum(w)) return w;
        if (is_flonum(w)) return word_from_double(vm, trunc(word_to_double(w)));
        vm->error_code = 1; return word_nil();
    }
    word a = promote_to_bignum(vm, vm->sp[0]);
    word b = promote_to_bignum(vm, vm->sp[1]);
    if (bignum_count(ptr_from_word(b)) == 0) { vm->error_code = 1; return word_nil(); }
    word rem;
    word q = bignum_divmod(vm, a, b, &rem);
    return bignum_to_fixnum_or_box(q);
}

word prim_round(vm_state_t* vm, int nargs) {
    if (nargs == 1) {
        word w = vm->sp[0];
        if (is_fixnum(w) || is_bignum(w)) return w;
        if (is_flonum(w)) return word_from_double(vm, round(word_to_double(w)));
        vm->error_code = 1; return word_nil();
    }
    // R7RS round uses "round to even" on ties — for (round a b) use truncate with tie-breaking
    word a = promote_to_bignum(vm, vm->sp[0]);
    word b = promote_to_bignum(vm, vm->sp[1]);
    if (bignum_count(ptr_from_word(b)) == 0) { vm->error_code = 1; return word_nil(); }
    word rem;
    word q = bignum_divmod(vm, a, b, &rem);
    word* hrem = ptr_from_word(rem);
    if (bignum_count(hrem) != 0) {
        // Compare 2*|rem| with |b| to decide rounding direction
        word two_rem = bignum_mul(vm, bignum_abs(vm, rem), bignum_from_int64(vm, 2));
        word abs_b = bignum_abs(vm, b);
        int c = bignum_cmp(two_rem, abs_b);
        if (c > 0 || (c == 0 && (bignum_limbs(ptr_from_word(q))[0] & 1))) {
            // Round away from zero (or to even on tie)
            if (bignum_sign(ptr_from_word(a)) == 0)
                q = bignum_add(vm, q, bignum_from_int64(vm, 1));
            else
                q = bignum_sub(vm, q, bignum_from_int64(vm, 1));
        }
    }
    return bignum_to_fixnum_or_box(q);
}

// ============================================================
// Conversion primitives
// ============================================================

word prim_number_to_string(vm_state_t* vm, int nargs) {
    if (nargs < 1 || nargs > 2) { vm->error_code = 1; return word_nil(); }
    int radix = 10;
    if (nargs == 2) {
        if (!is_fixnum(vm->sp[1])) { vm->error_code = 1; return word_nil(); }
        radix = (int)word_to_fixnum(vm->sp[1]);
    }
    word w = vm->sp[0];
    char* str;
    if (is_fixnum(w)) {
        str = bignum_to_string(vm, bignum_from_int64(vm, word_to_fixnum(w)), radix);
    } else if (is_bignum(w)) {
        str = bignum_to_string(vm, w, radix);
    } else if (is_flonum(w)) {
        double d = word_to_double(w);
        str = (char*)malloc(64);
        snprintf(str, 64, "%.17g", d);
    } else {
        vm->error_code = 1; return word_nil();
    }
    int len = (int)strlen(str);
    word* str_obj = vm->gc->alloc_words((size_t)len + 3);
    obj_set_type(str_obj, OBJ_TYPE_STRING);
    str_obj[DATA_START_INDEX] = (word)len;
    for (int i = 0; i < len; i++)
        string_set(str_obj, i, word_from_char((unsigned char)str[i]));
    free(str);
    return ptr_to_word(str_obj);
}

word prim_string_to_number(vm_state_t* vm, int nargs) {
    if (nargs < 1 || nargs > 2) { vm->error_code = 1; return word_nil(); }
    int radix = 10;
    if (nargs == 2) {
        if (!is_fixnum(vm->sp[1])) { vm->error_code = 1; return word_nil(); }
        radix = (int)word_to_fixnum(vm->sp[1]);
    }
    word w = vm->sp[0];
    if (!is_ptr(w) || obj_type(ptr_from_word(w)) != OBJ_TYPE_STRING) {
        vm->error_code = 1; return word_nil();
    }
    word* hdr = ptr_from_word(w);
    int len = (int)string_length(hdr);
    char buf[256];
    if (len >= 255) { vm->error_code = 1; return word_nil(); }
    for (int i = 0; i < len; i++)
        buf[i] = (char)word_to_char(string_ref(hdr, i));
    buf[len] = '\0';

    int has_dot = 0, has_exp = 0;
    for (int i = 0; i < len; i++) {
        if (buf[i] == '.') has_dot = 1;
        if (buf[i] == 'e' || buf[i] == 'E') has_exp = 1;
    }
    if (has_dot || has_exp) {
        double d = strtod(buf, NULL);
        return word_from_double(vm, d);
    } else {
        return bignum_to_fixnum_or_box(bignum_from_string(vm, buf, radix));
    }
}

word prim_exact_to_inexact(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_code = 1; return word_nil(); }
    word w = vm->sp[0];
    if (is_flonum(w)) return w;
    if (is_fixnum(w)) return word_from_double(vm, (double)word_to_fixnum(w));
    if (is_bignum(w)) return word_from_double(vm, word_as_double(w));
    vm->error_code = 1; return word_nil();
}

word prim_inexact_to_exact(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_code = 1; return word_nil(); }
    word w = vm->sp[0];
    if (is_fixnum(w) || is_bignum(w)) return w;
    if (is_flonum(w)) {
        double d = word_to_double(w);
        if (d == (double)(int64_t)d && d >= INT62_MIN && d <= INT62_MAX) {
            return word_from_fixnum((int64_t)d);
        }
        return bignum_to_fixnum_or_box(bignum_from_int64(vm, (int64_t)d));
    }
    vm->error_code = 1; return word_nil();
}

// ============================================================
// Transcendental primitives
// ============================================================

static double flonum_arg(word w) {
    if (is_fixnum(w)) return (double)word_to_fixnum(w);
    if (is_flonum(w)) return word_to_double(w);
    if (is_bignum(w)) return word_as_double(w);
    return 0.0;
}

word prim_sin(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_code = 1; return word_nil(); }
    return word_from_double(vm, sin(flonum_arg(vm->sp[0])));
}

word prim_cos(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_code = 1; return word_nil(); }
    return word_from_double(vm, cos(flonum_arg(vm->sp[0])));
}

word prim_tan(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_code = 1; return word_nil(); }
    return word_from_double(vm, tan(flonum_arg(vm->sp[0])));
}

word prim_asin(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_code = 1; return word_nil(); }
    return word_from_double(vm, asin(flonum_arg(vm->sp[0])));
}

word prim_acos(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_code = 1; return word_nil(); }
    return word_from_double(vm, acos(flonum_arg(vm->sp[0])));
}

word prim_atan(vm_state_t* vm, int nargs) {
    if (nargs == 1) return word_from_double(vm, atan(flonum_arg(vm->sp[0])));
    if (nargs == 2) return word_from_double(vm, atan2(flonum_arg(vm->sp[0]), flonum_arg(vm->sp[1])));
    vm->error_code = 1; return word_nil();
}

word prim_sqrt(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_code = 1; return word_nil(); }
    return word_from_double(vm, sqrt(flonum_arg(vm->sp[0])));
}

word prim_exp(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_code = 1; return word_nil(); }
    return word_from_double(vm, exp(flonum_arg(vm->sp[0])));
}

word prim_log(vm_state_t* vm, int nargs) {
    if (nargs == 1) return word_from_double(vm, log(flonum_arg(vm->sp[0])));
    if (nargs == 2) return word_from_double(vm, log(flonum_arg(vm->sp[0])) / log(flonum_arg(vm->sp[1])));
    vm->error_code = 1; return word_nil();
}

word prim_finitep(vm_state_t* vm, int nargs) {
    if (nargs != 1 || !is_flonum(vm->sp[0])) return word_false();
    return isfinite(word_to_double(vm->sp[0])) ? word_true() : word_false();
}

word prim_infinitep(vm_state_t* vm, int nargs) {
    if (nargs != 1 || !is_flonum(vm->sp[0])) return word_false();
    return isinf(word_to_double(vm->sp[0])) ? word_true() : word_false();
}

word prim_nanp(vm_state_t* vm, int nargs) {
    if (nargs != 1 || !is_flonum(vm->sp[0])) return word_false();
    return isnan(word_to_double(vm->sp[0])) ? word_true() : word_false();
}
