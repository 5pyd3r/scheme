# List and Vector Procedures Implementation Design

> **Phase:** Post-numeric-tower, pre-standard-library
> **Goal:** Implement R7RS-small list and vector operations as a foundation for Scheme standard library
> **Architecture:** List procedures in Scheme layer, vector primitives in C, vector utilities in Scheme
> **Tech Stack:** C11 primitives + Scheme library (loaded during bootstrap)

---

## C Primitive Layer

### New file: `src/primitives/vector.c`

Seven primitives for direct heap-allocated vector access using the existing OBJ_TYPE_VECTOR (1) type tag and vector_length/vector_elem/vector_set helpers from types.h.

- **`vector?`** — check OBJ_TYPE_VECTOR on pointer-tagged arg
- **`make-vector`** — allocate with `vm->gc->alloc_words(3 + n)`, store `n` at `DATA_START_INDEX`, fill elements with initial value
- **`vector`** — variadic: read nargs from stack, allocate, copy all args as elements
- **`vector-length`** — read `vector_length(hdr)`
- **`vector-ref`** — bounds check index, read `vector_elem(hdr, i)`
- **`vector-set!`** — bounds check index, write `vector_set(hdr, i, val)`
- **`list->vector`** — iterate pair chain, allocate, fill
- **`vector->list`** — iterate vector elements, cons into list. Note: returns a list, no allocation needed beyond cons cells.

### Modified: `src/primitives/pair.c`

- **`equal?`** — structural recursive comparison: if both pairs, recurse car+cdr; if both vectors, recurse element-by-element; otherwise fall back to eqv?. Must handle circular structure? For initial implementation, no circular detection (follow R7RS which says equal? on circular structures is unspecified).

### Modified: `src/vm/builtins.c`

Register all new primitives in prim_table with forward declarations.

### Modified: `src/vm/vm.c`

Add bytecode handlers for OP_MAKE_VEC, OP_VEC_REF, OP_VEC_SET.

---

## Scheme Library Layer

### New file: `src/scheme/lib.scm`

Loaded during Phase 1 bootstrap, after `compiler.scm`. Provides all list/vector procedures that can be expressed with existing or new C primitives.

#### List Procedures
All use existing primitives (cons, car, cdr, pair?, null?, eq?, eqv?, equal?):

- **`list . args`** — returns args unchanged (primitive in R7RS)
- **`list? x`** — proper-list detection (walk until null or circular)
- **`length list`** — iterate counting
- **`append . lists`** — concatenate (two-arg + variadic via fold)
- **`reverse list`** — fold cons
- **`list-tail list k`** — k cdr operations
- **`list-ref list k`** — car of list-tail
- **`list-set! list k obj`** — set-car! after k cdr
- **`memq obj list`** — eq? search
- **`memv obj list`** — eqv? search
- **`member obj list`** — equal? search
- **`assq obj alist`** — eq? association lookup
- **`assv obj alist`** — eqv? association lookup
- **`assoc obj alist`** — equal? association lookup
- **`list-copy list`** — copy pair structure

#### Iteration
- **`map proc list`** — apply proc to each element, collect results
- **`for-each proc list`** — apply proc for side effects
- **`filter pred list`** — keep elements satisfying pred (extension)

#### Vector Utilities (Scheme layer on C primitives)
- **`vector-fill! vec fill`** — iterate vector-ref + vector-set!
- **`vector-copy vec`** — allocate + copy elements
- **`vector-append . vecs`** — concatenate vectors
- **`vector-map proc vec`** — map over vector

### Modified: `src/main.c`

Load `src/scheme/lib.scm` after `src/scheme/compiler.scm` during Phase 1 bootstrap.

---

## Type Hierarchy & Memory Layout

```
Vector:  [GC_hdr][OBJ_TYPE_VECTOR=1][length:word][elem0][elem1]...
         total_words = 3 + length
         DATA_START_INDEX = 2 (length at hdr[2])
         elements start at hdr[DATA_START_INDEX + 1]

Pair:    [GC_hdr][OBJ_TYPE_PAIR=0][car][cdr]  (4 words total)
```

---

## Error Handling

- `make-vector` with negative/non-fixnum length → `vm->error_code = 1`
- `vector-ref`/`vector-set!` out-of-bounds → `vm->error_code = 1`
- `equal?` on circular structures → unspecified (may not terminate, per R7RS)
- Scheme-level procedures: use existing conventions (no explicit error signaling, just rely on primitives error behavior)

---

## Test Plan

### C-level tests (`tests/c/test_listvec.c`)
- make-vector with size and fill
- vector length/ref/set round-trip
- vector? on vector vs. non-vector
- equal? on pairs, vectors, non-identical fixnums
- equal? structural equality on nested pairs

### Scheme-level tests (`tests/scheme/test-listvec.ss`)
- list, length, append, reverse
- memq, member, assoc
- map, for-each, filter
- vector operations (make/fill/copy/append/map)
- list<->vector round-trip
- equal? on various types
