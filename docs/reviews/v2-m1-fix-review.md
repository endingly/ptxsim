# Historical / V2-M1 Remediation Record

> **Archive status:** Historical / Superseded remediation snapshot.
> **Original:** `m5-fix-review.md`; original report body SHA256
> `077ab14ccfe482411e563ee491c021187e49b78ed9cad886f9b08464660aea2e`.
> **Reviewed:** `fix/m5-main-review@24dd2308ef7869f950e6f956bb88544194a2059a`;
> baseline `main@13bd652377b2256da05b1b8b4d106c365b9488b6`.
> **Milestone mapping:** V2-M1 arithmetic remediation; retained branch name is
> pre-merge continuity, not V2-M5.
> **Supersedes:** [V2-M1 main review](v2-m1-main-review.md).
> **Superseded by:** [V2-M1 rereview](v2-m1-fix-rereview.r1.md).

---
# `fix/m5-main-review` 分支修复验收 Review

> **结论：NOT MERGE READY**  
> **审查对象：** `endingly/ptxsim` / `fix/m5-main-review`  
> **比较基线：** `main@13bd652377b2256da05b1b8b4d106c365b9488b6`  
> **审查快照：** `fix/m5-main-review@24dd2308ef7869f950e6f956bb88544194a2059a`  
> **规范基准：** NVIDIA PTX ISA 9.3  
> **前序 Review：** `m5-main-review.md`  
> **审查日期：** 2026-08-30  
> **用途：** 指导后续 Agent 完成剩余修复、纠正错误回归测试，并作为最终合并验收清单

---

## 1. 总体结论

该分支相对 `main` 前进 19 个提交、无落后提交，修改覆盖前序 `m5-main-review.md` 中除 GitHub branch protection 外的全部代码、测试、构建及文档事项。提交按照 `MAIN-P0/P1/P2-*` 编号逐项组织，修复范围清晰，整体质量明显高于原 `main`。

值得肯定的变化包括：

- 建立了较统一的 `operation × type × control` capability；
- `mad.hi.sat.s32` 改为专门的最终结果饱和路径；
- 补上了 `div.approx.f32` 大除数特殊语义；
- F64 approximate form 已接入 public API；
- `tanh.approx` 的 preserve/FTZ 能力得到收紧；
- UE8M0 下界、stochastic capability、BFI 低 8 位规则得到修正；
- Tensor control 在入口按 accumulator type 验证；
- packed lane 增加 debug 边界保护；
- min/max modifier、三输入 min/max、`testp` 形成 public API；
- 默认 vcpkg 依赖已缩小，测试和 frontend 迁入 feature；
- ptx_frontend overlay 不再在普通 port build 中执行 pip resolver。

但是，交叉审查仍发现两个会直接产生错误 PTX 数值结果的阻断问题：

1. **F64 `.approx.ftz` 在截取 `a[63:32]` 之前按完整 F64 判断 NaN。**
2. **S2F6 `.satfinite` 只修复了 NaN/Inf 端点，有限但越界的输入仍会回绕。**

更严重的是，这两个问题中的第一个已经被新增测试写成了错误 oracle；第二个则被测试范围遗漏。由此说明，当前分支不能仅凭新增测试数量或本地“全绿”声明判定为正确。

此外：

- 分支 head 没有可核验的 GitHub Actions run 或 commit status；
- `MAIN-P2-004` branch protection 仍未完成；
- generated snapshot 的“byte check”目前只有手工说明，没有自动门禁；
- 历史 `m4-review.md` 被直接删除，审计链被破坏；
- project plan 在上述问题关闭前提前声明 locally actionable scope 已全部完成。

### 1.1 前序问题关闭统计

| 状态 | 数量 |
|---|---:|
| PASS | 15 |
| PARTIAL | 3 |
| OPEN | 1 |
| FAIL | 1 |

### 1.2 当前严重度

| 严重度 | 数量 | 含义 |
|---|---:|---|
| P0 | 2 | 明确错误数值语义，合并前必须修复 |
| P1 | 1 | 诊断状态契约需与数值修复一并收敛 |
| P2 | 3 | CI 证据、生成物门禁和历史文档治理问题 |

---

# 2. 审查范围与限制

## 2.1 已审查内容

本次覆盖：

