#include "prim.h"
#include "debug.h"
#include "types.h"

word prim_cons(vm_state_t* vm, int nargs) {
    if (nargs != 2) { VM_ERROR(vm, ERR_ARITY, "cons: expected 2 arguments", word_nil()); return word_nil(); }
    word car = vm->sp[0];
    word cdr = vm->sp[1];
    word* pair = vm->gc->alloc_words(4);
    obj_set_type(pair, OBJ_TYPE_PAIR);
    pair_car(pair) = car;
    pair_cdr(pair) = cdr;
    return ptr_to_word(pair);
}

word prim_car(vm_state_t* vm, int nargs) {
    if (nargs != 1) { VM_ERROR(vm, ERR_ARITY, "car: expected 1 argument", word_nil()); return word_nil(); }
    word w = vm->sp[0];
    if (!is_ptr(w) || obj_type(ptr_from_word(w)) != OBJ_TYPE_PAIR)
        { VM_ERROR(vm, ERR_TYPE, "car: expected pair", w); return word_nil(); }
    return pair_car(ptr_from_word(w));
}

word prim_cdr(vm_state_t* vm, int nargs) {
    if (nargs != 1) { VM_ERROR(vm, ERR_ARITY, "cdr: expected 1 argument", word_nil()); return word_nil(); }
    word w = vm->sp[0];
    if (!is_ptr(w) || obj_type(ptr_from_word(w)) != OBJ_TYPE_PAIR)
        { VM_ERROR(vm, ERR_TYPE, "cdr: expected pair", w); return word_nil(); }
    return pair_cdr(ptr_from_word(w));
}

word prim_null(vm_state_t* vm, int nargs) {
    if (nargs != 1) { VM_ERROR(vm, ERR_ARITY, "null?: expected 1 argument", word_nil()); return word_nil(); }
    return is_nil(vm->sp[0]) ? word_true() : word_false();
}

word prim_pair(vm_state_t* vm, int nargs) {
    if (nargs != 1) { VM_ERROR(vm, ERR_ARITY, "pair?: expected 1 argument", word_nil()); return word_nil(); }
    return is_ptr(vm->sp[0]) && obj_type(ptr_from_word(vm->sp[0])) == OBJ_TYPE_PAIR
           ? word_true() : word_false();
}

word prim_eqv(vm_state_t* vm, int nargs) {
    if (nargs != 2) { VM_ERROR(vm, ERR_ARITY, "eqv?: expected 2 arguments", word_nil()); return word_nil(); }
    return vm->sp[0] == vm->sp[1] ? word_true() : word_false();
}

word prim_eq(vm_state_t* vm, int nargs) { return prim_eqv(vm, nargs); }

word prim_set_car(vm_state_t* vm, int nargs) {
    if (nargs != 2) { VM_ERROR(vm, ERR_ARITY, "set-car!: expected 2 arguments", word_nil()); return word_nil(); }
    word w = vm->sp[0];
    if (!is_ptr(w) || obj_type(ptr_from_word(w)) != OBJ_TYPE_PAIR)
        { VM_ERROR(vm, ERR_TYPE, "set-car!: expected pair", w); return word_nil(); }
    pair_car(ptr_from_word(w)) = vm->sp[1];
    return word_nil();
}

word prim_set_cdr(vm_state_t* vm, int nargs) {
    if (nargs != 2) { VM_ERROR(vm, ERR_ARITY, "set-cdr!: expected 2 arguments", word_nil()); return word_nil(); }
    word w = vm->sp[0];
    if (!is_ptr(w) || obj_type(ptr_from_word(w)) != OBJ_TYPE_PAIR)
        { VM_ERROR(vm, ERR_TYPE, "set-cdr!: expected pair", w); return word_nil(); }
    pair_cdr(ptr_from_word(w)) = vm->sp[1];
    return word_nil();
}

/* equal? -- structural recursive comparison */
static bool equal_rec(vm_state_t* vm, word a, word b) {
    if (a == b) return true;
    if (!is_ptr(a) || !is_ptr(b)) return false;
    word* ha = ptr_from_word(a);
    word* hb = ptr_from_word(b);
    int ta = (int)obj_type(ha);
    int tb = (int)obj_type(hb);
    if (ta != tb) return false;
    switch (ta) {
    case OBJ_TYPE_PAIR:
        if (!equal_rec(vm, pair_car(ha), pair_car(hb))) return false;
        return equal_rec(vm, pair_cdr(ha), pair_cdr(hb));
    case OBJ_TYPE_VECTOR: {
        size_t la = vector_length(ha), lb = vector_length(hb);
        if (la != lb) return false;
        for (size_t i = 0; i < la; i++) {
            if (!equal_rec(vm, vector_elem(ha, i), vector_elem(hb, i)))
                return false;
        }
        return true;
    }
    default:
        return false;
    }
}

word prim_equal(vm_state_t* vm, int nargs) {
    if (nargs != 2) { vm->error_kind = 1; return word_nil(); }
    return equal_rec(vm, vm->sp[0], vm->sp[1]) ? word_true() : word_false();
}

