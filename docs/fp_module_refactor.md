# ptxsim FP 模块重构架构规则

**状态：** 重构施工规范
**适用范围：** `submod/fp`
**目标读者：** 负责分析、设计、实现或审查 FP 模块的 Coding Agent
**规范级别：**

* **MUST**：强制要求，违反即视为重构不合格。
* **SHOULD**：默认必须遵守；偏离时必须给出明确理由。
* **MAY**：可选实现方式。
* 对本规范的结构性偏离，MUST 通过 ADR 记录。

---

## 1. 重构目标

FP 模块应成为一个：

1. **位级确定性**的浮点语义执行模块；
2. 与宿主 CPU 浮点环境无关；
3. 与 SoftFloat 实现细节解耦；
4. 可扩展至 `Fp16`、`Bf16`、`Fp32`、`Fp64`、TF32、FP8 等格式；
5. 能准确表达不同格式、不同操作之间不对称的能力集合；
6. 能为 PTX executor 提供稳定、清晰且不可误用的 API；
7. 能在多线程模拟环境下提供明确的线程安全保证。

FP 模块的职责是：

```text
输入位模式
+ 操作类型
+ 舍入/非规格化数控制
        ↓
执行指定浮点语义
        ↓
输出位模式
+ 异常标志
```

FP 模块不负责：

* PTX 文本解析；
* opcode 解码；
* resolved IR 或 exec IR 的类型解析；
* 寄存器读写；
* warp/lane 调度；
* packed vector 指令的 lane 调度；
* trace 输出；
* CUDA intrinsic 调用；
* 宿主硬件性能模拟。

---

# 2. 总体分层

目标架构必须至少包含以下逻辑层：

```text
┌──────────────────────────────────────┐
│         Public FP API                │
│ Environment / Types / Result         │
└──────────────────┬───────────────────┘
                   │
┌──────────────────▼───────────────────┐
│       Operation Policy Layer         │
│ 能力检查 / 控制合法性 / PTX 语义约束  │
└──────────────────┬───────────────────┘
                   │
┌──────────────────▼───────────────────┐
│      Arithmetic Backend Layer        │
│ SoftFloatBackend / Bf16Backend / ... │
└──────────────────┬───────────────────┘
                   │
┌──────────────────▼───────────────────┐
│       Format Representation          │
│ Types / FormatTraits / Classification│
└──────────────────────────────────────┘
```

依赖方向 MUST 始终自上而下。

以下依赖均禁止：

```text
backend → Environment
backend → exec_ir
backend → PTX frontend
types → SoftFloat
validation → Environment implementation details
fp → scheduler / memory / trace
```

---

# 3. 浮点值类型规则

## 3.1 使用强类型表示浮点格式

每一种语义不同的浮点格式 MUST 使用独立类型：

```cpp
struct Fp16 {
  std::uint16_t bits{};
};

struct Bf16 {
  std::uint16_t bits{};
};

struct Fp32 {
  std::uint32_t bits{};
};

struct Fp64 {
  std::uint64_t bits{};
};
```

即使两个格式具有相同位宽，也禁止合并为同一个类型。

禁止：

```cpp
using Bf16 = Fp16;
```

禁止仅以位宽区分格式：

```cpp
Float<16>
```

因为：

```text
Fp16 != Bf16
Bf16 != 任意 FP8 扩展
Fp32 storage != TF32 arithmetic semantics
```

## 3.2 值类型约束

所有标量浮点值类型 MUST：

* trivially copyable；
* standard layout；
* 不拥有资源；
* 不进行动态分配；
* 默认构造表示正零；
* equality 表示位级相等；
* 不提供到宿主 `float` 或 `double` 的隐式转换；
* 不定义 `operator+`、`operator*` 等算术运算符。

禁止：

```cpp
operator float() const;
```

禁止：

```cpp
friend Bf16 operator+(Bf16, Bf16);
```

原因是算术操作必须显式携带舍入模式、非规格化数策略和异常结果。

## 3.3 位模式是唯一基础表示

执行路径中的浮点值 MUST 始终以整数位模式保存。

宿主 `float`、`double`、`long double` 只允许出现在：

* validation；
* 日志格式化；
* 测试诊断信息。

它们禁止参与生产执行语义。

---