```text
main...fix/m5-main-review 全部 19 个提交
35 个 changed files
public capability/control API
scalar / conversion / approximate / tensor / packed 实现
新增和修改的单元测试
CMakePresets
Linux CI workflow
vcpkg manifest features
ptx_frontend overlay port 和 generated snapshot
project plan / review 文档收尾
```

重点采用以下交叉检查：

```text
前序 issue
→ fix commit
→ production implementation
→ public reachability
→ regression test
→ PTX ISA 9.3
```

## 2.2 动态验证限制

当前分支 head：

```text
24dd2308ef7869f950e6f956bb88544194a2059a
```

没有可核验的 GitHub Actions workflow run，也没有 commit status。当前 workflow 只在：

```text
pull_request
push to main
monthly schedule
```

触发；该分支尚无对应 PR。

本次环境未能取得可供本地 CMake/CTest 使用的完整 checkout，因此：

- 本文完成了全量静态 review 和规范交叉检查；
- 未在本地重新执行五套 CMake workflow；
- 文档中“Luna 已运行五套 workflow”的说法没有可审计日志支撑；
- 最终修复仍必须通过 hosted CI 和干净 checkout 验证。

---

# 3. 前序 `m5-main-review.md` 关闭矩阵

| 前序 Issue | 状态 | 验收结论 |
|---|---|---|
| `MAIN-P0-001` | **PASS** | `mad.hi.sat.s32` 已采用专门 widened product/high/add/final clamp 路径，并增加正负溢出测试 |
| `MAIN-P0-002` | **PASS** | `div.approx.f32` 已处理 `2^126 < abs(b) < 2^128` 的 finite/Inf 特殊语义，并覆盖符号、边界和 FTZ |
| `MAIN-P0-003` | **PARTIAL** | F64 approximate public reachability已修复；但 `.approx.ftz.f64` 输入投影顺序仍错误，见 `FIX-P0-001` |
| `MAIN-P0-004` | **PASS** | public capability、dispatch 和 internal `OperationTraits` 已大幅统一，原先按类型 blanket validation 的主要问题关闭 |
| `MAIN-P1-001` | **PASS** | F16/BF16/F32 `tanh.approx` 已收紧为 preserve-only，并覆盖 subnormal |
| `MAIN-P1-002` | **PASS** | UE8M0 无 zero/no-subnormal 下界已单独处理，低于最小有限值不再跳到 raw `0x01` |
| `MAIN-P1-003` | **PARTIAL** | `-Inf → S2F6` 负端点已改为 `-128/64`；有限越界 `.satfinite` 仍错误，见 `FIX-P0-002` |
| `MAIN-P1-004` | **PASS** | stochastic rounding 已收紧到可实现的 F32 source forms，F64 路径被拒绝，并增加 replay/threshold 测试 |
| `MAIN-P1-005` | **PASS** | BFI 与 BFE 共用 low-8 operand normalization，256/257 边界已覆盖 |
| `MAIN-P1-006` | **PASS** | `div.approx` / `div.full` 成功结果已统一设置 `model_dependent` |
| `MAIN-P1-007` | **PASS** | 同类型 operation 的无效 `Result` 模板参数已去除或约束，新增 compile-time negative tests |
| `MAIN-P1-008` | **PASS** | `arithmetic_integer` 已收紧到明确的 8/16/32/64-bit signed/unsigned type set；predicate 单独分类 |
| `MAIN-P1-009` | **PASS** | Tensor control 已按 accumulator type 在入口验证，不再依赖执行第一个 MAC 后才报错 |
| `MAIN-P1-010` | **PASS** | `packed_t` 增加 `Lanes > 0` 和 debug lane assertion |
| `MAIN-P2-001` | **PASS** | public min/max modifier、三输入 F32 min/max、`testp` 和完整 checked dispatch 已接入 |
| `MAIN-P2-002` | **PASS** | 默认 manifest 仅依赖 SoftFloat；GTest 和 ptx_frontend 分别迁入 `tests` / `frontend-lowering` feature |
| `MAIN-P2-003` | **PARTIAL** | pip resolver 已从 port build 移除；generated snapshot 有 provenance 说明，但没有自动 byte-regeneration gate |
| `MAIN-P2-004` | **OPEN** | GitHub branch protection / required checks 仍是外部待办 |
| `MAIN-P2-005` | **PASS** | `types.hpp` 已直接包含 `<bit>`，header self-contained 问题关闭 |
| `MAIN-P2-006` | **FAIL** | stale gate 文本已更新，但旧 review 被直接删除且 closure 提前声明，历史审计链处理不合格 |