// Pair validation helper: returns pair pointer or NULL (sets error)
static word* cxr_check(vm_state_t* vm, word w) {
    if (!is_ptr(w) || obj_type(ptr_from_word(w)) != OBJ_TYPE_PAIR)
        { vm->error_kind = ERR_TYPE; return NULL; }
    return ptr_from_word(w);
}

// === caar..cddddr (2-level) ===
#define CXR2(name,a,b) word name(vm_state_t* vm, int na){if(na!=1){vm->error_kind=ERR_ARITY;return word_nil();}word* p=cxr_check(vm,vm->sp[0]);if(!p)return word_nil();return pair_##b(ptr_from_word(pair_##a(p)));}
CXR2(prim_caar,car,car) CXR2(prim_cadr,cdr,car) CXR2(prim_cdar,car,cdr) CXR2(prim_cddr,cdr,cdr)
// 3-level
word prim_caaar(vm_state_t* vm,int n){if(n!=1){vm->error_kind=ERR_ARITY;return word_nil();}word* p=cxr_check(vm,vm->sp[0]);if(!p)return word_nil();return pair_car(ptr_from_word(pair_car(ptr_from_word(pair_car(p)))));}
word prim_caadr(vm_state_t* vm,int n){if(n!=1){vm->error_kind=ERR_ARITY;return word_nil();}word* p=cxr_check(vm,vm->sp[0]);if(!p)return word_nil();return pair_car(ptr_from_word(pair_car(ptr_from_word(pair_cdr(p)))));}
word prim_cadar(vm_state_t* vm,int n){if(n!=1){vm->error_kind=ERR_ARITY;return word_nil();}word* p=cxr_check(vm,vm->sp[0]);if(!p)return word_nil();return pair_car(ptr_from_word(pair_cdr(ptr_from_word(pair_car(p)))));}
word prim_caddr(vm_state_t* vm,int n){if(n!=1){vm->error_kind=ERR_ARITY;return word_nil();}word* p=cxr_check(vm,vm->sp[0]);if(!p)return word_nil();return pair_car(ptr_from_word(pair_cdr(ptr_from_word(pair_cdr(p)))));}
word prim_cdaar(vm_state_t* vm,int n){if(n!=1){vm->error_kind=ERR_ARITY;return word_nil();}word* p=cxr_check(vm,vm->sp[0]);if(!p)return word_nil();return pair_cdr(ptr_from_word(pair_car(ptr_from_word(pair_car(p)))));}
word prim_cdadr(vm_state_t* vm,int n){if(n!=1){vm->error_kind=ERR_ARITY;return word_nil();}word* p=cxr_check(vm,vm->sp[0]);if(!p)return word_nil();return pair_cdr(ptr_from_word(pair_car(ptr_from_word(pair_cdr(p)))));}
word prim_cddar(vm_state_t* vm,int n){if(n!=1){vm->error_kind=ERR_ARITY;return word_nil();}word* p=cxr_check(vm,vm->sp[0]);if(!p)return word_nil();return pair_cdr(ptr_from_word(pair_cdr(ptr_from_word(pair_car(p)))));}
word prim_cdddr(vm_state_t* vm,int n){if(n!=1){vm->error_kind=ERR_ARITY;return word_nil();}word* p=cxr_check(vm,vm->sp[0]);if(!p)return word_nil();return pair_cdr(ptr_from_word(pair_cdr(ptr_from_word(pair_cdr(p)))));}
// 4-level (16 functions)
#define CXR4(name,a,b,c,d) word name(vm_state_t* vm, int n){if(n!=1){vm->error_kind=ERR_ARITY;return word_nil();}word* p=cxr_check(vm,vm->sp[0]);if(!p)return word_nil();return pair_##d(ptr_from_word(pair_##c(ptr_from_word(pair_##b(ptr_from_word(pair_##a(p)))))));}
CXR4(prim_caaaar,car,car,car,car) CXR4(prim_caaadr,cdr,car,car,car)
CXR4(prim_caadar,car,cdr,car,car) CXR4(prim_caaddr,cdr,cdr,car,car)
CXR4(prim_cadaar,car,car,cdr,car) CXR4(prim_cadadr,cdr,car,cdr,car)
CXR4(prim_caddar,car,cdr,cdr,car) CXR4(prim_cadddr,cdr,cdr,cdr,car)
CXR4(prim_cdaaar,car,car,car,cdr) CXR4(prim_cdaadr,cdr,car,car,cdr)
CXR4(prim_cdadar,car,cdr,car,cdr) CXR4(prim_cdaddr,cdr,cdr,car,cdr)
CXR4(prim_cddaar,car,car,cdr,cdr) CXR4(prim_cddadr,cdr,car,cdr,cdr)
CXR4(prim_cdddar,car,cdr,cdr,cdr) CXR4(prim_cddddr,cdr,cdr,cdr,cdr)
