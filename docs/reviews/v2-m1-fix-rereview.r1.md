# Historical / V2-M1 Rereview Record

> **Archive status:** Historical / Superseded rereview snapshot.
> **Original:** uploaded `m5-fix-rereview.r1.md`; original body SHA256
> `e32aea1a90c4d0f1dd0279950f287438d3d9a18f7b05fc1c103cb9a7b7875763`.
> **Reviewed:** `fix/m5-main-review@0f5ef9acfd47ebda3b7c4acd9ef1da829d9f7a1e`;
> baseline `main@13bd652377b2256da05b1b8b4d106c365b9488b6`.
> **Milestone mapping:** V2-M1 arithmetic remediation; retained branch name is
> pre-merge continuity, not V2-M5.
> **Supersedes:** [V2-M1 remediation review](v2-m1-fix-review.md).
> **Superseded by:** [current project plan](../../.agents/project_plan.md).

---
# `fix/m5-main-review` 分支重新 Review

> **结论：NOT MERGE READY**  
> **审查对象：** `endingly/ptxsim` / `fix/m5-main-review`  
> **比较基线：** `main@13bd652377b2256da05b1b8b4d106c365b9488b6`  
> **当前审查快照：** `fix/m5-main-review@0f5ef9acfd47ebda3b7c4acd9ef1da829d9f7a1e`  
> **上一审查快照：** `fix/m5-main-review@24dd2308ef7869f950e6f956bb88544194a2059a`  
> **分支关系：** ahead 24 / behind 0  
> **规范基准：** NVIDIA PTX ISA 9.3  
> **前序报告：** `docs/reviews/m5-fix-review.md`  
> **审查日期：** 2026-08-30  
> **用途：** 最终修复、Hosted CI、GitHub Ruleset 与 V2-M1 接受门禁

---

## 1. 总体结论

用户补交的五个提交已经覆盖上一轮 Review 留下的主要问题：

```text
05b0af2  fix(arith): project F64 approximate FTZ inputs
419e2e6  fix(arith): saturate finite S2F6 conversions
def4b8a  build: verify frontend snapshot integrity
777ac67  docs: restore arithmetic review audit trail
0f5ef9a  ci: verify frontend lowering feature
```

本轮重新审查确认：

- F64 `.approx.ftz.f64` 已经在 classification 之前投影 `a[63:32]`；
- low-only F64 NaN payload 已不再污染 PTX 1.11.20 输入分类；
- S2F6 `.satfinite` 对有限越界值、±Inf、NaN 的数值结果已统一到目标 endpoint；
- S2F6 数值回绕问题已经关闭；
- snapshot 已增加自动 hash/provenance 完整性检查；
- 历史 `m4` / `m5-fix` Review 已恢复到 `docs/reviews/`；
- project plan 已撤回“V2-M1 已完成”的过早声明；
- CI 已加入 `frontend-lowering` vcpkg feature install smoke step。

因此，**上一轮报告中的两个数值 P0 已经关闭**。当前没有再次发现会改变普通计算结果的同等级数值错误。

不过，分支仍然不能合并，主要原因是：

1. GitHub active ruleset 要求的 check 名称是 `Debug` / `Release`，而 workflow 已改名为 `GCC Debug` / `GCC Release`；
2. 当前分支没有 PR，也没有任何针对 head `0f5ef9a` 的 GitHub Actions run；
3. S2F6 对“真实值刚刚越过 endpoint、但舍入后仍落在 endpoint”的输入没有设置设计文档要求的 `overflow`；
4. project plan 仍把 `main` 描述为 unprotected，但仓库事实上已有 active ruleset，只是 required checks 已过时且不完整；
5. `m5-main-review.md` 仍是 provenance stub，而本会话中仍可恢复完整原始报告。

### 1.1 当前严重度

| 严重度 | 数量 | 说明 |
|---|---:|---|
| P0 | 1 | GitHub required-check 配置与 workflow 冲突，会阻断或错误放行合并 |
| P1 | 1 | S2F6 saturation 诊断在真实边界上不符合已写入设计文档的契约 |
| P2 | 3 | Hosted CI 证据、project plan 事实描述、Review 原件恢复 |
| P3 | 1 | GitHub Actions 供应链固定建议 |

### 1.2 分层判断

```text
上一轮数值 P0：PASS
核心代码静态审查：CONDITIONAL PASS
测试与构建源码门禁：已补强，但未在 hosted CI 验证
GitHub 合并门禁：FAIL
整体结论：NOT MERGE READY
```

---

# 2. 本轮新增提交验收矩阵