---

# 4. P0 — 合并阻断问题

---

## FIX-P0-001 — F64 `.approx.ftz` 在截取 upper word 之前错误判断 NaN

### 涉及文件

```text
submod/arith/src/detail/approximation_backend.cpp
submod/arith/test/test_special.cpp
```

### PTX 9.3 规则

对于：

```text
rcp.approx.ftz.f64
rsqrt.approx.ftz.f64
```

PTX 语义首先执行：

```text
tmp = a[63:32]
```

将 `a` 的高 32 位作为 1.11.20 格式输入，**低 32 位完全忽略**；随后才对 `tmp` 做分类、FTZ 和近似运算。

规范位置：

```text
PTX ISA 9.3 §9.7.3.14  rcp.approx.ftz.f64
PTX ISA 9.3 §9.7.3.17  rsqrt.approx.ftz.f64
```

### 当前实现顺序

当前 helper 的逻辑顺序是：

```text
flush full F64 subnormal
→ classify full F64 NaN
→ mask low 32 bits
→ execute
```

即先执行完整 F64 的 `is_nan(value)`，之后才：

```text
value.bits() & 0xFFFFFFFF00000000
```

### 明确反例

输入：

```text
a = 0x7ff0000000000001
```

按完整 IEEE F64：

```text
signaling NaN
```

但 PTX 要先取：

```text
a[63:32] = 0x7ff00000
```

在 1.11.20 格式中这是：

```text
+Inf
```

因此正确结果为：

| Operation | 正确结果 | invalid |
|---|---:|---:|
| `rcp.approx.ftz.f64` | `+0` | false |
| `rsqrt.approx.ftz.f64` | `+0` | false |

当前实现却返回：

```text
canonical NaN 0x7fffffff00000000
invalid = true
```

### 测试 oracle 也写错

新增的 `F64ApproximateForms` 测试把：

```text
0x7ff0000000000001
```

同时作为 RCP/RSQRT FTZ 的 signaling NaN case，期待 canonical NaN 和 invalid。

这不是单纯缺少测试，而是：

```text
production implementation
+ regression test oracle
```

共享了相同错误假设。

### 必须修复

输入处理必须调整为：

```text
1. raw mask：保留 a[63:32]，低 32 位清零
2. 对投影后的 1.11.20 值做 classification
3. 对投影后的 subnormal 做 sign-preserving FTZ
4. 执行 reference approximation
5. 输出低 32 位清零
```

建议建立专门 helper，例如：

```text
project_f64_to_ptx_1_11_20()
```

不要再复用“先按 full F64 classification、后截断”的普通路径。

### 必须新增/修改测试

至少加入：

```text
0x7ff0000000000001
  high word = +Inf
  low-only payload must be ignored

0xfff0000000000001
  high word = -Inf
  rcp -> -0
  rsqrt -> canonical NaN + invalid

0x7ff0000100000000
  high word itself is signaling NaN
  -> canonical NaN + invalid

0x7ff8000000000001
  high word itself is quiet NaN
  -> canonical NaN, invalid=false

positive/negative high-word subnormal
positive/negative zero
normal values with arbitrary low 32 bits
```

其中 oracle 必须只从：

```text
high 32-bit 1.11.20 fields
```

独立构造，不能调用 production F64 classifier。

### 验收条件

- low-only NaN payload 不影响结果和 invalid；
- high-word NaN 仍正确 canonicalize；
- RCP 和 RSQRT 共用同一 input projection；
- 现有错误 expected 被替换，而不是通过特殊 case patch production；
- F64 preserve `rsqrt.approx.f64` 路径不受影响。

---

## FIX-P0-002 — S2F6 `.satfinite` 对有限越界输入仍发生二补数回绕

### 涉及文件

```text
submod/arith/include/scalar.hpp
submod/arith/include/detail/canonical_conversion.hpp
submod/arith/test/test_conversion.cpp
```

