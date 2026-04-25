# Stage 0: C Core Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a working Scheme bytecode VM in C that can parse Scheme source, compile it to bytecode, and execute it — the foundation for all later bootstrapping.

**Architecture:** C11 program with PAL→GC→VM→Reader→BootstrapCompiler pipeline. All heap objects via tagged pointers. Mark-sweep GC. Stack-based VM with ~40 instructions.

**Tech Stack:** C11, Meson build system, POSIX (initial target)

---

### Task 1: Meson build system + directory scaffold

**Files:**
- Create: `meson.build`
- Create: `meson_options.txt`
- Create: `src/meson.build`
- Create: `tests/meson.build`
- Create: `tests/c/meson.build`

- [ ] **Step 1: Create directory structure**

```bash
mkdir -p src/include src/pal src/gc src/vm src/reader src/bootstrap src/primitives tests/c lib/scheme boot docs/superpowers/plans
```

- [ ] **Step 2: Write meson_options.txt**

```meson
option('gc_impl', type: 'string', value: 'marksweep', description: 'GC implementation')
```

- [ ] **Step 3: Write root meson.build**

```meson
project('scheme', 'c',
  default_options: [
    'c_std=c11',
    'warning_level=2',
  ]
)

subdir('src')
subdir('tests')
```

- [ ] **Step 4: Write src/meson.build**

```meson
scheme_sources = files(
  'main.c',
  'pal/pal_posix.c',
  'gc/gc.c',
  'vm/vm.c',
  'vm/builtins.c',
  'reader/reader.c',
  'bootstrap/compiler.c',
  'primitives/arith.c',
  'primitives/pair.c',
  'primitives/port.c',
)

scheme_inc = include_directories('include')

executable('scheme',
  scheme_sources,
  include_directories: scheme_inc,
)
```

- [ ] **Step 5: Write tests/meson.build**

```meson
subdir('c')
```

- [ ] **Step 6: Write tests/c/meson.build**

```meson
test_types = executable('test_types',
  'test_types.c',
  include_directories: scheme_inc,
)
test('types', test_types)

test_gc = executable('test_gc',
  'test_gc.c',
  include_directories: scheme_inc,
)
test('gc', test_gc)
```

- [ ] **Step 7: Verify meson setup**

```bash
meson setup build
meson compile -C build
```

Expected: build succeeds (object files created, linking may fail due to missing symbols — OK at this stage)

- [ ] **Step 8: Commit**

```bash
git add meson.build meson_options.txt src/meson.build tests/meson.build tests/c/meson.build
git commit -m "stage0: Meson build system scaffold"
```

---

### Task 2: Type system — tagged pointers

**Files:**
- Create: `src/include/types.h`

This is the single most important header — every other module depends on it.

- [ ] **Step 1: Write src/include/types.h**

```c
#ifndef SCHEME_TYPES_H
#define SCHEME_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// word: the fundamental unit of data in the VM (64 bits)
typedef uint64_t word;

// ---- Tagged pointer scheme ----
// Low 2 bits encode type, heap objects are 8-byte aligned so
// real pointers always have 00 in the low 2 bits.
#define TAG_MASK    ((word)0x3)
#define TAG_FIXNUM  ((word)0x0)  // 00: 62-bit signed integer
#define TAG_CHAR    ((word)0x1)  // 01: Unicode scalar value
#define TAG_PTR     ((word)0x2)  // 10: pointer to GC-managed heap object
#define TAG_IMM     ((word)0x3)  // 11: immediate (bool, nil, eof)

// Immediate sub-tags (stored in bits 2-3)
#define IMM_FALSE ((word)(0x0 << 2))
#define IMM_TRUE  ((word)(0x1 << 2))
#define IMM_NIL   ((word)(0x2 << 2))
#define IMM_EOF   ((word)(0x3 << 2))
#define IMM_MASK  ((word)(0x3 << 2))

// ---- Fixnum ----
// value encoded in bits 2..63, sign-extended
#define word_from_fixnum(n)   ((word)(((int64_t)(n) << 2) | TAG_FIXNUM))
#define word_to_fixnum(w)     ((int64_t)(w) >> 2)

// ---- Char ----
// Unicode scalar encoded in bits 2..31
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
#define is_bool(w)    (is_imm(w) && ((w) & IMM_MASK) != IMM_NIL && ((w) & IMM_MASK) != IMM_EOF)

// ---- Heap object layout ----
// Each heap object is: [GC_header(1 word)] [type_word(1 word)] [data...]
// ptr_from_word returns address of GC_header.
// Access: hdr[0]=GC_header, hdr[1]=type, hdr[2+]=data
#define GC_HEADER_SIZE  1   // in words
#define TYPE_WORD_INDEX 1
#define DATA_START_INDEX 2

// GC header bit layout:
//   bit 63..3: object size in words (including header word)
//   bit 2..1:  color (00=white, 01=grey, 10=black, 11=free)
//   bit 0:     mark bit
#define GC_MARK_BIT   0x1
#define GC_COLOR_BITS 0x6
#define GC_SIZE_SHIFT 3
#define GC_SIZE_MASK  (~((word)0x7))

#define gc_marked(hdr)    ((hdr) & GC_MARK_BIT)
#define gc_set_mark(hdr)  ((hdr) | GC_MARK_BIT)
#define gc_clr_mark(hdr)  ((hdr) & ~GC_MARK_BIT)
#define gc_size(hdr)      ((hdr) >> GC_SIZE_SHIFT)
#define gc_set_size(hdr, sz) (((hdr) & GC_MARK_BIT) | ((word)(sz) << GC_SIZE_SHIFT))

// Convert tagged pointer word to heap object base address
static inline word* ptr_from_word(word w) {
    return (word*)(uintptr_t)(w & ~TAG_MASK);
}

// Access heap object fields
static inline word* obj_data(word* hdr) {
    return hdr + DATA_START_INDEX;
}

// ---- Heap object type tags (stored at hdr[1]) ----
// Heap object type tags (stored at hdr[TYPE_WORD_INDEX])
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

// ---- Pair helpers (data: [car][cdr]) ----
#define pair_car(hdr)   ((hdr)[DATA_START_INDEX])
#define pair_cdr(hdr)   ((hdr)[DATA_START_INDEX + 1])
#define pair_set_car(hdr, v) ((hdr)[DATA_START_INDEX] = (v))
#define pair_set_cdr(hdr, v) ((hdr)[DATA_START_INDEX + 1] = (v))

// ---- Vector helpers (data: [length][elem1][elem2]...) ----
#define vector_length(hdr)   ((size_t)(hdr)[DATA_START_INDEX])
#define vector_elem(hdr, i)  ((hdr)[DATA_START_INDEX + 1 + (i)])
#define vector_set(hdr, i, v) ((hdr)[DATA_START_INDEX + 1 + (i)] = (v))

// ---- String helpers (data: [length][char1_word][char2_word]...) ----
// Each character stored as a word (Unicode scalar) for simplicity
#define string_length(hdr)   ((size_t)(hdr)[DATA_START_INDEX])
#define string_ref(hdr, i)   ((hdr)[DATA_START_INDEX + 1 + (i)])
#define string_set(hdr, i, c) ((hdr)[DATA_START_INDEX + 1 + (i)] = (word)(c))

// ---- Bytevector helpers (data: [length][byte0]...[byteN] packed 8/word) ----
#define bytevector_length(hdr)   ((size_t)(hdr)[DATA_START_INDEX])
// bytes stored one per byte within words, access via byte pointer
#define bytevector_data(hdr)     ((uint8_t*)((hdr) + DATA_START_INDEX + 1))

// ---- Symbol helpers ----
#define symbol_string(hdr)     ((hdr)[DATA_START_INDEX])

// ---- Closure helpers (data: [code_obj][env_ptr][num_free]) ----
#define closure_code(hdr)     ((hdr)[DATA_START_INDEX])
#define closure_env(hdr)      ((hdr)[DATA_START_INDEX + 1])
#define closure_num_free(hdr) ((size_t)(hdr)[DATA_START_INDEX + 2])

// Convert a heap object pointer back to a tagged pointer word
static inline word ptr_to_word(word* hdr) {
    return (word)(uintptr_t)hdr | TAG_PTR;
}

#endif
```

