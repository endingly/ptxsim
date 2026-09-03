> **Archive status:** Historical review snapshot; superseded
> **Original artifact:** `v2-m2-main-review.md`
> **Original SHA-256:**
> `3a67b54782de39421ea277b2f006c01395d8d0ebdc4ade885fee3dee6f7cef9c`
> **Reviewed branch/SHA:** `feat/v2-m2-exec-ir` at
> `629783182259479b175172e0e6080d558dc2672c`
> **Reviewed baseline:** `main@7da3628c0463f586b190921b283b15ab059d2022`
> **Current milestone mapping:** retired V2-M2 program/lowering design;
> replacement executable-program planning
> **Supersedes:** none
> **Superseded by:**
> [executable-program architecture](../../.agents/arch/resolved_ir_execution_architecture.md),
> [active exec_ir plan](../../.agents/milestone_plan/exec_ir_module_execution_plan.md),
> and [active executor plan](../../.agents/milestone_plan/executor_module_execution_plan.md)
>
> The original review body is preserved byte-for-byte below; its internal
> "Active" status is historical and is not live gate truth.
---
# `ptxsim` V2-M2 主 Review 与修复指南

> **文档状态：** Active review / remediation guide  
> **审查仓库：** `endingly/ptxsim`  
> **审查分支：** `feat/v2-m2-exec-ir`  
> **审查提交：** `629783182259479b175172e0e6080d558dc2672c`  
> **比较基线：** `main@7da3628c0463f586b190921b283b15ab059d2022`  
> **分支关系：** ahead 15 / behind 0  
> **规范基准：** NVIDIA PTX ISA 9.3  
> **设计基准：** `.agents/project_plan.md`、`.agents/milestone_plan/v2_m2_execution_plan.md`、`docs/exec_ir.md`、`docs/execution_model.md`、`docs/lowering_policy.md`  
> **建议归档名：** `docs/reviews/v2-m2-main-review.md`  
> **用途：** 指导修复 Agent 完成 V2-M2 收尾，并作为合并与进入 V2-M3 前的验收清单

---

## 1. 总体结论

该分支的总体架构方向正确，已经形成一条清晰的执行数据边界：

```text
ptx_frontend AST + resolved_ir
            |
            v
     ptxsim::lowering
            |
            v
       ProgramImage
            |
            +--> typed exec_ir
            +--> function / symbol / source metadata
            +--> dense register layouts

caller-provided metadata
            |
            v
       ThreadState
```

本次实现中值得保留的部分包括：

- `common` 中的强类型 ID 与 `RawValue`；
- 前端无关、数据化、可验证的 `exec_ir`；
- 拥有全部数据且构造时验证的 `ProgramImage`；
- 可观测未初始化读取的 `RegisterFile`；
- 两阶段 lowering、稳定 ID/PC 分配以及复制后的诊断和源码信息；
- 默认包不依赖 `ptx_frontend`、lowering 作为显式可选组件的包边界；
- 固定前端 revision、生成快照与哈希完整性检查。

但当前分支仍存在会影响合法 PTX lowering、M2 生命周期验收和公共 API 健壮性的缺陷。尤其是：

1. `.address_size 32` 的符号地址会被静默转换为 64 位地址；
2. 单元素参数化寄存器声明（例如 `%s<1>`）会被错误拒绝；
3. 托管 CI 并未实际运行 lowering 单元测试、生命周期 E2E、公共头检查或安装消费测试；
4. `ThreadState` 没有任何经 `ProgramImage` 验证的生产构造路径；
5. 人工损坏的 resolved IR 可令 `lower_module` 抛出异常，而不是返回结构化错误。

### 1.1 Review 判定

```text
结论：REQUEST_CHANGES
当前是否建议合并：否
是否可宣告 V2-M2 已完成：否
是否可将当前 gate 提升到 V2-M3：否
```

### 1.2 严重度统计

| 严重度 | 数量 | 含义 |
|---|---:|---|
| P0 | 2 | 会错误降低或错误拒绝合法 PTX；阻止合并 |
| P1 | 3 | M2 验收、公共 API 健壮性或状态一致性缺口；应在合并前处理 |
| P2 | 3 | 诊断、供应链和工程治理缺陷；建议与本次收尾一起修复 |
| **合计** | **8** | — |

### 1.3 合并阻断项

至少以下事项全部完成后，才应重新进行 fix review：

- 修复 `V2M2-MAIN-P0-001` 与 `V2M2-MAIN-P0-002`；
- 让托管 CI 真正执行完整的 feature-on lowering 测试；
- 关闭或通过正式计划变更处置 `ProgramImage -> ThreadState` 验收缺口；
- 保证 `lower_module` 对损坏输入不抛出可逃逸异常；
- 在目标 exact head 上重新运行完整 feature-off / feature-on 构建与测试；
- 将项目 gate 恢复为 V2-M2 review/remediation，直至本报告阻断项清零。

---

# 2. 审查范围与证据

## 2.1 审查范围

本次审查覆盖 `main@7da3628...` 到 `feat/v2-m2-exec-ir@6297831...` 的 15 个提交，重点检查：

