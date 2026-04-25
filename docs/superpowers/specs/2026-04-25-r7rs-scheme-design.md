# R7RS Small Scheme 实现设计文档

**日期：** 2026-04-25
**目标：** 设计并实现一个符合 R7RS Small 标准的 Scheme 语言实现
**语言：** C（C11） + Scheme（自举）
**执行模型：** 字节码 VM，未来扩展编译能力
**应用场景：** 嵌入式脚本

---

## 1. 整体架构

四层架构，每层只能依赖下层，下层不依赖上层：

```
┌─────────────────────────────────────────┐
│  Scheme 标准库 (Scheme)                   │
│  编译器 · 宏展开器 · 数值/字符串/端口库    │
├─────────────────────────────────────────┤
│  Scheme 引导编译器 (Scheme)               │
│  源码→字节码编译，逐步替换 C 引导编译器     │
├─────────────────────────────────────────┤
│  C 核心                                   │
│  ┌──────┬──────┬──────┬──────┬───────┐   │
│  │ VM   │  GC  │ PAL  │读取器 │ 原始  │   │
│  │      │      │      │      │ 过程库 │   │
│  └──────┴──────┴──────┴──────┴───────┘   │
├─────────────────────────────────────────┤
│  C 运行时 / 宿主接口                      │
│  main() · 嵌入 API · 平台入口            │
└─────────────────────────────────────────┘
```

### 层级职责

- **PAL（平台抽象层）：** 封装文件 I/O、命令行参数、动态库加载等 OS 差异，内部通过 `#ifdef _WIN32` 隔离实现，对外提供统一 C 接口
- **GC：** 通过 `gc_interface` 结构体（函数指针表）注册，运行时替换不影响上层
- **VM：** 读取字节码执行，持有 GC 和 PAL 引用
- **原始过程库（primitive procedures）：** C 实现的 Scheme 基础函数（cons、+、display 等），供 VM 直接调用

---

## 2. 数据表示与类型系统

所有 Scheme 对象统一用 `word` 表示，采用标记指针（tagged pointer）和堆分配对象结合的方式。

### 标记方案

64 位系统，一个字 = 64 bits。堆对象 8 字节对齐，低 3 位天然为零，借低 2 位作标签：

```
┌─────────────────────────────────────────┬──────┐
│             数据 / 指针                   │ type│
│               (62 bit)                  │(2bit)│
└─────────────────────────────────────────┴──────┘
```

**类型标签（低 2 位）：**
- `00` — fixnum（62 位有符号整数，作为立即值编码）
- `01` — char（Unicode 标量值编码在高位区域）
- `10` — 堆对象指针（指向 GC 管理的 8 字节对齐内存）
- `11` — 其他立即值（bool、nil、eof，子类型编码在剩余位中）

GC 标记位不嵌入指针，存储在堆对象的 `GC_header` 中。

### 堆对象

所有堆分配对象以 `GC_header` 开头（包含大小和标记位），后跟对象数据：

```
┌──────────┬──────────────┬──────────────────────┐
│ GC_header │ 类型字 (type) │    对象数据...         │
└──────────┴──────────────┴──────────────────────┘
```

**堆对象类型：**
- pair: `[car][cdr]`
- vector: `[length][elem1][elem2]...`
- string: `[length][char1][char2]...`（紧凑存储，字节序列）
- bytevector: `[length][byte1][byte2]...`
- closure: `[entry][env_ptr][num_free]`
- symbol: `[hash][string_ptr]`
- port: `[type][flags][buffer]...`（在 PAL 层实现）
- record: `[rtd][field1][field2]...`

---

## 3. 数值塔设计

### 数值层次

```
number
├── complex (real + imag, 可精确/非精确)
│   └── real
│       ├── rational (n/d, n,d 均为精确整数)
│       │   └── integer
│   │           ├── fixnum (62-bit, 内联立即值)
│   │           └── bignum (任意精度, 堆分配)
│       └── flonum (IEEE 754 double, 堆分配)
```

