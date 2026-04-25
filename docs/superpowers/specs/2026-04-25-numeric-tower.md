# R7RS 数值塔实现设计

**日期：** 2026-04-25
**目标：** 实现完整的 R7RS Small 数值塔（除理数和复数在 Scheme 层实现外）
**实现语言：** C11（核心算法）+ Scheme（理数/复数）

**NumPy-style type promotion:**
```
fixnum + fixnum → fixnum (溢出时 → bignum)
fixnum + bignum → bignum
fixnum + flonum → flonum
bignum + bignum → bignum
bignum + flonum → flonum
flonum + flonum → flonum
```

**架构原则：** 所有批编译（compile）和 VM 执行路径不可变。数值塔完全通过 C 原始过程和 Scheme 库实现，不修改编译器或 VM。

---

## 1. 类型表示

### 1.1 Bignum

堆对象布局（`OBJ_TYPE_BIGNUM = 7`，已在 types.h 中预留）：

```
[GC_hdr][type=7][sign: 1 word][limb_count: 1 word][limb0][limb1]...[limbN]
```

- **sign:** 0 = 非负，1 = 负数
- **limb_count:** uint32_t limb 数量（`size_t` 存储为 word）
- **limbs:** 小端序 uint32_t 数组（limb0 = 最低有效位），嵌入在堆对象中连续存储
- **归一化表示:** 无前导零 limb；值 0 为 sign=0, limb_count=0

选择 uint32_t limb 的原因：两个 32-bit 值的乘积完全适用于 uint64_t，方便乘法和除法算法中处理进位。

### 1.2 Flonum

堆对象布局（`OBJ_TYPE_FLONUM = 9`，已在 types.h 中预留）：

```
[GC_hdr][type=9][double_value: 1 word]
```

- **double_value:** 以 `uint64_t` 形式存储的 IEEE 754 双精度浮点数的位模式（通过 `memcpy` 转换）

Flonum 不做标记指针内联，因为 64-bit NaN-boxing 需要 3-bit tags，与当前 2-bit tag 方案不兼容。

### 1.3 Rational（Scheme 层）

使用 `define-record-type` 创建 `<rational>` 记录：

```
<rational> record: [numerator][denominator]
```

两个字段均为精确整数（fixnum 或 bignum）。构造时立即约分（GCD 归一化），分母总为正。

### 1.4 Complex（Scheme 层）

使用 `define-record-type` 创建 `<complex>` 记录：

```
<complex> record: [real][imag]
```

两个字段均为任意数值类型（fixnum、bignum、flonum 或 rational）。

---

## 2. 文件结构

### 2.1 C 源文件

**`number.c`**（新文件，替换 `arith.c`）— 约 1200 行

内部函数：
```
// Bignum core (~400 lines)
static word    make_bignum(int sign, const uint32_t* limbs, size_t n);
static word    bignum_from_int64(int64_t n);
static int64_t bignum_to_int64(word b, int* ok);
static word    bignum_normalize(word b);
static int     bignum_cmp(word a, word b);           // -1, 0, +1
static word    bignum_add(word a, word b);
static word    bignum_sub(word a, word b);
static word    bignum_mul(word a, word b);
static word    bignum_negate(word a);
static word    bignum_abs(word a);
static word    bignum_divmod(word a, word b, word* mod);
static word    bignum_gcd(word a, word b);
static word    bignum_from_string(const char* s, int radix);
static char*   bignum_to_string(word b, int radix);  // caller frees

// Flonum core (~200 lines)
static word    word_from_double(double d);
static double  word_to_double(word w);
static word    flonum_add(word a, word b);
static word    flonum_sub(word a, word b);
static word    flonum_mul(word a, word b);
static word    flonum_div(word a, word b);
static int     flonum_cmp(word a, word b);           // -1, 0, +1 for ordered, signals for NaN
static bool    flonum_finitep(word w);
static bool    flonum_infinitep(word w);
static bool    flonum_nanp(word w);

// Transcendental wrappers (~100 lines)
static word    flonum_sin(word x);
static word    flonum_cos(word x);
static word    flonum_tan(word x);
static word    flonum_asin(word x);
static word    flonum_acos(word x);
static word    flonum_atan(word x);
static word    flonum_atan2(word x, word y);
static word    flonum_sqrt(word x);
static word    flonum_exp(word x);
static word    flonum_log(word x);

// Fixnum overflow helpers (~50 lines)
static inline bool fixnum_add_overflows(int64_t a, int64_t b);
static inline bool fixnum_sub_overflows(int64_t a, int64_t b);
static inline bool fixnum_mul_overflows(int64_t a, int64_t b);
```

