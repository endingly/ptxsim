# Historical / V2-M1 Main Review

> **Archive status:** Historical / Superseded main-review artifact.
> **Original:** uploaded `m5-main-review.md`; original body SHA256
> `15a766f50eee090e0aaf1b9d2003a228e467526242855037cd8184119bfc167f`.
> **Reviewed:** `main@13bd652377b2256da05b1b8b4d106c365b9488b6`; baseline
> `main@13bd652377b2256da05b1b8b4d106c365b9488b6`.
> **Milestone mapping:** V2-M1 arithmetic remediation; retained
> `fix/m5-main-review` is pre-merge continuity, not V2-M5.
> **Supersedes:** [legacy m4 review](m4-review.md).
> **Superseded by:** [V2-M1 remediation review](v2-m1-fix-review.md).

---
# `ptxsim` main 分支 Review 与修复指南

> **文档状态：** Active review / remediation guide  
> **审查对象：** `endingly/ptxsim` / `main`  
> **审查基线：** `main@13bd652377b2256da05b1b8b4d106c365b9488b6`  
> **规范基准：** NVIDIA PTX ISA 9.3  
> **设计基准：** `docs/arith_module_design.md`  
> **前序文档：** `m4-review.md`（其针对 `refactor/arith-module` 的结论已被当前 main 状态部分取代）  
> **用途：** 指导后续 Coding Agent 修复 main 分支现存问题，并作为验收与回归测试清单

---

## 1. 总体结论

当前 `main` 已经合入新的 `ptxsim::arith` 数值语义模块，整体架构方向是正确的：

- `arith` 与 PTX instruction / executor / machine state 解耦；
- 强类型数值表示已经建立；
- SoftFloat 保持 private backend；
- BF16 FMA 使用单次目标舍入的精确中间表示；
- conversion 已开始采用 canonical decode → encode 思路；
- packed / tensor / approximate function 均已有初步实现；
- CI 已覆盖 GCC / Clang、Debug / Release、ASan + UBSan、consumer install test；
- 当前 main 对应 Linux CI 已通过。

但是，**“CI 全绿”不能等价于“PTX 9.3 数值语义已正确”**。本次针对 main 的复查仍确认了多项明确缺陷，其中若干会直接产生错误结果、错误能力声明或合法 PTX 形式不可达。

### 1.1 当前判断

```text
架构基础：可继续使用
构建状态：可构建，CI 已通过
arith API 稳定性：尚不足
PTX 9.3 数值正确性：尚不能作为可信 oracle
executor 正式稳定集成：建议等 P0 清零后再锁定接口
```

### 1.2 严重度统计

| 严重度 | 数量 | 定义 |
|---|---:|---|
| P0 | 4 | 明确 PTX 语义错误、合法形式不可达，建议阻止进一步稳定集成 |
| P1 | 10 | 高优先级正确性、capability/API 契约和边界问题 |
| P2 | 6 | 完整性、构建治理、依赖、文档和可维护性问题 |

---

# 2. Agent 工作原则

## 2.1 不要通过修改设计文档来迁就错误实现

`docs/arith_module_design.md` 的基本架构边界应继续保持：

```text
typed numeric values + numeric controls
              ↓
           arith
              ↓
numeric result + numeric status
```

不得为了保留当前错误行为而：

- 在 `arith` 中加入 PTX instruction/opcode/form 建模；
- 把 executor legality 判断重新塞入 `arith`；
- 用 host float/double 替代精确数值路径；
- 让 unsupported PTX form 因为“实现起来方便”而被 public API 接受。

## 2.2 先修 capability / control truth table，再修具体 operation

目前最大的结构性问题是：

```text
public capability
public control validation
internal OperationTraits
backend implementation
```

存在多套规则，彼此已经发生偏差。

任何涉及浮点、approximate、tensor control 的修复，优先建立：

```cpp
operation × type × control feature
```

的单一事实来源，再让 public API 和 backend 都依赖该事实。

## 2.3 每个 bug 必须带独立回归测试