- `submod/common`；
- `submod/exec_ir`；
- `submod/program`；
- `submod/state`；
- `submod/lowering`；
- 顶层 CMake、安装导出与 package component；
- `CMakePresets.json` 与 `.github/workflows/linux-ci.yml`；
- M2 设计、执行计划和状态文档；
- 固定的 `ptx_frontend` revision 与生成快照治理。

生成的 `ptx_frontend` `.gen.cpp/.gen.hpp` 文件规模很大。本次对它们执行的是：

```text
revision / provenance / topology / hash-integrity review
```

而不是逐行手工复核代码生成器输出。对 lowering 依赖的具体 resolved IR 结构，则结合固定 revision 的公开头文件与前端 corpus 进行了定向核对。

## 2.2 托管 CI 状态

目标 head 对应 GitHub Actions run `33342734906` 总体成功，以下标准任务显示为绿色：

```text
GCC Debug
GCC Release
Clang Debug
Clang Release
GCC ASan + UBSan
```

但这些绿色任务不能视为 V2-M2 lowering 验收已通过，原因见 `V2M2-MAIN-P1-001`。

## 2.3 本地动态验证限制

审查环境尝试从 GitHub 获取仓库以运行本地 configure/build/ctest，但容器 DNS 无法解析 `github.com`：

```text
Could not resolve host: github.com
```

因此本报告明确区分：

- 已完成：目标分支与固定前端 revision 的源码静态审查；
- 已完成：托管 GitHub Actions exact-head 状态与工作流覆盖范围核对；
- 未完成：审查容器内的独立本地构建、单测和 sanitizer 复跑。

本限制不会消除下述从确定代码路径直接得出的缺陷，但 fix review 必须补上独立动态验证。

---

# 3. P0 — 合并阻断的 lowering 正确性问题

## V2M2-MAIN-P0-001 — `.address_size 32` 的符号地址被静默提升为 64 位

### 涉及文件

- `submod/lowering/src/lowering.cpp`
- `submod/exec_ir/include/operand.hpp`
- `submod/exec_ir/include/instruction.hpp`
- `docs/exec_ir.md`
- `docs/lowering_policy.md`

### 当前行为

`ModuleLowerer::address_operand` 对寄存器基址强制要求 `RawWidth::b64`，并且无论基址是寄存器还是符号，最终都构造：

```cpp
exec_ir::AddressOperand{
    base,
    exec_ir::AddressWidth::bits64,
    offset,
};
```

与此同时，`ModuleLowerer::run` 并没有读取或保存 `AstAddressSizeDirective`。

这导致两个不一致行为：

1. `.address_size 32` + 32 位寄存器基址会因寄存器宽度不匹配而失败；
2. `.address_size 32` + 符号基址没有宽度检查，会成功产生 `bits64` 地址。

第二种情况是本问题的核心：合法的 32 位地址模块被悄悄表示为 64 位地址，而不是被明确拒绝。

### 最小复现

```ptx
.version 8.0
.target sm_80
.address_size 32

.global .u32 value;

.visible .entry kernel() {
  .reg .u32 %r0;
  ld.global.u32 %r0, [value];
}
```

当前 lowering 路径会把 `[value]` 记录为类似：

```text
address:b64:symbol:0:+0
```

而源模块明确声明了 32 位地址大小。

### 为什么是 P0

PTX 的 `.address_size` 是模块级语义事实，可取 32 或 64。当前 M2 文档又明确把初始 `ld/st` 子集限定为 64 位地址，并规定不支持的合法形式必须结构化失败，不能降低成“附近的受支持形式”。

当前行为同时违反：

```text
PTX 模块地址宽度事实
lowering 不得近似支持的原则
exec_ir 自身公开的 AddressWidth 契约
```

如果该记录进入后续 M3/M4 内存与执行层，运行时将无法判断它原本是 32 位地址，也就无法正确实现 PTX 要求的截断、扩展和地址计算规则。

### 必须修复

建议在 lowering 的模块预处理阶段完成以下工作：

1. 查找并验证模块的 `.address_size`；
2. 将其保存为明确的 module-level lowering fact；
3. 每条内存指令 lowering 时使用该事实；
4. M2 若只支持 64 位地址，则对 `.address_size 32` 的 `ld/st` 返回结构化 unsupported 诊断；
5. 不得仅对寄存器基址检查宽度，而放过符号基址；
6. 不得把 32 位地址统一写成 `AddressWidth::bits64`。

建议的阶段性行为：

```text
.address_size 64 + b64 register base -> 支持
.address_size 64 + symbol base       -> 支持
.address_size 32 + b32 register base -> 明确 unsupported
.address_size 32 + symbol base       -> 明确 unsupported
```

等未来真正实现 32 位地址时，再同时完成：

- `AddressWidth::bits32` lowering；
- b32 寄存器基址校验；
- ProgramImage verifier 覆盖；
- M3/M4 地址截断/扩展语义；
- 对应 E2E。

### 必须新增测试

建议新增独立用例：

