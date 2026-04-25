#ifndef SCHEME_OPCODES_H
#define SCHEME_OPCODES_H

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
    OP_LREF       = 0x10,  // +1B: local index
    OP_LSET       = 0x11,  // +1B: local index
    OP_FREF       = 0x12,  // +1B: depth, +1B: offset
    OP_FSET       = 0x13,
    OP_GREF       = 0x14,  // +1B: global index
    OP_GSET       = 0x15,

    // Procedure and call
    OP_CLOSE      = 0x20,  // +2B: code_object_index, +1B: nfree
    OP_CALL       = 0x21,  // +1B: nargs
    OP_TAIL_CALL  = 0x22,  // +1B: nargs
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