- [ ] **Step 2: Write compile test**

```bash
echo '#include "types.h"' > /tmp/test_types.c
echo 'int main(void) {
  word w = word_from_fixnum(42);
  return is_fixnum(w) && word_to_fixnum(w) == 42 ? 0 : 1;
}' >> /tmp/test_types.c
cc -std=c11 -Isrc/include -o /tmp/test_types /tmp/test_types.c && /tmp/test_types && echo "PASS"
```
Expected: PASS

- [ ] **Step 3: Commit**

```bash
git add src/include/types.h
git commit -m "stage0: type system with tagged pointer macros"
```

---

### Task 3: PAL — POSIX implementation

**Files:**
- Create: `src/include/pal.h`
- Create: `src/pal/pal_posix.c`

- [ ] **Step 1: Write src/include/pal.h**

```c
#ifndef SCHEME_PAL_H
#define SCHEME_PAL_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    void*  (*mmap_alloc)(size_t size);
    void   (*mmap_free)(void* ptr, size_t size);

    int    (*file_open)(const char* path, int mode);
    int    (*file_close)(int fd);
    int64_t (*file_read)(int fd, void* buf, uint64_t nbytes);
    int64_t (*file_write)(int fd, const void* buf, uint64_t nbytes);
    int64_t (*file_seek)(int fd, int64_t offset, int whence);
    int    (*file_exists)(const char* path);

    void*  (*dl_open)(const char* path);
    void   (*dl_close)(void* handle);
    void*  (*dl_sym)(void* handle, const char* symbol);
    char*  (*dl_error)(void);

    int64_t (*current_time_ms)(void);
    void   (*exit_fn)(int code);

    int    (*last_error)(void);
    char*  (*error_message)(int err);

    void*  state;
} pal_interface;

#define PAL_O_RDONLY 0
#define PAL_O_WRONLY 1
#define PAL_O_RDWR   2
#define PAL_O_CREAT  4
#define PAL_O_APPEND 8

#define PAL_SEEK_SET 0
#define PAL_SEEK_CUR 1
#define PAL_SEEK_END 2

pal_interface* pal_init(void);

#endif
```

- [ ] **Step 2: Write src/pal/pal_posix.c**

```c
#include "pal.h"
#include <sys/mman.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static void* pal_mmap_alloc(size_t size) {
    void* p = mmap(NULL, size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return (p == MAP_FAILED) ? NULL : p;
}

static void pal_mmap_free(void* ptr, size_t size) {
    munmap(ptr, size);
}

static int pal_file_open(const char* path, int mode) {
    int flags = 0;
    if (mode & PAL_O_RDWR)       flags = O_RDWR;
    else if (mode & PAL_O_WRONLY) flags = O_WRONLY;
    else                         flags = O_RDONLY;
    if (mode & PAL_O_CREAT)  flags |= O_CREAT;
    if (mode & PAL_O_APPEND) flags |= O_APPEND;
    return open(path, flags, 0666);
}

static int      pal_file_close(int fd)           { return close(fd); }
static int64_t  pal_file_read(int fd, void* b, uint64_t n) { return read(fd, b, n); }
static int64_t  pal_file_write(int fd, const void* b, uint64_t n) { return write(fd, b, n); }

static int64_t pal_file_seek(int fd, int64_t offset, int whence) {
    int w = (whence == PAL_SEEK_SET) ? SEEK_SET :
            (whence == PAL_SEEK_CUR) ? SEEK_CUR : SEEK_END;
    return lseek(fd, offset, w);
}

static int pal_file_exists(const char* path) {
    return access(path, F_OK) == 0;
}

static void*  pal_dl_open(const char* p)  { return dlopen(p, RTLD_NOW | RTLD_LOCAL); }
static void   pal_dl_close(void* h)        { dlclose(h); }
static void*  pal_dl_sym(void* h, const char* s) { return dlsym(h, s); }
static char*  pal_dl_error(void)          { return dlerror(); }

static int64_t pal_current_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + (int64_t)tv.tv_usec / 1000;
}

static void pal_exit_fn(int code) { exit(code); }
static int  pal_last_error(void) { return errno; }
static char* pal_error_message(int e) { return strerror(e); }

pal_interface* pal_init(void) {
    static pal_interface pal = {
        .mmap_alloc     = pal_mmap_alloc,
        .mmap_free      = pal_mmap_free,
        .file_open      = pal_file_open,
        .file_close     = pal_file_close,
        .file_read      = pal_file_read,
        .file_write     = pal_file_write,
        .file_seek      = pal_file_seek,
        .file_exists    = pal_file_exists,
        .dl_open        = pal_dl_open,
        .dl_close       = pal_dl_close,
        .dl_sym         = pal_dl_sym,
        .dl_error       = pal_dl_error,
        .current_time_ms = pal_current_time_ms,
        .exit_fn        = pal_exit_fn,
        .last_error     = pal_last_error,
        .error_message  = pal_error_message,
        .state          = NULL,
    };
    return &pal;
}
```

- [ ] **Step 3: Verify compilation**

```bash
meson compile -C build
```
Expected: compiles clean (may have unresolved symbols from other units — fine)

- [ ] **Step 4: Commit**

```bash
git add src/include/pal.h src/pal/pal_posix.c
git commit -m "stage0: POSIX platform abstraction layer"
```

---

### Task 4: GC — mark-sweep with replaceable interface

**Files:**
- Create: `src/include/gc.h`
- Create: `src/gc/gc.c`

- [ ] **Step 1: Write src/include/gc.h**

```c
#ifndef SCHEME_GC_H
#define SCHEME_GC_H

#include "types.h"
#include <stddef.h>

typedef struct {
    // Allocate zeroed memory (returns word-aligned pointer)
    word* (*alloc_words)(size_t nwords);
    // Trigger a full GC cycle
    void  (*collect)(void);
    // Mark one root word (and recursively its sub-objects)
    void  (*mark_root)(word w);
    // Mark a range of stack words as roots
    void  (*mark_stack)(word* stack, size_t count);
    // Current heap usage in bytes
    size_t (*heap_used)(void);
    void*  state;
} gc_interface;

gc_interface* gc_init(void);

#endif
```

- [ ] **Step 2: Write src/gc/gc.c**

The GC maintains a side table (linked list) of all live objects. The GC header in the object stores size and mark bit. The side table allows sweep to iterate all objects without inline pointers.