```text
ModuleLowering.RejectsAddressSize32SymbolBaseWithoutWidening
ModuleLowering.RejectsAddressSize32RegisterBaseWithoutWidening
ModuleLowering.LowersAddressSize64SymbolBaseAsBits64
ModuleLowering.LowersAddressSize64RegisterBaseAsBits64
ModuleLowering.RejectsMalformedOrMissingAddressSizeAtPublicBoundary
```

断言不能只检查失败，还应检查：

- `LoweringDiagnosticCode`；
- `instruction_context == "ld"` 或 `"st"`；
- `unsupported_feature`/`operand_or_control_detail` 明确包含 address size；
- 不产生任何部分构造的 `ProgramImage`。

---

## V2M2-MAIN-P0-002 — 单元素参数化寄存器 `%r<1>` 被错误视为普通寄存器

### 涉及文件

- `submod/lowering/src/lowering.cpp`
- `submod/lowering/test/test_lowering.cpp`

### 当前行为

声明绑定阶段使用：

```cpp
const auto count = symbol.parameterized_count.value_or(1);
register_bindings_[symbol_index] = RegisterBinding{base, width, count};
```

这样会丢失一个关键事实：

```text
count == 1
```

究竟来自：

```ptx
.reg .s32 %s;
```

还是：

```ptx
.reg .s32 %s<1>;
```

随后 `register_operand` 又显式拒绝：

```cpp
ref.parameterized_index && binding.count == 1
```

因此 `%s<1>` 声明产生的 `%s0` 引用必然被判定为 `invalid parameterized register index`。

固定 revision 的 `ptx_frontend` 会接受所有正的 32 位 parameterized count，并且其 corpus 已经包含：

```ptx
.reg .s32 %s<1>;
```

所以这不是前端不可能产生的理论状态，而是现实输入。

### 最小复现

```ptx
.version 8.0
.target sm_80
.address_size 64

.visible .entry kernel() {
  .reg .s32 %s<1>;
  .reg .s64 %d0;
  mul.wide.s32 %d0, %s0, 2;
}
```

该程序只使用当前 M2 文档声称支持的寄存器声明和 `mul.wide.s32` 形式，却会在 `%s0` lowering 时被错误拒绝。

### 根因

`RegisterBinding` 仅保存了：

```cpp
base
width
count
```

但合法性判断还需要：

```cpp
bool is_parameterized;
```

或者直接保存：

```cpp
std::optional<std::uint32_t> parameterized_count;
```

`value_or(1)` 把两个语义不同的声明压成了相同状态。

### 必须修复

推荐改为：

```cpp
struct RegisterBinding {
  common::RegisterSlot base;
  RawWidth width;
  std::uint32_t count;
  bool parameterized;
};
```

合法性规则应为：

```text
parameterized == true:
  引用必须带 parameterized_index
  index < count

parameterized == false:
  引用不得带 parameterized_index
  隐式 index = 0
```

不要再用 `count == 1` 推导是否 parameterized。

### 必须新增测试

至少覆盖：

```text
.reg .s32 %s<1>;  使用 %s0 成功
.reg .u32 %r<1>;  使用 %r0 成功
.reg .pred %p<1>; 使用 %p0 作为 guard 成功
.reg .u32 %r;     使用 %r 成功
```

还应构造损坏 resolved IR，验证：

```text
普通寄存器却带 index -> malformed_resolved_ir
参数化寄存器缺少 index -> malformed_resolved_ir
index == count           -> malformed_resolved_ir
```

建议测试名：

```text
ModuleLowering.LowersSingleElementParameterizedRegisters
ModuleLowering.DistinguishesParameterizedOneFromScalarDeclaration
ModuleLowering.RejectsMissingOrSpuriousParameterizedIndex
```

---

# 4. P1 — M2 验收与公共边界问题

## V2M2-MAIN-P1-001 — 托管 CI 没有运行真正的 lowering 验收测试

### 涉及文件

- `CMakePresets.json`
- `.github/workflows/linux-ci.yml`
- `submod/lowering/CMakeLists.txt`
- `submod/lowering/test/frontend_lowering_smoke.cpp`
- `submod/lowering/test/test_lowering.cpp`
- `submod/lowering/test/run_consumer.cmake`

### 当前行为

五个标准 CMake preset 都只启用：

```text
VCPKG_MANIFEST_FEATURES=tests
```

因此标准矩阵不安装 `frontend-lowering` feature，也不会创建 `ptxsim_lowering` target。

工作流仅在 `GCC Debug` 后额外执行：

```bash
cmake --preset ci-linux-gcc-debug \
  "-DVCPKG_MANIFEST_FEATURES=tests;frontend-lowering"
cmake --build ... --target test_ptxsim_frontend_lowering_smoke
ctest ... -R '^ptxsim_frontend_lowering_smoke$'
```

但 `frontend_lowering_smoke.cpp` 只是查询一个生成 descriptor 是否为空，并不会：

- 调用 `lower_module`；
- 构造 `ProgramImage`；
- 验证前端生命周期独立性；
- 构造 `ThreadState`；
- 执行 `test_ptxsim_lowering`；
- 编译 lowering 的公共头检查 target；
- 运行 build-tree consumer；
- 运行 installed-package consumer。

