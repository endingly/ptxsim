# PTXSim `simulator` Module Execution Plan

> **Status:** active; WP0--WP2 and package export are implemented through
> `Simulator::step()`/`run()`; WP3 is next
> **Prerequisites:** `execution_model`, `memory`, `runtime`, `exec_ir`,
> `exec_ir_lowering`, and `inst_execute_engine` are implemented
> **Primary objective:** compose the existing modules into the first
> deterministic PTX execution loop
> **Language/build:** C++23 / CMake / GoogleTest

---

## 1. Outcome

The next milestone is a vertical execution slice:

```text
checked ResolvedModule
        |
        v
exec_ir_lowering::lower
        |
        v
ExecutableProgram + prepared LaunchRuntime
        |
        v
Simulator: select -> issue -> fetch -> execute -> repeat
        |
        v
all Threads exited, or a structured stop/error result
```

The current accepted program is one function and one warp executing existing
`mov`, `add`, `setp.lt.u32`, predicated `bra`, and `exit` semantics. Scalar
memory is the next vertical slice.

## 2. Architectural boundary

`simulator` is the composition root. It may depend on the existing execution
subsystems; none of those subsystems may acquire a dependency on `simulator`.

```text
exec_ir_lowering -> exec_ir + ptx_frontend
runtime          -> execution_model + memory
inst_execute_engine
                 -> exec_ir + execution_model + runtime + arith
simulator        -> exec_ir + execution_model + runtime
                    + inst_execute_engine
```

The important ownership rules are:

- `Thread` remains the authoritative owner of its current PC and lifecycle
  status.
- `ExecutableProgram` owns immutable instructions and function layouts for the
  simulator lifetime.
- `LaunchRuntime` owns the launch topology, memory managers, and
  topology-to-resource bindings.
- `WarpIssueGroup` is transient scheduling output, never persistent warp state.
- `InstExecuteEngine` receives an already-fetched instruction and issue group;
  it does not fetch, select a warp, or own a program.
- `memory` and `execution_model` remain sibling modules with no dependency on
  each other. Their identities meet only in `runtime` and `simulator`.
- `simulator` does not interpret instruction operands or directly mutate
  register/memory contents. Instruction effects remain executor work.
- `simulator` accepts an `ExecutableProgram` and never links the frontend or
  lowering target. Only integration tests may build a program through lowering.

For the no-call milestone, one entry `FunctionId` applies to every Thread.
Function identity must move into per-thread activation state before call/return
is implemented; it must not become duplicated permanent state in `Simulator`.

## 3. Minimal public contract

The current implementation exposes an owning `Simulator` with bounded `step()`
and `run()` operations. It owns the immutable program and borrows the prepared
runtime and arithmetic context. `step()` reports issued/completed/trapped/
deadlocked state, an optional issue group, and lane faults; `run()` counts only
groups issued by that call.

## 4. Deterministic issue and step algorithm

One `step()` executes at most one warp issue group:

1. Traverse CTAs and warps in their stable topology order.
2. Select the first warp containing a ready Thread.
3. Select the PC of its lowest-numbered ready lane.
4. Form a `WarpIssueGroup` from all ready lanes in that warp at the selected PC.
5. Combine the entry function and selected PC into `common::CodeLocation`.
6. Fetch the instruction with `ExecutableProgram::fetch()`.
7. Ask `ExecutableProgram::fallthrough()` for the same-function successor.
   Convert only `no_fallthrough` to an absent successor; propagate other
   program errors.
8. Call `InstExecuteEngine::execute()` with the selected warp, issue group,
   fetched instruction, and optional successor.
9. Return the executor report. Do not update Thread PC/status a second time;
   successful executor commit already owns that transition.

Lanes at different PCs form separate issue groups. The first scheduler is a
deterministic topology-order scan, not a model of hardware scheduling or
reconvergence. Introduce a scheduler abstraction only when a second policy is
actually required.

Before selecting work, `step()` distinguishes:

- every Thread exited: completed;
- any trapped Thread: trapped stop;
- live Threads exist but none are ready: stalled stop;
- at least one ready Thread: issue exactly one group.