```c
#include "gc.h"
#include "pal.h"
#include <string.h>

extern pal_interface* pal;

// ---- Entry in the side table ----
typedef struct obj_entry {
    struct obj_entry* next;
    word*             hdr;    // pointer to GC header word
    size_t            nwords; // total size in words
} obj_entry_t;

// ---- GC state ----
static obj_entry_t* all_objects = NULL;
static obj_entry_t* free_entries = NULL; // recycled entry structs
static size_t total_words_allocated = 0;
static bool collecting = false;

#define ALIGN_WORDS(n) (((n) + (sizeof(word) - 1)) / sizeof(word))

// ---- Side table helpers ----
static obj_entry_t* entry_alloc(void) {
    if (free_entries) {
        obj_entry_t* e = free_entries;
        free_entries = e->next;
        memset(e, 0, sizeof(obj_entry_t));
        return e;
    }
    return (obj_entry_t*)pal->mmap_alloc(sizeof(obj_entry_t));
}

static void entry_free(obj_entry_t* e) {
    e->next = free_entries;
    free_entries = e;
}

// ---- Allocate ----
static word* gc_alloc_words(size_t nwords) {
    // Align minimum to header + type + 1 word of data
    if (nwords < 3) nwords = 3;

    // TODO: use free-list reclamation
    // For stage 0: direct mmap allocation per object (inefficient but correct)
    word* block = (word*)pal->mmap_alloc(nwords * sizeof(word));
    if (!block) return NULL;

    memset(block, 0, nwords * sizeof(word));
    block[0] = (nwords << GC_SIZE_SHIFT);  // size in upper bits, mark=0

    obj_entry_t* e = entry_alloc();
    e->hdr = block;
    e->nwords = nwords;
    e->next = all_objects;
    all_objects = e;

    total_words_allocated += nwords;
    return block;
}

// ---- Mark phase ----
// Mark a word: if it's a heap pointer, mark the object and recursively
// trace its contained pointers.
static void mark_word(word w) {
    if (!is_ptr(w)) return;
    word* hdr = ptr_from_word(w);
    if (gc_marked(hdr[0])) return;  // already marked

    hdr[0] = gc_set_mark(hdr[0]);

    // Recursively mark based on type
    switch (obj_type(hdr)) {
    case OBJ_TYPE_PAIR:
        mark_word(pair_car(hdr));
        mark_word(pair_cdr(hdr));
        break;
    case OBJ_TYPE_VECTOR: {
        size_t len = vector_length(hdr);
        for (size_t i = 0; i < len; i++)
            mark_word(vector_elem(hdr, i));
        break;
    }
    case OBJ_TYPE_CLOSURE:
        mark_word(closure_env(hdr));
        break;
    case OBJ_TYPE_SYMBOL:
        mark_word(symbol_string(hdr));
        break;
    }
    // Strings, bytevectors, bignums contain no pointers
    // Flonums, rationals, complex contain no pointers (data is numeric)
}

static void gc_mark_root(word w) {
    mark_word(w);
}

static void gc_mark_stack(word* stack, size_t count) {
    for (size_t i = 0; i < count; i++)
        mark_word(stack[i]);
}

// ---- Sweep phase ----
static void gc_sweep(void) {
    obj_entry_t** prev = &all_objects;
    obj_entry_t* cur = all_objects;

    while (cur) {
        word* hdr = cur->hdr;
        if (gc_marked(hdr[0])) {
            // Live — clear mark for next cycle
            hdr[0] = gc_clr_mark(hdr[0]);
            prev = &cur->next;
            cur = cur->next;
        } else {
            // Dead — free
            obj_entry_t* dead = cur;
            *prev = cur->next;
            cur = cur->next;

            pal->mmap_free(dead->hdr, dead->nwords * sizeof(word));
            total_words_allocated -= dead->nwords;
            entry_free(dead);
        }
    }
}

// ---- Collect ----
static void gc_collect(void) {
    if (collecting) return;
    collecting = true;

    // TODO: VM should push roots before calling collect
    // Mark is driven by VM calling mark_root/mark_stack

    gc_sweep();
    collecting = false;
}

static size_t gc_heap_used(void) {
    return total_words_allocated * sizeof(word);
}

// ---- Init ----
gc_interface* gc_init(void) {
    static gc_interface gc = { 0 };
    gc.alloc_words = gc_alloc_words;
    gc.collect     = gc_collect;
    gc.mark_root   = gc_mark_root;
    gc.mark_stack  = gc_mark_stack;
    gc.heap_used   = gc_heap_used;
    return &gc;
}
```

- [ ] **Step 3: Write test — tests/c/test_types.c**

```c
#include "types.h"
#include <stdio.h>

int main(void) {
    // Test fixnum encoding
    word w = word_from_fixnum(42);
    if (!is_fixnum(w)) return 1;
    if (word_to_fixnum(w) != 42) return 2;
    if (is_ptr(w) || is_char(w) || is_imm(w)) return 3;

    // Test negative fixnum
    w = word_from_fixnum(-1);
    if (word_to_fixnum(w) != -1) return 4;

    // Test fixnum range
    int64_t max_val = (int64_t)1 << 61;
    w = word_from_fixnum(max_val - 1);
    if (word_to_fixnum(w) != max_val - 1) return 5;

    // Test immediate values
    if (!is_true(word_true())) return 6;
    if (!is_false(word_false())) return 7;
    if (!is_nil(word_nil())) return 8;
    if (!is_eof(word_eof())) return 9;
    if (is_bool(word_nil())) return 10;

    // Test char
    w = word_from_char('A');
    if (!is_char(w)) return 11;
    if (word_to_char(w) != 'A') return 12;

    printf("ALL types tests PASSED\n");
    return 0;
}
```

- [ ] **Step 4: Build and run test**

```bash
meson compile -C build && ./build/tests/c/test_types
```
Expected: `ALL types tests PASSED`

- [ ] **Step 5: Commit**

```bash
git add src/include/gc.h src/gc/gc.c tests/c/test_types.c tests/c/meson.build
git commit -m "stage0: type tests + GC interface and mark-sweep implementation"
```

---

### Task 5: VM — opcodes, code objects, and instruction dispatch

**Files:**
- Create: `src/vm/opcodes.h`
- Create: `src/include/vm.h`
- Create: `src/vm/vm.c`

- [ ] **Step 1: Write src/vm/opcodes.h**

```c
#ifndef SCHEME_OPCODES_H
#define SCHEME_OPCODES_H

// All opcodes are 1 byte, followed by 0-4 bytes of immediate operands

typedef enum {
    // Stack operations
    OP_NOP        = 0x00,
    OP_PUSH_NIL   = 0x01,
    OP_PUSH_TRUE  = 0x02,
    OP_PUSH_FALSE = 0x03,
    OP_PUSH_CONST = 0x04,  // +1B: const index
    OP_PUSH_INT   = 0x05,  // +4B: int32 value
    OP_POP        = 0x06,
    OP_DUP        = 0x07,
    OP_PUSH_ENV   = 0x08,  // +1B depth, +1B offset

    // Variable access
    OP_LREF       = 0x10,  // +1B: local index (relative to fp)
    OP_LSET       = 0x11,  // +1B: local index
    OP_FREF       = 0x12,  // +1B: depth, +1B: offset (free var in closure)
    OP_FSET       = 0x13,
    OP_GREF       = 0x14,  // +1B: global index
    OP_GSET       = 0x15,

    // Procedure and call
    OP_CLOSE      = 0x20,  // +2B: code_object_index, +1B: nfree
    OP_CALL       = 0x21,  // +1B: nargs
    OP_TAIL_CALL  = 0x22,  // +1B: nargs (reuses current frame)
    OP_APPLY      = 0x23,
    OP_RETURN     = 0x24,

    // Control flow
    OP_JMP        = 0x30,  // +2B: signed offset from next instruction
    OP_JMP_IF     = 0x31,  // +2B: offset (if true, jump)
    OP_JMP_IF_NOT = 0x32,  // +2B: offset (if false, jump)
    OP_CALL_CC    = 0x33,

    // Object operations
    OP_CONS       = 0x40,
    OP_CAR        = 0x41,
    OP_CDR        = 0x42,
    OP_SET_CAR    = 0x43,
    OP_SET_CDR    = 0x44,
    OP_MAKE_VEC   = 0x45,  // +1B: length
    OP_VEC_REF    = 0x46,
    OP_VEC_SET    = 0x47,

    // Primitive call
    OP_PRIM_CALL  = 0x50,  // +1B: nargs, +2B: prim_index

    // Multiple values
    OP_MV_CALL    = 0x60,  // +1B: nargs
    OP_RESET_VALS = 0x61,
    OP_PUSH_VALS  = 0x62,

    // Termination
    OP_HALT       = 0xFF,
} opcode_t;

#endif
```

