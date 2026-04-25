#include "gc.h"
#include "pal.h"
#include <string.h>

extern pal_interface* pal;

typedef struct obj_entry {
    struct obj_entry* next;
    word*             hdr;
    size_t            nwords;
} obj_entry_t;

static obj_entry_t* all_objects = NULL;
static obj_entry_t* free_entries = NULL;
static size_t total_words_allocated = 0;
static bool collecting = false;

static void gc_sweep(void);
static void gc_collect(void);

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

static word* gc_alloc_words(size_t nwords) {
    if (nwords < 3) nwords = 3;
    word* block = (word*)pal->mmap_alloc(nwords * sizeof(word));
    if (!block) {
        // Retry after collection
        gc_collect();
        block = (word*)pal->mmap_alloc(nwords * sizeof(word));
        if (!block) return NULL;
    }

    memset(block, 0, nwords * sizeof(word));
    block[0] = (nwords << GC_SIZE_SHIFT);

    obj_entry_t* e = entry_alloc();
    e->hdr = block;
    e->nwords = nwords;
    e->next = all_objects;
    all_objects = e;

    total_words_allocated += nwords;
    return block;
}

static void mark_word(word w) {
    if (!is_ptr(w)) return;
    word* hdr = ptr_from_word(w);
    if (gc_marked(hdr[0])) return;

    hdr[0] = gc_set_mark(hdr[0]);

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
    case OBJ_TYPE_CODE: {
        // Code object: [hdr][type][bytecode_len][bytecode...][consts...]
        size_t nwords = gc_size(hdr[0]);
        size_t bc_len = (size_t)hdr[DATA_START_INDEX]; // stored at data[0]
        size_t bc_words = (bc_len + sizeof(word) - 1) / sizeof(word);
        size_t nconsts = nwords - 3 - bc_words;
        word* consts = hdr + DATA_START_INDEX + 1 + bc_words;
        for (size_t i = 0; i < nconsts; i++)
            mark_word(consts[i]);
        break;
    }
    }
}

static void gc_mark_root(word w) {
    mark_word(w);
}

static void gc_mark_stack(word* stack, size_t count) {
    for (size_t i = 0; i < count; i++)
        mark_word(stack[i]);
}

static void gc_sweep(void) {
    obj_entry_t** prev = &all_objects;
    obj_entry_t* cur = all_objects;

    while (cur) {
        word* hdr = cur->hdr;
        if (gc_marked(hdr[0])) {
            hdr[0] = gc_clr_mark(hdr[0]);
            prev = &cur->next;
            cur = cur->next;
        } else {
            obj_entry_t* dead = cur;
            *prev = cur->next;
            cur = cur->next;

            pal->mmap_free(dead->hdr, dead->nwords * sizeof(word));
            total_words_allocated -= dead->nwords;
            entry_free(dead);
        }
    }
}

static void gc_collect(void) {
    if (collecting) return;
    collecting = true;
    gc_sweep();
    collecting = false;
}

static size_t gc_heap_used(void) {
    return total_words_allocated * sizeof(word);
}

gc_interface* gc_init(void) {
    static gc_interface gc;
    gc.alloc_words = gc_alloc_words;
    gc.collect     = gc_collect;
    gc.mark_root   = gc_mark_root;
    gc.mark_stack  = gc_mark_stack;
    gc.heap_used   = gc_heap_used;
    return &gc;
}