Waiting-state progress is deferred until an implemented async or CTA-level
synchronization path needs it.

## 5. Error and stop contract

Do not flatten existing subsystem diagnostics into strings. `SimulatorError`
must retain the originating structured error for:

- invalid program fetch or successor lookup;
- invalid executor issue/dispatch;
- invalid entry-function or runtime binding required to begin execution.

Lane faults remain part of the executor step report: affected lanes are
trapped according to executor commit rules, while valid lanes may commit.
`Simulator::step()` and `run()` retain the faulting issue's lane-local faults
and stop immediately after that commit. A pre-existing trapped runtime has no
recorded issuing source and therefore reports no faults.

## 6. Work packages

### WP0 — Module and integration contract (implemented)

Add `submod/simulator` with one public header, one implementation file, one
test executable, and one CMake target. Install/export work may wait until the
runtime contract is proven.

Acceptance:

- the dependency direction in section 2 is enforced by CMake;
- construction makes program and borrowed-runtime lifetimes explicit;
- an already-completed launch and invalid entry function return structured
  results; and
- no instruction semantics or memory forwarding facade appears in the module.

### WP1 — Single-warp execution loop (implemented)

Implement deterministic issue grouping, program fetch, optional fallthrough,
executor invocation, and bounded `run()` for one entry function.

The integration test must start from real PTX, resolve and lower it, prepare
the required launch/register bindings, and execute:

```ptx
mov.b32 ...;
add.u32 ...;
exit;
```

Acceptance:

- all lanes observe the expected final register value;
- Thread PCs advance only through executor commit;
- all Threads finish as `Exited`;
- the run report contains the exact number of issued groups; and
- a too-small step limit stops deterministically without corrupting state.

### WP2 — Branch and divergent PCs (implemented)

Use the same grouping algorithm for lanes at different PCs. Add direct and
predicated branch coverage without a divergence stack.

Acceptance:

- taken and fallthrough lanes commit different PCs correctly;
- subsequent steps issue one same-PC lane group at a time;
- a terminal branch/exit does not require a successor; and
- an unsupported instruction is reported before a missing-fallthrough error.

### WP3 — Existing scalar memory path (next)

Exercise existing generic/global scalar `ld` and `st` through prepared
`LaunchRuntime` bindings. Simulator passes context to the executor and does not
resolve addresses itself.

Acceptance:

- stores and loads round-trip through the bound address space;
- missing or invalid bindings preserve the existing structured lane fault;
- valid lanes may commit when another lane faults, according to current
  executor policy; and
- `memory` still has no dependency on `execution_model` or `simulator`.

### WP4 — Multiple warps

Extend the existing topology-order selection from one warp to multiple warps.
The execution chain through `simulator` is already installed/exported and
covered by build-tree and installed-package consumers.

Acceptance:

- two warps make independent progress in deterministic order;
- completion and trapped/stalled results are derived from Thread states;
- build-tree and installed-package consumers continue to link successfully; and
- GCC/Clang Debug/Release plus sanitizer gates pass.

## 7. Deferred work

The following are not prerequisites for the first simulator loop:

- call/return, activation records, per-thread function identity, and call-stack
  resource lifetimes;
- async-memory progress, wakeup, `mbarrier`, and CTA/grid synchronization;
- special-register execution beyond the implemented `%tid.x` source;
- reconvergence stacks, scoreboards, latency, or cycle timing;
- pluggable scheduling policies;
- CLI loading, tracing/event sinks, or a device/context layer;
- new instruction families or broader lowering leaf support.

Each deferred feature should receive a focused plan only when its first
executable consumer is selected.

## 8. Verification gate

Every simulator work package runs its focused test plus the existing regression
targets that define the boundary it composes:

```text
test_ptxsim_exec_ir_lowering
test_ptxsim_execution_model
test_ptxsim_inst_execute_engine
test_ptxsim_runtime
relevant memory tests for memory-executing work
```

Documentation-only changes require link/diff checks. Source implementation
must additionally pass the repository's configured GCC/Clang and sanitizer
gates before the simulator target is considered exportable.