# 4. FormatTraits 规则

## 4.1 格式元数据必须集中定义

每个格式 MUST 提供唯一的格式描述 specialization：

```cpp
template <typename T>
struct FormatTraits;
```

示例：

```cpp
template <>
struct FormatTraits<Bf16> {
  using Bits = std::uint16_t;

  static constexpr unsigned total_bits = 16;
  static constexpr unsigned exponent_bits = 8;
  static constexpr unsigned fraction_bits = 7;
  static constexpr int exponent_bias = 127;

  static constexpr Bits sign_mask = 0x8000u;
  static constexpr Bits exponent_mask = 0x7F80u;
  static constexpr Bits fraction_mask = 0x007Fu;
  static constexpr Bits quiet_nan_bit = 0x0040u;

  static constexpr bool has_subnormal = true;
  static constexpr bool has_infinity = true;
  static constexpr bool has_signaling_nan = true;
};
```

格式相关常量禁止散落在：

* `environment.cpp`；
* validation；
* backend；
* 测试工具；
* conversion 实现。

## 4.2 分类函数必须泛化

以下函数 MUST 基于 `FormatTraits<T>` 统一实现：

```cpp
classify(T)
is_negative(T)
is_zero(T)
is_negative_zero(T)
is_subnormal(T)
is_nan(T)
is_quiet_nan(T)
is_signaling_nan(T)
is_infinity(T)
```

禁止为每个格式复制完整分类函数：

```cpp
classify(Fp16)
classify(Fp32)
classify(Fp64)
classify(Bf16)
```

可以保留薄 overload，但 overload MUST 委托给同一个模板实现。

## 4.3 不得假设所有格式能力一致

Traits SHOULD 能表达：

```cpp
has_subnormal
has_infinity
has_quiet_nan
has_signaling_nan
finite_only
```

后续接入 FP8 时，不得默认所有格式都具有与 IEEE binary32 完全相同的特殊值编码。

---

# 5. Public API 规则

## 5.1 Environment 是唯一公共执行入口

生产代码执行浮点操作时，MUST 通过公共 façade，例如：

```cpp
class Environment;
```

调用方不得直接调用：

```cpp
f32_add
f64_mulAdd
Bf16Backend::add
round_pack_bf16
softfloat_roundingMode
```

## 5.2 公共 API 不暴露 backend

公共头文件中禁止出现：

```cpp
float16_t
float32_t
float64_t
softfloat_*
ArithmeticBackend<T>
SoftFloatBackend<T>
```

SoftFloat 是实现依赖，不是 API 类型系统的一部分。

## 5.3 公共算术 API 按能力提供

公共 API SHOULD 保持类型明确的 overload 风格：

```cpp
Result<Fp32> add(Fp32, Fp32, ArithmeticControl = {}) const;
Result<Fp64> add(Fp64, Fp64, ArithmeticControl = {}) const;
Result<Bf16> add(Bf16, Bf16, ArithmeticControl = {}) const;
```

不要求所有浮点类型具有完全相同的操作集合。

例如某格式不支持 `div`，则该类型不应存在相应 overload：

```cpp
// 不应仅为了接口对称而添加
Result<Bf16> div(Bf16, Bf16, ...) const;
```

## 5.4 不支持的操作必须不可误用

优先级如下：

1. 不支持的操作：通过缺少 overload 在编译期阻止；
2. 不支持的运行时控制组合：抛出明确异常；
3. 禁止静默退化到其他舍入模式或其他格式。

禁止：

```cpp
if (rounding_not_supported)
  rounding = RoundingMode::NearestEven;
```

禁止以近似实现冒充精确操作。

## 5.5 FP API 不依赖 IR

FP API 参数中禁止出现：

```cpp
ptx_frontend::Instruction
resolved_ir::Type
exec_ir::Opcode
std::string modifier
```

executor 应负责将 IR 信息转换为 FP 模块定义的强类型控制参数。

---

# 6. 控制参数规则

## 6.1 舍入模式必须显式建模

舍入模式使用强类型枚举：

```cpp
enum class RoundingMode {
  NearestEven,
  TowardZero,
  TowardNegative,
  TowardPositive,
};
```

不得使用整数、字符串或 SoftFloat 常量作为公共 API。

