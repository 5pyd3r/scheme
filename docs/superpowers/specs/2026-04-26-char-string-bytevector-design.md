# Characters, Strings, and Bytevectors Design

> R7RS-small character, string, and bytevector procedures for bootstrapped Scheme

**Goal:** Implement the core character, string, and bytevector types and procedures as C primitives + Scheme library procedures

**Architecture:** Type infrastructure already exists in `types.h` (OBJ_TYPE_STRING=2, OBJ_TYPE_BYTEVECTOR=3, TAG_CHAR, helpers). Characters are immediate values (tag 0x1); strings are heap objects storing word-sized characters; bytevectors are heap objects storing packed bytes. Reader and display already handle string literals and basic character syntax. This spec adds the procedure implementations following the pattern established by `vector.c` / `lib.scm`.

---

## 1. Data Representation

### Characters (immediate, already implemented)
```
word_from_char('A')  →  (0x41 << 2) | 0x1
word_to_char(w)      →  (uint32_t)(w >> 2)
```
- `TAG_CHAR = 0x1` — distinct from fixnum, pointer, and immediate tags
- Reader supports `#\x` syntax; display outputs the raw character

### Strings (heap type OBJ_TYPE_STRING = 2)
```
[GC_hdr][type=2][length:word][char0:word][char1:word]...
```
- Each character stored as a full word (simple but space-wasteful)
- Total words: `3 + len`
- Existing macros: `string_length(hdr)`, `string_ref(hdr, i)`, `string_set(hdr, i, c)`
- Reader supports `"..."` literals; display outputs content

### Bytevectors (heap type OBJ_TYPE_BYTEVECTOR = 3)
```
[GC_hdr][type=3][length:word][u8_data:bytes...]
```
- Data stored as packed raw bytes
- Total words: `3 + ceil(len / sizeof(word))`
- Existing macros: `bytevector_length(hdr)`, `bytevector_data(hdr)` (returns `uint8_t*`)

### Named characters
Reader currently reads `#\x` for single characters. Need to add:
- `#\space` (U+0020)
- `#\newline` (U+000A)
- `#\tab` (U+0009)
- `#\return` (U+000D)

---

## 2. C Primitive Files

### `src/primitives/char.c` — 9 primitives

All follow signature: `word prim_xxx(vm_state_t* vm, int nargs)` with args on `vm->sp[0..nargs-1]`.

| Primitive | Args | Returns | Notes |
|---|---|---|---|
| `prim_charp` | 1 | `word_true()` or `word_false()` | Check `TAG_CHAR` |
| `prim_char_to_integer` | 1 | fixnum | Cast `word_to_char` to fixnum |
| `prim_integer_to_char` | 1 | char word | Range check (0-0x10FFFF) |
| `prim_char_eq` | 2 | bool | Compare `word_to_char` values |
| `prim_char_lt` | 2 | bool | `word_to_char` numeric comparison |
| `prim_char_gt` | 2 | bool | |
| `prim_char_le` | 2 | bool | |
| `prim_char_ge` | 2 | bool | |

### `src/primitives/string.c` — 10 primitives

| Primitive | Args | Returns | Notes |
|---|---|---|---|
| `prim_stringp` | 1 | bool | Check OBJ_TYPE_STRING |
| `prim_make_string` | 1-2 | string | `(make-string k [char])` - if 1 arg, fill with `#\space` |
| `prim_string` | variadic | string | Collect args into string |
| `prim_string_length` | 1 | fixnum | |
| `prim_string_ref` | 2 | char | Range check |
| `prim_string_set` | 3 | unspecified | Range check |
| `prim_string_eq` | 2 | bool | Element-by-element compare |
| `prim_string_lt` | 2 | bool | Lexicographic compare |
| `prim_string_to_list` | 1 | list | Build list of chars |
| `prim_list_to_string` | 1 | string | Build string from list of chars |

### `src/primitives/bytevector.c` — 8 primitives

| Primitive | Args | Returns | Notes |
|---|---|---|---|
| `prim_bytevectorp` | 1 | bool | Check OBJ_TYPE_BYTEVECTOR |
| `prim_make_bytevector` | 1-2 | bytevector | `(make-bytevector k [fill])` - default fill 0 |
| `prim_bytevector` | variadic | bytevector | Collect args |
| `prim_bytevector_length` | 1 | fixnum | |
| `prim_bytevector_u8_ref` | 2 | fixnum | Range check |
| `prim_bytevector_u8_set` | 3 | unspecified | Range check, value truncated to 8 bits |
| `prim_bytevector_to_u8_list` | 1 | list | Build list of fixnums |
| `prim_u8_list_to_bytevector` | 1 | bytevector | Build bytevector from list of fixnums |