- [ ] **Step 2: Write src/include/vm.h**

```c
#ifndef SCHEME_VM_H
#define SCHEME_VM_H

#include "types.h"
#include "gc.h"
#include "pal.h"

// VM state
typedef struct vm_state {
    gc_interface*  gc;
    pal_interface* pal;

    // Registers
    uint8_t* ip;      // instruction pointer (byte-level)
    word*   sp;       // stack pointer
    word*   fp;       // frame pointer
    word*   env;      // current environment chain
    word    acc;      // accumulator (single return value)

    // Stack
    word*   stack;    // operand/call stack base
    size_t  stack_cap;
    size_t  stack_len;

    // Code objects
    word**  code_objects;
    size_t  code_count;

    // Current executing code object (set by vm_execute)
    word*   current_code;

    // Globals
    word*   globals;  // array of global variable values
    size_t  global_count;
    int     next_global_slot;  // next available global slot for define

    // Primitive procedures
    word*   primitives;  // array of prim procedures

    // Error state
    int     error_code;
    word    error_arg;

    // GC state (set by VM for GC roots)
    bool    gc_active;
} vm_state_t;

// Initialize VM with GC and PAL instances
vm_state_t* vm_init(gc_interface* gc, pal_interface* pal);

// Load a bytecode code object
int vm_load_code(vm_state_t* vm, word* code_obj);

// Execute from the given code object index
word vm_execute(vm_state_t* vm, int entry_point);

// Register primitive procedure
int vm_register_prim(vm_state_t* vm, word prim);

#endif
```

- [ ] **Step 3: Write src/vm/vm.c — VM dispatch loop (skeleton with main loop)**

```c
#include "vm.h"
#include "opcodes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INITIAL_STACK_WORDS (64 * 1024)  // 512KB stack
#define INITIAL_GLOBALS     (256)

vm_state_t* vm_init(gc_interface* gc, pal_interface* pal) {
    vm_state_t* vm = (vm_state_t*)calloc(1, sizeof(vm_state_t));
    vm->gc = gc;
    vm->pal = pal;

    vm->stack = (word*)calloc(INITIAL_STACK_WORDS, sizeof(word));
    vm->stack_cap = INITIAL_STACK_WORDS;
    vm->sp = vm->stack;
    vm->fp = vm->stack;

    vm->globals = (word*)calloc(INITIAL_GLOBALS, sizeof(word));
    vm->global_count = INITIAL_GLOBALS;
    vm->next_global_slot = 0;

    vm->code_objects = (word**)calloc(64, sizeof(word*));
    vm->code_count = 0;

    return vm;
}

int vm_load_code(vm_state_t* vm, word* code_obj) {
    int idx = vm->code_count++;
    vm->code_objects = realloc(vm->code_objects, vm->code_count * sizeof(word*));
    vm->code_objects[idx] = code_obj;
    return idx;
}

// ---- Instruction decode helpers (byte-level ip) ----
static uint8_t  read_u8(uint8_t** ip)     { return *(*ip)++; }
static int32_t  read_s32(uint8_t** ip)    { int32_t v; memcpy(&v, *ip, 4); *ip += 4; return v; }
static int16_t  read_s16(uint8_t** ip)    { int16_t v; memcpy(&v, *ip, 2); *ip += 2; return v; }
static uint16_t read_u16(uint8_t** ip)    { uint16_t v; memcpy(&v, *ip, 2); *ip += 2; return v; }

// Code object layout: [GC_hdr][type][bytecode_len_bytes][bytecode_bytes...][consts...]
// bytecode_len is at word index 2 (byte offset 16 from object start)
static uint8_t* code_bytes(word* code_obj) {
    return (uint8_t*)(code_obj + 3);
}

static word* code_consts(word* code_obj) {
    size_t bc_len = (size_t)code_obj[2];  // bytecode length in bytes
    size_t bc_words = (bc_len + sizeof(word) - 1) / sizeof(word);
    return code_obj + 3 + bc_words;
}

word vm_execute(vm_state_t* vm, int entry_idx) {
    vm->current_code = vm->code_objects[entry_idx];
    vm->ip = code_bytes(vm->current_code);  // byte-level pointer

    for (;;) {
        uint8_t op = read_u8(&vm->ip);

        switch (op) {
        case OP_NOP:
            break;

        case OP_PUSH_NIL:
            *++vm->sp = word_nil();
            break;

        case OP_PUSH_TRUE:
            *++vm->sp = word_true();
            break;

        case OP_PUSH_FALSE:
            *++vm->sp = word_false();
            break;

        case OP_PUSH_CONST: {
            uint8_t idx = read_u8(&vm->ip);
            *++vm->sp = code_consts(vm->current_code)[idx];
            break;
        }

        case OP_PUSH_INT: {
            int32_t val = read_s32(&vm->ip);
            *++vm->sp = word_from_fixnum(val);
            break;
        }

        case OP_POP:
            vm->sp--;
            break;

        case OP_DUP: {
            word val = *vm->sp;
            *++vm->sp = val;
            break;
        }

        case OP_LREF: {
            uint8_t idx = read_u8(&vm->ip);
            *++vm->sp = vm->fp[-idx];
            break;
        }

        case OP_LSET: {
            uint8_t idx = read_u8(&vm->ip);
            vm->fp[-idx] = *vm->sp;
            break;
        }

        case OP_GREF: {
            uint8_t idx = read_u8(&vm->ip);
            *++vm->sp = vm->globals[idx];
            break;
        }

        case OP_GSET: {
            uint8_t idx = read_u8(&vm->ip);
            vm->globals[idx] = *vm->sp;
            break;
        }

        // ---- Object operations ----
        case OP_CONS: {
            word cdr = *vm->sp--;
            word car = *vm->sp;
            word* pair = vm->gc->alloc_words(4);
            obj_set_type(pair, OBJ_TYPE_PAIR);
            pair_car(pair) = car;
            pair_cdr(pair) = cdr;
            *vm->sp = ptr_to_word(pair);
            break;
        }

        case OP_CAR: {
            word* pair = ptr_from_word(*vm->sp);
            *vm->sp = pair_car(pair);
            break;
        }

        case OP_CDR: {
            word* pair = ptr_from_word(*vm->sp);
            *vm->sp = pair_cdr(pair);
            break;
        }

        // ---- Control flow ----
        case OP_JMP: {
            int16_t offset = read_s16(&vm->ip);
            vm->ip += offset;  // offset is relative to next instruction
            break;
        }

        case OP_JMP_IF: {
            int16_t offset = read_s16(&vm->ip);
            word val = *vm->sp--;
            if (!is_false(val))
                vm->ip += offset;
            break;
        }

        case OP_JMP_IF_NOT: {
            int16_t offset = read_s16(&vm->ip);
            word val = *vm->sp--;
            if (is_false(val))
                vm->ip += offset;
            break;
        }

        // ---- Primitive call ----
        // Args pushed left-to-right, *++vm->sp post-inc.
        // After N pushes, sp points at last arg.
        // Decrement by N-1 to make sp point at first arg:
        //   sp[0] = first arg, sp[1] = second arg, ...
        case OP_PRIM_CALL: {
            uint8_t nargs = read_u8(&vm->ip);
            uint16_t prim_idx = read_u16(&vm->ip);
            vm->sp -= (nargs - 1);  // sp now at first arg
            word result = vm_dispatch_prim(vm, prim_idx, nargs);
            vm->sp -= 1;  // pop remaining
            *++vm->sp = result;
            break;
        }

        // ---- Termination ---
        case OP_RETURN: {
            word val = *vm->sp;
            return val;
        }

        case OP_HALT:
            return *vm->sp;

        default:
            fprintf(stderr, "unknown opcode: 0x%02x\n", op);
            vm->error_code = 1;
            return word_nil();
        }
    }
}
```