| Commit | 对应问题 | 状态 | 结论 |
|---|---|---|---|
| `05b0af2` | `FIX-P0-001` | **PASS** | F64 FTZ 先投影 upper word，再分类/FTZ/运算；错误 oracle 已纠正 |
| `419e2e6` | `FIX-P0-002` | **PASS（数值）** | S2F6 有限越界不再回绕，±Inf/NaN endpoint 正确 |
| `419e2e6` | `FIX-P1-001` | **PARTIAL** | 主体 status policy 已定义；true-nextafter 边界仍漏报 overflow |
| `def4b8a` | `FIX-P2-002` | **PASS** | 自动验证 hash exact coverage、payload hash 与 portfile/provenance revision |
| `777ac67` | `FIX-P2-003` | **PARTIAL PASS** | 历史链恢复；完整原始 `m5-main-review.md` 仍可进一步恢复 |
| `0f5ef9a` | `FIX-P2-001` | **SOURCE PASS / GATE FAIL** | smoke step 已写入 workflow；没有 PR/run，且 ruleset context 与新 job 名冲突 |

---

# 3. 已关闭的上一轮 P0

---

## 3.1 `FIX-P0-001` — F64 `.approx.ftz` upper-word projection

### 当前实现

`submod/arith/src/detail/approximation_backend.cpp` 已增加：

```cpp
project_f64_to_ptx_1_11_20(...)
```

调用顺序调整为：

```text
mask low 32 bits
→ classify projected 1.11.20 value
→ input FTZ
→ execute reference approximation
→ output FTZ
→ mask low 32 result bits
```

这与 PTX 9.3 对：

```text
rcp.approx.ftz.f64
rsqrt.approx.ftz.f64
```

的 `a[63:32]` 语义一致。

### 测试质量

新增 `F64ApproximateFtzUsesOnlyUpperWord` 使用独立 upper-word field oracle，覆盖：

```text
low-only NaN payload
+Inf / -Inf upper word
upper-word sNaN / qNaN
positive / negative subnormal
normal input with arbitrary low 32 bits
```

原先错误的：

```text
0x7ff0000000000001 -> signaling NaN
```

已经改为：

```text
upper word 0x7ff00000 -> +Inf
rcp/rsqrt FTZ -> +0
invalid = false
```

### 结论

```text
FIX-P0-001: CLOSED
```

该 helper 和独立测试 oracle 均应保留。

---

## 3.2 `FIX-P0-002` — S2F6 finite saturation 数值语义

### 当前实现

S2F6 destination policy 已从 public pre-transform 移入：

```text
canonical_conversion.hpp::encode_fixed
```

现在：

```text
NaN + .finite
  -> +127
  -> invalid

+Inf + .finite
  -> +127
  -> overflow + inexact

-Inf + .finite
  -> -128
  -> overflow + inexact

finite out of range + .finite
  -> map to int8 type-range clamp
  -> clear integer-invalid
  -> overflow + inexact
```

这关闭了上一轮明确反例：

```text
+3.0 -> -1.0（回绕）
-3.0 -> 正值（回绕）
```

### 新增回归

测试已经覆盖：

```text
+3.0
-3.0
exact +127/64
exact -128/64
+2.0
-2.015625
±Inf
qNaN
sNaN
ReLU + negative finite / -Inf
```

### 结论

```text
FIX-P0-002 numeric result: CLOSED
```

但 saturation status 仍有一个精细边界问题，见 `REREVIEW-P1-001`。

---

# 4. P0 — GitHub 合并门禁阻断

---

## REREVIEW-P0-001 — Active ruleset required checks 与新 workflow job 名不一致

### 涉及范围

```text
.github/workflows/linux-ci.yml
GitHub repository ruleset: main / id 21723631
.agents/project_plan.md
```

### 当前 workflow job 名

`0f5ef9a` 将 matrix 中前两个 job 从：

```text
Debug
Release
```

改为：

```text
GCC Debug
GCC Release
```

完整 matrix 为：

```text
GCC Debug
GCC Release
Clang Debug
Clang Release
GCC ASan + UBSan
```

由于 job 本身定义：

```yaml
name: ${{ matrix.name }}
```

GitHub check run context 会使用这些新名称。

### 当前 active ruleset

仓库并非没有保护规则。

当前 repository ruleset：

```text
name: main
enforcement: active
condition: ~DEFAULT_BRANCH
strict required status checks: true
```

它仍要求：

```text
Debug
Release
```

且只要求这两个 check。

### 影响一：PR 可能永久等待不存在的 check

新 workflow 不再产生：

```text
Debug
Release
```