不得仅修改现有 expected value。

尤其以下类型必须使用**独立、固定的 PTX 9.3 golden vector**：

- UE8M0 / UE4M3；
- BFI/BFE/BFIND 边界；
- `.sat` / `.relu`；
- approximate corner cases；
- F64 approximate forms；
- tensor control legality。

## 2.4 错误应在最外层可验证入口被拒绝

非法控制不能依赖：

```text
进入第一个元素
→ 进入某个 scalar backend
→ 最后碰巧报错
```

合法性应在 operation entry point 一次性验证。

---

# 3. P0 — 合并后仍存在的阻断问题

## MAIN-P0-001 — `mad.hi.sat.s32` 合法 PTX 形式不可执行

**涉及文件**

- `submod/arith/include/scalar.hpp`

**当前行为**

整数 `mad` 当前拆成：

```text
mul(a, b, product_control)
→ add(product, c, overflow_control)
```

而 `mul` 明确拒绝：

```cpp
product_part::high + integer_overflow_mode::saturate
```

因此：

```cpp
mad(ctx, a, b, c,
    {.part = product_part::high,
     .overflow = integer_overflow_mode::saturate});
```

会返回 `unsupported_overflow_mode`。

**问题**

PTX 9.3 的 `mad.hi.sat.s32` 是合法形式，其语义应为：

```text
signed 64-bit product
→ 取高 32 位
→ 与 c 做 widened add
→ 对最终 s32 结果做一次 saturation
```

不是 `saturating mul.high → saturating add`。

**必须修复**

建议为整数 MAD 提供专门内核，而不是机械复用通用 `mul + add`。不得为了让该 case 通过而放宽 `mul.high.sat` 本身。

**必须新增测试**

至少包括：

```cpp
a = INT32_MAX
b = INT32_MAX
c = INT32_MAX
```

验证：

- API 不返回 unsupported；
- 最终结果饱和至 `INT32_MAX`；
- `status.overflow == true`；
- 再加入对应的负向溢出测试。

---

## MAIN-P0-002 — `div.approx.f32` 缺少 PTX 大除数特殊语义

**涉及文件**

- `submod/arith/src/detail/approximation_backend.cpp`
- `submod/arith/src/environment.cpp`
- `submod/arith/test/test_special.cpp`

**当前行为**

当前 `div_approx` 基本采用：

```text
approx reciprocal(rhs)
→ multiply(lhs, reciprocal)
```

**问题**

PTX 9.3 对 `div.approx.f32` 的大除数区间存在特殊结果要求。当：

```text
2^126 < abs(divisor) < 2^128
```

时需要先处理：

- finite dividend → signed zero；
- infinite dividend → NaN。

当前实现直接进入 reciprocal + multiply，会产生错误结果。

**明确反例**

```text
a = 1.0f
b = 2^127
```

当前路径可能产生 `2^-127`，规范结果应为 `+0`。

```text
a = +Inf
b = 2^127
```

当前路径可能产生 `+Inf`，规范结果应为 NaN。

**必须修复**

在 reciprocal / multiplication 之前处理 PTX 指定的 raw-bit domain。不要依赖 FTZ 后处理“碰巧”把错误结果变成 0。

**必须新增测试**

```text
±finite / ±large-divisor
±Inf / ±large-divisor
FTZ preserve
FTZ enabled
boundary around 2^126
boundary around 2^128
NaN operands
```

---

## MAIN-P0-003 — F64 approximate backend 存在，但 public API 全部不可达

**涉及文件**

- `submod/arith/include/scalar.hpp`
- `submod/arith/src/environment.cpp`
- `submod/arith/src/detail/backend.hpp`
- `submod/arith/src/detail/approximation_backend.cpp`
- `submod/arith/src/detail/operation_policy.hpp`

**当前状态**

内部已经实现或声明：

```cpp
rsqrt_approx(float64_t)
rcp_approx_ftz(float64_t)
rsqrt_approx_ftz(float64_t)
```

内部 `OperationTraits` 也声明相应能力。

