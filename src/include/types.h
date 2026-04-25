#ifndef SCHEME_TYPES_H
#define SCHEME_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef uint64_t word;

// ---- Tagged pointer scheme ----
#define TAG_MASK    ((word)0x3)
#define TAG_FIXNUM  ((word)0x0)
#define TAG_CHAR    ((word)0x1)
#define TAG_PTR     ((word)0x2)
#define TAG_IMM     ((word)0x3)

// Immediate sub-tags (bits 2-3)
#define IMM_FALSE ((word)(0x0 << 2))
#define IMM_TRUE  ((word)(0x1 << 2))
#define IMM_NIL   ((word)(0x2 << 2))
#define IMM_EOF   ((word)(0x3 << 2))
#define IMM_MASK  ((word)(0x3 << 2))

// ---- Fixnum ----
#define word_from_fixnum(n)   ((word)(((uint64_t)(int64_t)(n) << 2) | TAG_FIXNUM))
#define word_to_fixnum(w)     ((int64_t)(w) >> 2)

// ---- Char ----
#define word_from_char(c)     ((word)(((uint32_t)(c) << 2) | TAG_CHAR))
#define word_to_char(w)       ((uint32_t)((w) >> 2))

// ---- Immediate constants ----
#define word_true()  ((word)(IMM_TRUE  | TAG_IMM))
#define word_false() ((word)(IMM_FALSE | TAG_IMM))
#define word_nil()   ((word)(IMM_NIL   | TAG_IMM))
#define word_eof()   ((word)(IMM_EOF   | TAG_IMM))

// ---- Type predicates ----
#define is_fixnum(w)  (((w) & TAG_MASK) == TAG_FIXNUM)
#define is_char(w)    (((w) & TAG_MASK) == TAG_CHAR)
#define is_ptr(w)     (((w) & TAG_MASK) == TAG_PTR)
#define is_imm(w)     (((w) & TAG_MASK) == TAG_IMM)

#define is_true(w)    ((w) == word_true())
#define is_false(w)   ((w) == word_false())
#define is_nil(w)     ((w) == word_nil())
#define is_eof(w)     ((w) == word_eof())
static inline bool is_bool(word w) {
    return (w & TAG_MASK) == TAG_IMM && (w & IMM_MASK) != IMM_NIL && (w & IMM_MASK) != IMM_EOF;
}

// ---- Heap object layout ----
#define GC_HEADER_SIZE  1
#define TYPE_WORD_INDEX 1
#define DATA_START_INDEX 2

// GC header bit layout
#define GC_MARK_BIT   0x1
#define GC_COLOR_BITS 0x6
#define GC_SIZE_SHIFT 3
#define GC_SIZE_MASK  (~((word)0x7))

#define gc_marked(hdr)    ((hdr) & GC_MARK_BIT)
#define gc_set_mark(hdr)  ((hdr) | GC_MARK_BIT)
#define gc_clr_mark(hdr)  ((hdr) & ~GC_MARK_BIT)
#define gc_size(hdr)      ((hdr) >> GC_SIZE_SHIFT)
#define gc_set_size(hdr, sz) (((hdr) & (GC_MARK_BIT | GC_COLOR_BITS)) | ((word)(sz) << GC_SIZE_SHIFT))

// Convert tagged pointer word to heap object base address
static inline word* ptr_from_word(word w) {
    return (word*)(uintptr_t)(w & ~TAG_MASK);
}

// Access heap object fields
static inline word* obj_data(word* hdr) {
    return hdr + DATA_START_INDEX;
}

// ---- Heap object type tags ----
enum {
    OBJ_TYPE_PAIR         = 0,
    OBJ_TYPE_VECTOR       = 1,
    OBJ_TYPE_STRING       = 2,
    OBJ_TYPE_BYTEVECTOR   = 3,
    OBJ_TYPE_SYMBOL       = 4,
    OBJ_TYPE_CLOSURE      = 5,
    OBJ_TYPE_PORT         = 6,
    OBJ_TYPE_BIGNUM       = 7,
    OBJ_TYPE_RATIONAL     = 8,
    OBJ_TYPE_FLONUM       = 9,
    OBJ_TYPE_COMPLEX      = 10,
    OBJ_TYPE_CODE         = 11,
    OBJ_TYPE_RECORD       = 12,
};

#define obj_type(hdr)        ((hdr)[TYPE_WORD_INDEX])
#define obj_set_type(hdr, t) ((hdr)[TYPE_WORD_INDEX] = (word)(t))

// ---- Pair helpers ----
#define pair_car(hdr)   ((hdr)[DATA_START_INDEX])
#define pair_cdr(hdr)   ((hdr)[DATA_START_INDEX + 1])
#define pair_set_car(hdr, v) ((hdr)[DATA_START_INDEX] = (v))
#define pair_set_cdr(hdr, v) ((hdr)[DATA_START_INDEX + 1] = (v))

// ---- Vector helpers ----
#define vector_length(hdr)   ((size_t)(hdr)[DATA_START_INDEX])
#define vector_elem(hdr, i)  ((hdr)[DATA_START_INDEX + 1 + (i)])
#define vector_set(hdr, i, v) ((hdr)[DATA_START_INDEX + 1 + (i)] = (v))

// ---- String helpers ----
#define string_length(hdr)   ((size_t)(hdr)[DATA_START_INDEX])
#define string_ref(hdr, i)   ((hdr)[DATA_START_INDEX + 1 + (i)])
#define string_set(hdr, i, c) ((hdr)[DATA_START_INDEX + 1 + (i)] = (word)(c))

// ---- Bytevector helpers ----
#define bytevector_length(hdr)   ((size_t)(hdr)[DATA_START_INDEX])
#define bytevector_data(hdr)     ((uint8_t*)((hdr) + DATA_START_INDEX + 1))

// ---- Symbol helpers ----
#define symbol_string(hdr)     ((hdr)[DATA_START_INDEX])

// ---- Closure helpers ----
#define closure_code(hdr)     ((hdr)[DATA_START_INDEX])
#define closure_env(hdr)      ((hdr)[DATA_START_INDEX + 1])
#define closure_num_free(hdr) ((size_t)(hdr)[DATA_START_INDEX + 2])

// Convert a heap object pointer back to a tagged pointer word
static inline word ptr_to_word(word* hdr) {
    return (word)(uintptr_t)hdr | TAG_PTR;
}

#endif