### PTX 9.3 规则

`.s2f6x2` 是两个 signed fixed-point S2F6 lane：

```text
raw signed int8 / 2^6
range = [-128/64, +127/64]
      = [-2.0, 1.984375]
```

PTX 的 S2F6 目标 conversion 形式使用：

```text
cvt.rn.satfinite{.relu}...s2f6x2...
```

且 `.satfinite` 对该目标是 mandatory。

规范同时规定：

```text
abs(input) > MAX_NORM
→ sign-preserved MAX_NORM
```

### 当前修复覆盖范围

`MAIN-P1-003` 只在 `apply_destination_controls` 中处理：

```text
NaN
+Inf
-Inf
```

将其预先改成 endpoint。

有限数值不会进入这个特殊分支。

### 当前有限越界路径

以：

```text
+3.0 → S2F6
control.saturation = finite
```

为例：

```text
3.0 × 64 = 192
```

随后 `encode_fixed` 调用 `encode_integer<int8_t>`。

`encode_integer` 发现越界后只在：

```text
saturation == type_range
```

时钳制。对于：

```text
saturation == finite
```

它设置诊断位后继续将 magnitude 截到 8 位。

在当前 Linux 二补数目标上：

```text
192 -> 0xC0 -> int8 -64
```

最终得到：

```text
-64 / 64 = -1.0
```

正确 PTX 数值应是：

```text
+127 / 64 = +1.984375
```

负向反例：

```text
-3.0
```

也可能回绕成正值，而不是饱和到：

```text
-128 / 64 = -2.0
```

### 为什么现有测试没有发现

现有测试：

```text
S2F6FiniteSaturationEndpoints
```

只覆盖：

```text
+Inf
-Inf
NaN
```

另一个有限 `+3.0` 测试使用的是：

```text
saturation_mode::type_range
```

而不是 PTX S2F6 form 对应的：

```text
saturation_mode::finite
```

因此刚好绕开了错误路径。

### 必须修复

不要只在 public pre-control 中把 non-finite 改成 endpoint。

建议将 S2F6 的完整 target encoding policy 收敛到 `encode_fixed`：

```text
decode canonical number
→ apply relu（若有）
→ round to S2F6 quantum
→ if finite out of range: clamp to signed endpoint
→ if ±Inf: clamp to signed endpoint
→ if NaN: positive MAX_NORM
→ emit value + diagnostics
```

可复用 integer encoder，但必须显式将 S2F6 `.finite` 映射到该格式的 endpoint clamp，不能依赖普通 int8 wrap path。

### 诊断状态问题

当前 NaN/Inf 在进入 encoder 前被改写成普通 finite endpoint，因此 encoder 看不到原始 classification，新增测试还将：

```text
invalid
overflow
inexact
```

全部锁定为 false。

PTX 指令本身不暴露 IEEE flags，但 `ptxsim::arith::floating_status` 是库级诊断契约。当前 F16 `.satfinite` 对 infinity 会报告 overflow/inexact，而 S2F6 对同类 saturation 完全无状态，行为不一致。

修复时必须明确选择并文档化统一策略。推荐：

```text
finite overflow / ±Inf clamp:
  overflow=true
  inexact=true

NaN -> positive MAX_NORM:
  invalid=true
  inexact=true
```

若项目决定采用不同诊断策略，也必须：

- 在设计文档中明确；
- 保留原始 classification；
- 与其他 finite-only destination 统一；
- 不允许通过 pre-transform 静默丢失信息。

### 必须新增测试

数值至少覆盖：

```text
+3.0 + .finite -> rep 127
-3.0 + .finite -> rep -128
+1.984375 exact -> rep 127
-2.0 exact -> rep -128
just above +MAX_NORM -> rep 127
just below -MIN_NORM -> rep -128
+Inf -> rep 127
-Inf -> rep -128
NaN -> rep 127
negative + relu -> rep 0
```

控制测试应区分：

```text
finite      // PTX satfinite
type_range  // 若保留，属于 library-level extension
none        // 不代表合法 PTX instruction form
```

### 验收条件

- 所有 finite out-of-range 输入均不回绕；
- 正负 endpoint 不对称性正确；
- NaN/Inf 和 finite overflow 使用同一 S2F6 encode policy；
- PTX regression 使用 `.finite`，不再以 `.type_range` 代替；
- diagnostics 不再因 public pre-transform 丢失。