### Bignote on `make-bytevector` variadic
Unlike `make-vector` (which takes exactly 2 args), `make-bytevector` takes 1 or 2 args. Check `nargs`:
- 1 arg: allocate and zero-fill
- 2 args: allocate and fill with `(uint8_t)word_to_fixnum(sp[1])`

---

## 3. VM Opcodes

No new VM opcodes. All procedure calls go through `OP_PRIM_CALL`. The existing `OP_MAKE_VEC`, `OP_VEC_REF`, `OP_VEC_SET` are only for vectors; string/bytevector access is via C primitives.

---

## 4. Primitive Registration (builtins.c)

Add forward declarations and `prim_table[]` entries for all new primitives. Follow the existing pattern:

```c
// Forward declarations
word prim_charp(vm_state_t* vm, int nargs);
word prim_char_to_integer(vm_state_t* vm, int nargs);
// ...

// prim_table entries
{"char?", prim_charp},
{"char->integer", prim_char_to_integer},
// ...
```

---

## 5. Reader Changes (reader.c)

Add named character support in `read_atom`. After the current `#\x` single-char handler:

```c
// Named characters
if (s[*pos] == '#' && s[*pos + 1] == '\\') {
    *pos += 2;
    // Check for named characters
    if (strncmp(s + *pos, "space", 5) == 0) {
        *pos += 5;
        return word_from_char(' ');
    }
    if (strncmp(s + *pos, "newline", 7) == 0) {
        *pos += 7;
        return word_from_char('\n');
    }
    if (strncmp(s + *pos, "tab", 3) == 0) {
        *pos += 3;
        return word_from_char('\t');
    }
    if (strncmp(s + *pos, "return", 6) == 0) {
        *pos += 6;
        return word_from_char('\r');
    }
    // Single character
    char c = s[*pos]; (*pos)++;
    return word_from_char((unsigned char)c);
}
```

---

## 6. Scheme Library (lib.scm)

Loaded in Phase 1 by C compiler, so follow the same constraints: avoid `let`, use top-level helpers with explicit parameter passing.

### Character procedures