原始过程定义——`prim_number_pred`、`prim_integer_pred`、`prim_exact_pred`、`prim_inexact_pred`、`prim_zerop`、`prim_positivep`、`prim_negativep`、`prim_evenp`、`prim_oddp`

类型分派 +-*/（~200 行）：
```
word prim_add(vm_state_t* vm, int nargs);
word prim_sub(vm_state_t* vm, int nargs);
word prim_mul(vm_state_t* vm, int nargs);
word prim_div(vm_state_t* vm, int nargs);
word prim_lt(vm_state_t* vm, int nargs);
word prim_gt(vm_state_t* vm, int nargs);
word prim_eq_num(vm_state_t* vm, int nargs);
```

类型分派算法（以 `prim_add` 为例）：

```c
word prim_add(vm_state_t* vm, int nargs) {
    // 扫描参数确定最高类型
    bool has_flonum = false, has_bignum = false;
    for (int i = 0; i < nargs; i++) {
        word w = vm->sp[i];
        if (is_ptr(w)) {
            int t = (int)obj_type(ptr_from_word(w));
            if (t == OBJ_TYPE_FLONUM) has_flonum = true;
            else if (t == OBJ_TYPE_BIGNUM) has_bignum = true;
        } else if (!is_fixnum(w)) {
            vm->error_code = 1; return word_nil();
        }
    }

    // flonum 路径: 全部提升为 double
    if (has_flonum) {
        double sum = 0.0;
        for (int i = 0; i < nargs; i++) {
            word w = vm->sp[i];
            if (is_fixnum(w)) sum += (double)word_to_fixnum(w);
            else if (is_bignum(w)) sum = ...; // big->double
            else sum += word_to_double(w);
        }
        return word_from_double(sum);
    }

    // bignum 路径: 全部提升为 bignum, 累加
    if (has_bignum) {
        word acc = bignum_from_int64(0);
        for (int i = 0; i < nargs; i++) {
            word w = vm->sp[i];
            word bn = is_fixnum(w) ? bignum_from_int64(word_to_fixnum(w)) : w;
            acc = bignum_add(acc, bn);
        }
        return bignum_to_fixnum_or_box(acc);
    }

    // fixnum 快速路径: int64 累加 + 溢出检测
    int64_t sum = 0;
    for (int i = 0; i < nargs; i++) {
        if (fixnum_add_overflows(sum, word_to_fixnum(vm->sp[i]))) {
            // 溢出: 提升到 bignum 路径
            word acc = fixnum_to_bignum(sum);
            for (; i < nargs; i++) {
                word w = vm->sp[i];
                acc = bignum_add(acc, bignum_from_int64(word_to_fixnum(w)));
            }
            return bignum_to_fixnum_or_box(acc);
        }
        sum += word_to_fixnum(vm->sp[i]);
    }
    return word_from_fixnum(sum);
}
```

`bignum_to_fixnum_or_box` 辅助函数：若 bignum 的值在 fixnum 范围内则返回 fixnum，否则返回装箱的 bignum。这样可以减少堆分配。

除法（`prim_div`）特殊处理：
- 若结果为整数（整除）：以原类型返回（fixnum 或 bignum）
- 若结果非整数：返回 flonum（`(double)a / (double)b`）
- **已知分歧：** R7RS 要求 `(/ 5 2)` 返回精确理数 5/2，但理数在 Scheme 层实现。当前返回 flonum（非精确），当 Scheme 理数库加载后可通过包装器覆盖 `prim_div`。

### 2.2 Build 系统

修改 `src/meson.build`：
- 移除 `primitives/arith.c`
- 添加 `primitives/number.c`

### 2.3 已注册的原始过程

在 `builtins.c` 的 `prim_table` 中新增：

```c
{"number?",    prim_number_pred},
{"integer?",   prim_integer_pred},
{"exact?",     prim_exact_pred},
{"inexact?",   prim_inexact_pred},
{"zero?",      prim_zerop},
{"positive?",  prim_positivep},
{"negative?",  prim_negativep},
{"even?",      prim_evenp},
{"odd?",       prim_oddp},
{"modulo",     prim_modulo},
{"gcd",        prim_gcd},
{"lcm",        prim_lcm},
{"abs",        prim_abs},
{"max",        prim_max},
{"min",        prim_min},
{"floor",      prim_floor},
{"ceiling",    prim_ceiling},
{"truncate",   prim_truncate},
{"round",      prim_round},
{"number->string", prim_number_to_string},
{"string->number", prim_string_to_number},
{"exact->inexact", prim_exact_to_inexact},
{"inexact->exact", prim_inexact_to_exact},
{"finite?",    prim_finitep},
{"infinite?",  prim_infinitep},
{"nan?",       prim_nanp},
{"sin",        prim_sin},
{"cos",        prim_cos},
{"tan",        prim_tan},
{"asin",       prim_asin},
{"acos",       prim_acos},
{"atan",       prim_atan},
{"sqrt",       prim_sqrt},
{"exp",        prim_exp},
{"log",        prim_log},
```