`submod/lowering/CMakeLists.txt` 已经定义了这些真实测试，但 workflow 通过只构建一个指定 target 将它们全部绕开。

### 影响

当前 GitHub Actions run 虽然整体绿色，但它没有覆盖 V2-M2 execution plan 中定义的正式退出流水线：

```text
source
-> parse/resolve
-> lowering
-> ProgramImage
-> destroy frontend objects
-> verify/dump/walk
-> ThreadState
```

因此：

```text
CI green != M2 acceptance green
```

本报告中的两个 P0 lowering bug 都能在当前 CI 继续保持绿色，正是该覆盖缺口的直接例证。

### 必须修复

增加明确的 feature-on CI 路径，至少做到：

```bash
cmake --preset <preset> \
  "-DVCPKG_MANIFEST_FEATURES=tests;frontend-lowering"
cmake --build out/build/<preset>
ctest --test-dir out/build/<preset> --output-on-failure
```

关键点是：

- 构建默认 `all`，不要只构建 descriptor smoke；
- 运行完整 `ctest`，不要只用单个 smoke regex；
- 保证 `test_ptxsim_lowering` 被发现并执行；
- 保证 public-header object target 被编译；
- 保证 build-tree / installed consumer 被执行；
- 保证生命周期 E2E 被执行；
- 对 feature-on 路径至少覆盖 GCC、Clang 和 sanitizer；
- 推荐直接复用当前五个标准维度，避免形成另一个未经验证的配置岛。

### 建议矩阵

| 模式 | GCC Debug | GCC Release | Clang Debug | Clang Release | GCC ASan+UBSan |
|---|---:|---:|---:|---:|---:|
| Core / feature-off | 必须 | 必须 | 必须 | 必须 | 必须 |
| Lowering / feature-on | 必须 | 必须 | 必须 | 必须 | 必须 |

若执行成本暂时过高，最低可接受的过渡矩阵为：

```text
feature-on GCC Debug
feature-on Clang Debug
feature-on GCC Release
feature-on GCC ASan+UBSan
```

但在正式宣告 M2 完成前仍建议恢复全矩阵。

### 必须增加的 CI 自检

建议在 workflow 中加入：

```bash
ctest --test-dir ... -N
```

并断言至少存在：

```text
ModuleLowering.*
LoweringContext.*
LoweringDiagnostic.*
ptxsim_lowering_build-tree_consumer
ptxsim_lowering_installed_consumer
```

从而防止未来因 feature、target 名或 `BUILD_TESTING` 配置错误再次出现“测试源存在但 CI 从未运行”的假绿色。

---

## V2M2-MAIN-P1-002 — 缺少经 `ProgramImage` 验证的 `ThreadState` 启动路径

### 涉及文件

- `submod/state/include/thread_state.hpp`
- `submod/state/src/thread_state.cpp`
- `submod/state/test/test_thread_state.cpp`
- `submod/lowering/test/test_lowering.cpp`
- `docs/execution_model.md`
- `.agents/milestone_plan/v2_m2_execution_plan.md`
- `.agents/project_plan.md`

### 当前行为

唯一公开工厂为：

```cpp
ThreadState::create(
    ThreadId,
    FunctionId,
    ProgramCounter,
    std::vector<RawWidth> register_layout);
```

它只验证 register layout 能否建立 `RegisterFile`，但不能验证：

- `FunctionId` 是否存在于某个 `ProgramImage`；
- `initial_pc` 是否为该函数的入口 PC；
- PC 是否位于函数范围；
- layout 是否与该函数的 canonical register layout 一致；
- 目标是否是 entry function；
- function、PC 和 layout 是否来自同一个 image/function。

现有测试甚至明确证明任意元数据都可接受：

```text
FunctionId{3}
ProgramCounter{11}
empty layout
```

而所谓 “AcceptsCopiedProgramImageFunctionMetadata” 测试，是由测试代码手工复制 widths 后再调用原始工厂；该路径并不阻止调用方混用不同函数或不同 image 的元数据。

### 影响

调用方可以合法地构造这种不可能状态：

```text
current_function = function A
current_pc       = function B.begin_pc
register_layout  = function C.registers
status           = ready
```

在 M4 executor 接入后，此状态会令：

- PC 到 function 的归属不可信；
- RegisterSlot 宽度校验与实际函数不一致；
- 调试 dump 看似合法但无法对应任何 `ProgramImage`；
- 错误可能被延迟到 fetch/execute 阶段才暴露。

此外，M2 execution plan 明确把 “从 ProgramImage metadata 构造 ThreadState” 列为验收条件，而当前文档又把 production adapter 宣告为 non-goal。两者需要做出一个明确、可审计的决定，不能一边宣告 M2 完成，一边只保留测试手工复制。

### 必须修复或正式处置

推荐方案是增加一个前端无关的启动适配器，而不是让 lowering 依赖 state。例如：

```cpp
namespace ptxsim::bootstrap {

expected<state::ThreadState, ThreadBootstrapError>
create_entry_thread(
    const program::ProgramImage& image,
    common::ThreadId thread,
    common::FunctionId entry);

}
```

