#include "types.h"
#include "gc.h"
#include "pal.h"
#include <stdio.h>

pal_interface* pal;

int main(void) {
    pal = pal_init();
    gc_interface* gc = gc_init();

    word* block = gc->alloc_words(10);
    if (!block) {
        printf("FAIL: alloc returned NULL\n");
        return 1;
    }

    word hdr = block[0];
    if (gc_size(hdr) != 10) {
        printf("FAIL: expected size 10, got %zu\n", gc_size(hdr));
        return 2;
    }

    word w = ptr_to_word(block);
    word* back = ptr_from_word(w);
    if (back != block) {
        printf("FAIL: ptr round-trip failed\n");
        return 3;
    }

    obj_set_type(block, OBJ_TYPE_PAIR);
    if (obj_type(block) != OBJ_TYPE_PAIR) {
        printf("FAIL: type set/get\n");
        return 4;
    }

    printf("ALL gc tests PASSED\n");
    return 0;
}
