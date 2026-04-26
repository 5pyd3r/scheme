# Macro System and Closure Implementation

> Phased implementation of R7RS-small closures and syntax-rules macros for bootstrap Scheme

**Goal:** Add flat-closure capture and a `syntax-rules`-based hygienic macro system in two phases.

**Architecture:** Three layers — (1) VM: already supports flat closures (OP_CLOSE/OP_CALL/OP_TAIL_CALL), (2) C Bootstrap Compiler: env threading + free var detection, (3) Scheme Compiler: full reimplementation with closures + macros + syntax-rules.

**Tech Stack:** C11 (VM, primitives, bootstrap compiler), Scheme (compiler.scm, lib.scm), meson build

**Approach:** Approach 1 — C compiler gets env threading + free variable detection. Scheme compiler (`compiler.scm`) is a full reimplementation of the C compiler's logic in Scheme, adding closure support and the macro system.

---

## Phase 1: Closures

### 1.1 C Bootstrap Compiler — Env Threading

The compile pipeline gains an `env` parameter: an alist `((sym . slot) ...)` representing bindings from enclosing lambda scopes.

Slot numbering matches VM frame layout (captured vars first):
- `fp[1]` = first captured var (LREF slot 1)
- `fp[nfree]` = last captured var (LREF slot nfree)
- `fp[nfree+1]` = first param (LREF slot nfree+1)
- `fp[nfree+n]` = last param (LREF slot nfree+n)

**Files changed:** `src/bootstrap/compiler.c`

**Key functions:**
- `compile_expr_to_buf(buf, vm, expr, scope, env)` — gains `env` parameter (word alist)
- `compile_lambda(buf, vm, args, body, parent_env)` — uses parent_env for free var detection
- `compile_list(buf, vm, expr, scope, env)` — threads env
- `compile_expr(vm, expr)` — calls with `env = word_nil()`

### 1.2 Free Variable Detection in compile_lambda

```
compile_lambda(buf, vm, args, body, parent_env):
  params = extract param symbols from args
  param_bindings = ((param1 . 1) (param2 . 2) ...)
  
  captured = []
  for each free symbol in body:
    if symbol in parent_env and NOT in params and NOT primitive:
      add to captured (deduplicate)
  
  assign slots: captured[0] → 1, captured[1] → 2, ...
  
  // Emit LREF for each captured var (pushes onto stack)
  for each cv in captured (in order):
    emit OP_LREF <parent_slot_for_cv>
  
  // OP_CLOSE with real nfree
  emit OP_CLOSE <code_idx> <nfree = |captured|>
  
  // Build child env for body compilation
  child_env = append(captured_bindings, param_bindings)
  compile body with child_env
```

### 1.3 Symbol Dispatch Change

In symbol compilation, three-way check:
1. In local params → `OP_LREF slot` (slot nfree+1..nfree+|params|)
2. In env → `OP_LREF slot` (captured var, slot 1..nfree)
3. Neither → `OP_GREF` (global lookup)

### 1.4 Primitive Detection

A symbol is "primitive" if `prim_lookup(name)` returns >= 0. Primitives are always globals, never captured.

Examples: `+`, `car`, `cons`, `pair?`, `null?`, `eq?`, and all other entries in the `prim_table`.

---

## Phase 2: Macros

### 2.1 New C Primitives

File: `src/primitives/macro.c` (new)

**gensym:**
```c
word prim_gensym(vm_state_t* vm, int nargs);
// Returns a unique uninterned symbol. Uses vm->gensym_counter (monotonic).
// Symbol name: "{g<N>}" — curly braces ensure no collision with user symbols.
```

**eval:**
```c
word prim_eval(vm_state_t* vm, int nargs);
// Takes 1 arg (expr), compiles and executes it via scheme_compile_and_assemble.
// Returns the result value.
```

**Registration:** Add entries to `prim_table[]` in `builtins.c`:
```c
{"gensym", prim_gensym},
{"eval",   prim_eval},
```

### 2.2 VM State Addition

Add `int gensym_counter` to `vm_state_t` (in `vm.h`). Initialized to 0 in `vm_init`.

### 2.3 Macro Table

A global variable `*macro-table*` in `lib.scm`:
```scheme
(define *macro-table* '())
```

### 2.4 define-syntax Special Form

In `compiler.scm`, detected in the list compilation dispatch before other special forms:

```scheme
(define (compile-list fn args s env)
  ;; Macro expansion hook (Phase 2)
  (let ((transformer (lookup-macro fn)))
    (if transformer
        (compile-expr (transformer (cons fn args)) s env)
        ;; Special form dispatch
        (cond
          ((eq? fn 'define-syntax) (compile-define-syntax args s))
          ((eq? fn 'quote)    ...)
          ...))))
```

`compile-define-syntax`:
1. Evaluates the transformer expression at compile time via `(eval transformer-expr)`
2. Pushes `(cons name transformer)` onto `*macro-table*`
3. Emits `OP_PUSH_NIL`

### 2.5 Macro Expansion Hook