## 6.2 非规格化数策略不得使用含义模糊的布尔值

目标设计 SHOULD 将：

```cpp
bool flush_subnormal;
```

替换为语义明确的类型：

```cpp
enum class SubnormalMode {
  Preserve,
  FlushToSignedZero,
};
```

若某 PTX 操作需要分别控制输入和结果，则应进一步拆分：

```cpp
struct SubnormalControl {
  SubnormalMode input;
  SubnormalMode output;
};
```

不得让 Agent 自行猜测 `flush_subnormal` 究竟表示：

* 只 flush 输入；
* 只 flush 输出；
* 输入输出均 flush；
* 是否保留符号；
* 是否影响异常标志。

## 6.3 算术和转换控制应区分

算术与转换的合法 rounding 集合可能不同。

SHOULD 使用独立控制结构：

```cpp
struct ArithmeticControl {
  RoundingMode rounding = RoundingMode::NearestEven;
  SubnormalMode subnormal = SubnormalMode::Preserve;
};

struct ConversionControl {
  RoundingMode rounding = RoundingMode::NearestEven;
};
```

## 6.4 控制合法性必须集中验证

每种格式、操作和控制组合的合法性 MUST 由统一 policy 检查。

禁止在多个函数内复制：

```cpp
if (control.flush_subnormal) {
  throw ...;
}
```

建议结构：

```cpp
validate_control<Operation::Add, Bf16>(control);
validate_control<Operation::Fma, Fp32>(control);
```

---

# 7. Operation Capability 规则

模块 MUST 有一个单一事实来源，用于描述：

```text
某格式支持哪些操作
某操作支持哪些舍入模式
某操作是否支持 FTZ
某操作输出何种格式
```

建议：

```cpp
enum class Operation {
  Add,
  Sub,
  Mul,
  Fma,
  Div,
  Sqrt,
  Convert,
};
```

并提供内部 capability traits：

```cpp
template <typename T, Operation Op>
struct OperationTraits;
```

对于 mixed-precision 操作，不得仅按输入类型推断输出类型。

例如：

```text
Bf16 × Bf16 + Fp32 → Fp32
```

必须作为独立签名表达，不能与：

```text
Bf16 × Bf16 + Bf16 → Bf16
```

共用含义模糊的 backend API。

---

# 8. ArithmeticBackend 规则

## 8.1 backend 表示“如何计算”

每一种执行策略 MUST 由内部 backend 承担：

```cpp
template <typename T>
struct ArithmeticBackend;
```

推荐关系：

```text
Fp16 ─┐
Fp32 ─┼── SoftFloatBackend
Fp64 ─┘

Bf16 ──── Bf16Backend
```

`FormatTraits<T>` 回答：

> 该格式是什么？

`ArithmeticBackend<T>` 回答：

> 该格式如何执行？

二者不得合并。

## 8.2 Environment 只能负责 façade 和 policy

重构完成后，`Environment` 的实现 SHOULD 类似：

```cpp
Result<Bf16> Environment::add(
    Bf16 lhs,
    Bf16 rhs,
    ArithmeticControl control) const {
  validate_control<Operation::Add, Bf16>(control);
  return detail::ArithmeticBackend<Bf16>::add(lhs, rhs, control);
}
```

`Environment` 中不应继续堆积：

* SoftFloat 类型转换；
* BF16 解包；
* guard/round/sticky；
* NaN 传播实现；
* exponent 对齐；
* bit mask 常量。

## 8.3 backend 必须确定性执行

backend 结果 MUST：

* 与宿主 CPU 架构无关；
* 与编译器 fast-math 设置无关；
* 与宿主当前 rounding mode 无关；
* 在相同输入和控制下给出完全相同的位结果；
* 不依赖未初始化状态；
* 不依赖调用顺序。

## 8.4 backend 禁止动态多态

FP 热路径禁止：

```cpp
virtual
std::function
dynamic_cast
heap allocation
```

优先使用：

* 模板 specialization；
* 普通函数；
* compile-time dispatch；
* 显式 overload。

---

# 9. SoftFloat 接入规则

## 9.1 SoftFloat 只允许存在于 detail 层

`softfloat.h` 只能由以下位置包含：

* `src/detail/softfloat_*`；
* 独立 reference test adapter。

