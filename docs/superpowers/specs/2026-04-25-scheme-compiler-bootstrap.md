# Scheme 编译器自举设计

**日期：** 2026-04-25
**目标：** 编写 Scheme 编译器 `compiler.scm`，替换 C 引导编译器

## 自举管线

```
扩展 C 编译器 (let/cond/begin)  →  C 编译器编译 compiler.scm
                                      ↓
                              Scheme 编译器生效
                                      ↓
                       后续代码使用 Scheme compile 编译
```

### 阶段划分

1. **C 编译器扩展**：添加 `let`、`cond`、`begin` 特殊形式支持
2. **C 原始过程扩展**：添加 `symbol->string`、`prim-index`、`assemble-code`、`read`
3. **编写 compiler.scm**：用 Scheme 编写递归下降编译器
4. **修改 main.c**：引导加载 compiler.scm，后续代码使用 Scheme compile

## C 编译器扩展

### `begin`

编译多个表达式的序列，返回最后一个表达式的值。前面每个表达式编译后添加 `OP_POP`：

```scheme
(begin (display "hi") (+ 1 2))
→ OP_PUSH_CONST "hi", OP_PRIM_CALL display, OP_POP, OP_PUSH_INT 1, OP_PUSH_INT 2, OP_PRIM_CALL +
```

空 `begin` 返回 `()(word_nil())`。单个表达式的 `begin` 直接编译该表达式。

### `cond`

编译时展开为嵌套 `if`。标准模式：

```scheme
(cond (test1 expr1) (test2 expr2) (else expr3))
→ (if test1 expr1 (if test2 expr2 expr3))
```

C 编译器不构建 S 表达式，直接递归编译：
- 对于每个 `(test expr)` 子句：编译 test（作为条件），编译 expr（作为结果），用 `JMP_IF_NOT` + `JMP` 连接
- `else` 子句即为无条件执行

### `let`

编译时构建 lambda S 表达式并递归编译。`((x 1) (y 2))` → `(lambda ...)`：

```scheme
(let ((x 1) (y 2)) (+ x y))
→ ((lambda (x y) (+ x y)) 1 2)
```

C 编译器在堆上分配 pair/list 构建完整的 lambda 调用 S 表达式，然后递归调用 `compile_expr_to_buf`。

## C 原始过程扩展

### `symbol->string`

```c
word prim_symbol_to_string(vm_state_t* vm, int nargs) {
    word sym = vm->sp[0];  // sp[0] = first arg
    word* hdr = ptr_from_word(sym);
    int len = (int)string_length(hdr);
    word* str = vm->gc->alloc_words(3 + (len + sizeof(word) - 1) / sizeof(word));
    obj_set_type(str, OBJ_TYPE_STRING);
    string_set(str, DATA_START_INDEX, (word)len);  // actually DATA_START_INDEX is where length is stored
    ...
}
```

返回 scheme string 对象，包含符号名字符。

### `prim-index`

```c
word prim_prim_index(vm_state_t* vm, int nargs) {
    word sym = vm->sp[0];
    // 提取符号名，查 prim_lookup
    int idx = prim_lookup(name);
    if (idx < 0) return word_false();
    return word_from_fixnum(idx);
}
```

用于 Scheme 编译器确定一个符号是否为原始过程及其索引。

### `assemble-code`

```c
word prim_assemble_code(vm_state_t* vm, int nargs) {
    word pair = vm->sp[0];
    // pair = (bytecode-list . const-list)
    // bytecode-list = (b1 b2 b3 ...) — 字节码整数列表
    // const-list = (c1 c2 c3 ...) — 常量列表
    // 构建 code object，调用 vm_load_code
    // 返回 code index (fixnum)
}
```

这是核心桥接函数。遍历 Scheme 列表提取字节码和常量，构造 `word* code_obj`，调用 `vm_load_code`。

### `read`

```c
word prim_read(vm_state_t* vm, int nargs) {
    // 从 stdin 读取一个 S 表达式
    // 使用 reader.c 的 read_sexp
    // 返回读取的表达式或 eof
}
```

## compiler.scm 结构

### 入口

```scheme
(define (compile expr)
  (let ((bc (compile-expr expr '())))
    (cons (reverse (car bc)) (reverse (cdr bc)))))
```

返回 `(bytecodes . consts)` 对。

