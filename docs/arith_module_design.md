# ptxsim `arith` 模块设计规范

> **规范基准：** NVIDIA PTX ISA 9.3  
> **目标模块：** `submod/arith` / `ptxsim::arith`  
> **文档用途：** 指导 `arith` 模块的实现、维护与后续扩展，并约束已移除的旧 `fp` 架构不得回流
> **文档性质：** 软件架构、公共 API、实现边界与验收规范
> **文档沿革：** 本文已取代历史 `docs/fp_module_refactor.md`；后续不得同时维护两份 fp/arith 重构规范

---

## 目录

1. [设计结论](#1-设计结论)
2. [规范术语](#2-规范术语)
3. [建设方式：新建而非兼容性重构](#3-建设方式新建而非兼容性重构)
4. [模块职责边界](#4-模块职责边界)
5. [“与 PTX ISA 一致”的准确含义](#5-与-ptx-isa-一致的准确含义)
6. [总体架构](#6-总体架构)
7. [公共类型系统](#7-公共类型系统)
8. [格式描述与能力分类](#8-格式描述与能力分类)
9. [控制策略设计](#9-控制策略设计)
10. [公共标量 API](#10-公共标量-api)
11. [返回值、状态与错误处理](#11-返回值状态与错误处理)
12. [整数与位运算语义](#12-整数与位运算语义)
13. [浮点标量语义](#13-浮点标量语义)
14. [BF16 设计](#14-bf16-设计)
15. [TF32、FP8、FP6、FP4 与定点格式](#15-tf32fp8fp6fp4-与定点格式)
16. [通用转换架构](#16-通用转换架构)
17. [Packed 类型与逐 lane 运算](#17-packed-类型与逐-lane-运算)
18. [张量数值内核](#18-张量数值内核)
19. [近似数学函数](#19-近似数学函数)
20. [NaN、非规格化数与未完全指定行为](#20-nan非规格化数与未完全指定行为)
21. [Backend 与第三方依赖](#21-backend-与第三方依赖)
22. [线程安全](#22-线程安全)
23. [建议文件结构](#23-建议文件结构)
24. [CMake 与模块依赖](#24-cmake-与模块依赖)
25. [测试策略](#25-测试策略)
26. [实施阶段](#26-实施阶段)
27. [施工 Agent 强制规则](#27-施工-agent-强制规则)
28. [验收清单](#28-验收清单)
29. [最终架构不变量](#29-最终架构不变量)
30. [规范参考](#30-规范参考)

---

# 1. 设计结论

`ptxsim` 应在 `submod` 下新建独立的：

```text
submod/arith
```

最终形成：

```cpp
namespace ptxsim::arith {
// numeric types
// controls
// scalar arithmetic
// conversions
// packed arithmetic
// tensor numeric kernels
}
```

该模块的定义是：

> **`ptxsim::arith` 是一个与模拟器机器状态解耦的纯数值语义库。它依据 PTX ISA 9.3 所要求的数值格式、舍入点、饱和规则、特殊值行为和张量累加约束，对强类型输入值进行标量算术、转换或张量数值计算。**

核心边界为：

```text
arith:
    typed values + controls -> numeric result + numeric status

executor:
    machine state + instruction -> machine state
```

本设计明确作出以下决策：

1. `arith` 是全新模块，不以兼容现有 `fp::Environment` 为设计前提。
2. 历史 `submod/fp` 仅曾作为 F32/F64、SoftFloat 状态管理和测试样本的实现参考，不构成当前依赖。
3. 仓库必须持续保持不含 `submod/fp`、`ptxsim::fp` namespace、旧测试和旧 CMake target；不得为兼容旧架构而恢复它们。
4. `arith` 不建立 PTX instruction、opcode、instruction form、modifier sequence、target SM 或 operand grammar 模型。
5. PTX 指令解析、合法性检查及指令到算术原语的映射属于 frontend / lowering / exec IR / executor。
6. `arith` 只提供类型驱动、操作驱动的数值 API。
7. 舍入、FTZ、饱和、激活、近似模式、乘积截取和张量累加规则必须作为独立控制维度建模，不能与每个 operation 组合成大量 form 类型。
8. 增加新格式时，不得新增一整套 `add/sub/mul/fma/cvt` overload。
9. TF32、FP8、FP6、FP4 等 tensor-oriented 格式可以拥有表示、分类、转换、pack/unpack 和张量输入能力，但不得因此伪造 PTX 9.3 不存在的标量算术能力。
10. 张量指令的 fragment、lane、warp、descriptor、TMEM 和异步执行语义不属于 `arith`；逻辑矩阵乘加的数值部分属于 `arith::tensor`。

---

# 2. 规范术语

本文使用以下约束词：

- **MUST**：强制要求，违反即视为设计或实现不合格。
- **MUST NOT**：明确禁止。
- **SHOULD**：默认应遵守；偏离时必须给出可审查理由。
- **MAY**：允许但不强制。

影响以下内容的偏离 MUST 通过 ADR 记录：

- 模块职责边界；
- 公共 API；
- 类型表示；
- 舍入点；
- Tensor 累加模型；
- SoftFloat 隔离方式；
- 与 PTX ISA 9.3 不一致的行为。

---

# 3. 建设方式：新建而非兼容性重构

## 3.1 Greenfield 原则

虽然该工作由现有 `fp` 模块演进而来，但施工方式应视为：

```text
新建 arith
+ 参考 fp 中可复用的算法和测试
+ 替换调用方
+ 删除 fp
```

而不是：

```text
在 fp::Environment 上持续打补丁
+ 保留旧 API
+ 增加 compatibility alias
+ 长期双轨运行
```

`arith` MUST NOT 依赖：

```text
submod/fp
ptxsim::fp
fp::Environment
fp::Fp16/Fp32/Fp64
```

允许施工 Agent 从旧模块提取或重写以下内容：

- SoftFloat rounding mode 映射；
- SoftFloat exception flag 映射；
- SoftFloat 状态保存与恢复思路；
- F32/F64 基本算术测试样本；
- signed zero、FTZ、NaN 和 conversion 测试样本；
- validation 中的 ULP 排序思路。

但复制后必须适配 `arith` 的新架构，不能让旧的 overload façade 或旧 namespace 泄露进新模块。

## 3.2 最终仓库状态

最终合并状态 MUST 满足：

```text
submod/arith/      存在
submod/fp/         不存在
ptxsim::arith      存在
ptxsim::fp         不存在
ptxsim_fp target   不存在
ptxsim_arith target 存在
```

不要求提供长期 deprecated compatibility layer。

如施工期间为分步编译临时建立 adapter，该 adapter：

- 只能存在于开发分支的中间提交；
- MUST NOT 成为最终合并结果；
- MUST NOT 影响 `arith` 的公共 API 设计。

---

# 4. 模块职责边界

## 4.1 `arith` 负责

`arith` 负责所有可以表示为：

```text
若干输入数值
+ 数值控制策略
-> 输出数值
+ 数值状态
```

的纯计算语义。

具体包括：

### 标量整数

- wrap-around add/sub/mul/mad；
- saturating add/sub；
- low/high/wide product；
- division/remainder；
- carry/borrow 数值计算；
- absolute、negate、min/max；
- SAD、dot-product 等纯数值部分；
- count、bit-field、shift、funnel shift 等纯位运算。

### 标量浮点

- F16、F32、F64；
- BF16；
- add/sub/mul/mad/fma/div；
- abs/neg/copysign/min/max；
- sqrt/rcp/rsqrt；
- sin/cos/lg2/ex2/tanh 等 PTX 所需近似数学语义；
- class/test/compare/select。

### 类型转换

- integer ↔ integer；
- integer ↔ floating；
- floating ↔ floating；
- fundamental floating ↔ alternate floating；
- scalar ↔ packed；
- fixed-point 和 scale format 转换；
- stochastic rounding 所需的确定性转换核心。

### Packed 数值

- pack/unpack；
- PTX 9.3 确实要求逐 lane 算术的 packed 类型；
- 通用 lane mapping。

### 张量数值

- logical matrix/tile 的 multiply-accumulate；
- mixed precision product 和 accumulation；
- TF32 输入量化；
- FP8/FP6/FP4 widened product；
- block scale 的数值应用；
- deterministic accumulation profile；
- 稀疏矩阵已经解码后的逻辑数值计算。

### 验证辅助

- bit exact；
- classification；
- ULP distance；
- 独立诊断状态；
- 测试用 host conversion。

## 4.2 `arith` 不负责

以下内容 MUST NOT 进入 `arith`：

```text
PTX parser
PTX opcode enum
PTX instruction class
instruction-form descriptor
modifier sequence parser
PTX syntax validation
target SM availability validation
resolved_ir
exec_ir
register file
predicate guard
CC register storage
memory state/addressing
atomicity/memory ordering
warp/lane scheduling
fragment-to-lane mapping
WGMMA descriptor
TMEM addressing
async group/barrier
trace
```

尤其禁止建立以下公共或内部入口：

```cpp
arith::evaluate(ptx_opcode, ptx_type, ptx_modifiers, operands);

arith::instruction_form<...>;

arith::ptx::fma_rn_f32;
```

这些设计会使 `arith` 重新承担第二套 executor/ISA model，破坏模块划分。

## 4.3 正确调用边界

executor 应进行：

```text
PTX instruction
    ↓ decode/lowering
operation + typed operands + independent controls
    ↓
ptxsim::arith
```

示例映射：

```ptx
fma.rn.ftz.f32 d, a, b, c;
```

由 executor 转换为：

```cpp
auto result = arith::fma<float32_t>(
    ctx,
    a,
    b,
    c,
    floating_control{
        .rounding = rounding_mode::nearest_even,
        .subnormal = subnormal_mode::flush_input_and_output,
    });
```

`arith` 不知道该调用来自 `fma.rn.ftz.f32`，只知道：

```text
执行 float32 fused multiply-add
使用 nearest-even
使用指定 subnormal policy
```

其他边界示例：

```ptx
mul.hi.s32 d, a, b;
```

```cpp
auto result = arith::mul<int32_t>(
    ctx,
    a,
    b,
    product_control{.part = product_part::high});
```

```ptx
cvt.rn.bf16.f32 d, a;
```

```cpp
auto result = arith::cvt<bfloat16_t>(
    ctx,
    a,
    conversion_control{.rounding = rounding_mode::nearest_even});
```

对 MMA/WGMMA/TCGEN05，executor 应先处理 fragment、lane、descriptor、TMEM 和 shape，再调用：

```cpp
auto result = arith::tensor::mma<float32_t>(
    ctx,
    logical_a,
    logical_b,
    logical_c,
    tensor_control);
```

---

# 5. “与 PTX ISA 一致”的准确含义

`arith` 的 public API 与 PTX ISA 9.3 一致，指的是**数值语义一致**，而不是**C++ API 复刻 PTX 指令语法**。

## 5.1 必须一致的方面

1. 数据格式及特殊值编码；
2. 标量操作可用的数据类型范围；
3. mixed-precision 的输入扩展和输出类型；
4. rounding mode；
5. FTZ/subnormal 行为；
6. saturation/relu/satfinite 行为；
7. fused 与 non-fused 的舍入点；
8. NaN、Infinity、signed zero 行为；
9. approximate function 的 corner cases 和误差界；
10. Tensor 乘积精度、累加精度和 block scaling 约束；
11. PTX 明确标记为 unspecified 或 implementation-defined 的范围。

## 5.2 不在 `arith` 中建模的方面

1. mnemonic spelling；
2. modifier 在 PTX 文本中的顺序；
3. instruction operand grammar；
4. destination/source register type checking；
5. target architecture availability；
6. guard predicate；
7. warp collective participation；
8. register fragment layout；
9. instruction side effects。

## 5.3 PTX modifier 到数值控制的映射

executor 负责把 PTX modifier 翻译为独立数值控制。推荐映射：

| PTX modifier/形式 | `arith` 数值表示 |
|---|---|
| `.rn` | `rounding_mode::nearest_even` |
| `.rz` | `rounding_mode::toward_zero` |
| `.rm` | `rounding_mode::toward_negative` |
| `.rp` | `rounding_mode::toward_positive` |
| `.rna` | `rounding_mode::nearest_away` |
| `.rs` | `rounding_mode::stochastic` + 显式 random-bits operand |
| `.ftz` | executor 根据该 operation 的 PTX 定义映射为明确 `subnormal_mode` |
| floating `.sat` | `saturation_mode::zero_to_one` |
| integer `.sat` | `saturation_mode::type_range` 或 `integer_overflow_mode::saturate` |
| `.satfinite` | `saturation_mode::finite` |
| `.relu` | `activation_mode::relu` |
| `.approx` | `approximation_mode::ptx_approximate` |
| `.full` | `approximation_mode::ptx_full` |
| `.lo/.hi/.wide` | `product_part::low/high/wide` |
| `.cc` / carry-in | 使用显式 `add_with_carry`、`sub_with_borrow`、`mad_with_carry` 输入输出 |

`.cc` 不是普通布尔 modifier：它关联可传递的 carry/borrow 数值状态，因此应通过显式输入/输出表达，而不是塞进通用 control bag。

## 5.4 API 映射原则

公共 operation 名称 SHOULD 与 PTX 数值操作名称接近：

```text
add / sub / mul / mad / fma / div / rem
abs / neg / min / max
rcp / sqrt / rsqrt
sin / cos / lg2 / ex2 / tanh
cvt
compare / select
```

但不要求一条 PTX instruction 对应一个独立函数。

推荐映射如下：

| PTX 数值需求 | `arith` 原语 |
|---|---|
| `add` | `arith::add` |
| `add.cc` / `addc` | `arith::add_with_carry` |
| `sub.cc` / `subc` | `arith::sub_with_borrow` |
| `mul.lo/hi/wide` | `arith::mul` + `product_control` |
| `mad.lo/hi/wide` | `arith::mad` + `product_control` |
| `fma` | `arith::fma` |
| `cvt` | `arith::cvt<To>` |
| `set/setp` | `arith::compare` |
| `selp/slct` | `arith::select` |
| packed F16/BF16 arithmetic | 同一 scalar CPO + `packed_t` lane engine |
| MMA/WGMMA/TCGEN05 数值部分 | `arith::tensor::mma` |

---

# 6. 总体架构

```text
┌──────────────────────────────────────────┐
│              Public Types                │
│ basic_float / packed_t / fixed_t / tile  │
└────────────────────┬─────────────────────┘
                     │
┌────────────────────▼─────────────────────┐
│          Independent Controls            │
│ rounding / subnormal / saturation / ...  │
└────────────────────┬─────────────────────┘
                     │
┌────────────────────▼─────────────────────┐
│       Generic Public Operation CPOs      │
│ add / mul / fma / cvt / tensor::mma ...  │
└────────────────────┬─────────────────────┘
                     │
┌────────────────────▼─────────────────────┐
│ Capability + Control Normalization       │
│ operation × arithmetic family × controls │
└────────────────────┬─────────────────────┘
                     │
┌────────────────────▼─────────────────────┐
│           Semantic Kernels               │
│ integer / floating / conversion / tensor │
└────────────────────┬─────────────────────┘
                     │
┌────────────────────▼─────────────────────┐
│                Backends                  │
│ SoftFloat / custom low precision / int   │
└──────────────────────────────────────────┘
```

## 6.1 依赖方向

依赖必须自上而下。

禁止：

```text
backend -> public CPO
backend -> executor
backend -> exec_ir
format traits -> SoftFloat
arith -> fp
arith -> memory/scheduler/trace
```

## 6.2 Operation CPO

公共 operation SHOULD 使用 customization-point object 或等价的单一泛型入口：

```cpp
inline constexpr detail::operation_fn<operation::add> add{};
inline constexpr detail::operation_fn<operation::sub> sub{};
inline constexpr detail::operation_fn<operation::mul> mul{};
inline constexpr detail::operation_fn<operation::fma> fma{};
```

调用形式：

```cpp
auto result = arith::add(ctx, lhs, rhs, control);
```

这允许 operation 名称固定，而具体实现依据：

```text
operation
+ operand/result types
+ arithmetic family
+ controls
```

进行 dispatch。

添加新数据类型 MUST NOT 要求在公共头中新增：

```cpp
add(new_type, new_type);
sub(new_type, new_type);
mul(new_type, new_type);
```

---

# 7. 公共类型系统

## 7.1 命名原则

类型名称向 `<cstdint>`、`<stdfloat>` 风格靠拢，并明确表达格式。

推荐公共类型：

| PTX 格式 | C++ 名称 |
|---|---|
| `.s8` | `ptxsim::arith::int8_t` |
| `.s16` | `ptxsim::arith::int16_t` |
| `.s32` | `ptxsim::arith::int32_t` |
| `.s64` | `ptxsim::arith::int64_t` |
| `.u8` | `ptxsim::arith::uint8_t` |
| `.u16` | `ptxsim::arith::uint16_t` |
| `.u32` | `ptxsim::arith::uint32_t` |
| `.u64` | `ptxsim::arith::uint64_t` |
| `.f16` | `ptxsim::arith::float16_t` |
| `.f32` | `ptxsim::arith::float32_t` |
| `.f64` | `ptxsim::arith::float64_t` |
| `.bf16` | `ptxsim::arith::bfloat16_t` |
| `.tf32` | `ptxsim::arith::tfloat32_t` |
| `.e4m3` | `ptxsim::arith::float8_e4m3_t` |
| `.e5m2` | `ptxsim::arith::float8_e5m2_t` |
| `.e2m3` | `ptxsim::arith::float6_e2m3_t` |
| `.e3m2` | `ptxsim::arith::float6_e3m2_t` |
| `.e2m1` | `ptxsim::arith::float4_e2m1_t` |
| `.ue8m0` | `ptxsim::arith::ufloat8_e8m0_t` |
| `.ue4m3` | `ptxsim::arith::ufloat7_e4m3_t` |
| `.s2f6` | `ptxsim::arith::fixed8_s2f6_t` |
| `.pred` | `ptxsim::arith::predicate_t` |
| `.b8/.b16/.b32/.b64/.b128` | `bits8_t/bits16_t/bits32_t/bits64_t/bits128_t` |

整数别名 MAY 直接映射到 `std::int*_t/std::uint*_t`。

浮点、alternate floating 和 fixed-point 类型 MUST NOT alias 到宿主 `float`、`double` 或编译器扩展 half 类型。

## 7.2 通用 `basic_float`

不得为每个浮点格式复制一个完整 class。

应使用：

```cpp
template <typename Format>
class basic_float;
```

然后定义 alias：

```cpp
using float16_t = basic_float<formats::binary16>;
using float32_t = basic_float<formats::binary32>;
using float64_t = basic_float<formats::binary64>;
using bfloat16_t = basic_float<formats::bfloat16>;
using float8_e4m3_t = basic_float<formats::e4m3>;
```

`basic_float` MUST：

- trivially copyable；
- standard layout；
- 不动态分配；
- 不隐式转换为 host float/double；
- 不定义 `operator+/-/*//`；
- equality 表示存储级相等；
- 通过 `from_bits` 和 `bits` 访问固定编码格式；
- 对 implementation-defined encoding 的格式限制 raw-bit API。

示意：

```cpp
template <typename Format>
class basic_float {
 public:
  using storage_type = typename format_traits<Format>::storage_type;

  static constexpr basic_float from_bits(storage_type bits) noexcept
    requires(format_traits<Format>::fixed_encoding);

  constexpr storage_type bits() const noexcept
    requires(format_traits<Format>::fixed_encoding);

  friend constexpr bool operator==(basic_float, basic_float) = default;

 private:
  storage_type storage_{};
};
```

## 7.3 storage type 与 semantic type 分离

PTX alternate formats 往往存储在 bit-type register 中。`arith` 必须区分：

```text
bits16_t       16-bit raw storage
bfloat16_t     BF16 numeric value
```

禁止：

```cpp
using bfloat16_t = bits16_t;
```

同理：

```text
bits32_t != tfloat32_t
bits8_t  != float8_e4m3_t
```

## 7.4 `tfloat32_t`

PTX 9.3 将 TF32 定义为矩阵乘加支持的特殊 32-bit 格式，范围与 F32 相同、精度降低，且内部布局 implementation-defined。[PTX-5.2.3]

因此 `tfloat32_t` SHOULD 是不暴露固定 raw layout 的 opaque numeric type：

```cpp
class tfloat32_t {
 public:
  [[nodiscard]] float32_t canonical_value() const noexcept;

 private:
  float32_t canonical_{};
};
```

其构造必须通过：

```cpp
auto value = cvt<tfloat32_t>(ctx, f32, control);
```

若确实需要模拟具体 target 的 `.b32` 编码，应通过独立 profile：

```cpp
bits32_t encode(tfloat32_t, const tf32_encoding_profile&);
tfloat32_t decode_tf32(bits32_t, const tf32_encoding_profile&);
```

不得把某个常见 TF32 bit truncation 布局无条件宣称为 PTX ISA 唯一编码。

## 7.5 定点格式

`s2f6` 应使用通用定点模板：

```cpp
template <typename Rep, int FractionBits>
class basic_fixed;

using fixed8_s2f6_t = basic_fixed<std::int8_t, 6>;
```

该类型表示：

```text
value = signed_rep × 2^-6
```

其算术能力应由 capability traits 决定，不因存在类型就自动提供所有 integer/floating operations。[PTX-5.2.4]

---

# 8. 格式描述与能力分类

## 8.1 `format_traits`

每个数值格式必须有一个集中描述：

```cpp
template <typename Format>
struct format_traits;
```

最低信息包括：

```cpp
struct format_info {
  unsigned storage_bits;
  unsigned value_bits;
  unsigned exponent_bits;
  unsigned fraction_bits;
  int exponent_bias;

  bool has_sign;
  bool has_zero;
  bool has_subnormal;
  bool has_infinity;
  bool has_nan;
  bool has_signaling_nan;
  bool fixed_encoding;
};
```

格式相关 masks、bias、quiet-NaN 位和特殊值编码 MUST 集中定义，禁止散落在 backend、conversion 和 test 中。

## 8.2 Arithmetic family

每个格式应归入少量 arithmetic family：

```cpp
enum class arithmetic_family {
  signed_integer,
  unsigned_integer,
  ieee_binary,
  bfloat,
  finite_low_precision,
  tensor_quantized,
  fixed_point,
  raw_bits,
  predicate,
};
```

示例：

```text
float16_t/float32_t/float64_t -> ieee_binary
bfloat16_t                    -> bfloat
float8_e4m3_t                 -> finite_low_precision
float8_e5m2_t                 -> finite_low_precision
float6_e2m3_t/float6_e3m2_t   -> finite_low_precision
float4_e2m1_t                 -> finite_low_precision
tfloat32_t                    -> tensor_quantized
fixed8_s2f6_t                 -> fixed_point
```

通用 operation 能力优先由 family 推导，而不是为每个具体类型复制几十个 specialization。

## 8.3 Capability traits

模块必须区分：

```text
类型存在
!=
支持标量 add
!=
支持标量 fma
!=
支持转换
!=
支持 tensor input
```

建议内部接口：

```cpp
template <operation Op, typename Result, typename... Operands>
struct operation_capability;
```

但该 trait 只描述**数值操作能力**，不得包含：

```text
PTX mnemonic spelling
modifier ordering
operand register grammar
target SM
instruction version
```

应提供 concepts：

```cpp
template <typename T>
concept scalar_addable = ...;

template <typename To, typename From>
concept convertible_to = ...;

template <typename T>
concept tensor_multiplicand = ...;
```

## 8.4 新增类型的改动预算

正常情况下，新增一种格式只应改动：

1. format tag；
2. public alias；
3. `format_traits`；
4. arithmetic family/capability 声明；
5. decode/encode 或格式特有 rounding；
6. tensor traits（如适用）；
7. tests。

MUST NOT 改动：

```text
公共 add/sub/mul/fma 声明
公共 cvt 组合函数集合
context 公共接口
executor opcode model
任何 dynamic std::variant 类型列表
```

---

# 9. 控制策略设计

## 9.1 独立控制维度

舍入和其他控制必须与 operation 分离。

推荐公共枚举：

```cpp
enum class rounding_mode {
  nearest_even,       // PTX .rn
  toward_zero,        // PTX .rz
  toward_negative,    // PTX .rm
  toward_positive,    // PTX .rp
  nearest_away,       // PTX .rna
  stochastic,         // PTX .rs
};

enum class subnormal_mode {
  preserve,
  flush_input,
  flush_output,
  flush_input_and_output,
};

enum class saturation_mode {
  none,
  type_range,
  zero_to_one,
  finite,
};

enum class activation_mode {
  none,
  relu,
};

enum class approximation_mode {
  exact,
  ptx_approximate,
  ptx_full,
};

enum class product_part {
  low,
  high,
  wide,
};

enum class integer_overflow_mode {
  wrap,
  saturate,
};

enum class comparison_relation {
  equal,
  not_equal,
  less,
  less_equal,
  greater,
  greater_equal,
};

enum class nan_comparison_mode {
  ordered,
  unordered,
};
```

这些类型表达语义，不表达 PTX 文本 modifier 的组合顺序。

## 9.2 Domain control，而非 per-operation control

禁止为每个 operation 创建：

```text
add_control
sub_control
mul_control
fma_control
```

这会重新产生组合爆炸。

应创建少量 domain-level control：

```cpp
struct integer_control {
  integer_overflow_mode overflow = integer_overflow_mode::wrap;
};

struct product_control {
  product_part part = product_part::low;
  integer_overflow_mode overflow = integer_overflow_mode::wrap;
};

struct floating_control {
  rounding_mode rounding = rounding_mode::nearest_even;
  subnormal_mode subnormal = subnormal_mode::preserve;
  saturation_mode saturation = saturation_mode::none;
  activation_mode activation = activation_mode::none;
};

struct conversion_control {
  rounding_mode rounding = rounding_mode::nearest_even;
  subnormal_mode source_subnormal = subnormal_mode::preserve;
  subnormal_mode destination_subnormal = subnormal_mode::preserve;
  saturation_mode saturation = saturation_mode::none;
  activation_mode activation = activation_mode::none;
};

struct special_function_control {
  approximation_mode approximation = approximation_mode::exact;
  subnormal_mode subnormal = subnormal_mode::preserve;
};

struct comparison_control {
  comparison_relation relation = comparison_relation::equal;
  nan_comparison_mode nan = nan_comparison_mode::ordered;
};
```

Tensor control 单独定义，见后文。

## 9.3 控制合法性

不是所有 operation/type 都支持所有 control。

例如：

```text
BF16 scalar add: nearest-even only
FP32 add: multiple directed rounding modes
FP8 E4M3: no scalar add capability
integer add: no floating rounding
```

因此每次 operation dispatch 必须经过：

```text
normalize defaults
→ validate controls against operation capability
→ execute semantic kernel
```

不支持的 control MUST 返回错误，不得静默忽略或改成 nearest-even。

## 9.4 默认值

默认 control 可以采用 PTX 9.3 常见默认语义，例如浮点 nearest-even，但 executor SHOULD 在从 PTX 指令映射时显式传入控制，以避免依赖隐式默认。

默认值的意义是：

```text
方便独立单元测试和普通库调用
```

而不是：

```text
由 arith 猜测某条 PTX 指令省略了哪些 modifier
```

## 9.5 Stochastic rounding

随机比特必须作为显式输入，而不能由 `arith` 内部生成：

```cpp
struct stochastic_rounding_input {
  bits32_t random_bits;
};
```

推荐：

```cpp
auto result = cvt<float8_e4m3_t>(
    ctx,
    value,
    conversion_control{
        .rounding = rounding_mode::stochastic,
    },
    stochastic_rounding_input{random_bits});
```

PRNG、seed 和 replay 属于 simulator runtime/executor，不属于 `arith`。

---

# 10. 公共标量 API

## 10.1 Context

推荐建立不可变 context：

```cpp
class context {
 public:
  explicit context(model_profile profile);

  [[nodiscard]] const model_profile& profile() const noexcept;

 private:
  model_profile profile_;
};
```

`context` 只包含不可变数值模型，例如：

- PTX 9.3 deterministic reference policy；
- NaN policy；
- approximate function profile；
- Tensor accumulation profile；
- TF32 encoding profile。

它不得包含：

```text
register state
current instruction
warp state
mutable exception state
```

建议 profile 结构：

```cpp
struct model_profile {
  nan_policy nan;
  approximation_profile approximation;
  tensor_arithmetic_profile tensor;
  tf32_encoding_profile tf32;

  static model_profile ptx_9_3_reference();
};
```

该 profile 只定义数值上未完全指定或 target-dependent 的选择，不负责判断某条指令是否可在某个 SM 上使用。

## 10.2 泛型 operation

公共 API SHOULD 是固定数量的 CPO/free-function templates。

示意：

```cpp
template <typename Result = deduced_result_t,
          typename A,
          typename B,
          typename Control = default_control_t<operation::add, A, B>>
auto add(
    const context&,
    A lhs,
    B rhs,
    const Control& control = {});

template <typename Result = deduced_result_t,
          typename A,
          typename B,
          typename Control = default_control_t<operation::mul, A, B>>
auto mul(
    const context&,
    A lhs,
    B rhs,
    const Control& control = {});

template <typename Result = deduced_result_t,
          typename A,
          typename B,
          typename C>
auto fma(
    const context&,
    A a,
    B b,
    C c,
    const floating_control& control = {});

template <typename To, typename From>
auto cvt(
    const context&,
    From value,
    const conversion_control& control = {});
```

同类型标量运算可以推导结果：

```cpp
auto r = add(ctx, float32_a, float32_b, floating_control{});
```

mixed precision 必须显式给出结果类型：

```cpp
auto r = fma<float32_t>(
    ctx,
    bfloat16_a,
    bfloat16_b,
    float32_c,
    floating_control{});
```

## 10.3 推荐 operation 集合

### 整数/通用

```cpp
add
sub
mul
mad
div
rem
abs
neg
min
max
sad
```

### 扩展精度

```cpp
add_with_carry
sub_with_borrow
mad_with_carry
```

### 浮点

```cpp
add
sub
mul
mad      // 非 fused，按 PTX 所需语义
fma      // fused，单次目标舍入

div
abs
neg
copysign
min
max
sqrt
rcp
rsqrt
sin
cos
lg2
ex2
tanh
```

### 比较/选择

```cpp
compare
select
classify
test
```

### 转换/packing

```cpp
cvt<To>
pack<To>
unpack
```

### 位操作

```cpp
popcount
count_leading_zeros
find_most_significant
bit_reverse
bit_extract
bit_insert
funnel_shift
```

## 10.4 Typed-only 边界

`arith` 的公共执行 API SHOULD 以编译期强类型 value 为主，不提供 `any_scalar`、runtime opcode 或 runtime type-id 驱动的总入口。

若 exec IR 在运行时持有类型，executor 应在自己的 dispatch 层完成：

```text
runtime exec type
→ typed value extraction
→ typed arith CPO
```

而不是把 dynamic dispatch 下沉到 `arith`。

## 10.5 禁止的 API

禁止 per-type overload：

```cpp
add(float32_t, float32_t);
add(float64_t, float64_t);
add(bfloat16_t, bfloat16_t);
```

禁止 pairwise conversion API：

```cpp
f32_to_bf16(...);
bf16_to_f32(...);
f32_to_e4m3(...);
```

禁止 instruction form API：

```cpp
evaluate(ptx::fma_rn_f32, ...);
evaluate(opcode::fma, type::f32, modifiers, ...);
```

禁止让调用者直接访问 backend：

```cpp
SoftFloatBackend::fma(...);
Bf16Backend::add(...);
```

---

# 11. 返回值、状态与错误处理

## 11.1 Result 模型

推荐统一外壳：

```cpp
template <typename Value, typename Status = no_status>
struct result {
  Value value;
  Status status;
};
```

状态按 domain 区分：

```cpp
struct floating_status {
  bool invalid{};
  bool divide_by_zero{};
  bool overflow{};
  bool underflow{};
  bool inexact{};
};

struct integer_status {
  bool carry{};
  bool borrow{};
  bool overflow{};
};

struct tensor_status {
  bool model_dependent{};
  bool inexact{};
};
```

这些状态属于算术诊断，不代表 `arith` 拥有 PTX 架构寄存器。

executor 对普通 PTX 浮点指令可只使用 `.value`；对 extended-precision integer 指令可将 carry/borrow 写入自身维护的 CC 状态。

## 11.2 Unsupported control

公共 checked API SHOULD 返回：

```cpp
std::expected<result_type, arithmetic_error>
```

`arithmetic_error` 至少包括：

```cpp
enum class arithmetic_error {
  unsupported_operation,
  unsupported_type_combination,
  unsupported_rounding,
  unsupported_subnormal_mode,
  unsupported_saturation,
  invalid_stochastic_input,
  invalid_tensor_shape,
  invalid_scale_layout,
};
```

禁止 silent fallback。

## 11.3 编译期与运行时检查

- 类型/arity 明确不支持：优先 concept/static assertion；
- control 值在运行时选择：返回 `std::expected`；
- 内部 executor 已验证的热路径 MAY 使用 private `assume_valid` 入口；
- `assume_valid` MUST NOT 成为公共 API。

---

# 12. 整数与位运算语义

## 12.1 避免 C++ signed overflow UB

有符号整数 wrap-around 算术必须在 unsigned bit domain 或更宽类型中实现。

禁止：

```cpp
std::int32_t r = a + b;
```

应使用：

```cpp
std::uint32_t bits =
    std::bit_cast<std::uint32_t>(a) +
    std::bit_cast<std::uint32_t>(b);

auto r = std::bit_cast<std::int32_t>(bits);
```

## 12.2 通用 width traits

```cpp
template <typename T>
struct integer_traits {
  static constexpr unsigned width = ...;
  static constexpr bool is_signed = ...;
  using unsigned_type = ...;
  using wider_type = ...;
};
```

通用 kernel：

```cpp
add_wrap<T>
add_saturate<T>
sub_wrap<T>
sub_saturate<T>
mul_low<T>
mul_high<T>
mul_wide<T>
```

`mul.lo/hi/wide` 的差异通过 `product_control::part` 表达，不复制公共函数。

## 12.3 Carry/borrow

纯数值 API：

```cpp
template <typename T>
result<T, integer_status> add_with_carry(
    T lhs,
    T rhs,
    bool carry_in = false);
```

该函数只计算 carry，不存取 CC register。

## 12.4 Bit operations

`popcount`、`clz`、`brev`、`bfe`、`bfi`、`shf` 等属于无状态位级语义，可以位于 `arith::bit` 或 `arith` 公共 CPO 中。

不得让 bit operation 依赖 PTX bit-type operand grammar。

---

# 13. 浮点标量语义

## 13.1 支持范围

基于 PTX 9.3，标量浮点层至少覆盖：

```text
float16_t
float32_t
float64_t
bfloat16_t
```

`float16x2_t` 和 `bfloat16x2_t` 可通过 packed lane engine 获得规范要求的逐 lane 运算。[PTX-9.7.4]

TF32、FP8、FP6、FP4 的标量能力见第 15 节。

## 13.2 Fused 与 non-fused

`fma` 必须：

```text
exact product + add
→ 一次目标格式舍入
```

`mad` 必须按照 PTX 对相应类型规定的 non-fused 或 target-dependent 语义实现，不能简单 alias 到 `fma`。

内部 kernel 必须明确标记 rounding point。

禁止：

```text
fma = mul(target-rounded) + add(target-rounded)
```

## 13.3 Floating control

`floating_control` 的字段保持独立：

```cpp
struct floating_control {
  rounding_mode rounding;
  subnormal_mode subnormal;
  saturation_mode saturation;
  activation_mode activation;
};
```

不同类型/operation 的合法组合由 capability 验证。

## 13.4 Classification

以下函数必须由 format traits 泛化：

```cpp
classify
is_zero
is_negative_zero
is_subnormal
is_normal
is_infinity
is_nan
is_quiet_nan
is_signaling_nan
```

禁止为每种格式复制完整实现。

---

# 14. BF16 设计

## 14.1 表示

```cpp
using bfloat16_t = basic_float<formats::bfloat16>;
```

格式：

```text
1 sign
8 exponent
7 fraction
```

BF16 与 F16 同为 16-bit，但必须使用不同强类型和 traits。[PTX-5.2.3]

## 14.2 BF16 ↔ F32

BF16→F32 是精确扩展，可通过位级扩展实现。

F32→BF16 必须经过通用 round/pack pipeline，不得只截断低 16 位。

必须处理：

```text
nearest-even
directed rounding
NaN/Inf
subnormal
overflow
underflow
inexact
```

## 14.3 BF16 scalar operations

PTX 9.3 为 BF16/BF16x2 提供部分标量/packed 运算，且 rounding/control 能力与 F16/F32 不完全相同。[PTX-9.7.4]

因此：

```text
类型存在
+ capability says operation supported
→ 公共 generic CPO 可调用
```

而不是为 BF16 添加新 overload。

## 14.4 BF16 FMA

BF16 FMA 必须单次舍入。

禁止直接假设：

```text
f32_mulAdd
→ round to BF16
```

在所有输入上都等价。

推荐直接整数/GRS core：

```text
unpack
→ exact significand multiply
→ exponent align
→ shift-right-jam
→ signed add/sub
→ normalize
→ round_pack_bfloat16 once
```

如采用 widened F32 或其他中间格式，必须提供严格等价性证明或独立 exhaustive/differential test。

---

# 15. TF32、FP8、FP6、FP4 与定点格式

## 15.1 PTX 9.3 数值能力概览

下表描述 `arith` 应提供的能力层级；“标量算术”是 generic CPO 的 capability，不是一组 per-type overload。

| 类型族 | 标量算术 | 通用转换 | Packed | Tensor 输入/累加 |
|---|---:|---:|---:|---:|
| signed/unsigned integer | 是 | 是 | 部分 | 是，按 PTX 支持组合 |
| `float16_t` | 是 | 是 | `float16x2_t` | 是 |
| `float32_t` | 是 | 是 | `float32x2_t`（按能力） | 作为输入/累加器 |
| `float64_t` | 是 | 是 | 否 | 是，按 PTX 支持组合 |
| `bfloat16_t` | PTX 规定的子集 | 是 | `bfloat16x2_t` | 作为输入，通常 F32 累加 |
| `tfloat32_t` | 否 | 受限：仅 F32 ↔ TF32 | 否 | 是，通常 F32 累加 |
| E4M3/E5M2 | 否（默认） | 是 | x2/x4 storage | 是 |
| E2M3/E3M2/E2M1 | 否 | 是 | PTX 规定 packed storage | 是 |
| UE8M0/UE4M3 | 否 | 是 | 按 PTX storage | 作为 scale |
| S2F6 | 不自动提供 | 是 | `s2f6x2` | 按 PTX 数值需求 |

具体 capability 以 PTX ISA 9.3 数值语义和 `operation_capability` 为准；该表不是 instruction availability/target-SM 表。

## 15.2 能力原则

这些格式可以具有：

```text
representation
classification
pack/unpack
conversion
quantization
tensor input capability
```

但不因类型存在而自动具有：

```text
scalar add
scalar mul
scalar fma
scalar div
```

## 15.3 TF32

`tfloat32_t` 是 tensor-oriented semantic type。

公开支持：

```text
F32 -> TF32 quantization
TF32 -> canonical F32 expansion
tensor input
profile-specific encode/decode
```

默认不支持：

```cpp
add(tfloat32_t, tfloat32_t);
mul(tfloat32_t, tfloat32_t);
fma(tfloat32_t, tfloat32_t, tfloat32_t);
```

## 15.4 FP8

```cpp
float8_e4m3_t
float8_e5m2_t
```

需要独立 encoding traits。

E4M3 和 E5M2 对 Infinity/NaN 的能力不同，不能仅以 exponent/fraction bit 数生成全部特殊值规则。[PTX-5.2.3]

公开支持至少包括：

```text
classification
conversion
pack/unpack
tensor input
```

标量 add/mul 默认不提供。

## 15.5 FP6/FP4

```cpp
float6_e2m3_t
float6_e3m2_t
float4_e2m1_t
```

逻辑 scalar type 可存在于 `arith` 内部或公共类型层，以便 conversion 和 tensor kernel 使用。

但 PTX 9.3 规定部分 sub-byte 格式必须以 packed form 使用，public storage API 必须尊重其 packing/padding 规则。[PTX-5.2.3][PTX-5.2.5]

例如：

```cpp
using float4_e2m1x2_t = packed_t<float4_e2m1_t, 2>;
```

## 15.6 UE8M0、UE4M3 和 S2F6

这些格式主要用于 scale、conversion 或 fixed-point 语义。

应支持：

- 固定 encoding；
- classification（如适用）；
- conversion；
- pack/unpack；
- tensor block scale 输入。

不得为方便而把它们转成 host float 后长期保存为近似值。

---

# 16. 通用转换架构

## 16.1 避免 O(N²) pairwise conversion

禁止采用：

```text
f16_to_f32
f16_to_f64
f16_to_bf16
f32_to_f16
f32_to_bf16
...
```

应建立通用 pipeline：

```text
source value
    ↓ decode
canonical/unpacked numeric representation
    ↓ round/encode
 destination value
```

该 pipeline 是 public `cvt` 的**唯一数值路径**：

```text
cvt<To>(context, source, control[, stochastic_bits])
    -> validate operation/type/control capability
    -> decode source to an exact canonical number
    -> apply destination-domain controls
    -> encode canonical number to To exactly once
```

public `cvt` MUST NOT 再分流到以 `(To, From)` 为键的第二套 backend
router。特别是不得重新引入 `backend::convert<To>`、`convert_impl` 或生成
逐对 wrapper 的宏。否则同一种转换会同时受 canonical core 和旧 F32 hub
控制，重新产生能力、错误映射和舍入点不一致。

每种格式通常只实现：

```text
decode_from_format
encode_to_format
```

因此新增格式的核心转换成本接近 O(N)，而不是 O(N²)。

## 16.2 Canonical representation

内部 canonical value 必须精确表示 source，而不是先量化到 F32。当前实现的
有限值语义为：

```cpp
enum class number_class { zero, finite, infinity, nan };

struct number {
  number_class classification;
  bool negative;
  bool signaling_nan;
  std::uint64_t nan_payload;
  unsigned nan_payload_bits;
  std::uint64_t significand;
  int exponent;
};
```

其中 finite value 严格等于：

```text
(-1)^negative × significand × 2^exponent
```

现有 scalar float、至多 64-bit integer 和 S2F6 source 都能无损进入该表示；
decode 阶段不得设置 sticky 或执行目标格式舍入。舍入、overflow、underflow
和 inexact 只在 destination encode 阶段决定。若未来加入超过该精确容量的
source，必须先扩展 canonical significand，而不是回退到 host float 或 F32
中转。

## 16.3 Pair-specific exception

仅在以下情况允许 pair-specific kernel：

- PTX 明确规定特殊 bit mapping；
- stochastic rounding operand 布局特殊；
- packed source lane order 特殊；
- TF32 profile-specific encoding；
- generic canonical pipeline 无法保持精确 PTX 语义。

此类 specialization 必须带测试和注释说明原因。

Pair-specific specialization 只能是 canonical pipeline 的叶端 adapter，不能
成为另一条通用转换路线。例如 TF32 的 profile-specific quantizer 可以作为
`encode<tfloat32_t>` 的最后一步，但不能据此恢复 `F32 -> X -> Y` hub。

## 16.4 Conversion controls

`conversion_control` 独立表达：

```text
rounding
source/destination subnormal
saturation
relu
```

随机位作为额外显式 operand，不嵌入全局 state。

## 16.5 Canonical conversion 与 low-precision backend 的职责边界

`canonical_conversion` 拥有 public conversion semantics，包括：

- source classification/decode；
- integer、floating、fixed 的统一精确 canonical value；
- directed、RNA 和显式 stochastic rounding；
- source/destination subnormal、`.sat`、`.satfinite`、ReLU；
- destination status 与 typed error。

`low_precision_backend` 不再是 public `cvt` router。它仍可拥有并复用：

- BF16 arithmetic 所需的精确 widening/narrowing；
- approximate low-precision operation 的内部 F32 bridge；
- 独立的 low-precision round/pack primitive。

这些内部 helper 的存在不构成另一套 public conversion contract。若一个
`narrow_from_f32`/`widen_to_f32` helper 仍被 arithmetic 或 approximation
kernel 使用，应保留；仅把它包装成无人调用的 `convert_impl(To, From)` 不会
增加数值能力，必须删除。

---

# 17. Packed 类型与逐 lane 运算

## 17.1 通用 `packed_t`

```cpp
template <typename Element,
          std::size_t Lanes,
          typename Layout = default_packed_layout_t<Element, Lanes>>
class packed_t;
```

推荐 alias：

```cpp
using float16x2_t = packed_t<float16_t, 2>;
using float32x2_t = packed_t<float32_t, 2>;
using bfloat16x2_t = packed_t<bfloat16_t, 2>;
using float8_e4m3x2_t = packed_t<float8_e4m3_t, 2>;
using float8_e4m3x4_t = packed_t<float8_e4m3_t, 4>;
using float4_e2m1x2_t = packed_t<float4_e2m1_t, 2>;
```

## 17.2 Layout traits

`packed_layout_traits` 必须描述：

```text
logical element bits
container bits
lane offset
padding bits
lane order
```

不能假定所有 sub-byte 类型紧密排列。例如部分 6-bit packed format 使用 byte container 并保留 padding。[PTX-5.2.5]

## 17.3 Lane engine

逐 lane 算术应复用 scalar operation：

```cpp
template <typename Packed, typename Operation, typename Control>
Packed map_lanes(Packed lhs, Packed rhs, Operation op, Control control);
```

只有 capability 明确支持 packed scalar arithmetic 的类型才能调用。

FP8/FP6/FP4 packed type 的存在不意味着提供 packed add/mul。

---

# 18. 张量数值内核

## 18.1 责任定义

`arith::tensor` 负责：

```text
logical A tile
× logical B tile
+ logical C tile
→ logical D tile
```

它不负责：

```text
warp/lane fragment layout
register tuple layout
shared-memory descriptor
TMEM address
async commit/wait
sparse metadata register decoding
```

executor 必须先把指令级 fragment/descriptor 转换为逻辑 tile/view，再调用 `arith::tensor`。

## 18.2 公共 API

推荐 static tile API：

```cpp
namespace ptxsim::arith::tensor {

template <std::size_t Rows,
          std::size_t Cols,
          typename T>
class tile;

template <typename D,
          typename A,
          typename B,
          typename C,
          std::size_t M,
          std::size_t N,
          std::size_t K>
auto mma(
    const context&,
    const tile<M, K, A>& a,
    const tile<K, N, B>& b,
    const tile<M, N, C>& c,
    const tensor_control& control)
    -> std::expected<result<tile<M, N, D>, tensor_status>, arithmetic_error>;

}
```

也可提供 dynamic matrix view，用于 executor integration：

```cpp
matrix_view<const A>
matrix_view<const B>
matrix_view<const C>
matrix_view<D>
```

但不得引入 PTX shape enum，例如：

```cpp
shape::m16n8k32
```

逻辑 shape 只由 M/N/K 或 view dimensions 表示。

## 18.3 Tensor control

```cpp
struct tensor_control {
  rounding_mode accumulator_rounding = rounding_mode::nearest_even;
  subnormal_mode product_subnormal = subnormal_mode::preserve;
  subnormal_mode accumulator_subnormal = subnormal_mode::preserve;

  accumulation_order order = accumulation_order::k_ascending;
  accumulation_precision precision = accumulation_precision::format_defined;

  saturation_mode saturation = saturation_mode::none;
  tensor_nan_mode nan = tensor_nan_mode::profile_default;
};
```

这是数值模型，不包含：

```text
mma family
warp size
fragment layout
kind qualifier spelling
```

## 18.4 Product 与 accumulation

Tensor kernel 的最小内部原语应是 widened MAC：

```text
decode A
+ decode B
→ widened/exact product
→ accumulator combine
→ profile-defined rounding
```

禁止：

```text
FP8 × FP8
→ round back to FP8
→ convert to F32
→ add to accumulator
```

推荐内部接口：

```cpp
template <typename Acc, typename A, typename B>
Acc accumulate_product(
    Acc accumulator,
    A a,
    B b,
    const tensor_control&);
```

该接口可以是 `detail`，不作为普通标量运算公开。

## 18.5 支持的数据组合

PTX 9.3 的 matrix data types 包括：

- F16 multiplicands，F16/F32 accumulators；
- BF16 multiplicands，F32 accumulators；
- TF32 multiplicands，F32 accumulators；
- E4M3/E5M2/E3M2/E2M3/E2M1 multiplicands，F16/F32 accumulators；
- 带 UE8M0/UE4M3 scale 的低精度 multiplicands，F32 accumulator；
- F64 multiplicands/accumulator；
- integer/sub-byte/single-bit matrix arithmetic。[PTX-9.7.15.2]

`arith::tensor` 的 capability table 应按**数据组合和数值规则**描述支持范围，不按 PTX instruction family/shape 描述。

## 18.6 Block scaling

Block scale 必须作为显式数值输入：

```cpp
template <typename Scale>
struct block_scale_view;
```

推荐 API：

```cpp
auto result = tensor::mma<D>(
    ctx,
    a,
    b,
    c,
    control,
    block_scale_view{scale_a},
    block_scale_view{scale_b});
```

不得把 scale 隐藏在 FP4/FP8 element type 中。

数值行为应实现：

```text
D = (A × scale_A) × (B × scale_B) + C
```

以及 PTX 9.3 规定的 scale-vector grouping。[PTX-9.7.15.3]

## 18.7 Sparse tensor

稀疏 metadata 解码和 fragment selection 属于 executor/tensor instruction layer。

`arith::tensor` 可以接受：

```text
已经展开的 logical sparse tile
```

或通用 sparse logical view，但不能知道 metadata register 的 PTX 编码。

## 18.8 Unspecified accumulation

对于 PTX 未唯一规定的 accumulation order、rounding 或 subnormal 行为，`context::model_profile` 必须给出确定选择。

推荐 reference profile：

```text
K 从 0 递增
明确 product precision
明确 accumulator precision
nearest-even
preserve subnormal
固定 NaN policy
```

结果状态应标记：

```cpp
status.model_dependent = true;
```

不得把该结果宣称为所有 NVIDIA target 的唯一 bit-exact 结果。

---

# 19. 近似数学函数

PTX 的 approximate operations 不能简单等同于 host `libm`。

至少需要：

```text
rcp
rsqrt
sin
cos
lg2
ex2
tanh
approximate div/sqrt variants
```

## 19.1 Model

```cpp
struct approximation_profile {
  approximation_model model;
  target_family target;
};
```

`ptx_approximate` 模式必须：

- 特殊值行为满足 PTX 9.3；
- 误差不超过规范要求；
- 同一输入确定性；
- 不依赖 host rounding mode；
- 测试以 corner-case + error bound 为准。

## 19.2 Exact 与 approximate 分离

`approximation_mode` 是独立 control。

禁止建立：

```cpp
sin_approx_f32(...);
sin_exact_f32(...);
```

应使用：

```cpp
sin(ctx, value, special_function_control{
    .approximation = approximation_mode::ptx_approximate,
});
```

---

# 20. NaN、非规格化数与未完全指定行为

## 20.1 NaN policy

必须集中定义：

```cpp
struct nan_policy {
  bool quiet_signaling_nan;
  bool preserve_payload;
  bool preserve_sign;
  bool canonicalize;
  nan_selection selection;
};
```

禁止由以下因素隐式决定最终 NaN：

```text
host CPU
compiler
SoftFloat build specialization
未定义操作数顺序
```

## 20.2 Subnormal policy

`subnormal_mode` 必须明确输入和输出阶段。

后端不得自行猜测 `.ftz` 的含义。

executor 将 PTX 指令 modifier 映射成明确模式；`arith` 只执行该模式。

## 20.3 implementation-defined / unspecified

以下行为必须通过 profile 明确：

- TF32 encoding；
- 部分 tensor accumulation；
- approximate function 的具体结果；
- PTX 只要求 unspecified NaN 的场景；
- target-specific legacy numeric behavior。

`model_profile` 是数值策略，不是 target instruction availability database。

---

# 21. Backend 与第三方依赖

## 21.1 Backend 分层

逻辑上可划分为以下 backend/semantic kernel：

```text
IntegerBackend
SoftFloatBackend
BfloatBackend
LowPrecisionBackend
FixedPointBackend
ApproximationBackend
TensorBackend
```

每个 backend 服务一个 arithmetic family/semantic kernel，不服务某条 PTX instruction。

当前 conversion 是一个有意的例外：它由 public template 所需的
`include/detail/canonical_conversion.hpp` 提供唯一 decode/encode core，而
不是由 `environment.cpp` 中的 pairwise dispatch 提供。该 detail header 会因
public template 实例化而随包安装，但仍不是受支持的 public include surface。

`environment.cpp`/`detail::dispatch` 只适配仍需要编译型 backend 的 arithmetic、
approximation 和 profile-specific TF32 leaf operation；不得增加 generic
`convert`/`convert_impl` façade。

## 21.2 SoftFloat

SoftFloat 可用于：

```text
float16_t
float32_t
float64_t
以及适合通过其 primitive 实现的 conversion
```

SoftFloat 只能存在于 private detail 层：

```text
src/detail/softfloat_*
```

公共头文件 MUST NOT：

```cpp
#include <softfloat/softfloat.h>
```

公共 API MUST NOT 暴露 SoftFloat types/constants。

## 21.3 Host floating point

生产数值路径 MUST NOT 使用：

```text
host float/double arithmetic
std::fma
std::fesetround
-ffast-math
```

host floating point 仅可用于：

- diagnostics；
- formatting；
- validation-only tolerance comparison。

## 21.4 Custom low precision

BF16、FP8、FP6、FP4、TF32 和 fixed-point 应使用独立格式 backend 或通用 unpack/round-pack core。

不得通过 host cast 代替规范化转换。

---

# 22. 线程安全

## 22.1 Context

`context` 必须不可变并可被多线程共享。

## 22.2 SoftFloat state

仅用 RAII 保存/恢复 SoftFloat global variables，不足以保证并发安全。

必须采用：

1. thread-local SoftFloat state；或
2. 每线程 backend context；或
3. 最后备选：内部 mutex 串行化。

优先推荐 thread-local/per-thread context。

## 22.3 无全局可变数值状态

禁止：

```text
global rounding mode
global exception flags
global random generator
global tensor accumulator mode
```

所有策略必须来自：

```text
context
control
explicit operand
```

---

# 23. 建议文件结构

```text
submod/arith/
├── CMakeLists.txt
│
├── include/ptxsim/arith/
│   ├── context.hpp
│   ├── model_profile.hpp
│   ├── result.hpp
│   ├── error.hpp
│   ├── concepts.hpp
│   │
│   ├── types.hpp
│   ├── packed.hpp
│   ├── fixed.hpp
│   ├── format.hpp
│   │
│   ├── controls.hpp
│   ├── scalar.hpp
│   ├── conversion.hpp
│   ├── comparison.hpp
│   ├── bit.hpp
│   ├── special.hpp
│   │
│   └── tensor/
│       ├── tile.hpp
│       ├── matrix_view.hpp
│       ├── control.hpp
│       ├── scale.hpp
│       └── mma.hpp
│
├── src/
│   ├── context.cpp
│   ├── model_profile.cpp
│   │
│   └── detail/
│       ├── format_traits.hpp
│       ├── operation_capability.hpp
│       ├── control_validation.hpp
│       │
│       ├── integer_backend.hpp
│       ├── integer_backend.cpp
│       ├── bit_backend.hpp
│       │
│       ├── softfloat_context.hpp
│       ├── softfloat_context.cpp
│       ├── softfloat_backend.hpp
│       ├── softfloat_backend.cpp
│       │
│       ├── unpacked_number.hpp
│       ├── round_pack.hpp
│       ├── bfloat_backend.hpp
│       ├── low_precision_backend.hpp
│       ├── fixed_backend.hpp
│       │
│       ├── comparison_backend.hpp
│       ├── approximation_backend.hpp
│       │
│       └── tensor_backend.hpp
│
└── test/
    ├── test_types.cpp
    ├── test_controls.cpp
    ├── test_capabilities.cpp
    ├── test_integer.cpp
    ├── test_bit.cpp
    ├── test_float16.cpp
    ├── test_float32.cpp
    ├── test_float64.cpp
    ├── test_bfloat16.cpp
    ├── test_low_precision.cpp
    ├── test_conversion.cpp
    ├── test_packed.cpp
    ├── test_special.cpp
    ├── test_tensor.cpp
    └── test_thread_safety.cpp
```

## 23.1 文件数量控制

上述结构是逻辑边界，不要求初期机械创建所有文件。

当前实现按较少文件合并这些逻辑边界：public conversion façade 位于
`scalar.hpp`，canonical template core 位于
`include/detail/canonical_conversion.hpp`，低精度 arithmetic helper 位于
`src/detail/low_precision_backend.*`。文件合并不改变第 16.5 节规定的所有权。

初期 MAY 合并小文件，但必须保持：

```text
public façade
controls
format metadata
backend
conversion
tensor
```

之间的依赖边界。

## 23.2 不建立 `ptx/` 目录

`submod/arith` 下禁止建立：

```text
ptx/opcode.hpp
ptx/form.hpp
ptx/modifier.hpp
ptx_isa_9_3.yaml
instruction registry
```

PTX instruction model 属于其他模块。

---

# 24. CMake 与模块依赖

目标：

```cmake
add_library(ptxsim_arith ...)
add_library(ptxsim::arith ALIAS ptxsim_arith)
```

依赖：

```cmake
target_link_libraries(
    ptxsim_arith
    PRIVATE
        softfloat)
```

SoftFloat MUST NOT 出现在 interface dependency 中。

`arith` 不得链接：

```text
ptx_frontend
exec_ir
simulator core
memory
scheduler
trace
fp
```

测试专用高精度依赖只能链接测试 target。

---

# 25. 测试策略

## 25.1 类型和格式测试

对 ≤16-bit fixed encoding 格式尽可能穷举：

```text
classification
pack/unpack
decode/encode
round-trip
canonicalization
```

包括：

```text
float16_t
bfloat16_t
float8_e4m3_t
float8_e5m2_t
float6_e2m3_t
float6_e3m2_t
float4_e2m1_t
ufloat8_e8m0_t
fixed8_s2f6_t
```

## 25.2 Generic typed tests

测试应由 capability traits 驱动，而不是手工为每个类型复制：

```cpp
typed_test_suite<scalar_addable_types>
typed_test_suite<convertible_types>
typed_test_suite<tensor_input_types>
```

新增格式后应自动进入适用的通用测试集合。

## 25.3 整数测试

- 8/16-bit 可穷举；
- 32/64-bit 使用边界 + 固定 seed；
- independent wider-integer reference；
- signed overflow UB sanitizer；
- carry/borrow；
- high/low/wide product；
- divide edge cases。

## 25.4 F16/F32/F64

- SoftFloat differential；
- 四种 directed rounding；
- FTZ；
- signed zero；
- NaN/Inf；
- overflow/underflow/inexact；
- FMA single-round；
- state isolation。

## 25.5 BF16

- 全位模式 classification；
- BF16→F32→BF16；
- tie-to-even；
- directed rounding；
- NaN policy；
- subnormal；
- FMA double-rounding regression；
- independent high-precision oracle。

## 25.6 Conversion

Conversion tests 必须覆盖：

```text
source/destination family combinations
rounding modes
saturation
relu
subnormal
stochastic random operand
packed lane order
padding bits
```

生产 conversion 与 oracle 不得共享同一个 encode core。

## 25.7 Tensor

Tensor 测试分为：

### `arith` numeric tests

```text
logical tiles
→ tensor::mma
→ logical result
```

### executor integration tests

```text
PTX fragment/lane/TMEM
→ logical tile
→ arith::tensor::mma
→ fragment/writeback
```

两类测试不得混合。

Tensor numeric tests 必须覆盖：

- F16/F32 accumulation；
- BF16→F32；
- TF32→F32；
- FP8/FP6/FP4 widened products；
- block scaling；
- fixed accumulation order；
- model-dependent status；
- no premature low-precision rounding。

## 25.8 Approximate functions

检查：

```text
corner-case behavior
error bound
determinism
subnormal policy
```

不得用任意宽松 tolerance 掩盖超出 PTX 误差要求的问题。

## 25.9 并发

至少两个线程使用不同 rounding/subnormal 模式并发调用，验证：

```text
value 不污染
status 不污染
SoftFloat state 不污染
```

## 25.10 可复现性

随机测试 MUST：

- 固定 seed；
- 输出 raw bits；
- 输出 control；
- 可单 case 重放。

---

# 26. 实施阶段

## 阶段 1：创建独立模块骨架

- 新建 `submod/arith`；
- 新建 `ptxsim::arith` namespace；
- 建立 CMake target；
- 不依赖 `fp`。

## 阶段 2：类型与 control

- `basic_float`；
- 标准库风格 aliases；
- `basic_fixed`；
- `packed_t`；
- format traits；
- independent control enums/aggregates；
- result/error/context。

## 阶段 3：整数和位运算

- 通用 width traits；
- wrap/saturate；
- high/low/wide product；
- carry/borrow；
- bit operations；
- generic tests。

## 阶段 4：F16/F32/F64

- SoftFloat private adapter；
- thread-safe state；
- scalar arithmetic；
- classification；
- conversion；
- 参考旧 fp 测试但不保留旧 API。

## 阶段 5：通用 conversion pipeline

- unpacked numeric representation；
- generic decode/encode；
- integer/floating/fixed conversions；
- packed conversion。

## 阶段 6：BF16

- representation/traits；
- conversion；
- scalar capabilities；
- single-round FMA；
- exhaustive/differential tests。

## 阶段 7：TF32/FP8/FP6/FP4/scale/fixed

先完成：

```text
representation
classification
conversion
packing
tensor traits
```

不得添加不存在的 scalar operation。

## 阶段 8：张量数值内核

- tile/view；
- widened product；
- deterministic accumulation；
- mixed precision；
- block scale；
- tensor tests。

## 阶段 9：executor 集成

在 executor 中建立 PTX instruction → `arith` primitive 的 mapping。

该 mapping 不进入 `arith`。

## 阶段 10：删除 fp

完成所有调用方迁移后：

- 删除 `submod/fp`；
- 删除 `ptxsim::fp`；
- 删除旧 target；
- 删除临时 adapter；
- 确认全仓库不存在旧 include。

---

# 27. 施工 Agent 强制规则

Agent MUST：

1. 把 `arith` 当作新模块实现；
2. 保持 `arith` 不依赖 PTX instruction/IR；
3. 使用 generic CPO/templates，而不是 per-type overload；
4. 使用标准库风格类型命名；
5. 将 controls 单独定义；
6. 通过 capability traits 控制类型/操作支持；
7. 使用 generic decode→canonical→encode conversion；
8. 保持 fused operation 的正确舍入点；
9. 将 Tensor 数值与 fragment/execution 分离；
10. 为 unsupported operation/control 返回明确错误；
11. 保证 SoftFloat 私有且线程隔离；
12. 在最终结果中删除 fp。

Agent MUST NOT：

```text
建立 PTX instruction form model
建立 opcode/modifier registry
把 exec_ir 引入 arith
为新类型添加整套 overload
添加逐对 conversion public API
为 TF32/FP8/FP4 伪造 scalar add/mul/fma
在 Tensor 中处理 lane/TMEM/descriptor
用 host float/double 执行生产语义
静默忽略 control
依赖旧 fp target
最终保留 compatibility namespace
```

每个 PR/施工结果必须说明：

1. 新增了哪些 numeric types；
2. 新增了哪些 scalar capabilities；
3. 新增了哪些 controls；
4. 使用哪个 backend；
5. 是否存在 model-dependent behavior；
6. 是否涉及 Tensor accumulation；
7. 新增了哪些 tests；
8. 是否仍有调用方依赖 `fp`。

---

# 28. 验收清单

- [ ] `submod/arith` 是独立新模块
- [ ] `arith` 不链接 `fp`
- [ ] public namespace 为 `ptxsim::arith`
- [ ] 类型名称采用 `float32_t`、`bfloat16_t`、`tfloat32_t` 等标准库风格
- [ ] 浮点类型基于通用 `basic_float<Format>`
- [ ] 定点类型基于通用 `basic_fixed`
- [ ] packed 类型基于通用 `packed_t`
- [ ] 格式 masks/bias/特殊值集中在 traits
- [ ] 新增类型不需要新增 add/sub/mul/fma overload
- [ ] public API 不包含 PTX opcode/form/modifier descriptor
- [ ] public API 不依赖 exec IR
- [ ] controls 与 operation 分离
- [ ] 不存在每个 operation 一套 control 类型
- [ ] control 合法性由 capability 验证
- [ ] unsupported control 不会静默降级
- [ ] conversion 不是 O(N²) pairwise API
- [ ] FMA 保证 single-round
- [ ] BF16 FMA 有 double-rounding regression test
- [ ] TF32 不假设 ISA 唯一 raw encoding
- [ ] FP8/FP6/FP4 默认无伪造标量算术
- [ ] packed padding/lane order 被正确建模
- [ ] tensor API 只接受 logical tile/view
- [ ] tensor API 不知道 warp/lane/TMEM/descriptor
- [ ] block scale 是显式输入
- [ ] low-precision tensor product 不提前 round 回输入格式
- [ ] model-dependent tensor 结果被标记
- [ ] SoftFloat 不出现在公共头
- [ ] SoftFloat state 线程隔离
- [ ] 生产路径不使用 host floating arithmetic
- [ ] signed integer 算术无 C++ UB
- [ ] generic typed tests 由 capability 驱动
- [ ] low-bit formats 有 exhaustive tests
- [ ] static numeric tests 与 executor integration tests 分离
- [ ] 最终仓库删除 `submod/fp`
- [ ] 最终仓库删除 `ptxsim::fp`
- [ ] 最终仓库无兼容 alias

---

# 29. 最终架构不变量

重写完成后，以下陈述必须始终成立：

```text
arith 只建模数值，不建模 PTX instruction。
executor 解释 PTX，arith 执行 typed numeric primitive。

类型由 format traits 描述。
控制由独立 control types 描述。
能力由 operation × arithmetic family traits 描述。
算法由少量 semantic kernels/backends 实现。

新增格式不会新增一组公共函数。
新增转换格式不会新增 N 个 pairwise conversion API。

TF32/FP8/FP6/FP4 可以参与 tensor，
但不存在的 scalar ISA capability 不会被伪造。

Tensor 模块处理 logical MMA 数值，
不处理 fragment、lane、warp、descriptor 或 TMEM。

SoftFloat 是私有实现工具。
PTX ISA 9.3 是数值语义事实来源。

arith 最终完全替代 fp，
仓库中只保留 ptxsim::arith。
```

---

# 30. 规范参考

本文以 NVIDIA 官方 **Parallel Thread Execution ISA Version 9.3** 为规范基准，重点参考：

- §5.2.1 Fundamental Types；
- §5.2.3 Alternate Floating-Point Data Formats；
- §5.2.4 Fixed-point Data format；
- §5.2.5 Packed Data Types；
- §6.5 Type Conversion 与 Rounding Modifiers；
- §9.7.1 Integer Arithmetic Instructions；
- §9.7.2 Extended-Precision Integer Arithmetic Instructions；
- §9.7.3 Floating-Point Instructions；
- §9.7.4 Half Precision Floating-Point Instructions；
- §9.7.5 Mixed Precision Floating-Point Instructions；
- §9.7.6–9.7.8 Comparison、Selection、Logic 与 Shift；
- §9.7.9 Data Movement and Conversion 中的数值转换部分；
- §9.7.15 Matrix Multiply-Accumulate、Matrix Data-types 与 Block Scaling。

官方文档：

<https://docs.nvidia.com/cuda/parallel-thread-execution/index.html>

引用标签：

```text
[PTX-5.2.3]   Alternate Floating-Point Data Formats
[PTX-5.2.4]   Fixed-point Data format
[PTX-5.2.5]   Packed Data Types
[PTX-9.7.4]   Half Precision Floating-Point Instructions
[PTX-9.7.15.2] Matrix Data-types
[PTX-9.7.15.3] Block Scaling for mma.sync
```