Uses direct expression evaluation (no internal `define` — C compiler doesn't support it).

```scheme
; --- Character predicates ---

(define char-alphabetic?
  (lambda (c)
    (or (and (>= (char->integer c) 65) (<= (char->integer c) 90))
        (and (>= (char->integer c) 97) (<= (char->integer c) 122)))))

(define char-numeric?
  (lambda (c)
    (and (>= (char->integer c) 48) (<= (char->integer c) 57))))

(define char-whitespace?
  (lambda (c)
    (or (char=? c #\space) (char=? c #\newline)
        (char=? c #\tab) (char=? c #\return))))

(define char-upper?
  (lambda (c)
    (and (>= (char->integer c) 65) (<= (char->integer c) 90))))

(define char-lower?
  (lambda (c)
    (and (>= (char->integer c) 97) (<= (char->integer c) 122))))

; --- Case conversion ---

(define char-upcase
  (lambda (c)
    (if (and (>= (char->integer c) 97) (<= (char->integer c) 122))
        (integer->char (- (char->integer c) 32))
        c)))

(define char-downcase
  (lambda (c)
    (if (and (>= (char->integer c) 65) (<= (char->integer c) 90))
        (integer->char (+ (char->integer c) 32))
        c)))

; --- Case-insensitive comparison ---

(define char-ci=?
  (lambda (a b) (char=? (char-downcase a) (char-downcase b))))

(define char-ci<?
  (lambda (a b) (char<? (char-downcase a) (char-downcase b))))

(define char-ci>?
  (lambda (a b) (char>? (char-downcase a) (char-downcase b))))

(define char-ci<=?
  (lambda (a b) (char<=? (char-downcase a) (char-downcase b))))

(define char-ci>=?
  (lambda (a b) (char>=? (char-downcase a) (char-downcase b))))
```

### String procedures

```scheme
(define _str-cpy
  (lambda (src dst i)
    (if (< i (string-length src))
        (begin
          (string-set! dst i (string-ref src i))
          (_str-cpy src dst (+ i 1)))
        dst)))

(define string-copy
  (lambda (s)
    (_str-cpy s (make-string (string-length s) #\space) 0)))

(define _str-app
  (lambda (a b la lb result)
    (_str-cpy a result 0)
    (_str-cpy b result la)
    result))

(define string-append
  (lambda (a b)
    (_str-app a b (string-length a) (string-length b)
              (make-string (+ (string-length a) (string-length b)) #\space))))

(define string-upcase
  (lambda (s)
    (_str-map char-upcase s (make-string (string-length s) #\space) 0)))

(define string-downcase
  (lambda (s)
    (_str-map char-downcase s (make-string (string-length s) #\space) 0)))

(define _str-map
  (lambda (proc src dst i)
    (if (< i (string-length src))
        (begin
          (string-set! dst i (proc (string-ref src i)))
          (_str-map proc src dst (+ i 1)))
        dst)))
```

### Bytevector procedures

```scheme
(define _bv-cpy
  (lambda (src dst i)
    (if (< i (bytevector-length src))
        (begin
          (bytevector-u8-set! dst i (bytevector-u8-ref src i))
          (_bv-cpy src dst (+ i 1)))
        dst)))

(define bytevector-copy
  (lambda (bv)
    (_bv-cpy bv (make-bytevector (bytevector-length bv)) 0)))

(define _bv-app
  (lambda (a b la lb result)
    (_bv-cpy a result 0)
    (_bv-cpy b result la)
    result))

(define bytevector-append
  (lambda (a b)
    (_bv-app a b (bytevector-length a) (bytevector-length b)
             (make-bytevector (+ (bytevector-length a) (bytevector-length b)) 0))))
```

---

## 7. Build System

### `src/meson.build` — add 3 new source files:

```meson
scheme_core_sources = files(
  ...
  'primitives/vector.c',
  'primitives/char.c',       # new
  'primitives/string.c',     # new
  'primitives/bytevector.c', # new
)
```

### `tests/c/meson.build` — add 3 new test executables:

```meson
test_char = executable('test_char',
  'test_char.c',
  include_directories: test_scheme_inc,
  link_with: scheme_core,
  link_args: ['-lm'],
)
test('char', test_char)

test_string = executable('test_string',
  'test_string.c',
  include_directories: test_scheme_inc,
  link_with: scheme_core,
  link_args: ['-lm'],
)
test('string', test_string)

test_bytevector = executable('test_bytevector',
  'test_bytevector.c',
  include_directories: test_scheme_inc,
  link_with: scheme_core,
  link_args: ['-lm'],
)
test('bytevector', test_bytevector)
```

---

## 8. C Tests

### `tests/c/test_char.c`

```c
#include "types.h"
#include <stdio.h>
#include <assert.h>

// Minimal VM stub for primitive testing
typedef struct { word sp[16]; int error_code; int nargs; } test_vm;
#define vm_state_t test_vm

// word prim_charp(vm_state_t* vm, int nargs) { ... }
// (declarations from char.c)

void test_char_pred(void) {
    vm_state_t vm = {0};
    vm.sp[0] = word_from_char('A');
    word result = prim_charp(&vm, 1);
    assert(result == word_true());
    vm.sp[0] = word_from_fixnum(65);
    result = prim_charp(&vm, 1);
    assert(result == word_false());
    printf("PASS: char? tests\n");
}

void test_char_conversion(void) {
    vm_state_t vm = {0};
    vm.sp[0] = word_from_char('A');
    word result = prim_char_to_integer(&vm, 1);
    assert(is_fixnum(result));
    assert(word_to_fixnum(result) == 65);
    vm.sp[0] = word_from_fixnum(65);
    result = prim_integer_to_char(&vm, 1);
    assert(is_char(result));
    assert(word_to_char(result) == 'A');
    printf("PASS: char conversion tests\n");
}

void test_char_comparison(void) {
    vm_state_t vm = {0};
    vm.sp[0] = word_from_char('A');
    vm.sp[1] = word_from_char('A');
    assert(prim_char_eq(&vm, 2) == word_true());
    assert(prim_char_lt(&vm, 2) == word_false());
    vm.sp[1] = word_from_char('B');
    assert(prim_char_lt(&vm, 2) == word_true());
    assert(prim_char_gt(&vm, 2) == word_false());
    printf("PASS: char comparison tests\n");
}

int main(void) {
    test_char_pred();
    test_char_conversion();
    test_char_comparison();
    printf("ALL PASS\n");
    return 0;
}
```

### `tests/c/test_string.c`

Similar pattern — tests for string?, make-string, string-length, string-ref, string-set!, string->list, list->string, string=?, string<?.

### `tests/c/test_bytevector.c`

Similar pattern — tests for bytevector?, make-bytevector, bytevector-length, bytevector-u8-ref, bytevector-u8-set!, bytevector->u8-list, u8-list->bytevector.

---

## 9. Scheme Tests

### `tests/scheme/test-char.ss`

```scheme
; tests/scheme/test-char.ss

(display (char? #\a)) (newline)
(display (char? 42)) (newline)
(display (char->integer #\A)) (newline)
(display (integer->char 65)) (newline)

(define cup (char-upcase #\a))
(display (char=? cup #\A)) (newline)

(define cdown (char-downcase #\A))
(display (char=? cdown #\a)) (newline)

(display (char-ci=? #\a #\A)) (newline)
(display (char-alphabetic? #\x)) (newline)
(display (char-numeric? #\5)) (newline)
(display (char-whitespace? #\space)) (newline)
(display (char-upper? #\A)) (newline)
(display (char-lower? #\a)) (newline)
```

### `tests/scheme/test-string.ss`

```scheme
; tests/scheme/test-string.ss

(define s1 "hello")
(display (string? s1)) (newline)
(display (string-length s1)) (newline)

(define sr (string-ref s1 1))
(display (char=? sr #\e)) (newline)

(define ms (make-string 3 #\z))
(display (string=? ms "zzz")) (newline)

(string-set! ms 1 #\a)
(display (string=? ms "zaz")) (newline)

(define scol (string-copy s1))
(display (string=? scol s1)) (newline)

(define sapp (string-append "ab" "cd"))
(display (string=? sapp "abcd")) (newline)

(define sl (string->list "abc"))
(display (equal? sl '(#\a #\b #\c))) (newline)

(define ls (list->string '(#\x #\y)))
(display (string=? ls "xy")) (newline)

(define sup (string-upcase "aBc"))
(display (string=? sup "ABC")) (newline)

(define sdown (string-downcase "XyZ"))
(display (string=? sdown "xyz")) (newline)
```

### `tests/scheme/test-bytevector.ss`

```scheme
; tests/scheme/test-bytevector.ss

(define bv1 (bytevector 1 2 3))
(display (bytevector? bv1)) (newline)
(display (bytevector-length bv1)) (newline)

(define bvr (bytevector-u8-ref bv1 1))
(display (= bvr 2)) (newline)

(define mbv (make-bytevector 3 255))
(bytevector-u8-set! mbv 0 42)
(display (= (bytevector-u8-ref mbv 0) 42)) (newline)

(define bvl (bytevector->u8-list bv1))
(display (equal? bvl '(1 2 3))) (newline)

(define lbv (u8-list->bytevector '(10 20)))
(display (= (bytevector-u8-ref lbv 1) 20)) (newline)

(define bvc (bytevector-copy bv1))
(display (= (bytevector-u8-ref bvc 0) 1)) (newline)

(define bva (bytevector-append (bytevector 1 2) (bytevector 3 4)))
(display (= (bytevector-length bva) 4)) (newline)
(display (= (bytevector-u8-ref bva 2) 3)) (newline)
```

---

## 10. Implementation Order

1. **char.c** + C tests — character primitives (independent, no dependencies)
2. **string.c** + C tests — string primitives (independent)
3. **bytevector.c** + C tests — bytevector primitives (independent)
4. **reader.c** — named character support
5. **builtins.c** — register all new primitives
6. **meson.build** — add source files
7. **lib.scm** — add Scheme character, string, bytevector procedures
8. **Scheme tests** — test-char.ss, test-string.ss, test-bytevector.ss
9. **Smoke test** — verify nothing regressed

---

## 11. Edge Cases & Error Handling

| Case | Behavior |
|---|---|
| `integer->char` with out-of-range value (>= 0x110000) | `vm->error_code = 1`, return `word_nil()` |
| `string-ref` with negative or out-of-bounds index | `vm->error_code = 1`, return `word_nil()` |
| `string-set!` with non-char value | `vm->error_code = 1`, return `word_nil()` |
| `make-bytevector` with negative length | `vm->error_code = 1`, return `word_nil()` |
| `bytevector-u8-set!` with value > 255 | Truncate to 8 bits (low byte) |
| `string->list` on empty string | Return `()` (nil) |
| `bytevector->u8-list` on empty bytevector | Return `()` (nil) |

---

## 12. Files Summary

| Action | Path |
|---|---|
| **Create** | `src/primitives/char.c` |
| **Create** | `src/primitives/string.c` |
| **Create** | `src/primitives/bytevector.c` |
| **Create** | `tests/c/test_char.c` |
| **Create** | `tests/c/test_string.c` |
| **Create** | `tests/c/test_bytevector.c` |
| **Create** | `tests/scheme/test-char.ss` |
| **Create** | `tests/scheme/test-string.ss` |
| **Create** | `tests/scheme/test-bytevector.ss` |
| **Modify** | `src/vm/builtins.c` |
| **Modify** | `src/meson.build` |
| **Modify** | `src/scheme/lib.scm` |
| **Modify** | `src/reader/reader.c` |
| **Modify** | `tests/c/meson.build` |