公共头文件及一般业务代码禁止包含 SoftFloat。

## 9.2 SoftFloat 是实现工具，不是语义标准

PTX ISA 是语义事实来源。

SoftFloat 只能作为实现手段。

当 SoftFloat 默认行为与目标 PTX 语义不一致时，MUST 在 wrapper/backend 层修正，包括但不限于：

* NaN canonicalization；
* signaling NaN；
* FTZ；
* rounding mode；
* tininess detection；
* 异常标志；
* mixed precision；
* unsupported operation。

不得以“SoftFloat 就是这样返回的”为理由改变 PTX 语义。

## 9.3 SoftFloat 状态必须调用隔离

以下状态不得泄露到调用外部：

```cpp
softfloat_roundingMode
softfloat_detectTininess
softfloat_exceptionFlags
```

每次调用 MUST：

1. 保存原状态；
2. 安装本次调用状态；
3. 清除本次异常；
4. 执行操作；
5. 捕获结果异常；
6. 恢复原状态。

## 9.4 必须解决并发安全问题

仅使用 RAII 保存和恢复全局状态，并不能保证并发安全。

重构必须明确采用以下方案之一：

### 首选方案

SoftFloat 状态使用 thread-local storage。

### 备选方案

所有 SoftFloat 操作通过内部锁串行化。

禁止在未提供并发隔离的情况下宣称：

```text
Environment is thread-safe
```

必须增加至少一个并发测试，证明两个线程使用不同 rounding mode 时不会互相污染。

---

# 10. 异常标志规则

## 10.1 Result 保持值与标志同时返回

保留如下基本设计：

```cpp
template <typename T>
struct Result {
  T value;
  ExceptionFlags flags;
};
```

异常标志不得存储在 `Environment` 成员中，也不得依赖上一次操作。

## 10.2 只报告架构可见语义阶段的异常

对于复合实现：

```text
输入扩展
→ 中间计算
→ 最终目标格式舍入
```

只允许合并目标操作语义上应当出现的异常。

禁止让纯实现细节产生的异常泄露。

例如 BF16 通过 F32 中间层实现时：

* BF16→F32 exact expansion 不得产生异常；
* 如果 F32 中间计算理论上应当精确，则不得把中间实现的 `Inexact` 泄露；
* BF16 最终舍入产生的 `Inexact` 必须保留。

## 10.3 异常合并必须集中实现

禁止随意操作裸整数：

```cpp
flags |= 0x02;
```

必须通过类型化接口：

```cpp
flags |= ExceptionFlag::Underflow;
```

或统一 helper。

---

# 11. BF16 专项规则

BF16 是第一个不由 SoftFloat 原生类型直接覆盖的格式，因此它必须作为独立 backend 接入。

## 11.1 BF16 类型和 FP16 完全分离

```cpp
struct Bf16 {
  std::uint16_t bits{};
};
```

BF16 traits：

```text
sign:     1 bit
exponent: 8 bits
fraction: 7 bits
precision: 8 bits
```

不得复用 FP16 masks、bias 或 NaN 位定义。

## 11.2 BF16→F32 扩展必须 exact

BF16 转 F32 应使用位级精确扩展：

```cpp
constexpr Fp32 expand_to_f32(Bf16 value) noexcept {
  return Fp32{
      static_cast<std::uint32_t>(value.bits) << 16
  };
}
```

该过程不得经过宿主 `float`。

## 11.3 F32→BF16 必须有独立舍入实现

必须提供独立的：

```cpp
Result<Bf16> round_f32_to_bf16(
    Fp32 value,
    ConversionControl control);
```

实现必须处理：

* round-to-nearest-even；
* directed rounding；
* exact/inexact；
* overflow；
* underflow；
* normal/subnormal；
* 正负零；
* infinity；
* quiet NaN；
* signaling NaN；
* NaN quieting/canonicalization policy。

禁止只执行：

```cpp
Bf16{static_cast<std::uint16_t>(value.bits >> 16)}
```

除非目标操作明确要求截断。

## 11.4 BF16 add/sub/mul 的中间精度必须经过证明

BF16 的 `add/sub/mul` MAY 使用：

```text
BF16 exact expand
→ SoftFloat F32 operation
→ BF16 final round
```

但实现者 MUST 满足以下至少一项：

