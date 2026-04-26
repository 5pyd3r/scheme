# Macro System and Closure Support Design

> R7RS-small syntax-rules macros with flat-closure model for bootstrap Scheme

**Goal:** Add real closure support (free variable capture) and a `syntax-rules`-based hygienic macro system

**Architecture:** Three layers — (1) VM: flat-closure capture in OP_CLOSE/OP_CALL/OP_TAIL_CALL, (2) Compiler: free variable detection + env threading + define-syntax/macro-expand hooks, (3) Scheme: syntax-rules library with pattern matching, template filling, and gensym-based hygiene

**Tech Stack:** C11 (VM, primitive), Scheme (compiler, syntax-rules library), meson build

---

## 1. Closure Support

### 1.1 Flat Closure Model

The VM uses a flat-closure model where captured free variables are stored in a GC-managed vector within the closure object and unpacked into the stack frame at call time.

**Closure object layout (unchanged: 5 words):**
```
[GC_hdr][type=CLOSURE][code_ptr][env_vec_ptr][nfree]
                          ^2         ^3          ^4
```

### 1.2 OP_CLOSE — Real capture

When `OP_CLOSE` executes with `nfree > 0`, it pops that many values from the VM stack, packs them into a GC'd vector, and stores the vector pointer as `closure_env`.

```
Before OP_CLOSE:  sp -> [val_nfree-1] ... [val_0] [other_stuff]
After:            sp -> [closure_obj]
closure_env(clo) -> GC_vector[0..nfree-1] = captured values
```

**C implementation (vm.c):**
```c
case OP_CLOSE: {
    uint16_t code_idx = read_u16(&vm->ip);
    uint8_t nfree = read_u8(&vm->ip);
    word* clo = vm->gc->alloc_words(5);
    obj_set_type(clo, OBJ_TYPE_CLOSURE);
    closure_code(clo) = ptr_to_word(vm->code_objects[code_idx]);

    word* env_vec = vm->gc->alloc_words(3 + nfree);
    obj_set_type(env_vec, OBJ_TYPE_VECTOR);
    env_vec[DATA_START_INDEX] = (word)nfree;
    for (int i = nfree - 1; i >= 0; i--)
        env_vec[DATA_START_INDEX + 1 + i] = *vm->sp--;
    closure_env(clo) = ptr_to_word(env_vec);
    clo[DATA_START_INDEX + 2] = nfree;
    *++vm->sp = ptr_to_word(clo);
    break;
}
```

### 1.3 OP_CALL — Unpack captured vars into frame

The stack frame currently has 4 header words followed by `nargs` argument slots. With closure support, captured variables are unpacked after the arguments:

```
Frame layout:
base[0]             = saved_sp
base[1]             = saved_ip
base[2]             = saved_env
base[3]             = saved_fp
base[4..4+nfree-1]  = captured_vars (from closure env, NEW)
base[4+nfree..4+nfree+nargs-1] = args (shifted by nfree)
```

The args shift distance increases from 4 to `4 + nfree`.

**C implementation (vm.c):**
```c
case OP_CALL: {
    uint8_t nargs = read_u8(&vm->ip);
    word* clo = ptr_from_word(*vm->sp);
    uint8_t nfree = (uint8_t)(clo[DATA_START_INDEX + 2]);
    word* base = vm->sp - nargs;

    word old_sp = (word)(uintptr_t)(base - 1);

    // Shift args by 4 + nfree slots
    for (int i = nargs - 1; i >= 0; i--)
        base[i + 4 + nfree] = base[i];

    // Save frame header
    base[0] = old_sp;
    base[1] = (word)(uintptr_t)vm->ip;
    base[2] = (word)(uintptr_t)vm->env;
    base[3] = (word)(uintptr_t)vm->fp;

    // Unpack captured vars from closure env
    word* env_vec = ptr_from_word(closure_env(clo));
    for (int i = 0; i < nfree; i++)
        base[4 + i] = env_vec[DATA_START_INDEX + 1 + i];

    vm->fp = base + 3;
    vm->env = (word*)(uintptr_t)closure_env(clo);
    vm->sp = base + 4 + nargs + nfree;
    vm->current_code = ptr_from_word(closure_code(clo));
    vm->ip = code_bytes(vm->current_code);
    break;
}
```

### 1.4 OP_TAIL_CALL — Same unpacking

Same logic for tail calls: after copying args into `fp[1..nargs]`, unpack captured vars into `fp[nargs+1..nargs+nfree]`.