不依赖 GMP/MPFR 等外部数值库，全部自实现。

### Fixnum

低 2 位为 `00`，高 62 位为补码表示整数值。范围 `[-2^61, 2^61-1]`。
所有操作先内联尝试，溢出时自动晋升 bignum。

### Bignum

以 `uint32_t` 为 limb，BASE = 2^32，小端序存储。符号位单独记录。

核心算法（Knuth TAOCP Vol 2）：
- 加法/减法：按 limb 进/借位线性扫描
- 乘法：教科书式 O(n²)，未来可替换为 Karatsuba
- 除法：Knuth 算法 D（试商法）
- GCD：二进制 GCD 算法（Stein's），避免除法
- 字符串转换：反复除以 10

### Rational

两个精确整数字段（numerator / denominator），始终以最简形式存储（建值时做 GCD 约分，分母总为正）。

### Flonum

堆分配的 IEEE 754 double，提供 eqv? 可区分的对象身份。

### Complex

两个 real 字段（real / imag），精度取决于两部分。

### 类型晋升规则

| 左 \ 右   | fixnum  | bignum | rational | flonum |
|-----------|---------|--------|----------|--------|
| fixnum    | fixnum* | bignum | rational | flonum |
| bignum    | bignum  | bignum | rational | flonum |
| rational  | rational| rational| rational | flonum |
| flonum    | flonum  | flonum | flonum   | flonum |

*fixnum + fixnum 溢出时自动晋升 bignum

### 必实现数值过程

所有 R7RS Small 要求的数值谓词、算术、转换、类型转换过程。

---

## 4. 字节码 VM 设计

### 架构

基于栈的虚拟机，调用帧嵌入操作数栈。

**运行时寄存器：**
- **ip** — 指令指针
- **sp** — 操作数栈顶指针
- **fp** — 当前帧基址
- **env** — 当前环境链表
- **acc** — 累加器（单返回值优化）

### 帧布局

```
sp → [argN]       ← 调用者推送的参数
     [arg1]
fp → [frame_mark] ← 帧标记(返回地址 + env + stack_bottom)
     [local1]      ← 局部变量
     [local0]
     [temp...]
```

### 指令集

**栈操作：** `nop`, `push_nil`, `push_true`, `push_false`, `push_const` (1B idx), `push_int` (4B), `push_env` (1B depth, 1B offset), `pop`, `dup`

**变量操作：** `local_ref` (1B idx), `local_set` (1B idx), `free_ref` (1B depth, 1B offset), `free_set` (1B depth, 1B offset), `global_ref` (1B idx), `global_set` (1B idx)

**过程与调用：** `close` (2B code_off, 1B nfree), `call` (1B nargs), `tail_call` (1B nargs), `apply`, `return`

**控制流：** `jmp` (2B offset), `jmp_if` (2B offset), `jmp_if_not` (2B offset), `call/cc`

**对象操作：** `cons`, `car`, `cdr`, `set_car!`, `set_cdr!`, `make_vec`, `vec_ref`, `vec_set`, `alloc` (1B type, 2B size)

**原始调用：** `prim_call` (1B nargs, 2B prim_index)

**特殊：** `halt`, `mv_call` (1B nargs), `reset_values`, `push_values`

### 尾调用优化

`tail_call` 在当前帧上直接替换参数 → 重置操作数栈 → 跳转目标，不分配新帧。

### call/cc

捕获整个操作数栈 + ip + env 为 continuation 对象（堆分配，GC 管理），可多次调用。

### 字节码代码对象

```
[GC_header][type][bytecode_len][bytecode...][const_count][const_1...const_N]
```

字节码区不可变、不含指针，GC 无需扫描。

---

## 5. GC 设计与可替换接口

### GC 接口

```c
typedef struct {
    gc_alloc_fn       alloc;
    gc_realloc_fn     realloc;
    gc_free_fn        free;
    gc_collect_fn     collect;
    gc_heap_size_fn   heap_size;
    void*             state;
} gc_interface;
```