---

# 5. P1 — 诊断契约问题

---

## FIX-P1-001 — S2F6 saturation status 与其他目标格式不一致

该问题与 `FIX-P0-002` 同源，建议在同一提交中处理。

当前新增测试明确要求：

```text
+Inf → S2F6 endpoint
-Inf → S2F6 endpoint
NaN  → S2F6 endpoint
```

且所有状态均为 false。

与此同时，现有 F16 `.satfinite` 测试要求：

```text
overflow = true
inexact = true
```

这使同一个 public `conversion_control::finite` 对不同 destination 产生难以解释的诊断差异。

### 必须决策

在实现前写清楚：

```text
floating_status 表示 IEEE architectural flags
还是
floating_status 表示 arith 数值诊断
```

当前项目文档更接近后者，因此建议 saturation 事件可观察。

### 验收条件

- S2F6、FP4/FP6/FP8、F16/BF16/TF32 的 saturation diagnostics 有统一规则；
- test 不再仅为配合现有 implementation 把所有 flag 写死为 false；
- executor 可明确选择忽略 status，但 status 本身不能无规则漂移。

---

# 6. P2 — 构建、CI 与文档治理问题

---

## FIX-P2-001 — 分支 head 没有可核验 hosted CI

### 当前状态

分支 head：

```text
24dd2308ef7869f950e6f956bb88544194a2059a
```

没有 GitHub Actions run，也没有 combined commit status。

原因是 workflow 只监听：

```yaml
pull_request
push:
  branches: [main]
schedule
```

当前分支没有对应 PR。

### 风险

本分支同时修改了：

```text
C++ capability/dispatch
large header templates
SoftFloat wrapper
tests
CMake presets
vcpkg features
ptx_frontend overlay generated source
```

静态 review 不能证明：

```text
GCC/Clang 都能实例化
Release/Debug 都能链接
sanitizer 没有失败
installed consumer 正常
frontend-lowering feature 能安装
```

### 必须完成

在声明 merge-ready 前：

1. 建立 PR 触发 hosted matrix；
2. 确认五个 job 全绿：
   ```text
   GCC Debug
   GCC Release
   Clang Debug
   Clang Release
   GCC ASan + UBSan
   ```
3. 确认 consumer/install tests 实际运行；
4. 对 frontend feature 增加独立 configure/install smoke test；
5. 将 hosted job 设为 `main` required checks（`MAIN-P2-004`）。

本问题不是说本地 workflow 一定失败，而是当前没有可审计证据。

---

## FIX-P2-002 — generated snapshot 只有手工复现说明，没有自动 byte check

### 涉及文件

```text
.ports/ptx-frontend/generated/**
.ports/ptx-frontend/generated/README.md
.ports/ptx-frontend/portfile.cmake
.agents/project_plan.md
```

### 已完成的正确改进

overlay port 已：

- 固定 ptx_frontend commit；
- 固定 source archive SHA512；
- 移除 venv/pip/jsonschema/PyPI resolver；
- 将 generated public/private files 与 port 一起保存；
- 在 README 中给出 regeneration + `diff -ruN` 方法。

这些变化确实改善了普通构建的离线性和确定性。

### 剩余问题

project plan closure map 写的是：

```text
generated snapshot byte check
```

但仓库中实际只有手工步骤，没有：

```text
CTest
CMake target
CI job
hash manifest verification
regenerate-and-diff workflow
```

因此当前状态更准确的描述是：

```text
manual reproducibility procedure
```

而不是自动 gate。

### 建议处理

二选一：

#### 方案 A：增加自动 regeneration gate

在独立、非默认 job 中：

```text
checkout pinned ptx_frontend revision
使用固定生成环境
regenerate
byte-for-byte diff
```

该 job 可在：

```text
overlay port 变更
generated snapshot 变更
pinned frontend revision 变更
```

时触发。

#### 方案 B：暂时只做完整性 hash gate

提交：

```text
SHA256SUMS
```

CI 验证 snapshot 内部文件未被意外修改，同时将 project plan wording 改成：

```text
snapshot integrity check + documented manual regeneration
```