但 public API 当前出现：

```text
rcp<f64> approximate
→ unsupported_operation

rsqrt<f64> preserve
→ unsupported_subnormal_mode

rsqrt<f64> FTZ
→ unsupported_operation
```

没有路径实际调用这些 F64 backend。

**问题**

PTX 9.3 存在对应 F64 approximate forms，例如：

```text
rcp.approx.ftz.f64
rsqrt.approx.f64
rsqrt.approx.ftz.f64
```

这说明 public capability、public dispatch、backend capability 已明显不一致。

**必须修复**

把 F64 approximate legality 纳入统一 capability/control matrix，确保：

```text
backend supports
⇔ public reachable
⇔ tests cover
```

**必须新增测试**

每个合法形式至少测试：

```text
normal
zero
negative zero
positive infinity
negative finite for rsqrt
quiet NaN
signaling NaN
subnormal
```

并验证 preserve / FTZ、result bits/class、status flags、`model_dependent`。

---

## MAIN-P0-004 — 浮点控制合法性只按类型验证，没有按 operation 验证

**涉及文件**

- `submod/arith/include/concepts.hpp`
- `submod/arith/include/scalar.hpp`
- `submod/arith/src/environment.cpp`
- `submod/arith/src/detail/operation_policy.hpp`

**当前结构**

public dispatch 的主要验证逻辑是：

```cpp
validate<T>(floating_control)
```

它只知道 `T`，不知道当前 operation 是 `add/sub/mul/fma/div/sqrt/...`。与此同时 internal `OperationTraits<T, Op>` 又有另一套 legality。

**造成的第一类错误：合法控制被拒绝**

例如合法的 F16 `.sat`、BF16 FMA `.relu` 可能被 blanket validation 拒绝。

**造成的第二类错误：不存在的形式被接受**

例如：

```cpp
sqrt(ctx, x,
     {.saturation = saturation_mode::zero_to_one});
```

当前 generic post-processing 可能执行 `sqrt → clamp [0,1]`，但 PTX 并不存在 `sqrt.sat.f32`。

类似风险也存在于 `div.sat` 和错误 FTZ 组合。

**造成的第三类错误：public 与 internal policy 冲突**

例如 BF16 普通 arithmetic：internal `OperationTraits` 认为不支持 FTZ，但 public dispatch 可手工 `input_ftz/output_ftz`，绕过内部 policy。

**必须修复**

建立一个统一事实来源，例如：

```cpp
template<scalar_operation Op, typename T>
struct floating_operation_control_capability;
```

至少表达：

```text
rounding
FTZ
saturation
activation
approximation
```

修复后必须满足：

```text
capability says supported
⇔ public API accepts
⇔ dispatch can reach implementation
⇔ backend semantics exist
```

**必须新增测试**

建立 capability/control matrix，覆盖 F16/BF16/F32/F64 的主要 scalar operation，并验证合法/非法 controls 以及精确的 `arithmetic_error`。

---

# 4. P1 — 高优先级问题

## MAIN-P1-001 — `tanh.approx.bf16` 的 FTZ 规则错误

**涉及文件**

- `submod/arith/include/scalar.hpp`
- `submod/arith/src/detail/operation_policy.hpp`
- `submod/arith/src/detail/approximation_backend.cpp`
- `submod/arith/test/test_special.cpp`

BF16 `tanh` 当前要求 `flush_input_and_output`，默认 preserve 反而返回 `unsupported_subnormal_mode`，实现还会主动把 BF16 subnormal 输入冲成 zero。

**目标**：F16 / BF16 / F32 `tanh.approx` 的合法 subnormal 控制必须严格按 PTX 9.3 form 建模，不能因为 control struct 有 `subnormal` 字段就默认所有 approximate op 都支持 `.ftz`。

**回归测试**：BF16/F16/F32 的 preserve + subnormal 正向测试，以及非法 FTZ negative tests。

---

## MAIN-P1-002 — UE8M0 下界舍入逻辑错误

**涉及文件**