而 ruleset 不认识：

```text
GCC Debug
GCC Release
```

因此 PR 即使五个新 job 全绿，也可能仍显示：

```text
Expected — Waiting for status to be reported
```

这属于直接的 merge blocker。

### 影响二：规则没有覆盖要求的五个门禁

即使不改 workflow 名，当前 ruleset 也只要求：

```text
Debug
Release
```

没有强制：

```text
Clang Debug
Clang Release
GCC ASan + UBSan
```

这不符合 project plan 和 Review 中定义的 V2-M1 acceptance gate。

### 必须修复

推荐直接更新 active ruleset，required contexts 设为：

```text
GCC Debug
GCC Release
Clang Debug
Clang Release
GCC ASan + UBSan
```

并保持：

```text
strict_required_status_checks_policy = true
pull_request rule = active
```

备选方案是把 workflow 名恢复为 `Debug` / `Release`，但这只能消除前两个 context 的断裂，仍未解决另外三个 job 未被强制的问题。

### 必须验证

创建 PR 后确认 GitHub Checks 显示的 context 与 ruleset 完全一致。

不要仅凭 workflow YAML 推断名字；以实际 check run 名称为准。

### 验收条件

- [ ] ruleset 不再等待 `Debug` / `Release`
- [ ] 五个 matrix job 均为 required
- [ ] PR 最新 head 五个 required checks 全绿
- [ ] strict/up-to-date branch policy 生效
- [ ] 无 bypass actor

---

# 5. P1 — S2F6 saturation 边界诊断

---

## REREVIEW-P1-001 — 输入刚越过 endpoint、舍入后仍落到 endpoint 时漏报 `overflow`

### 涉及文件

```text
submod/arith/include/detail/canonical_conversion.hpp
submod/arith/test/test_conversion.cpp
docs/arith_module_design.md
```

### 已定义的设计契约

设计文档现在明确规定：

```text
对无 NaN/Inf 编码的目标 conversion：

有限范围外与 ±Inf 的 endpoint saturation
  -> overflow + inexact

NaN -> 指定 endpoint
  -> invalid

精确 endpoint / ReLU 消除的负输入
  -> no diagnostic
```

### 当前实现检测时点

`encode_fixed` 将 S2F6 的：

```text
saturation_mode::finite
```

映射为 int8：

```text
saturation_mode::type_range
```

然后复用 `encode_integer<int8_t>`。

但是 `encode_integer` 的流程是：

```text
先按 rounding 得到 integer magnitude
→ 再判断 rounded magnitude 是否超出 int8 range
```

所以它检测的是：

```text
rounded result out of range
```

而不是：

```text
exact input outside S2F6 representable range
```

### 明确反例

S2F6 正 endpoint：

```text
127 / 64 = 1.984375
F32 bits = 0x3ffe0000
```

下一个更大的 F32：

```text
0x3ffe0001
```

其真实值已经大于 `127/64`，但乘以 64 后非常接近 127，RN 会舍入到 127。

当前结果：

```text
value.rep = 127        // 数值正确
inexact = true
overflow = false       // 与设计文档不一致
```

负 endpoint：

```text
-128 / 64 = -2.0
F32 bits = 0xc0000000
```

下一个更负的 F32：

```text
0xc0000001
```

也会在 RN 后得到 magnitude 128，从而不触发当前 `out_of_range`，漏掉 `overflow`。

### 为什么现有测试没有发现

测试注释中的：

```text
0x40000000  // just above +127/64
```

实际是：

```text
+2.0
```

它不是 nextafter 边界；缩放后正好是 128，因此当前实现能够检测 overflow。

负向使用的：

```text
0xc0010000
```

是约 `-2.015625`，同样远离真正边界。

### 必须修复

在 rounding 之前，用 canonical exact number 与 S2F6 endpoint 比较。

可采用：

```text
positive max: +127 * 2^-6
negative min: -128 * 2^-6
```

建立一个：

```cpp
bool outside_s2f6_range(canonical::number input);
```

然后：

```text
exact_outside = compare before rounding
encoded = round + clamp
if .finite && exact_outside:
    overflow = true
    inexact = true
    invalid = false
```

NaN/Inf 按现有专门策略处理。

### 必须新增测试

F32：

```text
0x3ffe0000  exact +MAX -> no overflow
0x3ffe0001  next above +MAX -> overflow + inexact
0xc0000000  exact -MIN -> no overflow
0xc0000001  next below -MIN -> overflow + inexact
```

再增加直接 F64 source 的 nextafter case，避免 F64 → canonical → S2F6 路径遗漏。

定向舍入至少验证：