注意：hash gate 只能证明仓库内一致性，不能证明 snapshot 与 upstream generator 一致。

### 验收条件

- closure map 与实际门禁一致；
- pinned frontend revision 变化时必须强制更新 snapshot/provenance；
- snapshot 内容不能只依赖 reviewer 手工执行 README 命令。

---

## FIX-P2-003 — 删除 `m4-review.md` 破坏历史审计链，closure 声明过早

### 当前修改

最终文档提交：

- 更新 `.agents/project_plan.md`；
- 直接删除根目录 `m4-review.md`；
- 宣称 locally actionable MAIN P0/P1/P2 items 已全部完成；
- 声称五套 local workflow 已作为 closure gate 执行。

### 问题一：历史 Review 不应删除

`m4-review.md` 记录了：

```text
原 refactor branch 的错误
修复优先级
设计决策
测试 oracle 问题
后续 remediation 来源
```

它是当前 19 个修复提交的审计来源。

正确处理应是：

```text
保留
→ 移到 docs/reviews/ 或 reviews/
→ 顶部标记 Historical / Superseded
→ 指向 m5-main-review 和本次 fix review
```

直接删除会使后续 Agent 无法回答：

```text
某段实现为何存在
某项 capability 为什么被收紧
哪项旧错误已经关闭
```

### 问题二：closure 声明与当前事实冲突

当前仍有：

```text
FIX-P0-001
FIX-P0-002
MAIN-P2-004
hosted CI evidence 缺失
```

所以 project plan 中：

```text
Arithmetic conformance is now complete
all locally actionable items are fixed
```

应撤回。

### 必须修复

建议建立：

```text
docs/reviews/m4-review.md
docs/reviews/m5-main-review.md
docs/reviews/m5-fix-review.md
```

每份文档顶部记录：

```text
status
reviewed branch/SHA
supersedes / superseded-by
```

project plan 应只记录当前 gate，不应复制完整 review 内容。

### 验收条件

- 恢复历史 review；
- 当前 review 被纳入审计链；
- P0 和 hosted CI 通过前，不得标记 V2-M1 complete；
- closure map 的 commit/test/status 与真实仓库一致。

---

# 7. 建议工作包拆分

剩余问题规模不大，不应推倒本分支重做。

## WP-A — F64 PTX 1.11.20 Projection

**负责：**

```text
FIX-P0-001
```

**修改范围：**

```text
approximation_backend.cpp
test_special.cpp
可选新增 test-only independent 1.11.20 classifier
```

**禁止顺手修改：**

```text
F32 polynomial kernels
general SoftFloat backend
F64 preserve rsqrt profile
```

**交付：**

- projection helper；
- corrected test oracle；
- low-only payload vectors；
- high-word qNaN/sNaN vectors。

---

## WP-B — S2F6 Encode Policy

**负责：**

```text
FIX-P0-002
FIX-P1-001
```

**修改范围：**

```text
canonical_conversion.hpp
scalar.hpp 中 S2F6 pre-control（应简化或移除）
test_conversion.cpp
必要的 design status policy
```

**推荐设计：**

```text
所有 S2F6 destination classification/range handling
统一放入 encode_fixed
```

**交付：**

- finite overflow clamp；
- NaN/Inf endpoint；
- diagnostic merge；
- `.finite` golden vectors；
- `.type_range` extension 语义隔离。

---

## WP-C — CI / Snapshot / Review Governance

**负责：**

```text
FIX-P2-001
FIX-P2-002
FIX-P2-003
MAIN-P2-004
```

**交付：**

- 建 PR 并取得 hosted matrix；
- frontend feature smoke test；
- generated snapshot 自动 integrity/regeneration gate；
- 恢复历史 review；
- 修正 project plan completion 状态；
- 配置 required checks。

该工作包不得修改数值算法。

---

## Integration Agent

负责：

1. 合并 WP-A/WP-B；
2. 运行全部 arith tests；
3. 运行五套 workflow；
4. 运行 frontend feature smoke test；
5. 确认 project plan 只在验证完成后关闭 gate；
6. 出具最终 issue closure matrix。

---

# 8. 强制回归向量

## 8.1 F64 approximate FTZ