- `submod/arith/include/detail/canonical_conversion.hpp`
- `submod/arith/src/detail/low_precision_backend.cpp`
- `submod/arith/test/test_conversion.cpp`

UE8M0：

```text
has_zero = false
has_subnormal = false
raw 0x00 = minimum finite = 2^-127
```

当前 generic encode 仍复用 IEEE subnormal boundary 逻辑。对小于最小有限值的输入，例如 `0.75 × 2^-127`，可能错误舍入到 raw `0x01 = 2^-126`，而不是 raw `0x00 = 2^-127`。

**必须修复**：UE8M0 需要显式 no-zero / no-subnormal / minimum-finite endpoint 编码策略。

**测试**：exact min、0.75×min、0.5×min、just-below-min、directed rounding、zero input、negative input。

---

## MAIN-P1-003 — S2F6 对 `-Inf` 的有限饱和端点错误

**涉及文件**

- `submod/arith/include/scalar.hpp`
- `submod/arith/include/detail/canonical_conversion.hpp`
- `submod/arith/test/test_conversion.cpp`

S2F6 range：

```text
[-128/64, +127/64]
```

当前 finite saturation 对正负 infinity 使用相同 magnitude 127，仅改变 sign，导致：

```text
-Inf → -127/64
```

而不是：

```text
-128/64
```

**修复要求**：对二补数不对称范围分别处理正负 endpoint。

---

## MAIN-P1-004 — stochastic rounding capability 过宽，极小 F64 路径随机位失效

**涉及文件**

- `submod/arith/include/concepts.hpp`
- `submod/arith/include/detail/canonical_conversion.hpp`
- `submod/arith/test/test_conversion.cpp`

public capability 当前允许 F64 → 多种低精度格式 stochastic rounding，但 canonical `round_right` 对过大的 discarded shift 无法正确利用 32-bit stochastic threshold，某些极小值路径会退化为固定不进位。

**必须决策**：

- 方案 A：收紧 public PTX capability，只公开 PTX 9.3 确有 `.rs` 的组合；
- 方案 B：保留扩展能力，则正确实现 arbitrary-length discarded fraction 的随机阈值比较。

**测试**：near/far midpoint、extremely tiny F64、random_bits=0、UINT32_MAX、replay determinism。

---

## MAIN-P1-005 — `bit_insert` 没有使用 PTX `pos/len` 低 8 位规则

**涉及文件**

- `submod/arith/include/bit.hpp`
- `submod/arith/test/test_bit.cpp`

BFE 已执行：

```cpp
pos = offset & 0xff;
len = width & 0xff;
```

但 `bit_insert` 直接使用完整参数。

明确错误样本：

```cpp
bit_insert(base, field, 256, 1)
```

PTX 应等价于 `pos=0,len=1`；当前实现会认为 offset 越界。

```cpp
bit_insert(base, field, 0, 256)
```

PTX 中 `len=0`，应 unchanged；当前可能替换整个 word。

**修复要求**：让 BFI 与 BFE 使用同一 operand-normalization helper。

---

## MAIN-P1-006 — `div.approx` / `div.full` 未设置 `model_dependent`

**涉及文件**

- `submod/arith/src/environment.cpp`
- `submod/arith/test/test_special.cpp`

approximate unary 经 `approximate(...)` 会设置：

```cpp
status.model_dependent = true;
```

但 `div_approx` / `div_full` 直接走普通 `execute`，没有设置。

导致：

```text
rcp.approx → model_dependent = true
div.approx → model_dependent = false
```

**修复要求**：统一 unary/binary approximate result status policy。

---

## MAIN-P1-007 — 多个 public overload 的 `Result` 模板参数被静默忽略

**涉及文件**

- `submod/arith/include/scalar.hpp`

存在：

```cpp
template <typename Result = void, arithmetic_integer T>
add(...)
```

但实现始终返回 `T`。例如调用者可能写：

```cpp
auto r = add<float64_t>(ctx, f32a, f32b);
```

代码可编译，但仍得到 F32 结果。