VM 通过 `gc_interface` 指针访问 GC，不直接调用任何 GC 函数。

### 标记-清除实现

- **GC_header（一个字）：** [size(61bit)][COLOR(2bit)][MARK(1bit)]
- **三色标记：** white（未访问） → grey（加入工作列表） → black（已标记完成）
- **根集：** VM 寄存器、操作数栈、全局变量表、常量池
- **空闲链表：** First-fit 策略
- **写屏障：** 初始版本不需要，接口预留未来接入分代 GC
- **内存来源：** `mmap`（POSIX）/ `VirtualAlloc`（Windows）

---

## 6. 平台抽象层（PAL）

### 接口定义

```c
typedef struct {
    // 内存管理
    void* (*mmap_alloc)(size_t size);
    void  (*mmap_free)(void* ptr, size_t size);

    // 文件 I/O
    int     (*file_open)(const char* path, int mode);
    int     (*file_close)(int fd);
    int64_t (*file_read)(int fd, void* buf, uint64_t nbytes);
    int64_t (*file_write)(int fd, const void* buf, uint64_t nbytes);
    int64_t (*file_seek)(int fd, int64_t offset, int whence);
    int     (*file_exists)(const char* path);

    // 动态库加载
    void* (*dl_open)(const char* path);
    void  (*dl_close)(void* handle);
    void* (*dl_sym)(void* handle, const char* symbol);
    char* (*dl_error)(void);

    // 环境信息
    int64_t (*current_time_ms)(void);
    int     (*command_line)(int* argc, char*** argv);
    void    (*exit)(int code);

    // 字符串/路径编码转换
    char* (*utf8_to_native)(const char* utf8);
    char* (*native_to_utf8)(const char* native);

    // 错误信息
    int   (*last_error)(void);
    char* (*error_message)(int err);

    void* state;
} pal_interface;
```

### 实现策略

- POSIX：直接映射到 `mmap` / `open` / `read` / `write` / `dlopen`
- Windows：UTF-8 → UTF-16 转换后调用 `_wopen` / `LoadLibraryW` / `CreateFileMapping` + `MapViewOfFile`
- 通过 `#ifdef _WIN32` 或构建系统选择不同实现源

---

## 7. 自举管线

### 阶段 0：C 核心

实现 PAL + GC + VM + 引导读取器 + 原始过程库 + **引导编译器**（C 实现：S 表达式 → 字节码）。产出可执行文件。

### 阶段 1：Scheme 编译器

用 Scheme 编写 `compiler.scm`（宏展开、编译优化、库导入）。使用阶段 0 引导编译器编译 → `compiler.bc`。

### 阶段 2：Scheme 标准库

用 Scheme 编写所有 R7RS 标准库模块，编译为字节码镜像。

### 阶段 3：完整自举

Scheme 编译器编译自身。系统不再依赖 C 引导编译器。

### 阶段 4：消除 C 引导编译器（可选）

完全自举后 C 引导编译器代码可删除，系统由 C 核心 + Scheme 编译器字节码镜像组成。

### 字节码镜像格式

```
magic: "SBC\x01" (4B)
version: uint32_t
symbol_count: uint32_t
symbol_table: [...]
code_count: uint32_t
code_objects: [name_index, bytecode_len, bytecode..., const_count, consts...]
entry_point: uint32_t
```

---

## 8. 错误处理与异常系统

### 原则

异常是 Scheme 层面概念，C 核心通过返回错误码传递异常，Scheme 层统一处理。

### C 层错误路径

原始过程检测到错误时设置 `vm->error_code` 和 `vm->error_arg`，返回哨兵值。VM 检测到哨兵后构造条件对象，沿调用栈查找处理器。

### 条件对象层次

```
&condition
├── &error
│   ├── &read
│   └── &assertion
├── &serious
└── &warning
```

使用 Scheme record 实现。

---

## 9. R7RS Small 合规范围

### 优先级

