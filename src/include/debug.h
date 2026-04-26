#ifndef SCHEME_DEBUG_H
#define SCHEME_DEBUG_H

#include "types.h"
#include <stdio.h>
#include <stdlib.h>

#ifdef SCHEME_DEBUG

// ---- Assertion ----
#define DASSERT(cond, fmt, ...) do { \
    if (!(cond)) { \
        fprintf(stderr, "ASSERT at %s:%d: %s: (%s)\n", __FILE__, __LINE__, __func__, #cond); \
        fprintf(stderr, "  " fmt "\n", ##__VA_ARGS__); \
        abort(); \
    } \
} while(0)

// ---- Type assertion ----
#define DASSERT_TYPE(w, expected_type) do { \
    if (is_ptr(w)) { \
        word* _hdr = ptr_from_word(w); \
        word _actual = obj_type(_hdr); \
        if (_actual != (expected_type)) { \
            fprintf(stderr, "ASSERT at %s:%d: %s: expected type %d, got %ld\n", \
                    __FILE__, __LINE__, __func__, (int)(expected_type), (long)_actual); \
            abort(); \
        } \
    } else { \
        fprintf(stderr, "ASSERT at %s:%d: %s: expected ptr, got immediate word 0x%lx\n", \
                __FILE__, __LINE__, __func__, (unsigned long)(w)); \
        abort(); \
    } \
} while(0)

// ---- Debug log ----
#define DEBUG_LOG(fmt, ...) \
    fprintf(stderr, "DEBUG %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)

// ---- VM error setter ----
#define VM_ERROR(vm, kind, msg, arg) do { \
    (vm)->error_kind = (kind); \
    (vm)->error_msg = (msg); \
    (vm)->error_arg = (arg); \
} while(0)

// ---- Signal handler registration ----
void debug_install_handlers(vm_state_t* vm);

#else // !SCHEME_DEBUG

#define DASSERT(cond, fmt, ...)         ((void)0)
#define DASSERT_TYPE(w, expected_type)  ((void)0)
#define DEBUG_LOG(fmt, ...)             ((void)0)
#define VM_ERROR(vm, kind, msg, arg) do { \
    (vm)->error_kind = (kind); \
    (vm)->error_msg = (msg); \
    (vm)->error_arg = (arg); \
} while(0)

static inline void debug_install_handlers(vm_state_t* vm) { (void)vm; }

#endif // SCHEME_DEBUG

#endif // SCHEME_DEBUG_H