该适配器应：

1. 验证 function ID；
2. 按策略验证它是否属于 `entry_points()`；
3. 使用 canonical `begin_pc`；
4. 从 canonical `FunctionRecord::registers` 生成布局；
5. 返回能区分 image/function/entry/layout 错误的 `ThreadBootstrapError`；
6. 不保留 `ProgramImage` 引用；
7. 不引入 `lowering -> state` 依赖。

放置位置可选择：

```text
新建 frontend-independent bootstrap/runtime 子模块
或在 M4 executor 模块中提供，但在 M2 gate 中显式记录延期
```

不建议简单增加：

```cpp
ThreadState::create(const FunctionRecord&)
```

因为它仍不能证明该 record 来自当前 image，也不能处理 entry membership。

若维护者决定把该适配器正式延期至 M4，则必须同时：

- 修改 V2-M2 execution plan 的 acceptance；
- 修改 `.agents/project_plan.md`；
- 在 ADR/计划变更中说明理由；
- 不再把现有手工复制测试描述成 production-ready M2 exit gate。

### 必须新增测试

若实现 adapter，至少覆盖：

```text
有效 entry -> ready ThreadState
不存在的 FunctionId -> structured error
非 entry function -> structured error（若 API 要求 entry）
正确 function ID + 其他函数 PC/layout 不再可由安全 API 构造
返回的 ThreadState 在 ProgramImage 销毁后仍拥有自己的布局和值状态
```

---

## V2M2-MAIN-P1-003 — 损坏的 resolved ID 会从 `lower_module` 抛出异常

### 涉及文件

- `submod/lowering/include/lowering.hpp`
- `submod/lowering/src/lowering.cpp`
- 固定前端 revision 的 `ptx_symbol_table.cpp`
- `submod/lowering/test/test_lowering.cpp`

### 当前行为

`lower_module` 的公共返回类型是：

```cpp
std::expected<program::ProgramImage, LoweringDiagnostic>
```

并且诊断枚举已经包含：

```text
malformed_resolved_ir
internal_lowering_error
```

但是 `collect_functions` 直接调用：

```cpp
resolved_.symbols.symbol(resolved_function.symbol_id)
```

`function_for_scope` 等路径也直接调用 `scope(...)`。固定前端 revision 的这些访问器使用 bounds-checked vector access；当 ID 被破坏为超出范围时，会抛出 `std::out_of_range`。

因此，虽然 lowering 已经为一些 AST/resolved mismatch 返回结构化错误，但 ID 越界这一类损坏输入会绕过 `expected` 契约，令进程异常退出。

### 最小复现思路

```cpp
parse valid module
resolve valid module
resolved.functions[0].symbol_id = SymbolId{UINT32_MAX}
call lower_module(ast, resolved, "broken.ptx")
```

当前预期结果是 `std::out_of_range` 逃逸，而不是：

```text
LoweringDiagnosticCode::malformed_resolved_ir
```

### 影响

`lower_module` 接受两个独立对象：AST 与 resolved module。即使正常前端不会产生越界 ID，以下场景仍可能发生：

- 测试或工具手工构造/修改 resolved IR；
- AST 与 resolved object 来自不同解析结果；
- 前端 ABI/版本错配；
- 内存破坏或反序列化后的未来输入；
- ptx_frontend bug。

既然该 API 已显式承诺结构化 malformed/internal 错误，异常不应从公共边界逃逸。

### 必须修复

建立统一的安全访问辅助函数，例如：

```cpp
expected<const binding::Symbol*, LoweringDiagnostic>
checked_symbol(SymbolId, SourceRange, context);

expected<const binding::Scope*, LoweringDiagnostic>
checked_scope(ScopeId, SourceRange, context);
```

在访问前验证：

- ID 是否落在对应 dense vector 范围；
- symbol/scope kind 是否符合上下文；
- owned scope、parent、function mapping 是否一致。

可以在 `lower_module` 最外层把已知的 `std::out_of_range` 转换为 `internal_lowering_error` 作为最后防线，但这不应替代逐点验证，因为逐点验证才能给出准确的 source/function/instruction context。

### 必须新增测试

建议对每类前端 identity 增加 mutation test：

```text
invalid function symbol ID
invalid function scope ID
invalid register symbol ID
invalid label symbol ID
invalid data symbol ID
invalid parent scope chain
```

每个测试必须：

```cpp
EXPECT_NO_THROW(...)
ASSERT_FALSE(result)
EXPECT_EQ(result.error().code,
          LoweringDiagnosticCode::malformed_resolved_ir)
```

并检查稳定的 function/source/detail context。

---

# 5. P2 — 诊断、供应链与工程治理问题

## V2M2-MAIN-P2-001 — `ProgramImage` 验证失败细节在 lowering 边界被全部丢失

### 涉及文件

- `submod/program/include/program_image.hpp`
- `submod/program/src/program_image.cpp`
- `submod/lowering/src/lowering.cpp`
- `submod/lowering/include/diagnostic.hpp`

### 当前行为

`ProgramError` 已经包含：