**建议修复**：same-type operation 移除 `Result` 模板参数；mixed-result 使用独立 overload。若暂时保留，至少约束 `Result` 只能为 `void` 或 `T`。

---

## MAIN-P1-008 — capability 把 `bool` 和非 PTX host integral 当成整数算术类型

**涉及文件**

- `submod/arith/include/types.hpp`
- `submod/arith/include/concepts.hpp`

`predicate_t = bool`，但 add/sub/mul capability 使用 `std::integral<T>`，因此会把 bool 声明为算术能力 true；而真正 public `arithmetic_integer` 又排除了 bool。此外 `char/wchar_t` 等 host integral 也可能被误分类。

**修复要求**：建立明确 public integer type set，仅允许 PTX 所建模的固定宽度整数；predicate 单独分类。

---

## MAIN-P1-009 — Tensor control validation 未按 accumulator type 细分

**涉及文件**

- `submod/arith/include/tensor.hpp`
- `submod/arith/test/test_tensor.cpp`

当前 `valid_tensor_control<D>` 过于统一。例如 F16 directed rounding 可能先通过 tensor entry，到第一个 scalar FMA 才失败；I32 的 rounding 字段实际上不适用，却可能被静默忽略。

**修复要求**：按 F16/F32/F64/I32 accumulator 分别验证 control，并保证非法控制在空矩阵、K=0、正常 K 下返回同一错误。

---

## MAIN-P1-010 — `packed_t::operator[]` 越界可触发无效移位

**涉及文件**

- `submod/arith/include/types.hpp`
- packed tests

`operator[]` 无边界检查：

```cpp
bits >> lane_offset(lane)
```

越界 lane 可能导致 shift >= width。

**建议修复**：至少 `static_assert(Lanes > 0)` + debug `assert(lane < Lanes)`，或增加 checked `at()` 并明确 `operator[]` 的 unchecked 前置条件。

---

# 5. P2 — 完整性、构建与治理问题

## MAIN-P2-001 — internal min/max/testp 能力没有形成 public API

internal 已存在 min/max NaN modifiers、`.abs/.xorsign`、3-input F32 min/max、`testp`、较完整 compare operation；public API 却只暴露二输入、空 modifier 的 min/max，没有 `testp`，compare 又单独维护另一套实现。

建议建立 numeric-level public control，但保持 instruction syntax/form legality 在 executor/lowering。

---

## MAIN-P2-002 — vcpkg manifest 无条件拉取尚未使用的依赖

默认依赖当前包含：

```text
ptx-frontend
softfloat
fmt
gtest
```

但当前 production target 主要消费 SoftFloat，lowering 尚未出现。

**建议**：使用 vcpkg feature 分离 core/arith、tests、frontend-lowering，避免默认构建 frontend。

---

## MAIN-P2-003 — ptx_frontend overlay port 在构建阶段执行未锁定 pip 下载

port 会创建 venv 并执行：

```text
pip install -r requirements.txt
```

requirements 中仍有范围依赖而非完整 lock/hash。

风险：构建非完全可复现、离线困难、resolver 随时间变化。应在 frontend 成为正式 production dependency 前引入 lock/hash 或明确的 codegen artifact 策略。

---

## MAIN-P2-004 — CI 存在但 required status checks 未强制

当前 CI 已成功执行，但 main 的 required checks enforcement 尚未成为强制合并门禁。

建议至少强制：

```text
GCC Debug
GCC Release
Clang Debug
Clang Release
GCC ASan + UBSan
installed-package consumer
```

第三方 Actions 也应逐步固定 commit SHA。

---

## MAIN-P2-005 — `types.hpp` 缺少直接 `<bit>` include

`types.hpp` 使用 `std::bit_cast`，但没有直接 include `<bit>`。当前能构建不代表头文件真正 self-contained。

**修复**：补 `#include <bit>`，并保留 public header standalone compile gate。

---

## MAIN-P2-006 — Review / project plan 状态标记已经过时

