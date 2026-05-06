#ifndef SCHEME_GC_H
#define SCHEME_GC_H

#include "types.h"
#include <stddef.h>

typedef struct {
    word* (*alloc_words)(size_t nwords);
    void  (*collect)(void); // Caller MUST mark roots first via mark_root/mark_stack
    void  (*mark_root)(word w);
    void  (*mark_stack)(word* stack, size_t count);
    size_t (*heap_used)(void);
    void  (*set_root_marker)(void (*fn)(void*), void* state);
    void* (*state_ref)(void);
    void*  state;
} gc_interface;

gc_interface* gc_init(void);

#endif