```text
ProgramErrorCode
optional FunctionId
optional PC
optional index
optional InstructionErrorCode
```

但 `ModuleLowerer::run` 在 `ProgramImage::create` 失败时只返回：

```text
code = lowering_invariant_violation
instruction = "program-image"
unsupported-feature = "ProgramImage::create"
operand-or-control = "verification failed"
```

除可选 function name 外，原始错误代码、PC、index 和 instruction error 都被丢弃。

### 影响

未来任何 lowering 内部不变量失败都会变成相同文本，修复 Agent 无法从用户报告判断是：

```text
branch target
register slot
register width
symbol ID
source map
function partition
instruction record validation
```

这会显著增加 M3/M4 集成后的定位成本。

### 建议修复

- 为 `ProgramErrorCode` 提供稳定 `to_string`；
- 把 `code/pc/index/instruction_error` 映射进 `LoweringDiagnostic` detail；
- 若有 PC，通过 `source_locations_by_pc` 附加源码位置；
- 保留 function context；
- 考虑公开 `verify(const ProgramImageData&)`，使 lowering 能在 move 前获得完整诊断；
- 不要把结构化错误压成固定的 `"verification failed"`。

### 建议测试

通过测试专用 builder 或 injectable verifier 制造：

```text
invalid branch target
register slot not found
register width mismatch
invalid symbol
source map mismatch
```

验证每类错误最终产生不同、稳定、可机器判断的 lowering detail。

---

## V2M2-MAIN-P2-002 — `actions/cache` 从固定 SHA 退化为浮动 major tag

### 涉及文件

- `.github/workflows/linux-ci.yml`

### 当前行为

基线 workflow 使用固定提交：

```yaml
uses: actions/cache@55cc8345863c7cc4c66a329aec7e433d2d1c52a9 # v6
```

目标分支的 compiler cache 步骤改成：

```yaml
uses: actions/cache@v6
```

同一 workflow 中其他第三方 action 仍使用固定 SHA，因此这不是统一策略，而是一次可复现性与供应链约束回退。

### 影响

- `@v6` 可在不修改仓库的情况下指向新的 action commit；
- exact-head CI 结果不再能完全重放；
- 上游 tag 被移动或发布新实现时，构建行为可能变化；
- 与此前工程采用的 pinned-action 习惯不一致。

### 建议修复

- 恢复经过审核的 exact SHA；
- 保留注释中的语义版本；
- 增加 workflow lint，拒绝除仓库内 action 外的浮动 tag；
- 若升级 action，使用独立依赖治理提交并记录新 SHA。

---

## V2M2-MAIN-P2-003 — 项目计划提前把 current gate 标成 V2-M3

### 涉及文件

- `.agents/project_plan.md`
- `.agents/milestone_plan/v2_m2_execution_plan.md`
- `docs/execution_model.md`
- `docs/lowering_policy.md`
- `.agents/review_policy.md`

### 当前行为

`.agents/project_plan.md` 顶部已经写明：

```text
Current gate: V2-M3 memory and storage
```

但同一分支：

- V2-M2 尚未合入 main；
- 尚无正式 V2-M2 review/fix/rereview 审计链；
- 托管 CI 尚未运行正式 lowering acceptance；
- 本报告仍确认 2 个 P0 和 3 个 P1；
- execution plan 与 execution model 在 `ThreadState` adapter 上存在未决差异。

按照仓库自己的 review governance，live `project_plan.md` 应表达当前真实 gate，而不是预期 gate。

### 影响

提前推进 gate 会令后续 Agent：

- 误以为 M2 API 已冻结；
- 直接在有缺陷的地址模型和状态启动契约上实现 M3；
- 把 M2 修复错误归类为“回归”；
- 形成文档、GitHub milestone 和代码真实状态不一致。

### 建议修复

在本次 review/fix 完成前，将状态改为：

```text
Current gate: V2-M2 review/remediation
Next gate: V2-M3 memory and storage
```

完成以下事项后再切换：

```text
P0/P1 清零
feature-on exact-head CI 全绿
fix review 通过
必要的计划变更/ADR 已归档
V2-M2 review audit chain 已加入 docs/reviews
```

---

# 6. 正向结论与应保留设计

以下设计在本次审查中表现良好，修复时不应为了省事而破坏。

## 6.1 前端隔离边界正确

默认核心包的依赖方向清晰：

```text
common
  +--> exec_ir --> program
  +--> state

lowering --public--> ptx_frontend::resolved_ir
```

`ptx_frontend` 没有泄漏到 `common/exec_ir/program/state` 的公共数据模型中。修复 lowering 时应继续保持：

```text
只有 ptxsim::lowering 可以链接 frontend
ProgramImage 不保留 frontend object/reference/string_view
```

## 6.2 `ProgramImage` 的 owning + verify 模式正确

`ProgramImage::create(ProgramImageData)` 在取得所有权前验证：

- canonical function/symbol/source IDs；
- 函数 PC 分区；
- register slot 与 width；
- entry function；
- source side table；
- instruction validator；
- function 内 branch target；
- symbol/register operand 引用。