### compile-expr

```scheme
(define (compile-expr expr acc)
  (cond ((fixnum? expr) (compile-fixnum expr acc))
        ((symbol? expr) (compile-symbol expr acc))
        ((pair? expr) (compile-list expr acc))
        (else (compile-const expr acc))))
```

`acc` 是累加器参数（字节码列表反向构建），在每个 `compile-*` 函数间传递。常量也逆序收集在 cdr 部分：

返回 `(bytecodes . consts)` 对。

### compile-list

```scheme
(define (compile-list expr acc)
  (let ((fn (car expr)) (args (cdr expr)))
    (cond ((eq? fn 'quote) (compile-quote args acc))
          ((eq? fn 'if) (compile-if args acc))
          ((eq? fn 'lambda) (compile-lambda args acc))
          ((eq? fn 'define) (compile-define args acc))
          (else (compile-call fn args acc)))))
```

### 辅助函数

```scheme
(define (emit-byte bc acc)
  (cons (cons bc (car acc)) (cdr acc)))

(define (emit-word w acc)
  (emit-byte (logand w #xff)
    (emit-byte (logand (ash w -8) #xff)
      (emit-byte (logand (ash w -16) #xff)
        (emit-byte (logand (ash w -24) #xff) acc)))))
```

### 常量池管理

常量从表达式提取，已存在的常量复用索引。`acc` 的 cdr 部分是常量列表（逆序）。`add-const` 返回新 acc 并将索引通过局部变量绑定传递：

```scheme
(define (add-const expr acc)
  (let ((consts (cdr acc)))
    (let looking ((i 0) (cs consts))
      (cond ((null? cs)
             (cons (car acc) (cons expr consts)))  ; 新 acc
            ((equal? (car cs) expr)
             acc)  ; 已存在，不修改
            (else (looking (+ i 1) (cdr cs)))))))
```

## main.c 修改

### vm_call 辅助函数

```c
word vm_call(vm_state_t* vm, word fn, int nargs, word arg) {
    // 将 arg 推入栈，在 fn 上执行 OP_CALL
    // 返回执行结果
}
```

### 引导流程

```c
int main(int argc, char** argv) {
    // ... 初始化 ...

    // Phase 1: Load compiler.scm using C compiler
    if (pal->file_exists("compiler.scm")) {
        exec_file("compiler.scm");  // 用 C 编译器编译执行，定义 compile
    }

    // Phase 2: Use Scheme compiler for subsequent expressions
    if (argc > 1) {
        exec_file_with_scheme(argv[1]);
    } else {
        repl_with_scheme();
    }
}
```

```c
static void repl_with_scheme(void) {
    // 获取 compile 函数
    word compile_fn = vm_find_global_by_name(vm, "compile");

    while (1) {
        word expr = read_sexp(vm, buf, &pos);

        // 调用 Scheme compile
        word result = vm_call(vm, compile_fn, 1, expr);
        word code_pair = result;

        // 用 C 原始过程 assemble
        word code_idx = prim_assemble_code(vm, code_pair);
        vm_execute(vm, (int)word_to_fixnum(code_idx));
    }
}
```

## 修改的文件

| 文件 | 修改内容 |
|------|---------|
| `src/bootstrap/compiler.c` | 添加 `begin`、`cond`、`let` 特殊形式 |
| `src/primitives/port.c` | 添加 `read` 原始过程 |
| `src/primitives/symbol.c` (新) | 添加 `symbol->string`、`string->symbol` |
| `src/vm/builtins.c` | 注册新原始过程；添加 `prim-index`、`assemble-code` |
| `src/main.c` | 引导流程：Phase 1 加载 compiler.scm，Phase 2 使用 Scheme compile |
| `src/include/vm.h` | 添加 `vm_call` 声明 |
| `src/vm/vm.c` | 添加 `vm_call` 实现；添加 `vm_find_global_by_name` |
| `compiler.scm` (新) | Scheme 编译器源代码 |

## 测试策略

1. **C 编译器扩展**：meson test + 手动测试 `let`/`cond`/`begin`
2. **compiler.scm 自举**：编译 compiler.scm → 对比 Scheme compile 结果与 C compile 结果一致
3. **完整管线**：用 C 编译器编译 compiler.scm → 用 Scheme 编译器编译 test.scm → 验证正确