1. 给出中间 F32 精度足以得到正确 BF16 单次舍入结果的推导；
2. 使用独立高精度 oracle 完成充分的差分验证；
3. 改用直接 BF16 integer core。

不得仅因为“F32 精度更高”就默认不存在 double rounding。

## 11.5 BF16 FMA 必须 single-round

标量 BF16 FMA 必须满足：

```text
exact a × b + c
→ 一次 BF16 舍入
```

禁止：

```text
f32_mulAdd
→ round to F32
→ round to BF16
```

除非存在完整、可审查的等价性证明。

推荐实现：

```text
unpack BF16
→ exact significand multiply
→ exponent alignment
→ shift-right-jam
→ signed add/sub
→ normalize
→ guard/round/sticky
→ round_pack_bf16
```

FMA core MUST 正确处理极小的加数改变 midpoint 舍入方向的情况。

## 11.6 packed BF16 不属于标量 backend

`.bf16x2` SHOULD 由 packed/lane adapter 处理：

```text
unpack low lane
unpack high lane
分别调用标量 Bf16 backend
重新 pack
```

不得把 `Bf16x2` 当成一种新的浮点格式加入 `FormatTraits`。

## 11.7 mixed precision 必须显式建模

例如：

```cpp
Result<Fp32> fma(
    Bf16 a,
    Bf16 b,
    Fp32 c,
    ArithmeticControl control) const;
```

其结果格式和舍入点必须在签名中明确。

不得通过模板自动猜测 mixed operation 的 accumulator 类型。

---

# 12. NaN 规则

NaN 行为必须集中形成明确 policy，至少说明：

* signaling NaN 是否触发 `Invalid`；
* signaling NaN 如何 quiet；
* payload 是否保留；
* 多个 NaN 输入时选择哪个 payload；
* 是否生成 canonical NaN；
* NaN sign 是否保留；
* 不同格式转换时 payload 如何截断。

禁止让以下因素隐式决定 NaN 行为：

* 宿主 CPU；
* 编译器；
* SoftFloat build specialization；
* 随机操作数顺序；
* 未定义行为。

如果当前 PTX ISA 对某项行为未做位级保证，validation MUST 区分：

```text
bit-exact NaN
same NaN class
any quiet NaN
```

---

# 13. Validation 规则

## 13.1 validation 和 execution 必须分离

生产 backend 禁止依赖：

```cpp
validation::within_ulp
validation::within_relative
validation::within_absolute
```

validation 可以依赖类型和分类函数，但不得成为执行链路的一部分。

## 13.2 bit-exact 是主要正确性标准

对于确定性模拟器，默认验证规则应为：

```cpp
bit_exact(expected, actual)
```

ULP、absolute、relative comparison 只用于：

* 与非精确外部实现比较；
* 诊断；
* 性能模型近似路径；
* 明确允许容差的测试。

不得使用容差测试掩盖本应 bit-exact 的 backend 错误。

## 13.3 通用 validation 必须模板化

以下逻辑 SHOULD 基于 `FormatTraits<T>` 统一：

```cpp
bit_exact
same_float_class
ordered_bits
ulp_distance
within_ulp
```

不得继续为每个格式复制完整实现。

## 13.4 宿主浮点只允许用于诊断型比较

`within_relative` 和 `within_absolute` 可以将值转换为宿主浮点，但必须有注释：

```cpp
// Validation only. Never use for execution semantics.
```

---

# 14. 文件和依赖组织

推荐目标结构：

```text
submod/fp/
├── include/
│   ├── types.hpp
│   ├── controls.hpp
│   ├── exceptions.hpp
│   ├── environment.hpp
│   ├── validation.hpp
│   └── detail/
│       └── format_traits.hpp
│
├── src/
│   ├── environment.cpp
│   ├── validation.cpp
│   └── detail/
│       ├── operation_policy.hpp
│       ├── softfloat_context.hpp
│       ├── softfloat_context.cpp
│       ├── softfloat_backend.hpp
│       ├── softfloat_backend.cpp
│       ├── bf16_rounding.hpp
│       ├── bf16_rounding.cpp
│       ├── bf16_backend.hpp
│       └── bf16_backend.cpp
│
└── test/
    ├── test_types.cpp
    ├── test_f32.cpp
    ├── test_f64.cpp
    ├── test_bf16.cpp
    ├── test_conversions.cpp
    ├── test_validation.cpp
    └── test_thread_safety.cpp
```