这比把错误延迟到 executor 更可靠。后续应增强诊断，不应删除或弱化 verifier。

## 6.3 `RegisterFile` 的未初始化策略正确

内部零占位不通过 `read()` 暴露，读取未写 slot 会得到：

```text
RegisterErrorCode::uninitialized_read
```

这符合模拟器可观测性的目标。不要把所有寄存器静默初始化为架构零。

## 6.4 两阶段 lowering 方向正确

当前 lowering 已按：

```text
pass 1: functions / registers / data symbols / labels / PCs
pass 2: instruction records + source metadata
```

组织。修复地址宽度和 identity 校验时应沿用该结构，不应退回运行时字符串查找。

## 6.5 可选 package component 方向正确

默认：

```cmake
find_package(ptxsim CONFIG REQUIRED)
```

不应发现或要求前端。

显式：

```cmake
find_package(ptxsim CONFIG REQUIRED COMPONENTS lowering)
```

才加载 lowering 及其 `ptx_frontend` 公共依赖。该边界值得保留，CI 修复应增加验证而不是把前端重新变成默认依赖。

## 6.6 前端快照治理总体合理

overlay port 固定前端 commit，并通过：

- provenance README；
- `SHA256SUMS`；
- exact payload coverage；
- LF/timestamp 检查；
- portfile REF 一致性；

避免构建时依赖 Python/PyPI 重新生成。修复时应继续保持 snapshot 与 revision 的原子更新。

---

# 7. 必须补充的回归测试清单

## 7.1 Lowering 正确性

| 建议测试 | 覆盖问题 | 必须断言 |
|---|---|---|
| `LowersSingleElementParameterizedRegisters` | P0-002 | `%r<1>` / `%p<1>` 成功且 slot 稳定 |
| `DistinguishesParameterizedOneFromScalarDeclaration` | P0-002 | scalar 与 parameterized-one 不混淆 |
| `RejectsAddressSize32SymbolBaseWithoutWidening` | P0-001 | 结构化拒绝，不生成 b64 record |
| `RejectsAddressSize32RegisterBaseWithoutWidening` | P0-001 | 与 symbol base 行为一致 |
| `LowersAddressSize64MemoryOperands` | P0-001 | symbol/register 均为 bits64 |
| `RejectsOutOfRangeFrontendIdentitiesWithoutThrowing` | P1-003 | `EXPECT_NO_THROW` + malformed diagnostic |
| `PreservesProgramVerifierFailureDetails` | P2-001 | code/pc/index/instruction detail 不丢失 |

## 7.2 生命周期与启动状态

| 建议测试 | 覆盖问题 | 必须断言 |
|---|---|---|
| `CreatesEntryThreadFromProgramImage` | P1-002 | canonical function/PC/layout |
| `RejectsUnknownEntryFunction` | P1-002 | structured bootstrap error |
| `RejectsNonEntryFunctionWhenEntryRequired` | P1-002 | entry policy 明确 |
| `ThreadOutlivesProgramImageAfterBootstrap` | P1-002 | 不保留 image 引用 |
| `LoweredImageOutlivesAllFrontendObjects` | M2 exit | walk/dump/verify/source/symbol/branch 全可用 |

## 7.3 包与构建

必须在 feature-on 配置中实际执行：

```text
ptxsim_lowering_public_header_check
test_ptxsim_lowering
ptxsim_lowering_build-tree_consumer
ptxsim_lowering_installed_consumer
```

installed consumer 至少应完成：

```text
find_package(ptxsim REQUIRED COMPONENTS lowering)
parse
resolve
lower
verify
dump
```

feature-off consumer 必须继续验证：

```text
find_package(ptxsim REQUIRED)
```

不会发现或加载 `ptx_frontend`。

---

# 8. 推荐 CI 验收矩阵

## 8.1 Core / feature-off

每个标准 preset：

```text
configure
build all
ctest all
install
external core consumer
```

并确保：

```text
ptx_frontend not found / not loaded
```

## 8.2 Lowering / feature-on

每个 feature-on preset：

```text
VCPKG_MANIFEST_FEATURES=tests;frontend-lowering
configure
build all
ctest all
install
build-tree lowering consumer
installed lowering consumer
```

推荐维度：

```text
GCC Debug
GCC Release
Clang Debug
Clang Release
GCC ASan + UBSan
```

## 8.3 Exact-head 证据

fix review 时必须记录：

- 审查 head SHA；
- 每个 required job 的 run/job ID；
- 所有 required job 均针对相同 SHA；
- `ctest -N` 中 lowering 测试数量与名称；
- installed consumer 实际执行日志；
- sanitizer 配置没有排除 lowering target。

不要只记录 workflow 总体绿色。

---

# 9. 推荐修复顺序

## Wave A — 先修 semantic truth

1. 修复 module address-size 采集与 32 位地址拒绝；
2. 修复 parameterized-one register binding；
3. 添加独立回归测试；
4. 在本地 feature-on GTest 中先证明两个 P0 已关闭。

不要先扩展 M3 memory，也不要为了让测试通过而把所有地址统一定义为 64 位。

## Wave B — 修复公共边界