- **P0（必须）：** `(scheme base)` — 核心语法、过程、异常、库系统
- **P1（重要）：** `(scheme read)` `(scheme write)` `(scheme file)` `(scheme eval)` `(scheme cxr)` `(scheme char)` `(scheme lazy)` `(scheme load)` `(scheme process-context)` `(scheme time)` `(scheme box)` `(scheme case-lambda)`
- **P2（高阶）：** `(scheme inexact)` `(scheme complex)` `(scheme bitwise)` `(scheme vector)` `(scheme sort)` `(scheme repl)`

### 测试策略

1. **C 单元测试** — 测试 bignum、GC、字节码编码等底层
2. **Scheme 合规测试** — R7RS 官方测试套件
3. **集成测试** — 自举流程验证

---

## 10. 嵌入 API

```c
// 初始化/销毁
scheme_state_t* scheme_init(void);
scheme_state_t* scheme_init_with_gc(gc_interface* gc);
scheme_state_t* scheme_init_with_pal(pal_interface* pal);
void            scheme_destroy(scheme_state_t* s);

// 执行代码
scheme_result_t scheme_eval_string(scheme_state_t* s, const char* code);
scheme_result_t scheme_eval_file(scheme_state_t* s, const char* path);
scheme_result_t scheme_call(scheme_state_t* s, const char* proc_name,
                             int argc, scheme_val_t* args);

// 值操作
scheme_val_t    scheme_make_fixnum(scheme_state_t* s, int64_t n);
scheme_val_t    scheme_make_string(scheme_state_t* s, const char* str);
int64_t         scheme_fixnum_value(scheme_val_t v);
const char*     scheme_string_value(scheme_val_t v);
scheme_val_type scheme_type_of(scheme_val_t v);

// GC 控制
void            scheme_gc(scheme_state_t* s);
size_t          scheme_heap_size(scheme_state_t* s);
```

---

## 11. 项目结构与构建

### 目录布局

```
scheme/
├── docs/superpowers/specs/     # 设计文档
├── src/
│   ├── include/                # C 头文件
│   │   ├── scheme.h            # 主头文件/嵌入 API
│   │   ├── types.h             # 类型定义
│   │   ├── vm.h                # VM 内部接口
│   │   ├── gc.h                # GC 接口
│   │   ├── pal.h               # PAL 接口
│   │   └── prim.h              # 原始过程声明
│   ├── pal/
│   │   ├── pal_posix.c         # POSIX 实现
│   │   └── pal_win32.c         # Windows 实现
│   ├── gc/
│   │   └── gc.c                # GC 实现
│   ├── vm/
│   │   ├── vm.c                # VM 主循环
│   │   ├── opcodes.h           # 指令码定义
│   │   └── builtins.c          # 内建过程
│   ├── reader/
│   │   └── reader.c            # C 引导读取器
│   ├── bootstrap/
│   │   └── compiler.c          # C 引导编译器
│   ├── primitives/
│   │   ├── arith.c             # 数值运算
│   │   ├── pair.c              # pair/list 操作
│   │   ├── vector.c            # 向量操作
│   │   ├── string.c            # 字符串操作
│   │   ├── port.c              # 端口操作
│   │   ├── control.c           # 控制流程
│   │   └── eval.c              # eval/environment
│   └── main.c                  # 入口
├── lib/
│   ├── scheme/                 # R7RS 标准库 Scheme 源码
│   ├── compiler.scm            # Scheme 编译器
│   └── expander.scm            # 宏展开器
├── boot/
│   └── bootstrap.scm           # 自举辅助脚本
├── tests/
│   ├── c/                      # C 单元测试
│   └── scheme/                 # Scheme 测试
├── meson.build                 # Meson 根构建文件
├── meson_options.txt           # 构建选项
└── README.md
```

### Meson 构建

```
meson setup build          # 配置构建
meson compile -C build     # 编译
meson test -C build        # 运行测试
meson test -C build r7rs   # 运行 R7RS 合规测试
```