`m4-review.md` 仍写有 `NOT MERGE READY`、P0 未清零、禁止 executor integration 等针对旧 refactor 分支的状态；project plan 也仍把旧 remediation gate 描述为当前 blocking gate。

**建议**：

1. `m4-review.md` 顶部标记 `Superseded / Historical`；
2. 指向本文件；
3. project plan 更新当前 gate；
4. 建立 issue → fix commit → regression test 映射。

---

# 6. 推荐工作包拆分

## WP-A — Capability / Control Truth Table

**优先级：最高**

负责：

- MAIN-P0-004
- MAIN-P1-001
- MAIN-P1-007
- MAIN-P1-008
- MAIN-P1-009
- MAIN-P2-001 的 API 基础

主要目标：

```text
只保留一套 operation/type/control capability truth
```

建议先完成 WP-A，再让其他 floating / tensor Agent rebase。

## WP-B — Integer / Bit Semantics

可与 WP-A 并行。

负责：

- MAIN-P0-001 `mad.hi.sat.s32`
- MAIN-P1-005 BFI low-8 rules

必须新增独立 PTX golden tests。

## WP-C — Approximate Functions

依赖 WP-A 的 control 结构稳定后完成最终整合。

负责：

- MAIN-P0-002 `div.approx` large-divisor
- MAIN-P0-003 F64 approximate reachability
- MAIN-P1-001 tanh control
- MAIN-P1-006 `model_dependent`

不建议借此重写全部 approximation polynomial；只修明确 PTX form/corner-case contract。

## WP-D — Conversion / Low Precision

可与 WP-B 并行，最终需与 WP-A 的 conversion control capability 对齐。

负责：

- MAIN-P1-002 UE8M0 lower bound
- MAIN-P1-003 S2F6 negative endpoint
- MAIN-P1-004 stochastic rounding
- 低精度独立 golden vectors

## WP-E — Packed / Tensor API

依赖 WP-A。

负责：

- MAIN-P1-009 tensor control
- MAIN-P1-010 packed lane bounds
- MAIN-P2-001 comparison/minmax/testp public completeness（若本里程碑纳入）

## WP-F — Build / Dependency / Governance

可以独立施工。

负责：

- MAIN-P2-002 dependency feature split
- MAIN-P2-003 pip reproducibility
- MAIN-P2-004 required CI
- MAIN-P2-005 header self-contained
- MAIN-P2-006 review / plan state

该工作包不得修改数值算法。

---

# 7. 推荐施工顺序

```text
WP-A capability/control foundation
        |
        +------> WP-C approximate
        |
        +------> WP-E tensor/public controls

WP-B integer/bit  --------------------+
                                      |
WP-D conversion/low-precision --------+--> full regression

WP-F build/governance ----------------+
```

若需要最短路径先解除 executor 集成风险：

```text
1. MAIN-P0-004
2. MAIN-P0-001
3. MAIN-P0-002
4. MAIN-P0-003
5. MAIN-P1-001/002/003/005/006
6. 其余 P1
7. P2
```

---

# 8. 测试与验收门禁

每个工作包完成后不得只运行单文件测试。至少运行：

```bash
cmake --workflow --preset ci-linux-gcc-debug
cmake --workflow --preset ci-linux-gcc-release
cmake --workflow --preset ci-linux-clang-debug
cmake --workflow --preset ci-linux-clang-release
cmake --workflow --preset ci-linux-gcc-asan-ubsan
```

## 8.1 必须继续保留的门禁

```text
public header standalone compilation
build-tree consumer
installed-package consumer
SoftFloat TLS / thread-local tests
sanitizer
```

## 8.2 必须新增的回归类别

### Capability negatives

```text
legal op + legal control → accepted
legal op + illegal control → exact expected error
unsupported type → unavailable or unsupported_type_combination
```

### Fixed raw-bit goldens

至少新增：

```text
UE8M0 minimum and below-minimum
UE4M3 NaN/max finite
S2F6 endpoints
TF32 boundaries
BFI pos/len wrap
F64 approximate corner cases
```

### Error contract