In `compile-list`, before special form dispatch:
1. Call `(lookup-macro fn)` to check `*macro-table*`
2. If found, call `(transformer (cons fn args))` to get the expanded form
3. Compile the expanded form recursively

`lookup-macro` scans `*macro-table*` using `assq`.

### 2.6 syntax-rules Library

Implemented in Scheme (`lib.scm` or a new `src/scheme/syntax-rules.scm`).

**syntax-rules form:**
```scheme
(syntax-rules (<literals>)
  ((<pattern> <template>) ...))
```

**Pattern matching** (`match-pattern`):
- Literal identifiers must match exactly (eq?)
- Pattern variables match anything and produce bindings
- Ellipsis `...` matches zero or more repetitions
- Nested patterns recurse into sub-structure
- Returns an alist of `(pattern-var . matched-input)` or `#f` on failure

**Template filling** (`fill-template`):
- Pattern variables → substituted with matched values
- Literals → copied as-is
- Lists → recursively filled
- Ellipsis in template → iterates over matched repetitions

**Hygiene** (`rename`):
- When `syntax-rules` creates a transformer closure, it captures a unique macro ID
- Each expansion call gets a unique sub-ID
- Immune symbols (special forms: lambda, if, define, set!, begin, quote, cond — and primitives checked via `prim-index`) pass through unrenamed
- All other free identifiers in templates get: `symbol{M<macro-id>-<call-id>}`
- Pattern variables are substituted as-is (they come from the call site)

### 2.7 Ellipsis Handling

The most complex part of syntax-rules. Strategies:

**Pattern side:** When encountering `(pattern ...)` in a rule pattern:
1. Try matching 0 repetitions first (greedy)
2. If the rest of the pattern fails, try 1, 2, ... repetitions
3. Collect all matched sub-bindings

**Template side:** When encountering `(template ...)` in a rule template:
1. Look up the pattern variable bound to the ellipsis
2. Iterate over each matched element, filling the inner template
3. Splice results into the output

---

## 3. Scheme Compiler (`compiler.scm`)

New file: `src/scheme/compiler.scm`

Provides a global `(compile expr)` function that returns `(bytecode-list . consts-list)`.

Reimplements the C bootstrap compiler's logic in Scheme:
- `(compile expr)` → dispatches on type
- Fixnums → `(OP_PUSH_INT val)`
- Symbols → local/env/global lookup
- Pairs → `(compile-list fn args)`
- Others → constant

Handles all special forms the C compiler handles, plus closures (from day one) and macros (Phase 2).

Bytecode emission model: `compile` returns a pair `(bytecode-list . consts-list)` where bytecode-list is a proper list of fixnums (opcodes and operands) and consts-list is a proper list of constant values. The C primitive `assemble-code` converts this to a VM code object.

---

## 4. Files

| File | Phase | Action | Purpose |
|------|-------|--------|---------|
| `src/bootstrap/compiler.c` | 1 | Modify | Env threading, free var detection, real nfree |
| `src/scheme/compiler.scm` | 1 | Create | Full Scheme compiler with closure support |
| `src/primitives/macro.c` | 2 | Create | gensym, eval primitives |
| `src/vm/builtins.c` | 2 | Modify | Register gensym, eval |
| `src/include/vm.h` | 2 | Modify | Add gensym_counter to vm_state_t |
| `src/scheme/lib.scm` | 2 | Modify | *macro-table*, macro helpers, syntax-rules |
| `tests/c/test_macro.c` | 2 | Create | gensym, eval C-level tests |
| `tests/scheme/test-macro.ss` | 2 | Create | Syntax-rules integration tests |
| `tests/scheme/test-closure.ss` | 1 | Create | Closure capture tests |
| `tests/c/meson.build` | 1-2 | Modify | Add test_macro entry |
| `tests/meson.build` | 1-2 | Modify | Add Scheme test entries |

---

## 5. Testing Strategy

### Phase 1 Tests (test-closure.ss)
- Simple closure: `(((lambda (x) (lambda (y) (+ x y))) 1) 2)` → 3
- Multiple captured vars: `(((lambda (x y) (lambda (z) (+ x (+ y z)))) 1 2) 3)` → 6
- Closure in define: `(define (make-adder n) (lambda (x) (+ x n)))` then `((make-adder 5) 10)` → 15
- Nested closures: closure returning a closure, both capturing different scopes
- No capture (nfree=0): simple lambda without free vars
- Shadowing: inner lambda param shadows outer binding

### Phase 2 C Tests (test_macro.c)
- gensym returns a symbol
- Two gensym calls return different symbols
- eval evaluates `(+ 1 2)` → 3
- eval evaluates `(cons 'a '(b))` → (a b)

### Phase 2 Scheme Tests (test-macro.ss)
- Simple macro: `(define-syntax my-when (syntax-rules () ((_ test body ...) (if test (begin body ...)))))`
- Macro with multiple clauses: `(define-syntax my-or ...)`
- Macro hygiene — macro-introduced identifiers don't shadow call-site bindings
- Nested macro expansion