现有 entries (+, -, *, /, <, >, =, quotient, remainder) 不变——它们将直接使用 number.c 中的新实现。

---

## 3. Bignum 算法

所有算法实现遵循 Knuth TAOCP Vol 2 的描述。

### 3.1 加法（`bignum_add`）

```
输入: A (sign sA, limbs a[0..m-1]), B (sign sB, limbs b[0..n-1])
输出: C = A + B

若 sA == sB:
    C ← add_limbs(A, B)    // 无符号 limb 加法 + 进位传播
    C.sign ← sA
否则:
    cmp ← compare_abs(A, B)
    若 cmp >= 0:
        C ← sub_limbs(A, B)  // 无符号 limb 减法 + 借位传播
        C.sign ← sA
    否则:
        C ← sub_limbs(B, A)
        C.sign ← sB
归一化 C (移除前导零 limb)
```

`add_limbs`: 对每对 limb 做 `uint64_t sum = (uint64_t)a[i] + b[i] + carry; result[i] = (uint32_t)sum; carry = sum >> 32;`

### 3.2 减法（`bignum_sub`）

类似加法，但用借位代替进位处理。

### 3.3 乘法（`bignum_mul`）

教科书式 O(n²) 算法：

```
输入: A (limbs a[0..m-1]), B (limbs b[0..n-1])
输出: C = A × B（产物 m+n limbs）

result[i+j] += a[i] * b[j]  // 作为 uint64_t，累加低 32 位，进位推到高位
```

### 3.4 除法（`bignum_divmod`）

Knuth 算法 D（试商法）：

```
输入: A (limbs a[0..m-1]), B (limbs b[0..n-1]), m >= n, n >= 1, b[n-1] >= 2^31
输出: 商 Q (limbs q[0..m-n]), 余数 R (limbs r[0..n-1])

// 归一化：乘以 d = 2^32 / (b[n-1] + 1) 的系数
// 主循环 for j = m-n downto 0:
//   试商 q̂ = (a[j+n]*B + a[j+n-1]) / b[n-1]
//   如果 q̂ >= B: q̂ = B-1
//   减 q̂ * b: 如果结果为负则修正 (q̂--, 加回 b)
//   q[j] = q̂
// 反归一化余数
```

### 3.5 GCD（`bignum_gcd`）

二进制 GCD 算法（Stein's）：

```
输入: 非负 A, B
输出: gcd(A, B)

若 A == 0: return B
若 B == 0: return A

shift ← 0
while ((A | B) & 1) == 0:
    A >>= 1; B >>= 1; shift++

while (A & 1) == 0: A >>= 1
while (B & 1) == 0: B >>= 1

while A != B:
    if A > B: swap(A, B)
    B ← B - A
    while (B & 1) == 0: B >>= 1

return A << shift
```

### 3.6 字符串转换

`bignum_from_string`（十进制）：
```
result ← 0
for each digit d in string:
    result ← result * 10 + d
return result
```

`bignum_to_string`（十进制）：
```
反复除以 10，收集余数，反转后得到小端序十进制字符串
```

---

## 4. Flonum 算法

所有 flonum 算术映射到 C 的 `double` 运算。

### 4.1 核心转换

```c
static word word_from_double(double d) {
    word* obj = vm->gc->alloc_words(3);  // 3 words: GC_hdr + type + value
    obj_set_type(obj, OBJ_TYPE_FLONUM);
    uint64_t bits;
    memcpy(&bits, &d, sizeof(bits));
    obj[DATA_START_INDEX] = bits;
    return ptr_to_word(obj);
}

static double word_to_double(word w) {
    word* hdr = ptr_from_word(w);
    uint64_t bits = hdr[DATA_START_INDEX];
    double d;
    memcpy(&d, &bits, sizeof(d));
    return d;
}
```

### 4.2 传递函数

```c
// 检测
bool flonum_finitep(double d) { return isfinite(d); }
bool flonum_infinitep(double d) { return isinf(d); }
bool flonum_nanp(double d) { return isnan(d); }

// 超验的
double flonum_sin(double x) { return sin(x); }
double flonum_cos(double x) { return cos(x); }
// ... 映射到 libm

// 字符串转换
double flonum_from_string(const char* s) { return strtod(s, NULL); }
// flonum_to_string: 使用 sprintf %.17g（保证往返精度）
```