**C implementation (vm.c):**
```c
case OP_TAIL_CALL: {
    uint8_t nargs = read_u8(&vm->ip);
    word* clo = ptr_from_word(*vm->sp);
    uint8_t nfree = (uint8_t)(clo[DATA_START_INDEX + 2]);

    for (int i = 0; i < nargs; i++)
        vm->fp[1 + i] = vm->sp[i - nargs];

    word* env_vec = ptr_from_word(closure_env(clo));
    for (int i = 0; i < nfree; i++)
        vm->fp[1 + nargs + i] = env_vec[DATA_START_INDEX + 1 + i];

    vm->sp = vm->fp + nargs + nfree;
    vm->env = (word*)(uintptr_t)closure_env(clo);
    vm->current_code = ptr_from_word(closure_code(clo));
    vm->ip = code_bytes(vm->current_code);
    break;
}
```

### 1.5 OP_RETURN — No change

The return instruction is unchanged — it restores `sp`, `ip`, `env`, `fp` from the frame header, which still occupies `base[0..3]`.

---

## 2. Compiler: Free Variable Detection

### 2.1 Env threading

The `compile-inner-*` function family gains an `env` parameter: an association list `((symbol . slot) ...)` representing bindings from enclosing lambda scopes.

The slot numbers are relative to the inside of the lambda after unpacking:
- slot 1..|params| = parameters
- slot |params|+1.. = captured variables from enclosing scopes

### 2.2 Free variable detection in compile-lambda-child

When compiling `(lambda (params...) body...)` with enclosing `env`:

1. Build available bindings: `env' = (map (lambda (p i) (cons p i)) params '(1 2 ...))`
2. Scan `body` for free variables: symbols that appear in `env` but are NOT in `params` and NOT primitives
3. Remove duplicates, assign sequential indices after params
4. Before `OP_CLOSE`, emit `LREF` instructions to push each captured variable's current value
5. Set `nfree` to the count of captured variables
6. When compiling `body`'s inner expressions, pass `(append captured-bindings env')` as the new `env`

### 2.3 Symbol dispatch change

In `compile-inner-symbol-1`, the three-way dispatch:
- In `params` → `LREF slot` (local param)
- In `env` → `LREF slot` (captured var from enclosing scope, higher index)
- Neither → `compile-expr` → `GREF` (global)

### 2.4 Primitive detection

A symbol is a "primitive" if `(prim-index sym)` returns non-`#f`. Primitives like `+`, `car`, `cons` etc. are always treated as globals, never captured.

---

## 3. Compiler: Macro System

### 3.1 gensym C primitive

Returns a unique uninterned symbol each time it's called. Uses a monotonically incrementing counter to generate names like `{gensym-N}` where N increases each call.

**Signature:** `word prim_gensym(vm_state_t* vm, int nargs)` — takes 0 args, returns a symbol

### 3.2 eval C primitive

Takes an S-expression and evaluates it using the VM's existing eval mechanism, returning the result.

**Signature:** `word prim_eval(vm_state_t* vm, int nargs)` — takes 1 arg (expr), evaluates it, returns result

Implementation: uses `scheme_compile_and_assemble` internally to compile the expression and execute it. (Or, since the Scheme compiler is already loaded, calls the `compile` global function followed by `assemble-code` and uses `vm_execute`.)

### 3.3 Macro table

A global variable `*macro-table*` stores the macro list as an a-list: `((name . transformer) ...)`. Defined in `lib.scm`:

```scheme
(define *macro-table* (quote ()))
```

The compiler accesses it via `find-global-slot` and `create-global-slot` (already available).

### 3.4 define-syntax — Compiler special form

Added in `compile-list-1` before the `cond` dispatch:

```scheme
((eq? fn 'define-syntax) (compile-define-syntax args s))
```

`compile-define-syntax`:
1. Evaluates the transformer expression at compile time via `eval` C primitive
2. Adds `(name . transformer)` to `*macro-table*` via `set!`
3. Emits `OP-PUSH-NIL` as a no-op (define-syntax has no runtime effect)

```scheme
(define (compile-define-syntax args s)
  (compile-define-syntax-1 (car args) (car (cdr args)) s))

(define (compile-define-syntax-1 name transformer-expr s)
  (compile-set-macro name (eval transformer-expr) s))

(define (compile-set-macro name transformer s)
  ;; set! *macro-table* to (cons (cons name transformer) *macro-table*)
  ... )
```

### 3.5 Macro expansion hook

In `compile-list-1`, before the `cond` dispatch, insert a macro expansion step:

```scheme
(define (compile-list-1 fn args s)
  (compile-list-2 fn args s
    (lookup-macro fn)))

(define (compile-list-2 fn args s macro)
  (if macro
      (compile-expr (macro (cons fn args)) s)
      (compile-list-3 fn args s)))  ;; existing special form dispatch
```

The `lookup-macro` function scans `*macro-table*` for a matching name:

```scheme
(define (lookup-macro name)
  (lookup-macro-1 name *macro-table*))

(define (lookup-macro-1 name table)
  (if (null? table)
      #f
      (if (eq? name (car (car table)))
          (cdr (car table))
          (lookup-macro-1 name (cdr table)))))
```

