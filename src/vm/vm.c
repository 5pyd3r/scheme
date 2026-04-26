#include "vm.h"
#include "debug.h"
#include "opcodes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Forward declaration -- defined in builtins.c (Task 6)
word vm_dispatch_prim(vm_state_t* vm, int prim_index, int nargs);

#define INITIAL_STACK_WORDS (64 * 1024)
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
    int idx = vm->code_count;
    // Grow if needed (power-of-2 strategy starting from 64)
    if (idx >= 64) {
        size_t new_slots = vm->code_count * 2;
        word** new_objs = realloc(vm->code_objects, new_slots * sizeof(word*));
        if (!new_objs) return -1;
        vm->code_objects = new_objs;
        memset(vm->code_objects + vm->code_count, 0,
               (new_slots - vm->code_count) * sizeof(word*));
    }
    vm->code_count = idx + 1;
    vm->code_objects[idx] = code_obj;
    return idx;
}

static uint8_t  read_u8(uint8_t** ip)     { return *(*ip)++; }
static int32_t  read_s32(uint8_t** ip)    { int32_t v; memcpy(&v, *ip, 4); *ip += 4; return v; }
static int16_t  read_s16(uint8_t** ip)    { int16_t v; memcpy(&v, *ip, 2); *ip += 2; return v; }
static uint16_t read_u16(uint8_t** ip)    { uint16_t v; memcpy(&v, *ip, 2); *ip += 2; return v; }

static uint8_t* code_bytes(word* code_obj) {
    return (uint8_t*)(code_obj + 3);
}

static word* code_consts(word* code_obj) {
    size_t bc_len = (size_t)code_obj[2];
    size_t bc_words = (bc_len + sizeof(word) - 1) / sizeof(word);
    return code_obj + 3 + bc_words;
}

word vm_execute(vm_state_t* vm, int entry_idx) {
    vm->current_code = vm->code_objects[entry_idx];
    vm->ip = code_bytes(vm->current_code);

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
            *++vm->sp = vm->fp[-(int)idx];
            break;
        }

        case OP_LSET: {
            uint8_t idx = read_u8(&vm->ip);
            vm->fp[-(int)idx] = *vm->sp;
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
            DASSERT_TYPE(*vm->sp, OBJ_TYPE_PAIR);
            word* pair = ptr_from_word(*vm->sp);
            *vm->sp = pair_car(pair);
            break;
        }

        case OP_CDR: {
            DASSERT_TYPE(*vm->sp, OBJ_TYPE_PAIR);
            word* pair = ptr_from_word(*vm->sp);
            *vm->sp = pair_cdr(pair);
            break;
        }

        case OP_JMP: {
            int16_t offset = read_s16(&vm->ip);
            vm->ip += offset;
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

        case OP_PRIM_CALL: {
            uint8_t nargs = read_u8(&vm->ip);
            uint16_t prim_idx = read_u16(&vm->ip);
            vm->sp -= (nargs - 1);
            word result = vm_dispatch_prim(vm, prim_idx, nargs);
            vm->sp -= 1;
            *++vm->sp = result;
            break;
        }

        case OP_RETURN: {
            word val = *vm->sp;
            return val;
        }

        case OP_HALT:
            return *vm->sp;

        default:
            DASSERT(false, "unknown opcode: 0x%02x at IP offset %ld",
                    op, (long)(vm->ip - 1 - (uint8_t*)(vm->current_code + 3)));
            VM_ERROR(vm, ERR_INTERNAL, "unknown opcode", word_from_fixnum(op));
            return word_nil();
        }
    }
}