---

## 5. 溢出检测

Fixnum 操作需要在每条指令后检查是否超出 62-bit 范围：

```c
#define INT62_MAX 0x1FFFFFFFFFFFFFFFLL  // 2^61 - 1
#define INT62_MIN (-INT62_MAX - 1)      // -2^61

static inline bool fixnum_add_overflows(int64_t a, int64_t b) {
    return (b > 0 && a > INT62_MAX - b) ||
           (b < 0 && a < INT62_MIN - b);
}

static inline bool fixnum_mul_overflows(int64_t a, int64_t b) {
    if (a == 0 || b == 0) return false;
    return (a > INT62_MAX / b) || (a < INT62_MIN / b);
}

static inline bool fixnum_negate_overflows(int64_t a) {
    return a == INT62_MIN;
}
```

---

## 6. 测试策略

### 6.1 C 测试（`tests/c/test_number.c`）

```
test_bignum_basic:     创建零 bignum，确认 limb_count == 0 等
test_bignum_add:       各种大小 bignum 加法
test_bignum_sub:       借位和符号测试
test_bignum_mul:       小 × 小、大 × 大、零乘
test_bignum_div:       精确除法、有余数除法、除数为零
test_bignum_cmp:       =, <, >, <=, >=
test_bignum_convert:   bignum↔fixnum、字符串转换往返
test_bignum_gcd:       gcd(12,8)=4, gcd(0,5)=5, etc
test_fixnum_overflow:  fixnum + fixnum → bignum 提升
test_fixnum_no_overflow:  fixnum max + 0 = 正常 fixnum
test_flonum_basic:     flonum 创建、比较
test_flonum_arith:     add/sub/mul/div
test_flonum_transcend: sin/cos/sqrt 等
test_number_predicates:  number?, integer?, exact? 等
test_mixed_arith:      fixnum + bignum, fixnum + flonum, bignum + flonum
```

### 6.2 Scheme 测试（`tests/scheme/test-number.ss`）

```
test_fixnum_arith:      (+ 1 2) = 3, etc
test_bignum_arith:      (+ 9223372036854775807 1) → bignum
test_large_mul:         (* 123456789 987654321)
test_gcd:               (gcd 12345678901234567890 987654321)
test_predicates:        (number? 42) #t, (integer? 3.0) ??? 等
test_overflow_chain:    (+ (- 9223372036854775807 1) 2 3) → bignum chain
test_flonum:            (+ 1.0 2.0) = 3.0, (sin 0) = 0.0
test_mixed:             (+ 1 2.0) = 3.0
test_string_convert:    (number->string 12345) = "12345"
                        (string->number "12345") = 12345
test_rational:          (rational? (make-rational 1 2)) #t  (在 Scheme 层)
test_complex:           (real-part (make-rectangular 3 4)) = 3 (在 Scheme 层)
```

---

## 7. 实施顺序

1. **Bignum 核心**（加法、减法、比较、规范化、int64 转换）—— 使 bignum 可以在内部创建和操作
2. **乘法**和**除法**（Knuth 算法 D）—— 完整的 bignum 算术功能
3. **Fixnum 溢出检测**—— 连接 fixnum 快路径到 bignum 慢路径
4. **类型分派包装器** —— 使 +、-、*、/、<、>、= 处理混合类型
5. **Bignum 谓词和转换** —— number?、integer?、数字->字符串、字符串->数字
6. **Flonum 核心并注册** —— 双精度运算和超验函数
7. **Scheme 理数/复数** —— 在 Scheme 层实现理数和复数

---

## 8. 超出范围

- **定点误差：** 没有 bigfloat、decimal 或精确实数（除理数外）的支持
- **数值输入/输出：** radix 支持仅限于十进制（string->number 的未来版本可以扩展）
- **Kawa 风格的广义复数：** 仅实现标准的自反同复数类型

---

## 9. 文件更改摘要

| 文件 | 操作 | 描述 |
|------|------|------|
| `src/primitives/arith.c` | **删除** | 由 number.c 替换 |
| `src/primitives/number.c` | **创建** | bignum + flonum 核心 + 所有数值原始过程（约 1200 行） |
| `src/vm/builtins.c` | **修改** | ~35 个新的原始过程条目 |
| `src/include/types.h` | 无变化 | 类型标签已存在 |
| `src/meson.build` | **修改** | arith.c → number.c |