### 3.6 Hygiene via gensym

When `syntax-rules` fills a template, free identifiers (symbols in the template that are NOT pattern variables) need hygienic renaming. Approach:

1. **Immune symbols** — special forms (`lambda`, `if`, `define`, `set!`, `begin`, `quote`, `cond`) and primitives (`+`, `car`, `cons`, `pair?`, etc.) are NOT renamed. The compiler recognizes them by `eq?` check and `prim-index` lookup before symbol resolution.

2. **User identifiers** — all other free identifiers in the template get a unique per-macro-call suffix. The `syntax-rules` function maintains a counter; each expansion call generates a fresh suffix.

3. **How it works:**
   - `syntax-rules` captures a counter value when created (macro definition ID)
   - At expansion time, the expansion gets a unique sub-ID
   - Pattern variables are substituted as-is (they come from the call site)
   - Non-immune free identifiers in template → `(string->symbol (string-append (symbol->string id) "{M" macro-id "-" call-id "}"))`
   - Since special forms and primitives are checked by the compiler BEFORE general symbol resolution, they work without renaming.

**Example:**
```
Template: (lambda (temp) (set! temp val))
Pattern vars: val
Immune: lambda, set!
Renamed: temp → temp{M0-0}
Input val: user-val (substituted as-is)
Result: (lambda (temp{M0-0}) (set! temp{M0-0} user-val))
```

---

## 4. syntax-rules Library (Scheme)

### 4.1 Pattern matching

Pattern matching against input expressions, handling:
- Literal identifiers: `(eq? input-symbol keyword)` — must match exactly
- Pattern variables: `x`, `body`, `var`, etc. — match anything, bind to input
- Ellipsis: `(x ...)` — match zero or more repetitions
- Nested patterns: `((var val) ...)` — match nested structure with repetition

```scheme
(define (match-pattern pattern input keywords)
  ;; Returns an alist of (pattern-var . matched-input) or #f on failure
  (cond ((null? pattern) (null? input))  ;; '() matches '()
        ((not (pair? pattern))           ;; symbol pattern
         (if (member pattern keywords)
             (eq? pattern input)  ;; literal match
             ;; variable bind — returns alist
             (if (null? input) '() (cons (cons pattern input) '()))))
        ((not (pair? input)) #f)         ;; pair pattern vs non-pair input
        (else                            ;; pair pattern
         (match-pair pattern input keywords))))
```

### 4.2 Template filling

Substitute pattern variables into the template, handling ellipsis expansion:
```
Template: ((lambda (var ...) body ...) val ...)
With bindings: var=(x) body=((+ x 1)) val=(1)
→ ((lambda (x) (+ x 1)) 1)
```

### 4.3 Ellipsis handling

The most complex part. `...` in patterns matches repeated structure. `...` in templates indicates where to expand:
- Pattern `(x ...)` matches `(a b c)` → `x` binds to `(a b c)`
- Template `(f x ...)` fills as `(f a b c)`

---

## 5. Files

| File | Action | Purpose |
|------|--------|---------|
| `src/vm/vm.c` | Modify OP_CLOSE | Capture free variables into GC'd env vector |
| `src/vm/vm.c` | Modify OP_CALL | Unpack captured vars into frame after args |
| `src/vm/vm.c` | Modify OP_TAIL_CALL | Same unpacking for tail calls |
| `src/primitives/macro.c` | Create | gensym, eval primitives |
| `src/vm/builtins.c` | Modify | Register gensym, eval |
| `src/scheme/compiler.scm` | Modify | Env threading, free var detection, define-syntax, macro expand |
| `src/scheme/lib.scm` | Modify | syntax-rules, *macro-table*, expand helpers |
| `tests/c/test_macro.c` | Create | gensym, eval C-level tests |
| `tests/scheme/test-macro.ss` | Create | Syntax-rules integration tests |
| `tests/c/meson.build` | Modify | Add test_macro |
| `tests/meson.build` | Modify | Add Scheme macro test |

---

## 6. Testing Strategy

### C tests (test_macro.c)
- gensym returns a symbol
- Two gensym calls return different symbols
- eval evaluates `(+ 1 2)` → 3
- eval evaluates `(cons 'a '(b))` → (a b)

### Scheme tests (test-macro.ss)
- Simple macro: `(define-syntax my-when (syntax-rules () ((_ test body ...) (if test (begin body ...)))))`
  - `(my-when #t (display "ok"))` → ok
  - `(my-when #f (display "no"))` → nothing
- Macro with multiple clauses: `(define-syntax my-or (syntax-rules () ((_) #f) ((_ x) x) ((_ x . rest) (let ((temp x)) (if temp temp (my-or . rest))))))`
- Macro hygiene: macro-introduced identifiers don't shadow call-site bindings
- Nested macros: macro defined via another macro