> **Note:** Step 3 has an unresolved reference (`vm_dispatch_prim`, defined in builtins.c Task 6). The VM dispatch loop structure is the deliverable; the prim dispatch is resolved in the primitives task.

- [ ] **Step 4: Remove duplicate ptr_to_word declaration**

`ptr_to_word` was already added to types.h in Task 2. Remove any duplicate definition from vm.c or other files.

- [ ] **Step 5: Verify compile**

```bash
meson compile -C build
```
Expected: compiles clean (linker may still have undefined `vm_dispatch_prim` — resolved in Task 6)

- [ ] **Step 6: Commit**

```bash
git add src/vm/opcodes.h src/include/vm.h src/vm/vm.c
git commit -m "stage0: VM opcodes, state, and instruction dispatch skeleton"
```

---

### Task 6: Primitive procedure dispatch

**Files:**
- Create: `src/include/prim.h`
- Create: `src/vm/builtins.c`

- [ ] **Step 1: Write src/include/prim.h**

```c
#ifndef SCHEME_PRIM_H
#define SCHEME_PRIM_H

#include "types.h"
#include "vm.h"

// Primitive procedure: takes args from stack, returns result
// nargs tells how many arguments were pushed
// (vm, nargs) -> result word
typedef word (*prim_fn_t)(vm_state_t* vm, int nargs);

// Register all built-in primitives with the VM
void prim_init_all(vm_state_t* vm);

// VM calls this when executing OP_PRIM_CALL
word vm_dispatch_prim(vm_state_t* vm, int prim_index, int nargs);

// Look up primitive by name, return index or -1 if not found
int prim_lookup(const char* name);

#endif
```

- [ ] **Step 2: Write src/vm/builtins.c — prim dispatch + init**

```c
#include "prim.h"
#include "opcodes.h"
#include <stdio.h>
#include <string.h>

// Forward declarations of all primitive implementations
// (defined in src/primitives/*.c)
word prim_cons(vm_state_t* vm, int nargs);
word prim_car(vm_state_t* vm, int nargs);
word prim_cdr(vm_state_t* vm, int nargs);
word prim_set_car(vm_state_t* vm, int nargs);
word prim_set_cdr(vm_state_t* vm, int nargs);
word prim_null(vm_state_t* vm, int nargs);
word prim_pair(vm_state_t* vm, int nargs);
word prim_eq(vm_state_t* vm, int nargs);
word prim_eqv(vm_state_t* vm, int nargs);
word prim_add(vm_state_t* vm, int nargs);
word prim_sub(vm_state_t* vm, int nargs);
word prim_mul(vm_state_t* vm, int nargs);
word prim_div(vm_state_t* vm, int nargs);
word prim_lt(vm_state_t* vm, int nargs);
word prim_gt(vm_state_t* vm, int nargs);
word prim_display(vm_state_t* vm, int nargs);
word prim_newline(vm_state_t* vm, int nargs);

// Primitive table
typedef struct {
    const char* name;
    prim_fn_t   fn;
} prim_entry_t;

static prim_entry_t prim_table[] = {
    {"cons",     prim_cons},
    {"car",      prim_car},
    {"cdr",      prim_cdr},
    {"set-car!", prim_set_car},
    {"set-cdr!", prim_set_cdr},
    {"null?",    prim_null},
    {"pair?",    prim_pair},
    {"eq?",      prim_eq},
    {"eqv?",     prim_eqv},
    {"+",        prim_add},
    {"-",        prim_sub},
    {"*",        prim_mul},
    {"/",        prim_div},
    {"<",        prim_lt},
    {">",        prim_gt},
    {"display",  prim_display},
    {"newline",  prim_newline},
};

#define NUM_PRIMS (sizeof(prim_table) / sizeof(prim_table[0]))

word vm_dispatch_prim(vm_state_t* vm, int prim_index, int nargs) {
    if (prim_index < 0 || prim_index >= (int)NUM_PRIMS) {
        fprintf(stderr, "invalid prim index: %d\n", prim_index);
        return word_nil();
    }
    return prim_table[prim_index].fn(vm, nargs);
}

void prim_init_all(vm_state_t* vm) {
    (void)vm;
}

int prim_lookup(const char* name) {
    for (size_t i = 0; i < NUM_PRIMS; i++) {
        if (strcmp(prim_table[i].name, name) == 0)
            return (int)i;
    }
    return -1;
}
```

- [ ] **Step 3: Implement primitive operations (in primitives/pair.c as example)**

```c
// src/primitives/pair.c
#include "prim.h"
#include "types.h"

word prim_cons(vm_state_t* vm, int nargs) {
    if (nargs != 2) { vm->error_code = 1; return word_nil(); }
    word car = vm->sp[0];  // sp[0]=first arg
    word cdr = vm->sp[1];
    word* pair = vm->gc->alloc_words(4);
    obj_set_type(pair, OBJ_TYPE_PAIR);
    pair_car(pair) = car;
    pair_cdr(pair) = cdr;
    return ptr_to_word(pair);
}

word prim_car(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_code = 1; return word_nil(); }
    word* pair = ptr_from_word(vm->sp[0]);
    return pair_car(pair);
}

word prim_cdr(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_code = 1; return word_nil(); }
    word* pair = ptr_from_word(vm->sp[0]);
    return pair_cdr(pair);
}

word prim_null(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_code = 1; return word_nil(); }
    return is_nil(vm->sp[0]) ? word_true() : word_false();
}

word prim_pair(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_code = 1; return word_nil(); }
    return is_ptr(vm->sp[0]) && obj_type(ptr_from_word(vm->sp[0])) == OBJ_TYPE_PAIR
           ? word_true() : word_false();
}

word prim_eqv(vm_state_t* vm, int nargs) {
    if (nargs != 2) { vm->error_code = 1; return word_nil(); }
    return vm->sp[0] == vm->sp[1] ? word_true() : word_false();
}

word prim_eq(vm_state_t* vm, int nargs) { return prim_eqv(vm, nargs); }

word prim_set_car(vm_state_t* vm, int nargs) {
    if (nargs != 2) { vm->error_code = 1; return word_nil(); }
    word* pair = ptr_from_word(vm->sp[0]);
    pair_car(pair) = vm->sp[1];
    return word_nil();
}

word prim_set_cdr(vm_state_t* vm, int nargs) {
    if (nargs != 2) { vm->error_code = 1; return word_nil(); }
    word* pair = ptr_from_word(vm->sp[0]);
    pair_cdr(pair) = vm->sp[1];
    return word_nil();
}
```

- [ ] **Step 4: Compile and verify**

```bash
meson compile -C build
```
Expected: clean compile

- [ ] **Step 5: Commit**

```bash
git add src/include/prim.h src/vm/builtins.c src/primitives/pair.c
git commit -m "stage0: primitive procedure dispatch and pair operations"
```

---

### Task 7: Bootstrap reader — S-expression parser

**Files:**
- Create: `src/reader/reader.c`
- Create: `src/include/reader.h`

- [ ] **Step 1: Write src/include/reader.h**

```c
#ifndef SCHEME_READER_H
#define SCHEME_READER_H

#include "types.h"
#include "vm.h"

// Read a single S-expression from the given C string.
// Returns the parsed object, or sets vm->error_code on error.
// *end_pos is set to the number of bytes consumed.
word read_sexp(vm_state_t* vm, const char* input, int* end_pos);

#endif
```