```text
toward_zero
toward_positive
toward_negative
nearest_even
```

无论最终 rounded rep 是否仍等于 endpoint，只要 exact input 在范围外，status 应遵守已写入设计文档的 policy。

### 验收条件

- [ ] exact endpoint 无 overflow
- [ ] true nextafter outside endpoint 有 overflow
- [ ] 数值仍 clamp 到正确 endpoint
- [ ] directed rounding 不改变 saturation event 的可观察性
- [ ] F32/F64 source 行为一致

---

# 6. P2 — Hosted CI、文档事实与审计原件

---

## REREVIEW-P2-001 — 当前 head 没有 Hosted CI 证据

### 当前事实

审查时：

```text
head SHA: 0f5ef9acfd47ebda3b7c4acd9ef1da829d9f7a1e
PR: none
workflow runs for head: 0
```

workflow 虽已加入：

```text
five CMake matrix jobs
frontend-lowering vcpkg install smoke
snapshot integrity CTest
```

但源码中“存在 step”并不等于 step 已成功执行。

### 必须完成

建立 PR 触发：

```yaml
pull_request:
  types: [opened, synchronize, reopened]
```

至少取得：

```text
GCC Debug PASS
GCC Release PASS
Clang Debug PASS
Clang Release PASS
GCC ASan + UBSan PASS
frontend-lowering smoke PASS
consumer/install tests PASS
snapshot integrity PASS
```

若 smoke step 失败，不应仅删除 step；应先检查：

```text
vcpkg manifest feature CLI
overlay port
generated source topology
installed ptx_frontend package config
```

---

## REREVIEW-P2-002 — Project plan 对 GitHub protection 状态的描述不准确

### 当前文档

`.agents/project_plan.md` 写道：

```text
main is currently unprotected
```

### 实际状态

仓库存在：

```text
active default-branch ruleset
PR required
deletion prohibited
non-fast-forward prohibited
strict required status checks
```

所以准确描述应是：

```text
main 已有 active ruleset，
但 required check context 已过时并且只覆盖 2/5 job。
```

### 必须修复

将 project plan 当前 gate 改为类似：

```text
The default branch is governed by active ruleset 21723631, but its required
status contexts must be updated from Debug/Release to the final five hosted
job names. V2-M1 remains pending until a final-head PR satisfies that ruleset.
```

同时更新 remediation map 中的 `MAIN-P2-004`：

```text
不是“完全没有 branch protection”
而是“ruleset 已存在，required checks 配置未完成”
```

---

## REREVIEW-P2-003 — 完整 `m5-main-review.md` 仍可恢复，不应长期保留 stub

### 当前仓库状态

`docs/reviews/m5-main-review.md` 目前只有 provenance 和 issue mapping，并声明：

```text
Original artifact unavailable
```

### 当前可恢复原件

本次会话中仍存在上一轮实际生成的完整文件：

```text
filename: m5-main-review.md
size: 24642 bytes
SHA-256:
15a766f50eee090e0aaf1b9d2003a228e467526242855037cd8184119bfc167f
```

因此原件并非不可恢复。

### 建议处理

将完整原件提交为：

```text
docs/reviews/m5-main-review.md
```

并在顶部添加 archival metadata：

```text
Historical / Superseded
reviewed SHA
superseded-by: m5-fix-review.md
```

当前 2.4 KB provenance stub 可：

- 移入 `docs/reviews/README.md`；
- 或作为完整报告的附录；
- 不应继续代替原始正文。

该项不是数值合并阻断，但完整恢复后审计链将明显更可靠。

---

# 7. P3 — 非阻断供应链建议

---

## REREVIEW-P3-001 — GitHub Actions 仍使用可变 major tag

当前 workflow 使用：

```text
actions/checkout@v7
actions/cache@v6
lukka/run-vcpkg@v11
```

这些是可变 tag。

建议在本里程碑关闭后，固定到完整 commit SHA，并通过注释保留版本号，例如：

```yaml
uses: actions/checkout@<full-sha> # v7.x
```

该项不应与当前两个实际 gate 混为一谈，但适合作为后续 hardening issue。

---

# 8. 值得保留的修改

后续 Agent 不应推倒以下实现：

## 数值语义

- `project_f64_to_ptx_1_11_20`
- F64 low-only payload independent oracle
- S2F6 destination policy集中到 `encode_fixed`
- NaN / ±Inf / finite overflow 分离
- F16/BF16/F32 `tanh.approx` preserve-only capability
- F32 `div.approx` large-divisor special domain
- approximate `model_dependent` helper

## Capability / API

