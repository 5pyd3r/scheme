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
    vm->global_names = (word*)calloc(INITIAL_GLOBALS, sizeof(word));
    vm->global_count = INITIAL_GLOBALS;
    vm->next_global_slot = 0;

    vm->code_objects = (word**)calloc(64, sizeof(word*));
    vm->code_count = 0;

    vm->symbol_table = NULL;
    vm->symbol_count = 0;
    vm->symbol_capacity = 0;
    vm->gensym_counter = 0;

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

int vm_find_global_slot(vm_state_t* vm, word sym) {
    if (!is_ptr(sym)) return -1;
    word* sym_hdr = ptr_from_word(sym);
    if (obj_type(sym_hdr) != OBJ_TYPE_SYMBOL) return -1;
    int slen = (int)string_length(sym_hdr);
    for (int i = 0; i < vm->next_global_slot; i++) {
        word name = vm->global_names[i];
        if (!is_ptr(name)) continue;
        word* name_hdr = ptr_from_word(name);
        if (obj_type(name_hdr) != OBJ_TYPE_SYMBOL) continue;
        int nlen = (int)string_length(name_hdr);
        if (nlen != slen) continue;
        int match = 1;
        for (int j = 0; j < slen; j++) {
            if (string_ref(sym_hdr, j) != string_ref(name_hdr, j)) {
                match = 0; break;
            }
        }
        if (match) return i;
    }
    return -1;
}

int vm_find_global_by_name(vm_state_t* vm, const char* name) {
    int nlen = (int)strlen(name);
    for (int i = 0; i < vm->next_global_slot; i++) {
        word name_word = vm->global_names[i];
        if (!is_ptr(name_word)) continue;
        word* hdr = ptr_from_word(name_word);
        if (obj_type(hdr) != OBJ_TYPE_SYMBOL) continue;
        int slen = (int)string_length(hdr);
        if (slen != nlen) continue;
        int match = 1;
        for (int j = 0; j < slen; j++) {
            if (word_to_char(string_ref(hdr, j)) != (unsigned char)name[j]) {
                match = 0; break;
            }
        }
        if (match) return i;
    }
    return -1;
}