- [ ] **Step 2: Write src/reader/reader.c**

```c
#include "reader.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Forward declarations
static word read_expr(vm_state_t* vm, const char* s, int* pos);

// Skip whitespace and comments (; to end of line)
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

// Read an atom: number, boolean, character, string, or symbol
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
        (*pos)++; // skip opening quote
        // Count characters
        int len = 0;
        int scan = *pos;
        while (s[scan] && s[scan] != '"') {
            scan++; len++;
        }
        // Allocate string object
        size_t nwords = 3 + len; // header + type + length + chars
        word* str = vm->gc->alloc_words(nwords);
        obj_set_type(str, OBJ_TYPE_STRING);
        string_set(str, 0, (word)len); // store length at data[0]
        for (int i = 0; i < len; i++)
            string_set(str, i, word_from_char((unsigned char)s[*pos + i]));
        *pos += len + 1; // skip content + closing quote
        return ptr_to_word(str);
    }

    // Number (integer): optional - followed by digits
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
    if (neg) (*pos)--;  // not a number, back up

    // Symbol: read until whitespace or delimiter
    if (isalpha(s[*pos]) || strchr("!$%&*+-./:<=>?@^_~", s[*pos])) {
        int len = 0;
        int scan = *pos;
        while (s[scan] && !isspace(s[scan]) && s[scan] != '(' && s[scan] != ')' &&
               s[scan] != '"' && s[scan] != ';') {
            scan++; len++;
        }
        // Allocate interned symbol (simplified: just make a string)
        size_t nwords = 3 + len;
        word* sym = vm->gc->alloc_words(nwords);
        obj_set_type(sym, OBJ_TYPE_SYMBOL);
        // For stage 0, symbol stores its name as a word array
        // First data word = length, then chars
        string_set(sym, 0, (word)len);
        for (int i = 0; i < len; i++)
            string_set(sym, i, word_from_char((unsigned char)s[*pos + i]));
        *pos += len;
        return ptr_to_word(sym);
    }

    // Error
    vm->error_code = 1;
    return word_nil();
}

// Read a list: (e1 e2 ... en) or (e1 e2 ... . en)
static word read_list(vm_state_t* vm, const char* s, int* pos) {
    (*pos)++; // skip '('
    skip_ws(s, pos);

    if (s[*pos] == ')') {
        (*pos)++;
        return word_nil();  // empty list
    }

    // Read first element
    word car = read_expr(vm, s, pos);
    if (vm->error_code) return word_nil();
    skip_ws(s, pos);

    word cdr;
    if (s[*pos] == '.') {
        // Dotted pair
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

// Main expression dispatcher
static word read_expr(vm_state_t* vm, const char* s, int* pos) {
    skip_ws(s, pos);
    if (!s[*pos]) return word_eof();

    char c = s[*pos];
    if (c == '(')  return read_list(vm, s, pos);
    if (c == '\'') {
        // Quote sugar: 'x  ->  (quote x)
        (*pos)++;
        word expr = read_expr(vm, s, pos);
        word* pair2 = vm->gc->alloc_words(4);
        obj_set_type(pair2, OBJ_TYPE_PAIR);
        pair_car(pair2) = expr;
        pair_cdr(pair2) = word_nil();

        word* pair1 = vm->gc->alloc_words(4);
        obj_set_type(pair1, OBJ_TYPE_PAIR);
        // Create a 'quote symbol and put it in car
        word* qsym = vm->gc->alloc_words(3 + 5);  // header + type + len + "quote"
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
```

- [ ] **Step 3: Compile and test**

```bash
meson compile -C build
```
Expected: clean compile

- [ ] **Step 4: Commit**

```bash
git add src/include/reader.h src/reader/reader.c
git commit -m "stage0: S-expression reader"
```

---

### Task 8: Bootstrap compiler — S-expr to bytecode

**Files:**
- Create: `src/include/compiler.h`
- Create: `src/bootstrap/compiler.c`

- [ ] **Step 1: Write src/include/compiler.h**

```c
#ifndef SCHEME_COMPILER_H
#define SCHEME_COMPILER_H

#include "types.h"
#include "vm.h"

// Compile a single S-expression into a bytecode code object
word compile_expr(vm_state_t* vm, word expr);

// Compile a list of expressions (top-level program)
word compile_program(vm_state_t* vm, word exprs);

#endif
```

- [ ] **Step 2: Write src/bootstrap/compiler.c**