具体文件名可以调整，但以下边界 MUST 保持：

* SoftFloat wrapper 独立；
* BF16 rounding 独立；
* BF16 arithmetic backend 独立；
* Environment 只做 façade；
* validation 与 execution 分离；
* 测试不得全部堆在单一大文件中。

---

# 15. 测试规则

## 15.1 重构前先建立 characterization tests

在改变结构之前，Agent MUST 先固定现有 F32/F64 行为，包括：

* 基本算术；
* 四种 rounding mode；
* exception flags；
* FTZ；
* signed zero；
* NaN；
* conversion；
* SoftFloat 状态恢复。

结构提取阶段不得顺便改变现有语义。

## 15.2 每种格式的最低测试集合

每个格式至少覆盖：

* `+0`、`-0`；
* 最小/最大 subnormal；
* 最小 normal；
* 普通 normal；
* 最大 finite；
* `+∞`、`-∞`；
* quiet NaN；
* signaling NaN；
* halfway rounding；
* overflow boundary；
* underflow boundary；
* exact result；
* inexact result。

## 15.3 BF16 专项测试

BF16 必须额外覆盖：

* 所有 65,536 个 BF16 位模式的 classification；
* BF16→F32→BF16 round-trip；
* NaN round-trip policy；
* tie-to-even；
* 正负 directed rounding；
* subnormal conversion；
* BF16 FMA double-rounding 反例；
* 极小加数改变 FMA midpoint 方向；
* overflow 到 infinity/最大 finite；
* signed zero cancellation；
* signaling NaN invalid flag。

## 15.4 reference 必须独立

生产实现与 reference oracle 不得调用同一个核心函数。

禁止：

```text
production: Bf16Backend::fma
reference:  Bf16Backend::fma
```

可接受的 reference：

* Boost.Multiprecision 整数/有理数；
* MPFR 测试依赖；
* 离线生成的 golden corpus；
* 独立 Python 高精度生成器；
* 与生产实现不同的直接数学模型。

## 15.5 随机测试必须可复现

随机测试 MUST：

* 使用固定 seed；
* 输出失败样本的原始位模式；
* 输出 rounding mode；
* 输出 expected/actual bits；
* 可以单独重放失败 case。

## 15.6 并发测试是合并条件

若 `Environment` 被声明为线程安全，测试必须验证：

```text
thread A: TowardPositive
thread B: TowardNegative
```

并发执行时结果和 flags 互不污染。

---

# 16. 性能规则

正确性优先于微优化。

Agent 在没有 benchmark 和 bit-exact 回归测试时，不得：

* 合并舍入阶段；
* 删除 sticky bit；
* 使用宿主 FMA 替代 software implementation；
* 使用 `-ffast-math`；
* 用普通右移替代 shift-right-jam；
* 缓存跨调用 exception state；
* 将 exact conversion 改为宿主 cast。

FP 热路径 SHOULD：

* 无动态分配；
* 无虚函数；
* 无字符串处理；
* 无异常路径上的正常控制流；
* 使用 trivially-copyable 小对象；
* 对无效 control 提前失败。

---

# 17. 禁止事项

施工 Agent 不得实施以下设计：

```text
1. 用 host float/double 作为执行真值
2. 用 std::fenv 频繁切换宿主舍入模式
3. 把 BF16 当作截断版 F32 而忽略舍入
4. 用 f32_mulAdd + BF16 round 实现 BF16 fused FMA
5. 让 SoftFloat 类型泄露到 public API
6. 为接口对称而添加 ISA 不支持的操作
7. 把 Bf16x2 当作标量格式
8. 让 validation 参与 production execution
9. 在 Environment 内继续复制每个格式的完整实现
10. 使用一个 Float<Bits> 模板表示所有同位宽格式
11. 静默忽略不支持的 rounding/FTZ
12. 在同一次重构中修改 exec_ir、scheduler、memory 或 trace
13. 未添加回归测试就删除现有行为
14. 声称线程安全但继续使用未隔离的 SoftFloat 全局状态
```

---

# 18. 推荐施工顺序