1. 为 symbol/scope/identity 访问增加 checked helpers；
2. 保证损坏 resolved IR 不抛异常；
3. 决定并实现 `ProgramImage -> ThreadState` 安全启动 API，或正式修改 M2 计划；
4. 保持 lowering 与 state 解耦。

## Wave C — 修复 CI 与包验收

1. feature-on 构建默认 all；
2. 运行完整 ctest；
3. 纳入 header check 和两个 consumer；
4. 扩展至 GCC/Clang/Release/sanitizer；
5. 恢复 action exact-SHA pinning。

## Wave D — 修复诊断与治理

1. 传播 `ProgramError` 细节；
2. 更新 project gate；
3. 创建并维护 V2-M2 review audit chain；
4. exact-head CI 全绿后再进入 fix review。

---

# 10. Agent 实施约束

## 10.1 不得用文档迁就错误实现

不得把文档改成：

```text
所有地址一律 64 位
%r<1> 不支持
CI smoke 即代表 lowering acceptance
任意 FunctionId/PC/layout 均为合法 ThreadState
```

除非这是经过正式批准、同时修改 project plan、execution plan 和后续里程碑依赖的架构变更。

## 10.2 不得扩大当前 instruction subset 来掩盖基础缺陷

本轮目标不是增加更多 opcode，而是稳定：

```text
ID
operand
instruction record
ProgramImage
state bootstrap
lowering
package/CI
```

在 P0/P1 清零前，不建议继续加入 float、conversion、call、ret、atomics 或更多 memory space。

## 10.3 每个缺陷必须有独立回归测试

不得只修改现有 expected dump，也不得只依赖大 E2E。每个 finding 至少需要一个能单独失败、单独定位的测试。

## 10.4 Unsupported 必须显式且一致

对合法但超出 M2 子集的 PTX：

```text
必须返回 structured unsupported diagnostic
不得静默近似
不得依赖未来 executor 再发现
不得因 symbol/register 两种基址而产生不同支持策略
```

## 10.5 公共 `expected` API 不得泄漏异常

除内存分配失败等不可恢复系统异常外，面向 malformed frontend object 的路径应稳定返回 `LoweringDiagnostic`。

---

# 11. Fix Review 验收清单

## 11.1 P0

- [ ] `.address_size 32` symbol base 不再被转换成 b64；
- [ ] `.address_size 32` register base 与 symbol base 使用同一支持策略；
- [ ] `.address_size 64` 的现有路径继续成功；
- [ ] `%r<1>`、`%s<1>`、`%p<1>` lowering 成功；
- [ ] scalar register 与 parameterized-one declaration 不再混淆；
- [ ] 每个 P0 都有独立回归测试。

## 11.2 P1

- [ ] 托管 CI 构建并运行 `test_ptxsim_lowering`；
- [ ] 生命周期 E2E 在托管 CI 执行；
- [ ] lowering public-header check 在托管 CI 编译；
- [ ] build-tree consumer 在托管 CI 执行；
- [ ] installed consumer 在托管 CI 执行；
- [ ] GCC、Clang、Release 和 sanitizer 至少各有 feature-on 覆盖；
- [ ] `lower_module` 对所有越界 frontend identity 返回错误而不抛异常；
- [ ] `ProgramImage -> ThreadState` 安全启动路径已实现，或 M2 计划已正式变更并获批准。

## 11.3 P2

- [ ] `ProgramError` 的 code/pc/index/instruction detail 能进入 lowering diagnostic；
- [ ] `actions/cache` 恢复 exact SHA；
- [ ] workflow lint 防止浮动第三方 action ref；
- [ ] current gate 改回 V2-M2 review/remediation；
- [ ] V2-M2 review/fix 文档按 `docs/reviews/` 治理归档。

## 11.4 Exact-head 验收

- [ ] 所有测试针对同一个修复 head SHA；
- [ ] feature-off 五矩阵全绿；
- [ ] feature-on 规定矩阵全绿；
- [ ] ASan/UBSan 实际链接并运行 lowering 测试；
- [ ] `ctest -N` 证明所有预期测试已注册；
- [ ] 本地或独立环境完成一次 clean configure/build/test；
- [ ] 无未说明的测试跳过或 `continue-on-error`。

---

# 12. 最终建议

该分支已经建立了可持续的 `exec_ir`、`ProgramImage` 与 lowering 基础，整体方向不需要推倒重来。当前问题主要集中在：

```text
模块级地址事实未进入 lowering
寄存器声明语义被 count==1 压缩
M2 真实测试没有进入 CI
ThreadState 启动一致性没有生产契约
malformed frontend identity 缺少防御
```

这些问题都可以在现有架构上定向修复，但不应带病合并，也不应在其上立即开始 M3 memory/storage 实现。

最终判定：

```text
REQUEST_CHANGES
```

建议完成 Wave A 到 Wave D 后，对修复分支进行一次新的 V2-M2 fix review。fix review 应以 exact-head 完整 CI、两个 P0 的最小复现回归、malformed-input 防御测试以及明确的 ThreadState bootstrap 决策为核心证据。