```text
rcp.approx.ftz.f64
  0x7ff0000000000001 -> 0x0000000000000000, invalid=false
  0xfff0000000000001 -> 0x8000000000000000, invalid=false

rsqrt.approx.ftz.f64
  0x7ff0000000000001 -> 0x0000000000000000, invalid=false
  0xfff0000000000001 -> canonical NaN, invalid=true

high-word signaling NaN
  -> canonical NaN, invalid=true

high-word quiet NaN
  -> canonical NaN, invalid=false
```

测试 oracle 应先：

```text
upper = input >> 32
```

再独立解析 1.11.20 fields。

## 8.2 S2F6 `.satfinite`

```text
+3.0                -> rep  127
-3.0                -> rep -128
+1.984375            -> rep  127
-2.0                 -> rep -128
just above +endpoint -> rep  127
just below -endpoint -> rep -128
+Inf                 -> rep  127
-Inf                 -> rep -128
NaN                  -> rep  127
negative + ReLU      -> rep    0
```

必须使用：

```cpp
.saturation = saturation_mode::finite
```

不能用 `type_range` 代替 PTX `.satfinite` regression。

---

# 9. 最终合并门禁

只有以下全部满足，才建议合入 `main`：

## 数值语义

- [ ] F64 FTZ 在 classification 前截取 `a[63:32]`
- [ ] low-only F64 payload 被正确忽略
- [ ] S2F6 finite overflow 不再回绕
- [ ] S2F6 diagnostics 已定义并统一
- [ ] 两个错误 test oracle 已纠正

## 编译与测试

- [ ] GCC Debug
- [ ] GCC Release
- [ ] Clang Debug
- [ ] Clang Release
- [ ] GCC ASan + UBSan
- [ ] build-tree consumer
- [ ] installed-package consumer
- [ ] public header standalone compile
- [ ] `frontend-lowering` feature configure/build/install smoke test
- [ ] generated snapshot integrity/regeneration gate

## GitHub 治理

- [ ] PR 对最终 head 产生 hosted CI run
- [ ] `main` required checks 启用
- [ ] branch protection 完成
- [ ] 历史 review 恢复并标记 superseded
- [ ] project plan 不再提前声明 completion

---

# 10. 不接受的修复方式

以下方式不能关闭本次问题：

- 仅删除 `0x7ff0000000000001` 测试；
- 对该常量增加 hard-coded special case；
- 保留 full-F64 NaN classification，只在结果端覆盖；
- 将 S2F6 `+3.0` expected 改成回绕值；
- 用 `.type_range` 测试代替 `.satfinite`；
- 继续在 `apply_destination_controls` 中逐个列举更多 S2F6 输入；
- 在没有 hosted CI evidence 时只修改 project plan 为 complete；
- 用删除旧 review 的方式消除 stale status；
- 将 manual README 命令描述成 automatic byte check。

---

# 11. 值得保留的实现

后续 Agent 不应推倒以下工作：

- operation/type/control capability 的集中化；
- internal `OperationTraits` 向 public capability 对齐；
- `mad.hi.sat.s32` 的专门路径；
- `div.approx.f32` 大除数 domain；
- approximate `model_dependent` helper；
- preserve-only tanh capability；
- UE8M0 no-zero lower endpoint；
- stochastic source capability 收紧；
- BFI/BFE operand normalization helper；
- Tensor entry control validation；
- public min/max/testp dispatch；
- vcpkg feature split；
- ptx_frontend generated snapshot + no-pip port 架构；
- header self-contained gate。

本次剩余问题应以定点补丁解决，而不是重构整个 `arith`。

---

# 12. 最终结论

`fix/m5-main-review` 已经成功关闭前序 Review 中的大多数问题，尤其是 capability/control 架构、整数 MAD、F32 approximate corner case、Tensor entry validation 和依赖拆分，均可保留。

但当前仍不是 merge-ready：

```text
F64 approximate FTZ input projection 错误
S2F6 satfinite finite overflow 错误
hosted CI / branch protection 未闭环
generated snapshot gate 与文档声明不一致
历史 review 被删除
```

建议先由 WP-A、WP-B 分别修复两个 P0，再由 WP-C 完成可审计 CI 与文档收尾。最终 Integration Agent 应以本文件的 merge checklist 为准，不应继续采用当前 project plan 中“arith remediation 已全部关闭”的判断。