Agent MUST 按可回滚的小步骤实施。

## 阶段 1：固定现状

* 补齐当前 F32/F64 characterization tests；
* 补齐异常、FTZ、NaN 和状态恢复测试；
* 不改变公共 API。

## 阶段 2：提取格式 traits

* 引入 `FormatTraits<T>`；
* 泛化 classify 和 predicates；
* 确保行为完全不变。

## 阶段 3：隔离 SoftFloat 状态

* 提取 `SoftFloatContext`；
* 统一 rounding/flags 映射；
* 明确线程安全策略；
* 添加并发测试。

## 阶段 4：提取 arithmetic backend

* 将 F32/F64 操作迁入 `SoftFloatBackend`；
* `Environment` 改为委托；
* 不改变现有 API 和结果。

## 阶段 5：整理 operation policy

* 集中定义操作能力；
* 集中验证 rounding/FTZ；
* 删除散落的 `require_*_control`。

## 阶段 6：接入 Bf16 representation 和 conversion

* 增加 `Bf16`；
* 增加 traits；
* 实现 exact BF16→F32；
* 实现正确的 F32→BF16 rounding；
* 完成 conversion tests。

## 阶段 7：接入 BF16 arithmetic

* add/sub/mul；
* exact fused FMA；
* NaN 和 exception policy；
* independent golden corpus。

## 阶段 8：清理和文档化

* 拆分测试文件；
* 删除重复实现；
* 补充 module README；
* 为关键决定添加 ADR。

每个阶段结束时 MUST：

```text
configure 成功
build 成功
全部已有测试通过
新增测试通过
无公共行为的意外变化
```

---

# 19. Agent 输出要求

负责施工的 Agent 在提交结果时，必须同时给出：

1. 改动文件列表；
2. 新增的架构边界；
3. 保留的公共 API；
4. 发生变化的公共 API；
5. 新增支持的格式和操作；
6. 未支持的格式和操作；
7. rounding/FTZ/NaN policy；
8. SoftFloat 线程安全策略；
9. 测试覆盖说明；
10. 已知限制；
11. 后续建议，但不得把未完成项描述成已完成。

---

# 20. 合并验收清单

* [ ] 公共头文件不包含 `softfloat.h`
* [ ] `Environment` 不直接实现具体格式算术
* [ ] F32/F64 现有 API 保持源代码兼容
* [ ] 分类逻辑由统一 `FormatTraits<T>` 驱动
* [ ] 没有新增每格式复制的 classify/is_nan/is_zero
* [ ] operation capability 有单一事实来源
* [ ] 不支持的操作没有伪实现
* [ ] 不支持的控制不会静默降级
* [ ] BF16 与 FP16 使用不同强类型
* [ ] BF16→F32 是位级 exact expansion
* [ ] F32→BF16 实现完整 rounding 和特殊值处理
* [ ] BF16 FMA 只发生一次目标格式舍入
* [ ] internal widening 不泄露非架构异常标志
* [ ] NaN policy 被集中定义并测试
* [ ] production execution 不使用宿主浮点
* [ ] validation 不参与执行语义
* [ ] SoftFloat 状态在调用间完全恢复
* [ ] SoftFloat 并发状态已隔离或串行化
* [ ] 随机测试使用固定 seed
* [ ] BF16 reference oracle 与生产实现独立
* [ ] signed zero、subnormal、NaN、overflow、underflow 均有测试
* [ ] BF16 FMA double-rounding 反例已加入回归测试
* [ ] packed BF16 与标量 backend 分层
* [ ] 重构未侵入 exec_ir、scheduler、memory 和 trace
* [ ] 所有结构性偏离均有 ADR
* [ ] 完整构建和测试通过

---

# 21. 最终架构不变量

重构完成后，以下陈述必须始终成立：

```text
浮点格式由 FormatTraits 定义。
浮点算法由 ArithmeticBackend 实现。
操作合法性由 Operation Policy 决定。
Environment 只暴露稳定 façade。
SoftFloat 永远是内部依赖。
PTX ISA 永远是语义事实来源。
执行结果永远由位模式和异常标志组成。
validation 永远不参与生产执行。
每一个架构可见操作都有且只有明确的最终舍入点。
新增格式不要求复制整套模块代码。
```