word vm_intern(vm_state_t* vm, const char* name, int len) {
    for (size_t i = 0; i < vm->symbol_count; i++) {
        word* hdr = ptr_from_word(vm->symbol_table[i]);
        if ((int)string_length(hdr) != len) continue;
        bool match = true;
        for (int j = 0; j < len; j++) {
            if (word_to_char(string_ref(hdr, j)) != (unsigned char)name[j]) {
                match = false;
                break;
            }
        }
        if (match) return vm->symbol_table[i];
    }
    size_t nwords = 3 + len;
    word* sym = vm->gc->alloc_words(nwords);
    obj_set_type(sym, OBJ_TYPE_SYMBOL);
    sym[DATA_START_INDEX] = (word)len;
    for (int i = 0; i < len; i++)
        string_set(sym, i, word_from_char((unsigned char)name[i]));
    if (vm->symbol_count >= vm->symbol_capacity) {
        vm->symbol_capacity = vm->symbol_capacity ? vm->symbol_capacity * 2 : 256;
        vm->symbol_table = realloc(vm->symbol_table, vm->symbol_capacity * sizeof(word));
    }
    vm->symbol_table[vm->symbol_count++] = ptr_to_word(sym);
    return ptr_to_word(sym);
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
            *++vm->sp = vm->fp[(int)idx];
            break;
        }

        case OP_LSET: {
            uint8_t idx = read_u8(&vm->ip);
            vm->fp[(int)idx] = *vm->sp;
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

        case OP_APPLY: {
            uint8_t nargs = read_u8(&vm->ip);
            // Save original sp (before any adjustment)
            word* orig_sp = vm->sp;  // points to last arg (list)
            vm->sp -= (nargs - 1);  // sp now at closure (first arg)
            word closure = vm->sp[0];
            int nindividual = nargs - 2;
            word list_arg = vm->sp[nargs - 1];
            // Count list elements
            int list_len = 0;
            word cur = list_arg;
            while (is_ptr(cur) && obj_type(ptr_from_word(cur)) == OBJ_TYPE_PAIR) {
                list_len++;
                cur = pair_cdr(ptr_from_word(cur));
            }
            if (!is_nil(cur)) {
                vm->error_kind = ERR_TYPE;
                vm->error_msg = "apply: last argument must be a proper list";
                return word_nil();
            }
            int total_args = nindividual + list_len;
            // Save individual args and list elements locally, then rebuild stack
            word saved_individual[128];
            word saved_list[1024];
            for (int i = 0; i < nindividual && i < 128; i++)
                saved_individual[i] = vm->sp[1 + i];
            cur = list_arg;
            for (int i = 0; i < list_len && i < 1024; i++) {
                saved_list[i] = pair_car(ptr_from_word(cur));
                cur = pair_cdr(ptr_from_word(cur));
            }
            // Reset sp: go BEFORE the first apply arg pushed by compiler
            // orig_sp was at last arg; there are nargs total args.
            vm->sp = orig_sp - nargs;
            // Push all call args LEFT TO RIGHT: individual args, then list elements
            for (int i = 0; i < nindividual; i++)
                *++vm->sp = saved_individual[i];
            for (int i = 0; i < list_len; i++)
                *++vm->sp = saved_list[i];
            // Push closure (top of stack)
            *++vm->sp = closure;
            // Compute base pointer (used by both prim and closure paths)
            word* base = vm->sp - total_args;
            // Check if "closure" is actually a primitive fixnum index
            if (is_fixnum(closure)) {
                int pidx = (int)word_to_fixnum(closure);
                word* saved_sp = vm->sp;
                vm->sp = base + total_args - 1;  // point to last arg
                vm->sp -= (total_args - 1);       // adjust to first arg
                word result = vm_dispatch_prim(vm, pidx, total_args);
                vm->sp = saved_sp;  // restore to closure position
                *vm->sp = result;   // replace closure with result
                break;
            }
            // Regular closure call (same as OP_CALL frame setup)
            word* clo2 = ptr_from_word(closure);
            uint8_t nfree2 = (uint8_t)(clo2[DATA_START_INDEX + 2]);
            word old_sp = (word)(uintptr_t)(base - 1);
            for (int i = total_args - 1; i >= 0; i--)
                base[i + 4 + nfree2] = base[i];
            base[0] = old_sp;
            base[1] = (word)(uintptr_t)vm->ip;
            base[2] = (word)(uintptr_t)vm->env;
            base[3] = (word)(uintptr_t)vm->fp;
            if (nfree2 > 0) {
                word* env_vec = ptr_from_word(closure_env(clo2));
                for (int i = 0; i < nfree2; i++)
                    base[4 + i] = env_vec[DATA_START_INDEX + 1 + i];
            }
            vm->fp = base + 3;
            vm->env = ptr_from_word(closure_env(clo2));
            vm->sp = base + 4 + total_args + nfree2;
            vm->current_code = ptr_from_word(closure_code(clo2));
            vm->ip = code_bytes(vm->current_code);
            break;
        }

        case OP_CLOSE: {
            uint16_t code_idx = read_u16(&vm->ip);
            uint8_t nfree = read_u8(&vm->ip);
            word* clo = vm->gc->alloc_words(5);
            obj_set_type(clo, OBJ_TYPE_CLOSURE);
            closure_code(clo) = ptr_to_word(vm->code_objects[code_idx]);
            if (nfree > 0) {
                word* env_vec = vm->gc->alloc_words(3 + nfree);
                obj_set_type(env_vec, OBJ_TYPE_VECTOR);
                env_vec[DATA_START_INDEX] = (word)nfree;
                for (int i = nfree - 1; i >= 0; i--)
                    env_vec[DATA_START_INDEX + 1 + i] = *vm->sp--;
                closure_env(clo) = ptr_to_word(env_vec);
            } else {
                closure_env(clo) = 0;
            }
            clo[DATA_START_INDEX + 2] = (word)nfree;
            *++vm->sp = ptr_to_word(clo);
            break;
        }

        case OP_CALL: {
            uint8_t nargs = read_u8(&vm->ip);
            // Args compiled first, then closure on top: sp[-nargs+1..0] = args, sp[0] = closure
            word* clo = ptr_from_word(*vm->sp);
            uint8_t nfree = (uint8_t)(clo[DATA_START_INDEX + 2]);
            word* base = vm->sp - nargs;  // base[0..nargs-1] = args

            // Save caller sp (restore to before first arg, so frame header is overwritten)
            word old_sp = (word)(uintptr_t)(base - 1);

            // Shift args (base[0..nargs-1]) to base[4+nfree..nargs+4+nfree-1]
            for (int i = nargs - 1; i >= 0; i--)
                base[i + 4 + nfree] = base[i];

            // Save frame header (4 words) — base[0..3]
            base[0] = old_sp;
            base[1] = (word)(uintptr_t)vm->ip;   // saved_ip
            base[2] = (word)(uintptr_t)vm->env;  // saved_env
            base[3] = (word)(uintptr_t)vm->fp;   // saved_fp

            // Unpack captured vars into base[4..4+nfree-1]
            if (nfree > 0) {
                word* env_vec = ptr_from_word(closure_env(clo));
                for (int i = 0; i < nfree; i++)
                    base[4 + i] = env_vec[DATA_START_INDEX + 1 + i];
            }

            // Set new frame
            vm->fp = base + 3;  // fp[0]=saved_fp, fp[1]=first captured var (if nfree>0), fp[1+nfree]=arg1
            vm->env = ptr_from_word(closure_env(clo));
            vm->sp = base + 4 + nargs + nfree;  // point past last arg

            vm->current_code = ptr_from_word(closure_code(clo));
            vm->ip = code_bytes(vm->current_code);
            break;
        }

        case OP_RETURN: {
            word result = *vm->sp;
            word* base = vm->fp - 3;  // frame header start
            word* saved_sp = (word*)(uintptr_t)base[0];
            vm->ip = (uint8_t*)(uintptr_t)base[1];
            vm->env = (word*)(uintptr_t)base[2];
            vm->fp = (word*)(uintptr_t)base[3];
            vm->sp = saved_sp;
            *++vm->sp = result;  // place return value
            break;
        }

        case OP_TAIL_CALL: {
            uint8_t nargs = read_u8(&vm->ip);
            // closure at sp[0], args at sp[-nargs..-1]
            word* clo = ptr_from_word(*vm->sp);
            uint8_t nfree = (uint8_t)(clo[DATA_START_INDEX + 2]);

            // Copy args from stack into current frame's fp[1..nargs]
            for (int i = 0; i < nargs; i++)
                vm->fp[1 + i] = vm->sp[i - nargs];

            // Unpack captured vars into fp[1+nargs..1+nargs+nfree-1]
            if (nfree > 0) {
                word* env_vec = ptr_from_word(closure_env(clo));
                for (int i = 0; i < nfree; i++)
                    vm->fp[1 + nargs + i] = env_vec[DATA_START_INDEX + 1 + i];
            }

            // Reset sp, jump to new closure code
            vm->sp = vm->fp + nargs + nfree;
            vm->env = ptr_from_word(closure_env(clo));
            vm->current_code = ptr_from_word(closure_code(clo));
            vm->ip = code_bytes(vm->current_code);
            break;
        }

        case OP_MAKE_VEC: {
            uint8_t len = read_u8(&vm->ip);
            size_t nwords = 3 + (size_t)len;
            word* vec = vm->gc->alloc_words(nwords);
            obj_set_type(vec, OBJ_TYPE_VECTOR);
            vec[DATA_START_INDEX] = (word)len;
            for (int i = 0; i < len; i++)
                vector_set(vec, i, word_nil());
            *++vm->sp = ptr_to_word(vec);
            break;
        }

        case OP_VEC_REF: {
            word idx_w = *vm->sp--;
            word vec_w = *vm->sp;
            word* hdr = ptr_from_word(vec_w);
            size_t idx = (size_t)word_to_fixnum(idx_w);
            *vm->sp = vector_elem(hdr, idx);
            break;
        }

        case OP_VEC_SET: {
            word val = *vm->sp--;
            word idx_w = *vm->sp--;
            word vec_w = *vm->sp;
            word* hdr = ptr_from_word(vec_w);
            size_t idx = (size_t)word_to_fixnum(idx_w);
            vector_set(hdr, idx, val);
            break;
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