```c
#include "compiler.h"
#include "reader.h"
#include "prim.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MAX_BYTECODE 4096

typedef struct {
    uint8_t bytes[MAX_BYTECODE];
    int     len;
    word    consts[256];
    int     nconsts;
} code_buf_t;

// Emit a single byte
static void emit_byte(code_buf_t* buf, uint8_t b) {
    buf->bytes[buf->len++] = b;
}

// Emit a word (as 8 bytes)
static void emit_word(code_buf_t* buf, word w) {
    memcpy(buf->bytes + buf->len, &w, sizeof(word));
    buf->len += sizeof(word);
}

// Add a constant and return its index
static int add_const(code_buf_t* buf, word val) {
    buf->consts[buf->nconsts] = val;
    return buf->nconsts++;
}

// ---- Compilation helpers ----
static bool is_symbol(word w, const char* name) {
    if (!is_ptr(w)) return false;
    word* hdr = ptr_from_word(w);
    if (obj_type(hdr) != OBJ_TYPE_SYMBOL) return false;
    int len = (int)string_length(hdr);
    for (int i = 0; i < len; i++) {
        if (word_to_char(string_ref(hdr, i)) != (unsigned char)name[i])
            return false;
    }
    return name[len] == '\0';
}

// Forward declaration
static void compile_expr_to_buf(code_buf_t* buf, vm_state_t* vm, word expr, int* next_local);

// Compile a list (function call or special form)
static void compile_list(code_buf_t* buf, vm_state_t* vm, word expr, int* next_local) {
    word* hdr = ptr_from_word(expr);
    word fn = pair_car(hdr);
    word args = pair_cdr(hdr);

    // Special forms
    if (is_symbol(fn, "quote")) {
        word val = pair_car(ptr_from_word(args));
        int idx = add_const(buf, val);
        emit_byte(buf, OP_PUSH_CONST);
        emit_byte(buf, (uint8_t)idx);
        return;
    }

    if (is_symbol(fn, "define")) {
        word* ahdr = ptr_from_word(args);
        word name = pair_car(ahdr);
        word val_expr = pair_car(ptr_from_word(pair_cdr(ahdr)));
        compile_expr_to_buf(buf, vm, val_expr, next_local);
        int slot = vm->next_global_slot++;
        if (slot >= (int)vm->global_count) {
            size_t new_count = vm->global_count * 2;
            vm->globals = realloc(vm->globals, new_count * sizeof(word));
            vm->global_count = new_count;
        }
        emit_byte(buf, OP_GSET);
        emit_byte(buf, (uint8_t)slot);
        return;
    }

    if (is_symbol(fn, "if")) {
        word* ahdr = ptr_from_word(args);
        word test = pair_car(ahdr);
        word then_expr = pair_car(ptr_from_word(pair_cdr(ahdr)));
        word else_expr = word_nil();
        word rest = pair_cdr(ptr_from_word(pair_cdr(ahdr)));
        if (!is_nil(rest))
            else_expr = pair_car(ptr_from_word(rest));

        compile_expr_to_buf(buf, vm, test, next_local);
        int jmp_false_pos = buf->len;
        emit_byte(buf, OP_JMP_IF_NOT);
        emit_byte(buf, 0); emit_byte(buf, 0);  // placeholder offset

        compile_expr_to_buf(buf, vm, then_expr, next_local);
        int jmp_end_pos = buf->len;
        emit_byte(buf, OP_JMP);
        emit_byte(buf, 0); emit_byte(buf, 0);  // placeholder

        // Patch false branch
        int false_start = buf->len;
        int false_offset = false_start - (jmp_false_pos + 3);
        buf->bytes[jmp_false_pos + 1] = (uint8_t)(false_offset >> 8);
        buf->bytes[jmp_false_pos + 2] = (uint8_t)(false_offset & 0xFF);

        if (!is_nil(else_expr))
            compile_expr_to_buf(buf, vm, else_expr, next_local);

        // Patch end jump
        int end_offset = buf->len - (jmp_end_pos + 3);
        buf->bytes[jmp_end_pos + 1] = (uint8_t)(end_offset >> 8);
        buf->bytes[jmp_end_pos + 2] = (uint8_t)(end_offset & 0xFF);

        return;
    }

    // Lambda
    if (is_symbol(fn, "lambda")) {
        // TODO: full lambda compilation
        // For now, just push nil
        emit_byte(buf, OP_PUSH_NIL);
        return;
    }

    // Regular function call: look up prim index from function name
    int nargs = 0;
    word* cur = ptr_from_word(args);
    while (is_ptr(cur) && obj_type(cur) == OBJ_TYPE_PAIR) {
        compile_expr_to_buf(buf, vm, pair_car(cur), next_local);
        cur = ptr_from_word(pair_cdr(cur));
        nargs++;
    }
    if (is_ptr(fn) && obj_type(ptr_from_word(fn)) == OBJ_TYPE_SYMBOL) {
        word* fhdr = ptr_from_word(fn);
        int nlen = (int)string_length(fhdr);
        char fname[64];
        if (nlen < 63) {
            for (int i = 0; i < nlen; i++)
                fname[i] = (char)word_to_char(string_ref(fhdr, i));
            fname[nlen] = '\0';
            int prim_idx = prim_lookup(fname);
            if (prim_idx >= 0) {
                emit_byte(buf, OP_PRIM_CALL);
                emit_byte(buf, (uint8_t)nargs);
                emit_byte(buf, (uint8_t)(prim_idx & 0xFF));
                emit_byte(buf, (uint8_t)((prim_idx >> 8) & 0xFF));
                return;
            }
        }
    }
    emit_byte(buf, OP_PUSH_NIL);  // unknown function
}

// Compile a single expression into the code buffer
static void compile_expr_to_buf(code_buf_t* buf, vm_state_t* vm, word expr, int* next_local) {
    if (is_fixnum(expr)) {
        int32_t val = (int32_t)word_to_fixnum(expr);
        emit_byte(buf, OP_PUSH_INT);
        emit_byte(buf, (uint8_t)(val & 0xFF));
        emit_byte(buf, (uint8_t)((val >> 8) & 0xFF));
        emit_byte(buf, (uint8_t)((val >> 16) & 0xFF));
        emit_byte(buf, (uint8_t)((val >> 24) & 0xFF));
        return;
    }

    if (is_ptr(expr)) {
        word* hdr = ptr_from_word(expr);
        if (obj_type(hdr) == OBJ_TYPE_PAIR) {
            compile_list(buf, vm, expr, next_local);
            return;
        }
    }

    // Variable reference (symbol)
    if (is_ptr(expr) && obj_type(ptr_from_word(expr)) == OBJ_TYPE_SYMBOL) {
        int idx = add_const(buf, expr);
        emit_byte(buf, OP_GREF);
        emit_byte(buf, (uint8_t)idx);
        return;
    }

    // Fallback: constant
    int idx = add_const(buf, expr);
    emit_byte(buf, OP_PUSH_CONST);
    emit_byte(buf, (uint8_t)idx);
}

word compile_expr(vm_state_t* vm, word expr) {
    code_buf_t buf = { 0 };
    int dummy = 0;
    compile_expr_to_buf(&buf, vm, expr, &dummy);
    emit_byte(&buf, OP_HALT);  // terminate execution

    // Build code object: [GC_hdr][type][bytecode_len_bytes][bytecode...][consts...]
    size_t bc_words = (buf.len + sizeof(word) - 1) / sizeof(word);
    size_t total_words = 3 + bc_words + buf.nconsts;
    word* code_obj = vm->gc->alloc_words(total_words);
    obj_set_type(code_obj, OBJ_TYPE_CODE);
    code_obj[2] = (word)buf.len;  // bytecode length in bytes

    // Copy bytecode
    memcpy(code_obj + 3, buf.bytes, buf.len);

    // Copy constants
    for (int i = 0; i < buf.nconsts; i++)
        code_obj[3 + bc_words + i] = buf.consts[i];

    return ptr_to_word(code_obj);
}

word compile_program(vm_state_t* vm, word exprs) {
    // Compile a list of expressions
    // For stage 0: just compile the first expression
    if (is_nil(exprs)) {
        code_buf_t buf = { 0 };
        emit_byte(&buf, OP_PUSH_NIL);
        emit_byte(&buf, OP_HALT);

        size_t bc_words = (buf.len + sizeof(word) - 1) / sizeof(word);
        size_t total_words = 3 + bc_words;
        word* code_obj = vm->gc->alloc_words(total_words);
        obj_set_type(code_obj, OBJ_TYPE_CODE);
        code_obj[2] = (word)buf.len;  // bytecode length in bytes
        memcpy(code_obj + 3, buf.bytes, buf.len);
        return ptr_to_word(code_obj);
    }
    return compile_expr(vm, pair_car(ptr_from_word(exprs)));
}
```

- [ ] **Step 3: Compile**

```bash
meson compile -C build
```
Expected: clean compile

- [ ] **Step 4: Commit**

```bash
git add src/include/compiler.h src/bootstrap/compiler.c
git commit -m "stage0: bootstrap S-expression to bytecode compiler"
```

---

### Task 9: More primitive procedures (arith, port)

**Files:**
- Create: `src/primitives/arith.c`
- Create: `src/primitives/port.c`

- [ ] **Step 1: Write src/primitives/arith.c — fixnum arithmetic**

```c
#include "prim.h"
#include <stdio.h>

word prim_add(vm_state_t* vm, int nargs) {
    int64_t sum = 0;
    for (int i = 0; i < nargs; i++) {
        word w = vm->sp[i];
        if (!is_fixnum(w)) { vm->error_code = 1; return word_nil(); }
        sum += word_to_fixnum(w);
    }
    return word_from_fixnum(sum);
}

word prim_sub(vm_state_t* vm, int nargs) {
    if (nargs == 0) { vm->error_code = 1; return word_nil(); }
    if (nargs == 1) return word_from_fixnum(-word_to_fixnum(vm->sp[0]));
    word w0 = vm->sp[0];
    if (!is_fixnum(w0)) { vm->error_code = 1; return word_nil(); }
    int64_t result = word_to_fixnum(w0);
    for (int i = 1; i < nargs; i++) {
        word w = vm->sp[i];
        if (!is_fixnum(w)) { vm->error_code = 1; return word_nil(); }
        result -= word_to_fixnum(w);
    }
    return word_from_fixnum(result);
}

word prim_mul(vm_state_t* vm, int nargs) {
    int64_t product = 1;
    for (int i = 0; i < nargs; i++) {
        word w = vm->sp[i];
        if (!is_fixnum(w)) { vm->error_code = 1; return word_nil(); }
        product *= word_to_fixnum(w);
    }
    return word_from_fixnum(product);
}

word prim_div(vm_state_t* vm, int nargs) {
    if (nargs < 1) { vm->error_code = 1; return word_nil(); }
    if (nargs == 1) return word_from_fixnum(word_to_fixnum(vm->sp[0]));
    int64_t result = word_to_fixnum(vm->sp[0]);
    for (int i = 1; i < nargs; i++) {
        result /= word_to_fixnum(vm->sp[i]);
    }
    return word_from_fixnum(result);
}

word prim_lt(vm_state_t* vm, int nargs) {
    for (int i = 1; i < nargs; i++) {
        if (word_to_fixnum(vm->sp[i-1]) >= word_to_fixnum(vm->sp[i]))
            return word_false();
    }
    return word_true();
}

word prim_gt(vm_state_t* vm, int nargs) {
    for (int i = 1; i < nargs; i++) {
        if (word_to_fixnum(vm->sp[i-1]) <= word_to_fixnum(vm->sp[i]))
            return word_false();
    }
    return word_true();
}
```

