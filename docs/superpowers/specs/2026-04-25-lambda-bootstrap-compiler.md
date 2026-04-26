# 引导编译器 lambda 闭包支持设计

**日期：** 2026-04-25
**目标：** 扩展 C 引导编译器支持 lambda 表达式、函数调用和局部变量引用 — 让 Scheme 编译器能用 Scheme 编写

## 动机

Stage 0 C 编译器已支持：数值、符号、quote、define、if 和原始过程调用。Lambda 是桩代码（只推 nil）。要自然地编写 `compiler.scm`，编译器必须支持带局部变量的用户定义函数。

## 帧布局

`OP_CALL` 执行后，栈帧布局如下：

```
sp → [临时变量...]        ← 嵌套调用时压入临时值
     [局部_N]             ← LREF n（局部变量在帧标记上方）
     [局部_1]
     [保存的 fp]          ← 旧帧指针（1 word）
     [保存的 env]         ← 旧环境（1 word）
     [保存的 ip]          ← 返回地址（1 word，编码为 word）
fp → [参数_1]             ← LREF 1（第一个参数索引为 1）
```

- `LREF n` / `LSET n` 访问 `fp[n]` — 正索引，不是负数
- `fp[0]` 保留；`fp[1..nargs]` 是参数
- 局部变量在帧标记上方（更高索引）

## 调用约定

1. 编译器发出代码先压入闭包值，然后从左到右压入所有参数
2. `OP_CALL nargs` 从 `sp[-nargs]` 读取闭包，提取 code 和 env，压入帧标记（fp、env、ip），设置新 fp = sp - nargs，调整 sp，跳转到闭包代码
3. `OP_RETURN` 弹出帧，恢复 fp/env/ip，压入返回值
4. `OP_TAIL_CALL nargs` 复用当前帧 — 原地替换参数，不压入新帧

## Lambda 编译

编译器遇到 `(lambda (x y) body)` 时：

1. **分配一个代码对象**给 lambda 体 — 这是一个子函数
2. **记录参数名** x、y 为局部索引 1、2
3. **跟踪自由变量** — 体中的非参数、非全局变量引用都是自由变量（将被闭包捕获）
4. **在父代码中发出 `OP_CLOSE code_idx nfree`** — 运行时创建闭包对象，捕获当前环境的自由变量

### 局部变量跟踪

编译器有一个 `locals` 数组跟踪 name → index 映射。进入 lambda 时：
- 压入新的局部作用域
- 分配参数名为索引 1、2、...、n
- 局部变量引用发出 `LREF` / `LSET` + 对应索引

### 链式编译 define

`(define (f x) body)` 是 `(define f (lambda (x) body))` 的语法糖。编译器解糖处理：提取函数名、参数、体，编译为 lambda，然后 define。

## VM 指令实现

所有指令已在 `opcodes.h` 中定义：

- **OP_CLOSE** code_idx(2B) nfree(1B) — 分配闭包，捕获环境
- **OP_CALL** nargs(1B) — 调用闭包，压入帧
- **OP_TAIL_CALL** nargs(1B) — 尾调用，复用帧
- **OP_RETURN** — 从函数返回
- **OP_LREF** idx(1B) — 局部变量读取（改用正索引 `fp[idx]`）
- **OP_LSET** idx(1B) — 局部变量写入（改用正索引 `fp[idx]`）

## 修改的文件

| 文件 | 修改内容 |
|------|---------|
| `src/vm/vm.c` | 实现 OP_CLOSE、OP_CALL、OP_TAIL_CALL、OP_RETURN；LREF/LSET 改为 `fp[(int)idx]` |
| `src/bootstrap/compiler.c` | lambda 编译、函数调用编译、局部变量跟踪、闭包代码对象 |
| `src/include/vm.h` | 可能添加多代码对象所需的 `code_count` 跟踪 |

## 测试

1. **C 测试：** `meson test -C build` — 现有类型/GC 测试必须通过
2. **Scheme 测试：** 文件执行 `((lambda (x) (+ x 1)) 41)` → 42
3. **Scheme 测试：** `(define (f x) (+ x 1)) (f 41)` → 42
4. **Scheme 测试：** 嵌套闭包和词法作用域模式
