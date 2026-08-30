# Historical / Superseded

> **Archive status:** Historical / Superseded.
> **Original:** `m4-review.md`; original body SHA256
> `3b656f1a3b6c26eae7199bb03908652ed99dcb375d18b4a43a329056c2dba9fc`.
> **Reviewed:** `refactor/arith-module@3b9ceb99a43e65beee38ce4248e6a5658c9f85a7`;
> baseline `main@c7b2b31ae8d3e81198afaa6202506de35c77cde4`.
> **Milestone mapping:** legacy predecessor to V2-M1 arithmetic remediation.
> **Supersedes:** [no earlier archived review](README.md#arithmetic-review-audit-chain).
> **Superseded by:** [V2-M1 main review](v2-m1-main-review.md).

---
# `refactor/arith-module` 全量 Review

> **结论：NOT MERGE READY**  
> **审查对象：** `endingly/ptxsim` / `refactor/arith-module`  
> **审查快照：** branch tree `3b9ceb99a43e65beee38ce4248e6a5658c9f85a7`  
> **基线：** `main` commit `c7b2b31ae8d3e81198afaa6202506de35c77cde4`  
> **规范基准：** NVIDIA PTX ISA 9.3  
> **设计基准：** `docs/arith_module_design.md`  
> **审查日期：** 2026-08-28  
> **用途：** 作为后续 Coding Agent 的修复清单、拆分依据和验收门禁

---

## 1. 审查结论

该分支已经建立了一个**方向基本正确的独立算术模块骨架**：

- `arith` 没有引入 PTX instruction/opcode/form 模型；
- 没有依赖 `ptx_frontend`、`exec_ir`、state、memory、scheduler 或 simulator core；
- 旧 `ptxsim::fp` / `fp::Environment` 路线已被抛弃；
- 浮点值使用强类型 raw encoding，而不是宿主 `float`/`double`；
- SoftFloat 位于 private backend，并检查 TLS 配置；
- BF16 FMA 使用了显式精确中间表示和单次目标舍入；
- public control、result、error、context 的大方向与设计文档一致。

但是，当前实现仍存在多项**会直接产生错误数值结果或错误能力声明**的合并阻断问题。问题集中在五条连锁路径：

```text
格式事实
  -> format traits / classification
  -> capability
  -> conversion
  -> packed/tensor
  -> tests
```

其中 UE8M0/UE4M3、`mad`、signed BFE/BFIND、tensor 数据组合以及 block scaling 均已发现明确的 PTX 9.3 语义偏差。现有测试中还存在“生产代码与测试 oracle 共享同一错误假设”的情况，因此“现有测试全绿”不能作为该分支正确性的证据。

### 1.1 严重度统计

| 严重度 | 数量 | 含义 |
|---|---:|---|
| P0 | 8 | 数值错误、规范事实错误、错误能力暴露；合并前必须全部修复 |
| P1 | 10 | 设计契约未落地、重要控制/测试/profile 缺失；原则上应在本里程碑关闭 |
| P2 | 5 | 封装、构建、可维护性及文档债务；需关闭或形成明确后续 issue/ADR |

### 1.2 建议处置

1. **禁止在当前 capability 和 test oracle 上继续堆 case。** 先修格式事实和能力表。
2. **禁止通过修改 `arith_module_design.md` 来迁就当前实现。** 若确需偏离，先出 ADR，并证明仍符合 PTX 9.3。
3. 修复顺序必须是：

```text
格式 golden vectors
-> traits/classification
-> capability matrix
-> conversion core
-> scalar/bit
-> packed/tensor
-> approximate/profile
-> independent tests
-> CI gates
```

4. P0 未清零前，不应开始 executor 对 `arith` 的正式集成。

---

## 2. 审查范围与方法

### 2.1 已审查内容

本次覆盖分支相对 `main` 的全部 `arith` 相关变更，重点包括：

- `docs/arith_module_design.md`
- `submod/arith/include/**`
- `submod/arith/src/**`
- `submod/arith/test/**`
- `submod/arith/CMakeLists.txt`
- 顶层 CMake、presets、header registration helper
- `.github/workflows/linux-ci.yml`
- 过时的 `.agents/project_plan.md`

按逻辑边界检查了：

```text
module boundary
public types
format metadata
capabilities
controls/context/result/error
integer and bit operations
floating scalar operations
conversion
packed types
logical tensor kernels
approximate functions
SoftFloat isolation
validation helpers
tests
CMake/CI
```

### 2.2 规范来源

主要对照：

- [PTX ISA 9.3 — Alternate Floating-Point Data Formats](https://docs.nvidia.com/cuda/parallel-thread-execution/#alternate-floating-point-data-formats)
- [PTX ISA 9.3 — Integer Arithmetic Instructions](https://docs.nvidia.com/cuda/parallel-thread-execution/#integer-arithmetic-instructions)
- [PTX ISA 9.3 — Floating-Point Instructions](https://docs.nvidia.com/cuda/parallel-thread-execution/#floating-point-instructions)
- [PTX ISA 9.3 — Matrix Data-types / Block Scaling](https://docs.nvidia.com/cuda/parallel-thread-execution/#matrix-data-types)
- `docs/arith_module_design.md`

### 2.3 审查限制

本次环境无法从 GitHub 直接 clone 并执行本地构建，因此：

- 已完成逐文件静态审查、设计对照和规范交叉检查；
- **未实际运行** CMake configure/build、CTest、sanitizer 或并发压力测试；
- 所有修复 PR 仍必须在干净 checkout 上完成 Debug/Release 构建及完整测试；
- 本文中标为“需动态确认”的项目，不得被视为已通过运行时验证。

---

## 3. 设计一致性总表

| 设计领域 | 结论 | 说明 |
|---|---|---|
| 独立纯数值模块边界 | **PASS** | 未引入 PTX instruction/IR/machine state；未依赖旧 `fp` |
| 强类型数值表示 | **PARTIAL PASS** | `basic_float`、opaque TF32 方向正确；scale/fixed/packed 仍有编码与别名问题 |
| 集中格式元数据 | **FAIL** | UE8M0/UE4M3 特殊值事实错误；`format_info_v` 存在 blanket 假设 |
| 类型存在与操作能力分离 | **FAIL** | 多个 concept/capability 比实际 dispatch 更宽，tensor 表也不符合 PTX |
| 独立 control 建模 | **PARTIAL PASS** | 类型已分离，但 `.rna`/`.rs`/ReLU/saturation 等大面积未实现 |
| generic scalar API | **PARTIAL PASS** | public 名称是泛型模板，但 conversion 是巨型 pairwise `if constexpr` |
| 明确 result/status/error | **PARTIAL PASS** | 外壳存在；compound status 丢失，内部异常常被压成 `unsupported_operation` |
| F16/F32/F64/BF16 精确算术 | **PARTIAL PASS** | SoftFloat/BF16 core 较好；public `mad` 错误，部分控制未覆盖 |
| 通用 conversion pipeline | **FAIL** | 未形成 decode→canonical→encode；能力矩阵、路由和控制不一致 |
| Packed 表示与 lane engine | **PARTIAL PASS** | layout abstraction 已有；operation capability 过宽且 required aliases 不全 |
| Tensor 逻辑数值内核 | **FAIL** | 边界正确，但数据组合、block scale 布局与中间精度不正确 |
| Approximate/profile | **FAIL** | 部分 deterministic kernel 可用，但 profile 未驱动结果，若干 op 直接用 exact 替代 |
| SoftFloat private/thread isolation | **PASS / TEST GAP** | CMake TLS 检查与 RAII 方向正确；还需更强并发/嵌套恢复测试 |
| Independent tests | **FAIL** | UE scale 格式出现同源错误 oracle；缺少多类 capability/negative/error-bound 测试 |
| CMake/CI gate | **PARTIAL PASS** | Debug/Release 存在；缺 sanitizer、header consumer、install/export、完整 push gate |
| 与旧 roadmap 一致性 | **FAIL** | 旧计划仍以 `ptxsim::fp` / `Environment` 为中心，已失效 |

---

## 4. 值得保留的实现

后续 Agent 不应把整个分支推倒重来。以下设计或实现可作为修复基础：

### 4.1 模块边界

`submod/arith` 没有对 PTX instruction 建模，符合：

```text
arith: typed values + controls -> numeric result + numeric status
executor: machine state + instruction -> machine state
```

此边界必须保持。

### 4.2 类型系统

- `basic_float<Format>` 避免了每个格式复制完整 class；
- alternate formats 没有 alias 到 host float；
- `tfloat32_t` 不公开固定 raw layout，通过 profile encode/decode；
- `packed_layout_traits` 已能表达 logical bits、container bits、padding 和 lane offset。

### 4.3 SoftFloat 隔离

- SoftFloat 为 `PRIVATE` link dependency；
- CMake 强制检查 `THREAD_LOCAL=thread_local`；
- `SoftFloatContext` 保存并恢复 rounding/tininess/flags；
- public headers 未包含 SoftFloat header 或类型。

### 4.4 BF16 单次舍入 FMA

`low_precision_backend.cpp` 中 BF16 FMA 不是简单的：

```text
f32 fma -> bf16 round
```

而是使用精确整数表示、指数对齐、符号加减和最终一次 `round_pack_bf16`。这是符合设计文档精神的实现，应保留并补强独立 oracle。

### 4.5 Public checked API

`std::expected<result<...>, arithmetic_error>`、domain-level controls 和 immutable-style context 的方向正确。修复应收紧 capability、补齐控制和错误映射，而不是退回 overload façade 或异常式 public API。

---

# 5. P0 — 合并阻断问题

## ARITH-P0-001：UE8M0 / UE4M3 格式元数据和测试 oracle 错误

**涉及文件**

- `submod/arith/include/detail/format_traits.hpp`
- `submod/arith/src/detail/low_precision_backend.cpp`
- `submod/arith/test/test_conversion.cpp`
- `submod/arith/include/types.hpp`

**现状**

`FormatTraits<ufloat8_e8m0_t>` 将该格式建模为：

- 没有 NaN；
- `0xff` 是最大有限 exponent；
- `is_nan_fields` 永远为 false；
- generic classifier 将 raw `0x00` 视为 zero。

`FormatTraits<ufloat7_e4m3_t>` 同样将 `0x7f` 当作有限值。

测试又显式写入相同假设：

```cpp
classify(ue8) == (bits == 0 ? zero : normal)
classify(ue4) == finite_class(...)
```

PTX ISA 9.3 明确规定：

- UE8M0 不支持 infinity，唯一 NaN encoding 为 `0xff`，且必须以 `ue8m0x2` packed format 使用；
- UE4M3 不支持 infinity，唯一 NaN encoding 为 `0x7f`。

**影响**

该错误会同时污染：

```text
classification
is_nan/is_zero
F32 widening
F32 narrowing
round-trip
block scale validation
scaled tensor MMA
```

这不是孤立的显示错误，而是当前 tensor scale 数值链的根错误。

**必须修复**

1. 建立一份不依赖 production traits 的 PTX 9.3 format golden table。
2. 修正 UE8M0/UE4M3：special values、maximum finite、has_zero、decode/encode、NaN conversion policy。
3. 为 UE8M0 增加规范 packed alias，例如 `ufloat8_e8m0x2_t`；单 scalar type可继续作为内部 lane semantic type，但 public storage API 必须说明限制。
4. 不得只修改现有 test expected 使其跟随新 traits；必须添加固定 raw-bit golden vectors。

**验收条件**

- raw `0xff` 的 UE8M0 classification 为 NaN；
- raw `0x7f` 的 UE4M3 classification 为 NaN；
- UE8M0 最小有限 encoding 的数值映射有独立 golden；
- NaN→scale、negative→unsigned scale、overflow→scale 的行为逐项对照 PTX conversion form；
- block scale tests 使用修正后的 encoding；
- production decoder 和 oracle 不共享 `FormatTraits` 或 encode core。

---

## ARITH-P0-002：Capability concepts 对外承诺了实际不存在的操作

**涉及文件**

- `submod/arith/include/concepts.hpp`
- `submod/arith/include/scalar.hpp`
- `submod/arith/include/packed.hpp`
- `submod/arith/include/tensor.hpp`

**现状**

`convertible_to<To, From>` 基本是整数、scalar float、fixed 的 Cartesian product；但 `cvt` 只实现其中一小部分。例如：

- 多个 8/16-bit integer ↔ float 组合没有实际 dispatch；
- `float64_t` 与 BF16/FP8/FP6/FP4 等大量组合被 concept 宣称支持，但运行时返回 `unsupported_type_combination`；
- `tensor_multiplicand` 只按“是 scalar_float 或 integer”判断，没有表达 accumulator/result/scale 组合；
- packed capability 只看 element type，不看 lanes、layout 和 operation。

设计文档要求：

```text
明确不支持的 type/arity -> concept/static assertion
运行时选择的 control -> std::expected error
```

当前实现把本应在编译期拒绝的组合推迟到了运行时。

**影响**

- 调用方无法把 concept 当作真实能力表；
- generic typed tests 会得到假阳性；
- executor lowering 无法安全依据 capability 生成调用；
- 新增格式时更容易扩大“声明支持但实际失败”的区域。

**必须修复**

建立单一事实源：

```cpp
template <operation Op, typename Result, typename... Operands>
struct operation_capability;
```

并由它派生：

```text
scalar_addable
scalar_fma_compatible
convertible_to
packed_operation_capability
mma_capability
```

不得让 public concept 和 backend dispatch 分别维护两张表。

**验收条件**

- 所有 `concept == true` 的组合都有成功路径和测试；
- 所有无 PTX 数值能力的组合在 compile-time 为 false；
- 增加 compile-negative static assertions；
- 自动生成 capability coverage table，比较“trait true”与“测试实例化集合”。

---

## ARITH-P0-003：Conversion 未实现设计要求的 canonical pipeline

**涉及文件**

- `submod/arith/include/scalar.hpp`
- `submod/arith/src/detail/conversion_traits.hpp`
- `submod/arith/src/detail/low_precision_backend.cpp`
- `submod/arith/src/environment.cpp`

**现状**

public `cvt` 是一个大型 pairwise `if constexpr` 链。新增一种格式会要求修改 public `cvt` 本体，与设计文档的以下硬约束冲突：

```text
新增格式不得修改公共 cvt 组合函数集合
conversion 应使用 decode -> canonical -> encode
```

当前路由还混合了：

- direct SoftFloat pair kernels；
- 以 F32 为 hub 的 low-format 路由；
- fixed ↔ integer 通过 F32；
- pair-specific TF32；
- public header 中的 saturation/wrap 特例。

已见风险包括：

- 能力 concept 与实际 pair list 不一致；
- integer/fixed 经 F32 时可能丢失决定 wrap/saturation 的低位；
- `fixed8_s2f6_t` 无 saturation 的窄化最终使用 `static_cast<int8_t>`，越界结果依赖实现；
- unsigned scale 格式复用带 sign 的 generic narrow，negative input policy 未显式建模；
- status 只手工 OR 部分字段，缺少统一 composition rule。

**必须修复**

1. 引入内部 canonical representation，至少覆盖：

```text
unpacked_binary
unpacked_integer
unpacked_fixed
classification/sign/exponent/significand/sticky
```

2. 将 conversion 拆为：

```text
decode<From>
convert canonical family
round/encode<To>
```

3. 只为 PTX 明确特殊 mapping 的 pair 保留 specialization，并在代码中写明理由。
4. public `cvt` 只做 capability check、control validation 和 generic dispatch。
5. 明确 status composition，禁止零散手工复制字段。

**验收条件**

- 新增一个 synthetic test format 时不需要修改 public `cvt`；
- conversion capability 表与可实例化路径一一对应；
- 8/16/32/64 integer、IEEE、BF16、low precision、fixed family 组合按声明覆盖；
- 有 direct-vs-hub double-rounding regression；
- 所有窄化、wrap、satfinite、ReLU、NaN、negative-to-unsigned 行为都有独立 vectors。

---

## ARITH-P0-004：Public floating `mad` 语义错误且丢失状态

**涉及文件**

- `submod/arith/include/scalar.hpp`
- `submod/arith/src/environment.cpp`
- `submod/arith/src/detail/softfloat_backend.cpp`

**现状**

public `mad` 当前执行：

```text
mul(target rounded)
-> add(target rounded)
```

并只返回 `add` 的 status，乘法产生的 overflow/underflow/inexact 会丢失。

与此同时，private backend 已存在 `SoftFloatBackend::mad` / `backend::mad`，但 public façade 没有使用它。

PTX ISA 9.3 对 `sm_20+` 的 `mad.f32` 和 `mad.f64` 规定其与 FMA 相同：无限精度 product/add 后进行一次目标舍入。仅 legacy `sm_1x` 的 `mad.f32` 有特殊截断模型。当前 `context` 又没有选择 legacy model 的有效 typed profile，因此默认 public 行为不符合基准。

**影响**

- 数值可能与 PTX 9.3 不同；
- FMA double-rounding regression 会失败；
- status 不完整；
- public/backend 两套 `mad` 形成死实现和维护分叉。

**必须修复**

- 对 reference profile，`mad.f32/f64` 调用 fused backend；
- 如保留 legacy `sm_1x`，必须建立显式 profile 和独立 kernel，不能用普通 `mul+add` 近似；
- integer `mad` 等 compound op 同样必须组合所有阶段 status；
- 删除未使用的重复 path。

**验收条件**

- `mad` 与 `fma` 在 reference profile 的 raw bits/status 一致；
- 加入一组可区分 fused 与 two-round 的 vectors；
- legacy profile 若存在，加入 PTX 指定 truncation regression；
- compound operation status 有统一 OR/merge helper。

---

## ARITH-P0-005：Signed `bfe` / `bfind` 不符合 PTX

**涉及文件**

- `submod/arith/include/bit.hpp`
- 当前 bit tests

**现状**

`bit_extract<T>` 对 signed/unsigned 一律零扩展。

`find_most_significant<T>` 对 signed input 直接按 unsigned bit pattern 找最高 `1`。对于负数，这通常只会返回 sign bit 位置。

PTX 9.3 要求：

- signed `bfe.s32/.s64` 对提取字段进行 sign extension；字段越过 MSB 时也按 PTX 的 `sbit` 规则填充；
- signed `bfind` 对负输入寻找最高的 `0`，等价于先反转负数 bit pattern 后找最高 `1`；
- 未找到时返回 `.u32 0xffffffff`；
- `.shiftamt` 是另一个明确语义。

**影响**

所有 signed BFE/BFIND 结果均不可信，且现有 unsigned-only 小宽度测试无法发现。

**必须修复**

建议拆成明确数值 primitive：

```text
bit_extract_unsigned
bit_extract_signed
find_most_significant_non_sign
find_shift_amount
```

或用 operation control/type trait 精确区分。不得让 signed behavior 由 `std::make_unsigned_t` 意外决定。

**验收条件**

- 逐条翻译 PTX pseudocode；
- 32/64-bit signed/unsigned boundary vectors；
- `len=0`、`pos>msb`、跨越 MSB、全零、`-1`、最小负数；
- `bfind` normal/shiftamt 两种返回形式；
- sanitizer 下无越界 shift/UB。

---

## ARITH-P0-006：Tensor 数据组合 capability 与 PTX 9.3 不一致

**涉及文件**

- `submod/arith/include/tensor.hpp`
- `submod/arith/test/test_tensor.cpp`

**现状**

当前 `mma_capability`：

- 允许 `float32_t × float32_t -> float32_t`；PTX matrix data-type 表未将 F32 列为普通 MMA multiplicand；
- 对 low formats 强制 `A == B`，未表达 A/B 各自可选的合法组合；
- low formats 只允许 F32 accumulator，遗漏 PTX 表中的部分 F16 accumulator 组合；
- integer 路径要求 `D == C == A == B`，并允许任意 arithmetic integer；
- PTX 要求普通 integer MMA 为 A/B `.u8`/`.s8`，C/D `.s32`；sub-byte 和 single-bit 另有规则；
- integer saturation 被 tensor control 统一拒绝；
- integer overflow 被塞入 `tensor_status.inexact`，字段语义错误。

现有 tensor tests 又以 `int32 × int32 -> int32` 为正确能力，反向固化了问题。

**必须修复**

建立数据组合表，而不是“family 大致匹配”：

```cpp
tensor_capability<result, a, b, accumulator, scale_model>
```

至少区分：

```text
f16 -> f16/f32
bf16 -> f32
tf32 -> f32
low formats -> f16/f32 where specified
f64 -> f64
u8/s8 pair -> s32
u4/s4 pair -> s32
b1 pair + bit op -> s32
scaled low formats -> f32
```

`arith` 不需要 PTX shape enum，但必须表达正确的数值类型组合。

**验收条件**

- compile-time positive/negative matrix；
- A/B signedness 可独立选择的合法组合；
- integer accumulator widening，不在 multiplicand width 中 wrap；
- saturation/status 语义单独验证；
- 删除或改写当前错误的 `int32×int32` golden。

---

## ARITH-P0-007：Block scale 类型组合和布局模型错误

**涉及文件**

- `submod/arith/include/tensor.hpp`
- `submod/arith/test/test_tensor.cpp`

**现状**

`block_scale_view` 只有：

```text
flat span
+ group_elements
```

并按 row-major logical index 做 `index / group_elements`。

这不能表达 PTX 9.3 的 block scaling：

- scale A 是 `M × SFA_N`，按 A 的**每行 chunks**应用；
- scale B 是 `SFB_M × N`，按 B 的**每列 chunks**应用；
- scale matrix shape 由 scale-vector size 决定；
- UE4M3 只适用于 E2M1 的特定 model/scale-vector 组合；
- 普通 E4M3/E5M2/E3M2/E2M3 等 scaled MMA 使用 UE8M0。

当前代码允许任意 low format 搭配 UE4M3，并对 B 使用 `k*N+col` 的 flat row-major grouping，不能保证 column-chunk 语义。

**必须修复**

建立与 instruction spelling 解耦、但能表达数值布局的 descriptor，例如：

```cpp
struct block_scale_layout {
  scale_axis axis;       // row chunks / column chunks
  std::size_t chunks;
  std::size_t elements_per_chunk;
};
```

A/B 分别验证 shape、axis、chunk count 和 scale type capability。不要把 `.kind::*` 文本 enum 放入 `arith`，但必须表达其对应的数值模型。

**验收条件**

- scale A/B 的逻辑矩阵 shape 验证；
- scale B column-chunk 非方阵回归，确保不会被 row-major 假通过；
- UE8M0/UE4M3 与 operand type 的 positive/negative capability tests；
- 1X/2X/4X 等 reference models 的 deterministic golden；
- invalid layout 返回 `invalid_scale_layout`，而非越界或错误结果。

---

## ARITH-P0-008：Scaled MMA 存在 premature F32 overflow/underflow

**涉及文件**

- `submod/arith/include/tensor.hpp`：`detail::scaled_mac`

**现状**

当前实现：

```text
scaled_lhs = round_f32(A * scale_A)
scaled_rhs = round_f32(B * scale_B)
fma_f32(scaled_lhs, scaled_rhs, accumulator)
```

注释只论证了 significand bits 足够，却没有处理 exponent range。即使每个 operand×scale 的 significand 可精确表示，某一侧仍可能先 overflow/underflow，而另一侧的反向 scale 本可在最终 combined product 中抵消。

设计文档要求 tensor kernel 使用 widened/exact product，并禁止 premature low-precision rounding。

**影响**

scaled MMA 可能把本应有限的 combined product 变成 infinity/zero，且该问题会被当前小数值测试完全漏掉。

**必须修复**

- 将 `A × scale_A × B × scale_B` 解码到 widened canonical product；
- 在 exponent/significand 域组合 scale，再与 accumulator 合并；
- 只在 profile 定义的 accumulator rounding point 舍入；
- 不得通过两个独立 F32 `mul` 模拟 widened product。

**验收条件**

- 大 scale × 小 scale 抵消的 overflow regression；
- 小 scale × 大 scale 抵消的 underflow regression；
- exact/widened high-precision oracle；
- status 标记与最终舍入点一致。

---

# 6. P1 — 必须在本里程碑收敛的问题

## ARITH-P1-001：`.rna` / stochastic rounding 只有类型，没有可调用语义

`rounding_mode` 和 `stochastic_rounding_input` 已公开，但所有 public path 都统一拒绝 `nearest_away` 和 `stochastic`，也没有接受 explicit random bits 的 `cvt` overload。

**修复要求**

- capability 明确哪些 conversion 支持 `.rna`、`.rs`；
- stochastic random bits 必须作为显式 operand；
- 不得在 `context` 或 global state 内生成 PRNG；
- unsupported combination 仍返回 `unsupported_rounding`；
- 加固定 random-bit replay vectors。

---

## ARITH-P1-002：Saturation、ReLU 和 subnormal 控制未按 operation capability 落地

public control 中已有：

```text
type_range
zero_to_one
finite
relu
source/destination subnormal
```

但 floating arithmetic、TF32、low conversion 等多个 path 大面积统一拒绝，未形成“operation/type/control capability”。PTX 的 `fma.sat.f32`、多种 `cvt...satfinite/relu` 等不能由当前模型表达为成功路径。

**修复要求**

- control legality 必须由 operation capability 派生；
- implement PTX 9.3 实际需要的 saturation/ReLU forms；
- 不支持的组合精确返回对应 error；
- 处理 NaN 在 `.sat`/`.relu` 下的 PTX 特殊规则；
- control tests 不得只验证“返回 unsupported”。

---

## ARITH-P1-003：内部异常被粗暴映射为 `unsupported_operation`

`environment.cpp` 中 execute/convert helper 捕获所有 `std::invalid_argument` 并返回：

```cpp
arithmetic_error::unsupported_operation
```

这会丢失：

```text
unsupported_rounding
unsupported_subnormal_mode
unsupported_saturation
invalid stochastic input
```

**修复要求**

- backend 前完成 typed validation；
- internal backend 使用不可失败的 validated controls，或返回 typed internal error；
- public error code 一一保留；
- 不应依赖 exception text 判断错误类型。

---

## ARITH-P1-004：Packed operation capability 只看 element type，未看 operation/lane/layout

当前 `packed_operation_capability` 允许所有 `packed_t<f16|bf16|f32, Lanes, Layout>` 执行 add/sub/mul/fma，包括任意 lane count 和非规范 layout。

同时：

- 缺少 UE8M0x2、S2F6x2 等设计/ISA 所需 alias；
- generic `packed_t` 的 `pack/unpack` 假设 element 有 `.bits()` / `from_bits()`，不能支持文档所述的 packed integer subset；
- 没有 per-operation packed capability；
- lane order 与 PTX conversion operand order之间没有明确 adapter contract。

**修复要求**

建立：

```text
packed_format_capability<Element,Lanes,Layout>
packed_operation_capability<Op,Packed>
```

并补齐 storage aliases、padding golden、lane-order 文档及 compile-negative tests。

---

## ARITH-P1-005：`model_profile` 仍是 placeholder，未真正决定数值模型

`approximation_profile` 使用未类型化的 `int model/target_family`；tensor profile 主要是一个 `model_dependent` bool。实际 tensor 算术固定按 K ascending + FMA 执行，profile 只改变 status bit，不能选择 accumulation precision、rounding、NaN 或 subnormal policy。

**修复要求**

- 使用 typed enums/structs；
- profile 必须版本化，例如 `ptx_9_3_reference()`；
- 每个 implementation-defined/unspecified 选择有明确字段；
- profile 改变时要么改变数值路径，要么该字段不应存在；
- `model_dependent=false` 不能在 arithmetic 未证明 target-bit-exact 时使用。

---

## ARITH-P1-006：Approximate functions 未由 profile 驱动，若干实现直接返回 exact SoftFloat

`div_approx`、`div_full`、`rcp_approx`、`sqrt_approx` 直接使用 exact SoftFloat；`rsqrt_approx` 是 exact sqrt 后 exact reciprocal；sin/cos/lg2/ex2/tanh 使用自建 deterministic polynomial。

Exact 结果在部分定义域可能满足误差上界，但当前仍有三项契约缺失：

1. `context.approximation` 没有参与具体结果选择；
2. PTX 特殊定义域、unspecified 区域和 target/model-dependent 结果没有形成明确 profile；
3. 测试未证明整个规范定义域的 error bound。

**修复要求**

- 明确 reference approximation model；
- 逐 operation 建立 PTX corner-case table；
- exact-as-reference 仅在有明确 ADR、状态标记和误差证明时保留；
- 建立 MPFR/高精度或独立 math oracle，仅链接 test target；
- 对 sin/cos 的规范区间和区间外 model-dependent 行为分别测试。

---

## ARITH-P1-007：Public `rsqrt(exact)` 契约不清且是双舍入组合

public special-function API 允许 `approximation_mode::exact` 的 `rsqrt`，但实现是：

```text
rounded sqrt
-> rounded reciprocal
```

这不等价于“正确舍入的 1/sqrt(x)”。PTX 又没有一个普通 exact `rsqrt` contract 可自然对应。

**修复要求**

二选一：

- 删除/禁用 exact rsqrt capability，只保留明确的 PTX approximate model；或
- 定义独立数学 exact contract，并用单次目标舍入的高精度实现及 oracle 验证。

不得把两次正确舍入描述为一次正确舍入。

---

## ARITH-P1-008：Classification/format_info 公共契约不完整

设计要求通用提供：

```text
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

当前缺少 `is_normal`；`format_info_v.has_zero = true` 是 blanket 值；format metadata 中 special capabilities 没有被统一用于 public info。

**修复要求**

- 所有字段来自单一 format descriptor；
- 不允许 `format_info_v` 再写独立事实；
- 加 `is_normal` 和全格式 typed/exhaustive tests；
- 对 implementation-defined encoding 的类型保持 raw API 限制。

---

## ARITH-P1-009：测试 oracle 与 production 共享错误假设

最明显例子是 UE8M0/UE4M3：测试虽然写了“test-only decoder”，但仍手工复制了与 production 相同的错误 special-value 规则。因此 exhaustive 只证明了“两份错误代码一致”。

类似风险也存在于：

- low-format encode/decode；
- tensor capability；
- block scale grouping；
- approximation sample tolerance。

**修复要求**

测试数据来源分层：

```text
PTX table literals / checked-in golden vectors
independent high-precision oracle
SoftFloat differential for IEEE primitives
metamorphic properties
production implementation
```

至少两层独立来源才能确认关键语义。

---

## ARITH-P1-010：测试和 CI 尚未形成设计文档要求的门禁

当前只有少数聚合 test files，覆盖虽有穷举片段，但缺少或不足：

- capability compile-negative suite；
- controls legality matrix；
- signed bit operation suite；
- independent BF16 FMA oracle；
- stochastic replay；
- block-scale axis/layout regression；
- no-premature-rounding tensor corpus；
- approximate full-domain/error-bound tests；
- nested SoftFloat state restoration；
- high-contention thread isolation；
- ASan/UBSan；
- public-header self-contained compile；
- installed/external consumer test。

CI 对 PR 会运行，但 `push` 到 `main` 的 path filter 只覆盖 workflow/vcpkg 等少量路径，普通代码 direct push 或 post-merge 状态没有完整保护。

**修复要求**

参见第 9 节 merge gates。

---

# 7. P2 — 封装、构建和可维护性问题

## ARITH-P2-001：`include/detail` 实际会随 public include tree 暴露

`register_headers` 将整个 `include` 目录映射为 public build include root，测试也直接调用 `detail::dispatch::quantize_tf32`。这使 private dispatch 逐渐成为事实 public API。

**建议**

- public test 只测试 public API；
- private tests 可建 internal test target；
- 将不需要模板实例化的 detail 移到 `src/detail`；
- 对必须位于 header 的 implementation detail 明确“不稳定/不安装”。

---

## ARITH-P2-002：Validation helper 与核心 library 混编

`validation.cpp` 使用 host `float/double` 和 `<cmath>` 做 tolerance comparison；设计允许其用于 validation-only，但该文件当前直接编进 `ptxsim_arith`。

**建议**

拆为：

```text
ptxsim::arith              production semantics
ptxsim::arith_validation   test/diagnostic helpers
```

避免生产依赖者无意链接 host-FP validation code。

---

## ARITH-P2-003：没有 install/export 与 external consumer gate

CMake 设置了 `$<INSTALL_INTERFACE:include>`，但没有完整 install/export/package config。当前只能证明 build-tree symlink include 工作，不能证明：

- 安装后 headers 完整；
- SoftFloat 未泄露到 interface；
- consumer 只链接 `ptxsim::arith` 即可使用 public API。

**建议**

增加 install/export/package config 和最小 external consumer test。

---

## ARITH-P2-004：可移植性契约未明确

顶层启用 `CMAKE_CXX_EXTENSIONS ON`，实现使用 `__int128`。如果项目明确只支持 GCC/Linux，可以接受，但应写入 build contract；若计划支持 Clang/MSVC，则需抽象 wider integer backend。

**建议**

- 在 `project_plan.v2.md` 写明当前支持矩阵；
- CI 至少加入 GCC + Clang；
- 禁止因为 host compiler extension 改变数值语义。

---

## ARITH-P2-005：文件边界与设计逻辑边界开始重新耦合

`scalar.hpp` 和 `environment.cpp` 已承载大量 conversion pair、control bridge、dispatch、TF32、approximation adapter。虽然初期允许合并小文件，但当前结构已使：

- public header 增加格式时必须改动；
- private old-control bridge 长期存在；
- capability、validation、dispatch 分散在多处。

**建议重构边界**

```text
public façade
operation capability
control validation
canonical conversion
format encode/decode
scalar backends
tensor backend
```

重构目标是减少事实源，不是机械增加文件数量。

---

# 8. 文件级审查摘要

| 文件/目录 | 结论 | 主要动作 |
|---|---|---|
| `docs/arith_module_design.md` | 设计方向可继续作为规范 | 暂不降级；补 ADR/implementation status appendix |
| `include/types.hpp` | 基础良好，packed/scale aliases 不完整 | 补 required aliases，限制 packed instantiation |
| `include/detail/format_traits.hpp` | **需优先修复** | UE formats、single source descriptor、`is_normal` |
| `include/concepts.hpp` | **需重构** | 由真实 operation capability 派生 |
| `include/controls.hpp` | 类型基本可保留 | 补 explicit stochastic API 与 capability legality |
| `include/context.hpp` | placeholder | typed/versioned model profile |
| `include/result.hpp` | 外壳可保留 | status composition helper；tensor integer status 重新设计 |
| `include/scalar.hpp` | API 名称可保留，内部需大修 | `mad`、canonical conversion、exact rsqrt、status |
| `include/packed.hpp` | lane engine 可保留 | per-op/lane/layout capability，aliases/tests |
| `include/tensor.hpp` | public logical API 可保留 | 重写 capability、scale layout、widened MAC |
| `include/bit.hpp` | unsigned基础可保留 | signed BFE/BFIND、shiftamt、32/64 contracts |
| `src/detail/softfloat_*` | 方向正确 | 增强错误边界与并发测试 |
| `src/detail/low_precision_backend.cpp` | BF16 core 可保留 | 修正 traits依赖、generic canonical conversion |
| `src/detail/approximation_backend.cpp` | 部分 kernel 可保留 | profile化、domain/error-bound证明 |
| `src/environment.cpp` | 过度聚合 | 去 legacy bridge/异常压缩，拆 dispatch/validation |
| `src/validation.cpp` | validation-only 合理 | 从 production target 拆出 |
| `test/test_conversion.cpp` | 有穷举价值但 oracle 被污染 | 换 independent goldens，加入 negative capability |
| `test/test_scalar.cpp` | 基础回归可保留 | 增 mad/status/rounding/control/signed bit |
| `test/test_special.cpp` | 样例不足 | full-domain/corner/error-bound/profile |
| `test/test_tensor.cpp` | 逻辑 tile 测试框架可保留 | 删除错误 capability golden，重建 scale oracle |
| `submod/arith/CMakeLists.txt` | SoftFloat private/TLS良好 | sanitizers、validation target、install consumer |
| `.github/workflows/linux-ci.yml` | 基础 workflow 可用 | 修 path gate，增加 sanitizers/compiler matrix |

---

# 9. 修复工作流与 Agent 拆分

## 9.1 Lane A — Format truth 与 conversion core

**负责 issue**

```text
ARITH-P0-001
ARITH-P0-002 conversion portion
ARITH-P0-003
ARITH-P1-001
ARITH-P1-002 conversion portion
ARITH-P1-008
ARITH-P1-009 format oracle portion
```

**输入**

- PTX 9.3 format/conversion tables；
- checked-in raw-bit golden vectors；
- 现有 BF16 exact core。

**输出**

1. 单一 format descriptor；
2. 正确 UE8M0/UE4M3；
3. canonical conversion representation；
4. capability 驱动的 generic `cvt`；
5. stochastic explicit operand；
6. independent conversion test suite。

**禁止**

- 再增加 public pairwise conversion branch；
- 用 host cast 实现 production conversion；
- 用 production encode core 生成 golden。

---

## 9.2 Lane B — Scalar、compound status 与 bit semantics

**负责 issue**

```text
ARITH-P0-004
ARITH-P0-005
ARITH-P1-002 arithmetic portion
ARITH-P1-003
ARITH-P1-007
```

**输出**

- reference-profile `mad == fma`；
- legacy model 若保留则独立 profile/kernel；
-统一 status merge；
- signed BFE/BFIND；
-精确 public error mapping；
- exact rsqrt 明确处置。

**可并行性**

可与 Lane A 并行，但最终要基于统一 capability/control validator rebase。

---

## 9.3 Lane C — Packed 与 Tensor

**负责 issue**

```text
ARITH-P0-006
ARITH-P0-007
ARITH-P0-008
ARITH-P1-004
```

**依赖**

必须等待 Lane A 至少完成：

```text
scale format traits
low-format decode
conversion capability
```

**输出**

- PTX 9.3 numeric combination table；
- operation-specific packed capability；
- widened tensor MAC；
- axis-aware block scale layout；
- integer widened accumulator；
- independent tensor oracle corpus。

**禁止**

- 在 `arith` 中加入 PTX shape、lane fragment、TMEM descriptor 或 `.kind::*` spelling model；
- 用两个 rounded F32 scale multiplies替代 widened product。

---

## 9.4 Lane D — Profile、approximation 与 NaN policy

**负责 issue**

```text
ARITH-P1-005
ARITH-P1-006
ARITH-P1-008 profile-related portion
```

**输出**

- typed `model_profile`；
- `ptx_9_3_reference()` 的完整 deterministic choices；
- approximation model/profile；
- PTX corner-case tables；
- model-dependent status 规则；
- NaN policy 与 operation behavior 分离。

---

## 9.5 Lane E — Independent tests、CMake 与 CI

**负责 issue**

```text
ARITH-P1-009
ARITH-P1-010
ARITH-P2-001..005
```

**注意**

Lane E 不能等所有代码改完才开始。第一步应先提交**会在当前分支失败的 independent goldens/negative tests**，为其他 lane 提供红灯。

**输出**

- capability compile tests；
- independent raw vectors；
- sanitizers；
- GCC/Clang matrix；
- public header consumer；
- install/export consumer；
- SoftFloat nested/concurrent stress；
- CI path 修复。

---

## 9.6 Integration Agent

Integration Agent 不实现新语义，只负责：

1. 保证所有 lane 使用同一 capability/format/control 事实源；
2. 删除旧 bridge 和重复实现；
3. 运行全矩阵；
4. 更新 implementation status；
5. 对照本 review 逐项关闭 issue；
6. 生成最终 support matrix。

---

# 10. 推荐 PR 顺序

| 顺序 | PR | 依赖 | Merge 条件 |
|---:|---|---|---|
| 1 | `test: add independent PTX format goldens and negative capability checks` | 无 | 当前分支应出现预期失败 |
| 2 | `arith: correct format descriptors and scale encodings` | PR1 | UE tests/goldens 全绿 |
| 3 | `arith: centralize operation capabilities` | PR2 | trait/runtime 一致性测试全绿 |
| 4 | `arith: introduce canonical conversion pipeline` | PR2/3 | conversion matrix、controls、stochastic 全绿 |
| 5 | `arith: fix mad, compound status and signed bit semantics` | PR3 | scalar/bit differential 全绿 |
| 6 | `arith: make model_profile typed and deterministic` | PR3 | profile behavior tests 全绿 |
| 7 | `arith: repair packed capability and required layouts` | PR3/4 | packed compile/runtime matrix 全绿 |
| 8 | `arith: rebuild tensor capability and widened MAC` | PR2/4/6/7 | tensor exact/no-premature-rounding 全绿 |
| 9 | `arith: bind approximate functions to profile and prove bounds` | PR6 | full corner/error-bound suite 全绿 |
| 10 | `build: split validation, add sanitizers/install/consumer/CI gates` | 可并行，最终 rebase | 全 gate 通过 |
| 11 | `docs: close m4 review and publish support matrix` | 全部 | P0=0，P1=0或有批准 ADR |

不建议由一个 Agent 在单个超大 PR 中同时重写 conversion、tensor 和 tests；这会使 oracle 与 implementation 再次互相迁就。

---

# 11. 必须新增的测试门禁

## 11.1 Configure/build

```text
GCC Debug
GCC Release
Clang Debug
Clang Release
ASan + UBSan
```

## 11.2 Public surface

- 每个 public header 单独 include 可编译；
- umbrella header 可编译；
- external consumer 只链接 `ptxsim::arith`；
- SoftFloat headers/compile definitions 不出现在 interface；
- install/export 后 consumer 可编译运行。

## 11.3 Capability

- compile-positive/negative matrix；
- trait true 必有 runtime success test；
- unsupported scalar low-precision arithmetic无法实例化；
- invalid tensor combinations无法实例化或明确返回 type-combination error；
- packed lanes/layout/operation逐项检查。

## 11.4 Numeric

- ≤16-bit fixed encoding 尽可能 exhaustive；
- IEEE exact operation 对 SoftFloat differential；
- BF16 FMA 对独立 high-precision oracle；
- signed zero、NaN、Inf、subnormal、overflow、underflow、inexact；
- conversion tie、directed、satfinite、ReLU、stochastic；
- signed BFE/BFIND pseudocode vectors；
- tensor widened product、scale axis、compensating exponents；
- approximate full corner table和规范 error bound。

## 11.5 State isolation

- 嵌套 SoftFloat guard；
- exception path 恢复；
- 两线程不同 rounding/subnormal；
- 多线程高迭代、固定 seed、raw-bit replay；
- TSan 可作为后续可选 gate，但 TLS correctness 至少需 deterministic stress。

---

# 12. Merge Gate

分支只有在以下条件全部满足时才可进入正式集成：

- [ ] ARITH-P0-001 至 ARITH-P0-008 全部关闭；
- [ ] public capability 与真实实现一一对应；
- [ ] public `cvt` 不再维护 pairwise type list；
- [ ] UE8M0/UE4M3 使用 independent PTX goldens；
- [ ] `mad` reference profile 与 `fma` bit/status 一致；
- [ ] signed BFE/BFIND 符合 PTX pseudocode；
- [ ] tensor integer/low/scale combinations 符合 matrix data-type表；
- [ ] scaled MMA 无 premature F32 overflow/underflow；
- [ ] stochastic、satfinite、ReLU 的支持范围明确且已测试；
- [ ] approximation profile、corner cases、error bounds 已定义；
- [ ] SoftFloat private/TLS/restore 并发测试通过；
- [ ] GCC/Clang Debug/Release 通过；
- [ ] ASan/UBSan 通过；
- [ ] public/install consumer 通过；
- [ ] 全部测试可从固定 seed/raw bits 单 case 重放；
- [ ] `docs/arith_module_design.md` 的验收清单逐项有代码或测试证据；
- [ ] 全仓库不存在 `ptxsim::fp`、`fp::Environment` 或 `ptxsim_fp`；
- [ ] `project_plan.v2.md` 已替代旧计划作为当前路线图。

---

# 13. Agent 修复提交模板

每个修复 PR 必须在描述中填写：

```markdown
## Review issue
ARITH-P?-NNN

## Numeric capability changed
- operation:
- result type:
- operand types:
- controls:
- profile:

## Rounding points
- exact intermediate:
- first rounding:
- final rounding:

## Special values
- zero/subnormal:
- infinity:
- qNaN/sNaN:
- saturation/ReLU:

## Oracle independence
- specification table/vector source:
- oracle implementation:
- production code shared with oracle: no

## Tests
- positive:
- negative capability:
- boundary:
- randomized seed/replay:

## Dependency audit
- frontend/exec_ir/state/memory dependency added: no
- SoftFloat public exposure added: no
- old fp dependency remaining: no
```

---

# 14. 最终判断

该分支不是失败的架构尝试。它已经完成了最重要的第一步：**从旧 `fp::Environment` 路线切换为与机器状态解耦的 typed arithmetic library**。因此后续工作应当是一次有序的语义收敛，而不是回退。

但当前实现还不能被称为符合 `docs/arith_module_design.md` 或 PTX ISA 9.3 的完成态。尤其是：

```text
UE scale formats
capability truthfulness
canonical conversion
mad rounding point
signed bit semantics
tensor data combinations
block scaling
independent oracle
```

必须在任何 executor 集成前修复。只要按本文的依赖顺序执行，各 lane 可以并行推进，并避免再次产生“测试与实现共同正确地实现了错误规范”的情况。