- operation/type/control capability 单一事实化
- internal `OperationTraits` 向 public capability 派生
- explicit PTX integer type set
- Tensor entry-point control validation
- public min/max/testp API
- same-type `Result` template constraints

## 构建

- vcpkg `tests` / `frontend-lowering` feature split
- no-pip ptx_frontend overlay
- generated snapshot hash exact coverage
- portfile REF / README provenance match
- frontend-lowering install smoke step
- public header standalone check

## 文档

- `docs/reviews/m4-review.md`
- `docs/reviews/m5-fix-review.md`
- project plan 恢复为“V2-M1 pending acceptance”

---

# 9. 推荐剩余工作包

---

## WP-A — Ruleset + Hosted CI

### 负责

```text
REREVIEW-P0-001
REREVIEW-P2-001
REREVIEW-P2-002
MAIN-P2-004
```

### 工作内容

1. 更新 ruleset required contexts；
2. 修正 project plan protection 描述；
3. 创建 PR；
4. 观察真实 check run 名；
5. 运行五个 matrix job；
6. 验证 frontend smoke 和 snapshot CTest；
7. 记录 run URL / head SHA / check conclusion。

### 注意

ruleset 配置是 repository setting，不应试图用普通源码 commit “模拟完成”。

---

## WP-B — S2F6 Exact-Range Diagnostic

### 负责

```text
REREVIEW-P1-001
```

### 修改范围

```text
canonical_conversion.hpp
test_conversion.cpp
```

### 工作原则

- 不改已正确的 endpoint 数值；
- 不回退到 public pre-transform；
- 在 rounding 前识别 exact range loss；
- status policy 以现有设计文档为准；
- 增加 nextafter goldens。

---

## WP-C — Review 原件恢复

### 负责

```text
REREVIEW-P2-003
```

### 输入

完整原件：

```text
m5-main-review.md
SHA-256 15a766f50eee090e0aaf1b9d2003a228e467526242855037cd8184119bfc167f
```

### 输出

```text
docs/reviews/m5-main-review.md
```

带 historical metadata，并保持原正文不被重写。

---

# 10. 最终接受门禁

## 代码语义

- [x] `mad.hi.sat.s32`
- [x] `div.approx.f32` large divisor
- [x] F64 approximate form public reachability
- [x] F64 FTZ upper-word projection
- [x] S2F6 finite out-of-range value clamp
- [x] UE8M0 no-zero endpoint
- [x] BFI low-8 operands
- [x] Tensor entry control validation
- [ ] S2F6 true-nextafter overflow diagnostic

## 构建与测试

- [ ] PR exists for final head
- [ ] GCC Debug hosted PASS
- [ ] GCC Release hosted PASS
- [ ] Clang Debug hosted PASS
- [ ] Clang Release hosted PASS
- [ ] GCC ASan + UBSan hosted PASS
- [ ] frontend-lowering smoke PASS
- [ ] build-tree consumer PASS
- [ ] installed-package consumer PASS
- [ ] public header check PASS
- [ ] snapshot integrity CTest PASS

## GitHub ruleset

- [x] default branch active ruleset exists
- [ ] stale `Debug` / `Release` contexts removed or updated
- [ ] all five final job names are required
- [ ] strict up-to-date policy verified on PR

## 文档

- [x] m4 historical review restored
- [x] m5-fix historical review restored
- [x] V2-M1 completion claim withdrawn
- [ ] project plan protection wording corrected
- [ ] full m5-main original restored

---

# 11. 最终结论

相较 `24dd2308`，当前 `0f5ef9a` 已经完成了实质性的正确修复。上一轮两个数值 P0 均可关闭；本轮没有发现新的主结果错误。

当前阻止合并的首要问题已经从数值算法转移到 GitHub acceptance：

```text
active ruleset expects Debug / Release
workflow emits GCC Debug / GCC Release
```

这是一个确定的 required-check context 断裂，应在创建 PR 前修复。

代码侧仅剩 S2F6 true-nextafter saturation 的 `overflow` 诊断契约需要收口。该问题不改变 endpoint 数值，但与刚写入 `arith_module_design.md` 的公开诊断规则不一致，建议在 V2-M1 接受前关闭。

因此当前准确结论为：

```text
核心数值 remediation：PASS
诊断契约：1 个 P1 待修
Hosted CI：未验证
Ruleset：存在但配置错误
整体：NOT MERGE READY
```

完成 WP-A 和 WP-B 后，再以最终 PR head 进行一次只聚焦于 CI/ruleset/nextafter regression 的短验收即可，不需要再重做整套 `arith` 全量 Review。