- [ ] **Step 2: Write src/primitives/port.c — display/newline**

```c
#include "prim.h"
#include <stdio.h>

word prim_display(vm_state_t* vm, int nargs) {
    if (nargs != 1) { vm->error_code = 1; return word_nil(); }
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
    (void)nargs;
    putchar('\n');
    fflush(stdout);
    return word_nil();
}
```

- [ ] **Step 3: Compile**

```bash
meson compile -C build
```
Expected: clean compile

- [ ] **Step 4: Commit**

```bash
git add src/primitives/arith.c src/primitives/port.c
git commit -m "stage0: primitive procedures — arithmetic and port I/O"
```

---

### Task 10: Main entry — REPL and file execution

**Files:**
- Create: `src/main.c`

- [ ] **Step 1: Write src/main.c**

```c
#include "types.h"
#include "pal.h"
#include "gc.h"
#include "vm.h"
#include "reader.h"
#include "compiler.h"
#include "prim.h"
#include <stdio.h>
#include <string.h>

pal_interface* pal;
gc_interface*  gc;
vm_state_t*    vm;

// Read-Eval-Print loop
static void repl(void) {
    char buf[4096];
    printf("Scheme v0.1\n");

    while (1) {
        printf("> ");
        fflush(stdout);

        if (!fgets(buf, sizeof(buf), stdin)) {
            printf("\n");
            break;
        }

        // Skip empty lines
        if (buf[0] == '\n') continue;

        // Read
        int pos = 0;
        word expr = read_sexp(vm, buf, &pos);
        if (vm->error_code) {
            vm->error_code = 0;
            printf("read error\n");
            continue;
        }
        if (is_eof(expr)) break;

        // Compile
        word code_obj = compile_expr(vm, expr);
        int code_idx = vm_load_code(vm, ptr_from_word(code_obj));

        // Execute
        vm->error_code = 0;
        word result = vm_execute(vm, code_idx);

        // Print result
        vm->sp[0] = result;
        prim_display(vm, 1);
        printf("\n");
    }
}

// Execute a Scheme file
static int exec_file(const char* path) {
    // Read entire file
    int fd = pal->file_open(path, PAL_O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "cannot open: %s\n", path);
        return 1;
    }

    char buf[65536];
    int64_t n = pal->file_read(fd, buf, sizeof(buf) - 1);
    pal->file_close(fd);
    if (n < 0) {
        fprintf(stderr, "read error: %s\n", path);
        return 1;
    }
    buf[n] = '\0';

    // Read all S-expressions
    int pos = 0;
    while (pos < n) {
        word expr = read_sexp(vm, buf, &pos);
        if (vm->error_code) {
            vm->error_code = 0;
            fprintf(stderr, "read error at position %d\n", pos);
            break;
        }
        if (is_eof(expr)) break;

        word code_obj = compile_expr(vm, expr);
        int code_idx = vm_load_code(vm, ptr_from_word(code_obj));
        vm_execute(vm, code_idx);
    }

    return 0;
}

int main(int argc, char** argv) {
    pal = pal_init();
    gc = gc_init();
    vm = vm_init(gc, pal);
    prim_init_all(vm);

    if (argc > 1) {
        return exec_file(argv[1]);
    }

    repl();
    return 0;
}
```

- [ ] **Step 2: Build**

```bash
meson compile -C build
```
Expected: clean compile, `build/scheme` executable produced

- [ ] **Step 3: Quick smoke test**

```bash
echo '(display "hello")' | ./build/scheme
# or
./build/scheme <<< '(+ 1 2)'
```
Expected: prints "hello" or the result

- [ ] **Step 4: Remove ptr_to_word duplicate and fix all remaining references**

Ensure `ptr_to_word` is defined only in types.h (added in Task 5), not duplicated elsewhere.

- [ ] **Step 5: Commit**

```bash
git add src/main.c
git commit -m "stage0: main entry point with REPL and file execution"
```

---

### Task 11: C unit tests

**Files:**
- Create: `tests/c/test_gc.c`

- [ ] **Step 1: Write tests/c/test_gc.c**

```c
#include "types.h"
#include "gc.h"
#include "pal.h"
#include <stdio.h>

extern pal_interface* pal;

int main(void) {
    pal = pal_init();
    gc_interface* gc = gc_init();

    word* block = gc->alloc_words(10);
    if (!block) {
        printf("FAIL: alloc returned NULL\n");
        return 1;
    }

    // Verify header
    word hdr = block[0];
    if (gc_size(hdr) != 10) {
        printf("FAIL: expected size 10, got %zu\n", gc_size(hdr));
        return 2;
    }

    // Test tagged pointer round-trip
    word w = ptr_to_word(block);
    word* back = ptr_from_word(w);
    if (back != block) {
        printf("FAIL: ptr round-trip failed\n");
        return 3;
    }

    // Verify type system
    obj_set_type(block, OBJ_TYPE_PAIR);
    if (obj_type(block) != OBJ_TYPE_PAIR) {
        printf("FAIL: type set/get\n");
        return 4;
    }

    printf("ALL gc tests PASSED\n");
    return 0;
}
```

- [ ] **Step 2: Build and run tests**

```bash
meson compile -C build && meson test -C build -v
```
Expected: both `types` and `gc` tests PASS

- [ ] **Step 3: Commit**

```bash
git add tests/c/test_gc.c
git commit -m "stage0: C unit tests for types and GC"
```

---

### Task 12: End-to-end integration test

**Files:**
- Create: `tests/scheme/test-basic.ss`

- [ ] **Step 1: Write a basic Scheme test script**

```scheme
; tests/scheme/test-basic.ss
(display "Hello from Scheme!")
(newline)
(display (+ 1 2))
(newline)
```

- [ ] **Step 2: Create a test runner that verifies output**

```bash
echo '(display "Hello")(newline)(display (+ 40 2))(newline)' > /tmp/test.scm
expected_stdout="Hello
42"
actual_stdout=$(./build/scheme /tmp/test.scm 2>&1)

if [ "$actual_stdout" != "$expected_stdout" ]; then
    echo "FAIL: expected '$expected_stdout', got '$actual_stdout'"
    exit 1
fi
echo "PASS: integration test"
```

- [ ] **Step 3: Commit**

```bash
git add tests/scheme/test-basic.ss
git commit -m "stage0: basic Scheme integration test"
```

---

## Stage 0 Verification

After completing all tasks, verify the C core is working:

```bash
meson setup build
meson compile -C build
meson test -C build -v
# Interactive REPL test:
echo '(+ 1 (* 2 3))' | ./build/scheme
```

The expected outcome: a working `./build/scheme` binary that can:
- Parse S-expressions from stdin or a file
- Compile simple expressions to bytecode (numbers, +, *, display, define)
- Execute them via the VM
- Print results