不要只写：

```cpp
EXPECT_FALSE(result);
```

应尽量验证：

```cpp
EXPECT_EQ(result.error(), arithmetic_error::...);
```

---

# 9. Error taxonomy 建议

当前 error enum 已具备不错基础，但映射不统一。建议建立：

```text
operation/type form 不支持
→ unsupported_operation / unsupported_type_combination

control 不适用于已支持的 operation
→ 对应 unsupported_* control error

context/profile 不可用
→ unsupported_model_profile

请求 approximate/exact mode 本身非法
→ unsupported_approximation_mode
```

不要把 profile 不支持随机映射为 `unsupported_operation`，也不要把 FTZ 控制非法压成 `unsupported_approximation_mode`。executor 后续需要依赖这些 error 做诊断。

---

# 10. Agent 提交要求

每个修复 PR / Agent patch 建议包含：

```text
Issue IDs:
  MAIN-P?-xxx

PTX 9.3 rule:
  简短描述规范事实

Old behavior:
  当前错误

New behavior:
  修复结果

Tests:
  新增哪些独立回归
```

以下方式不能作为问题关闭条件：

- 仅修改测试 expected；
- 仅扩大 `if constexpr` 分支但保留两套 capability；
- catch internal exception 后统一映射 `unsupported_operation`；
- 把错误语义写进 `arith_module_design.md` 使实现“变正确”；
- 用 host `float` / `double` 快速补丁替代精确 backend；
- 因 executor 尚未使用某 API 就删除测试；
- 用 FTZ 后处理掩盖前面错误的 approximate result。

---

# 11. P0 关闭条件

只有满足以下全部条件，才建议把 `arith` 视为 executor 可稳定依赖的 v0.x 数值基础：

- [ ] `mad.hi.sat.s32` 正确执行；
- [ ] `div.approx.f32` 大除数特殊语义正确；
- [ ] F64 approximate 合法形式 public 可达；
- [ ] operation/type/control capability 已单一事实化；
- [ ] 所有 P0 有独立 regression；
- [ ] GCC Debug/Release 全绿；
- [ ] Clang Debug/Release 全绿；
- [ ] ASan + UBSan 全绿；
- [ ] build-tree consumer 全绿；
- [ ] installed consumer 全绿；
- [ ] public header standalone compile 全绿。

---

# 12. 后续架构要求

P0/P1 完成后，再开展 executor / `exec_ir` 主线时，应坚持：

```text
frontend / lowering:
    判断 PTX instruction form 合法性
    ↓
executor:
    将 instruction 映射为 operation + typed values + controls
    ↓
arith:
    只执行数值语义
```

不要让修复过程中重新出现：

```text
arith::evaluate(ptx_opcode, ...)
arith::instruction_form
arith 内建 PTX modifier parser
```

当前 `arith` 最大的价值就是边界已经基本建立。后续修复应让**能力真值、数值真值、测试真值**收敛，而不是重新扩大模块职责。

---

# 13. Review 限制

本次 main Review 基于：

- main 源码逐文件静态审查；
- public API / backend / capability 交叉检查；
- tests / CMake / vcpkg overlay / CI 配置检查；
- PTX ISA 9.3 语义对照；
- GitHub Actions 状态核对。

main 对应 Linux CI 已通过。

本次环境未重新 clone 仓库执行本地 CMake / CTest，因此所有 Agent 修复在提交前仍必须在干净环境执行完整 workflow。

---

## 最终结论

`main@13bd652` 已经从原先 `refactor/arith-module` 的“架构未定型状态”进入了“架构可用、语义需要继续收敛”的阶段。

下一阶段不应推倒 `arith` 重写，而应围绕：

```text
capability truth
control legality
PTX corner cases
low-precision boundaries
public/backend consistency
independent regression tests
```

进行定点修复。

**最高优先级是 MAIN-P0-004。** 只有 operation × type × control 的单一真值源建立后，后续 approximate、tensor、packed 和 executor 才不会继续累积第二轮 capability 漂移。
