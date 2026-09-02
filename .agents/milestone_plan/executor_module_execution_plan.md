# PTXSim `inst_execute_engine` Module Execution Plan

> **Status:** draft for review
> **Current prerequisite:** `execution_model`, `memory`, `runtime`, and `arith`
> **Blocked work:** `exec_ir` WP1 and later
> **Language/build:** C++23 / CMake / GoogleTest
> **Primary objective:** establish how one warp issue is prepared and committed before fixing the shape of `exec_ir`

---

## 1. Decision summary

The executor is designed before the C++ `exec_ir` representation.

The stable execution unit is a scheduler-selected
`execution_model::WarpIssueGroup`, not an isolated Thread and not a complete
instruction stream:

```text
scheduler selects Warp + WarpIssueGroup
                    |
                    v
           validate warp issue
                    |
                    v
             executor prepare
                    |
                    v
             executor commit
                    |
        +-----------+-----------+
        v           v           v
     Thread       memory      Warp/CTA
     PC/status    resources   sync state
```

`Thread::step()` and `Warp::step()` remain as constrained thin facades. Both
must reach the same executor contract:

```cpp
engine.step(Warp&, const WarpIssueGroup&)
```

`Thread::step()` forms a single-lane issue from its authoritative current PC
and lane ID, then forwards to that same contract. It is a convenience for
lane-local execution and tests, not a second instruction engine. Collective
instructions may reject a single-lane issue.

Thread owns the current PC value. The executor decides the next PC from the
instruction and commits it through Thread. Thread never infers fallthrough by
performing `pc + 1`.

The first implementation uses handwritten probe operations. It does not add
generated instruction records, a program image, fetch/decode, a handler
registry, or a general transaction system.

---

## 2. Why executor precedes `exec_ir`

`exec_ir` is an input contract for execution. Its required fields cannot be
known until the consumer proves how it handles:

- a group of lanes at one PC;
- per-lane predication;
- register operands and writeback;
- fallthrough and branch targets;
- lane-local failure;
- warp-scoped prepare and commit.

The existing `exec_ir` WP0 generator probe remains useful: it proves that the
packaged frontend model can be queried deterministically. It does not prove
the shape of a runtime instruction record.

Therefore:

- `.agents/milestone_plan/exec_ir_module_execution_plan.md` WP0 remains valid;
- its WP1 and later work are paused;
- the executor control-flow gate in this plan must complete before WP1 is
  redesigned and resumed;
- generated C++ instruction types must not be added merely to unblock the
  executor probe.

This plan supersedes the single-thread-first executor sequence in V2-M4 of
`.agents/project_plan.md`. It does not supersede the completed arithmetic,
execution-model, memory, or runtime work.

---

## 3. Existing architectural facts

### 3.1 Execution model

`execution_model` owns:

- Grid/CTA/Warp/Thread topology and IDs;
- the authoritative per-Thread PC and status;
- derived valid/ready/waiting/exited lane masks;
- transient `WarpIssueGroup` values;
- persistent warp rendezvous and CTA barrier state.

It does not own instructions, register storage, memory, fetching, arithmetic,
or instruction semantics.

Current gaps relevant to executor work:

- no scheduler constructs `WarpIssueGroup` values;
- `Warp::step()` does not accept an issue group;
- both existing `step()` templates use unconstrained `typename` parameters;
- `WaitReason` is stored but has no complete set/get/clear transition API;
- `Thread` has no current FunctionId or call stack.

### 3.2 Memory

`memory` owns register frames, address spaces, Tensor Memory, mbarrier state,
and asynchronous memory operations. Its managers return structured errors and
know nothing about execution topology.

Storage operations take effect immediately. There is no general transaction
or rollback API. The executor must therefore stage all fallible inputs before
the first mutation and initially limit each lane to one storage write.

### 3.3 Runtime

`runtime::LaunchRuntime` is the current launch composition layer. It owns the
Grid and memory managers and binds memory-owned handles to topology IDs.

Register and local-frame lookup requires `(ThreadId, FunctionId)`. Until call
state exists, the probe executor receives one explicit FunctionId from its
test/launch context. It must not invent a current-function field or call stack.

### 3.4 Arithmetic

`arith` provides instruction-independent numerical primitives. Executor code
maps operation controls and raw values to those primitives. `arith` must not
learn about PTX instructions, lane masks, PCs, runtime bindings, or memory.

---

## 4. Dependency and ownership rules

Required direction:

```text
common ───────────────> execution_model
common ───────────────> memory
execution_model + memory ──> runtime
runtime + arith ───────> executor

future exec_ir ────────> executor input
future scheduler ──────> executor caller
future simulator ──────> scheduler + program/fetch + executor + runtime
```

Forbidden direction:

```text
execution_model -X-> executor
memory          -X-> execution_model
memory          -X-> executor
runtime         -X-> exec_ir
executor        -X-> ptx_frontend
arith           -X-> executor/exec_ir/runtime
```

The frontend may be used by a future build-time generator and frontend
adapter. It is never a runtime executor dependency.

Do not add a separate `semantics` module for the first operations. Handwritten
executor handlers may call `arith` directly. Split a semantics target only
after multiple handlers demonstrate reusable instruction-independent code.

---

## 5. Step facades and canonical call path

### 5.1 Required engine contract

The execution-model facade should constrain the actual expression it invokes:

```cpp
template <typename Engine>
concept WarpIssueEngine =
    requires(Engine& engine, execution_model::Warp& warp,
             const execution_model::WarpIssueGroup& issue) {
      engine.step(warp, issue);
    };
```

The exact concept location and namespace may be chosen during WP0. There must
not be an unconstrained public `template <typename Engine>` facade after WP0.

### 5.2 Warp facade

Conceptual API:

```cpp
template <WarpIssueEngine Engine>
decltype(auto) Warp::step(Engine& engine, const WarpIssueGroup& issue)
    noexcept(noexcept(engine.step(*this, issue))) {
  return engine.step(*this, issue);
}
```

It must not choose a PC, build a different lane group, fetch an instruction,
or implement prepare/commit.

### 5.3 Thread facade

Conceptual behavior:

```text
read this Thread's current PC
build one correctly-sized LaneMask containing only lane_id()
forward engine.step(warp(), singleton issue)
```

This facade does not require a production `engine.step(Thread&)` overload.
Both facades reach the canonical warp-issue path directly.

Because `Thread` sees Warp through a forward declaration, implementation must
avoid a circular include. A small non-template helper implemented where Warp
is complete may construct the singleton issue; the public template remains a
thin constrained forwarder.

### 5.4 Meaning of Thread PC ownership

PC ownership and transition policy are distinct:

- Thread stores and exposes the authoritative current PC;
- executor obtains fallthrough/target information from the bound operation or
  future program representation;
- executor applies the chosen PC only during commit;
- Thread may later enforce transition invariants, but it never interprets an
  instruction to choose a destination.

The two facades are convenience syntax. They must not contain instruction
dispatch, register/memory access, or duplicated execution behavior.

---

## 6. Warp issue contract

A valid issue group must satisfy all of the following before prepare begins:

1. the lane mask width equals the Warp architectural width;
2. the group is non-empty;
3. every selected lane belongs to `Warp::valid_mask()`;
4. every selected Thread is Ready;
5. every selected Thread's current PC equals `issue.pc`;
6. the Warp belongs to the `LaunchRuntime` used by the executor.

Failure is an issue-contract error. No register, memory, PC, status, rendezvous,
or barrier state may change.

The scheduler chooses which Warp and PC to issue. The Warp may expose pure
queries used to validate or collect matching lanes, but it must not embed a
scheduling policy such as lowest-PC selection or reconvergence order.

Do not store the selected `WarpIssueGroup` in `WarpExecutionState`; it is
derived, transient state.

---

## 7. Prepare and commit

One scalar warp step has two phases.

### 7.1 Prepare

For every selected lane, in ascending LaneId order:

1. resolve the Thread and use the explicit FunctionId bound in the
   executor/test context;
2. resolve its register/local/address-space bindings;
3. read the predicate first, when present;
4. if predicated off, stage only the fallthrough transition;
5. otherwise read all operands;
6. convert raw values to the required arithmetic types;
7. compute the result without mutating machine state;
8. validate the one intended destination;
9. stage either a successful lane effect or a lane fault.

Prepare must not write storage, change PC/status, or update synchronization
state.

### 7.2 Commit

After every selected lane has been prepared:

1. commit successful lane storage effects in ascending LaneId order;
2. commit their next PC/status;
3. for faulting lanes, commit no data write and no next PC;
4. mark faulting lanes Trapped according to the current deterministic probe
   policy;
5. return the step report.

Every storage write result must still be checked. Prepare validates every
condition observable under the executor's single-host-thread, stable-resource
lifetime contract, so the first register write should not fail. If it does,
the write API guarantees that the lane's value was not partially changed; the
executor records a commit fault, does not update that lane's PC, and marks it
Trapped. Previously committed independent lanes are not rolled back.

The warp issue has a unified commit phase but is not an all-or-nothing
architectural transaction. Scalar lanes are independent: a fault in one lane
does not roll back another lane's successful result. This prevents behavior
from depending on whether the scheduler issued lanes separately or together.

Collective instructions are different. They require a complete group-level
prepare rule and are deferred until their participant and fault semantics are
specified.

### 7.3 Initial storage restriction

The first probe permits at most one register write per lane. This is sufficient
to validate executor sequencing against the existing RegisterManager without
building a general effect graph or rollback facility.

Add multi-write staging only when the first real supported instruction needs
it. Add cross-resource transaction machinery only if PTX-visible semantics
require atomic commit across those resources.

---

## 8. PC, status, and control rules

| Outcome | Data effect | PC/status commit |
|---|---|---|
| scalar success | staged write is committed | explicit fallthrough PC; Ready |
| predicated off | none | explicit fallthrough PC; Ready |
| taken branch | none unless instruction defines one | target PC; Ready |
| untaken branch | none | fallthrough PC; Ready |
| lane fault | none | retain faulting PC; Trapped |
| exit | instruction-specific prior effects only | Exited; PC no longer scheduled |
| wait | deferred | must be defined with its wakeup protocol |

No executor path may assume `ProgramCounter` is a byte address or increment it
implicitly. The probe operation supplies an explicit successor. A future
program representation decides how current PC maps to instruction and
fallthrough PC.

Predication is evaluated before non-predicate operands. A predicated-off lane
must not fault because an unused data operand is uninitialized or invalid.

Branch divergence requires no reconvergence structure for the first probe.
Each Thread commits its own next PC; a later scheduler naturally forms
different issue groups. Reconvergence policy remains separate scheduler work.

---

## 9. Errors and reporting

The executor distinguishes two levels.

### 9.1 Step-level errors

These reject the entire issue before mutation:

- malformed or empty WarpIssueGroup;
- foreign Warp/runtime pairing;
- missing bound execution context such as the probe FunctionId;
- unsupported/unrecognized bound operation;
- executor invariant failure represented as a structured internal error when
  it can arise from runtime input.

### 9.2 Lane faults

These are discovered during prepare or an individually checked commit and
reported with LaneId:

- missing/stale register or local-frame binding;
- register slot/width/uninitialized-read error;
- address resolution or memory access error;
- arithmetic conversion or operation failure;
- an unexpected failure of the lane's single staged storage write;
- later instruction-specific trap conditions.

The first return contract should be no larger than needed:

```text
expected<StepReport, StepError>

StepReport
└── zero or more { LaneId, LaneFault }
```

Do not duplicate committed/ready/exited masks in the report while they remain
cheaply derivable from the issue and Thread state. Add trace/event payloads
only with the debugger or event-sink milestone.

Mapping a low-level memory/arithmetic error to trap, unsupported behavior, or
another simulator policy belongs to executor/simulator policy. The memory and
arith modules keep returning their domain errors unchanged.

---

## 10. Initial module shape

Create files only as their work package begins. The expected stable minimum is:

```text
submod/inst_execute_engine/
├── CMakeLists.txt
├── include/
│   └── inst_execute_engine.hpp
├── src/
│   └── inst_execute_engine.cpp
└── test/
    └── test_inst_execute_engine.cpp
```

The CMake target and installed alias are `ptxsim_inst_execute_engine` and
`ptxsim::inst_execute_engine`; public C++ declarations use
`ptxsim::inst_execute_engine`.

The probe target remains build-tree-only until the post-control-flow gate has
reviewed its public types. Do not add it to root package installation merely
because the library compiles.

Keep the initial test operations handwritten and private to the executor
probe. Do not add YAML/code generation or claim ABI stability for them.

---

## 11. Work packages

### WP0 — Canonical step facade

**Goal:** preserve both convenience facades while proving they share one
warp-issue engine contract.

Tasks:

- constrain both facade templates with a concept/requires expression;
- change `Warp::step()` to accept a `WarpIssueGroup`;
- make `Thread::step()` construct a single-lane issue using its current PC;
- forward both directly to `engine.step(Warp&, const WarpIssueGroup&)`;
- add the missing Warp facade test;
- replace the existing Thread facade tests so they verify canonical forwarding,
  issue PC, mask width, and selected lane;
- leave all instruction semantics outside execution_model.

Acceptance:

- an engine without the canonical step expression is rejected at compile time;
- Thread and Warp facades reach the same fake-engine overload;
- Thread stepping selects exactly itself and its current PC;
- no production `engine.step(Thread&)` path is required;
- `execution_model` still depends only on `common`.

### WP1 — Register-move executor probe

**Goal:** prove warp-wide prepare/commit through runtime register bindings.

Use one private handwritten operation equivalent to:

```text
MoveB32Probe {
  source RegisterSlot
  destination RegisterSlot
  explicit fallthrough ProgramCounter
}
```

The probe executor is bound to:

- one `LaunchRuntime`;
- one explicit FunctionId;
- the concrete probe operation.

Tasks:

- add the build-tree `ptxsim_inst_execute_engine` target and one test
  executable;
- validate the complete issue group before prepare;
- resolve `(ThreadId, FunctionId)` register frames through LaunchRuntime;
- read every selected lane before any lane is committed;
- stage one b32 destination write and the explicit successor PC;
- commit successes and lane-local traps in one commit phase;
- return step-level errors separately from lane faults.

Acceptance tests:

- one lane moves b32 and advances to the explicit PC;
- two successful lanes use isolated register frames;
- one initialized and one uninitialized lane are fully prepared before commit;
- the successful lane commits while the failing lane retains PC, leaves its
  destination untouched, and becomes Trapped;
- missing/stale bindings never produce partial writes or dangling access;
- malformed issue groups leave the whole warp and storage unchanged;
- Thread and Warp facades produce identical results for equivalent singleton
  issues.

No `arith` link is required for this work package.

### WP2 — Predication and arithmetic probe

**Goal:** prove operand gating and `arith` integration without designing IR.

Add only:

- an optional predicate register plus explicit negation;
- one private `AddU32Probe` with register/immediate sources as required by the
  test;
- explicit fallthrough PC.

Tasks:

- read and apply the predicate before data operands;
- verify a false predicate advances PC without reading invalid data operands;
- map b32 raw values to `std::uint32_t`;
- invoke the public `arith::add` API with an executor-bound immutable
  `arith::context` and explicit `integer_control`;
- consume the returned numeric value for writeback and retain/ignore
  `integer_status` according to the operation contract; the initial wrapping
  `add.u32` does not create an architectural status register;
- stage the resulting b32 write;
- map conversion/arithmetic failure through the executor error policy.

Acceptance tests:

- true, false, and negated predicates;
- false predicate suppresses an otherwise uninitialized operand fault;
- register/register and the minimum required immediate case;
- unsigned wraparound agrees with `arith`;
- no SoftFloat/private arithmetic header is included by executor code.

### WP3 — Branch, exit, and divergence probe

**Goal:** establish the control-flow envelope required from future `exec_ir`.

Tasks:

- add one direct branch probe with explicit target and fallthrough PCs;
- reuse the same per-lane predicate logic;
- add one exit probe;
- commit different next PCs for lanes in the same issue group;
- use a test-only pure grouping helper to demonstrate that Ready lanes can be
  regrouped by their committed PCs without stored active/reconvergence masks;
  do not add a scheduler target or scheduling policy.

Acceptance tests:

- taken, untaken, predicated-off, and divergent branch behavior;
- non-participating lanes remain unchanged;
- exited lanes disappear from `ready_mask()`;
- a faulting branch operand retains the source PC;
- no scheduler, reconvergence stack, ProgramImage, or label lookup is added.

### Gate A — Resume `exec_ir` design

After WP3, audit the actual probe inputs and revise the `exec_ir` plan.

The audit must answer only facts proven by the executor:

- which operand reference/value forms are required;
- whether predicate belongs in a common instruction envelope;
- where explicit fallthrough and branch target PCs belong;
- what operation identity the dispatch actually consumes;
- which facts are compile-time/generated and which remain handwritten;
- whether executor owns fetch or receives an already-fetched instruction;
- how FunctionId/current frame is supplied.

Only then may `exec_ir` WP1 be rewritten and resumed. The probe operation
types may be replaced rather than preserved for compatibility.

### WP4 — `exec_ir` consumption and dispatch

**Prerequisite:** Gate A and the revised `exec_ir` WP1 are complete.

Tasks are intentionally bounded by the revised plan:

- consume owned frontend-independent instruction values;
- dispatch the supported bounded variant without a registry/factory;
- keep all runtime handles and topology objects outside `exec_ir`;
- preserve the prepare/commit behavior proved by WP1-WP3;
- reject unsupported operations before mutation.

Do not add program loading or source/frontend ownership to executor.

### WP5 — Scalar load/store

**Goal:** connect executor sequencing to existing address-space resources.

Initial scope:

- selected scalar global load/store;
- then local/shared forms only when their address-context requirements are
  represented;
- explicit size/alignment and generic-address resolution;
- one storage write per lane.

Requirements:

- loads complete all reads before register writeback;
- a store is the lane's only storage effect in its first implementation;
- MemoryRegion/AddressSpace errors are mapped at executor level;
- stores with racing lane addresses use one documented deterministic LaneId
  commit order; no claim is made for PTX data-race behavior;
- atomic operations are not implemented as ordinary stores.

Acceptance includes bounds, alignment, missing binding, initialization,
read-only storage, partial-warp masks, and lane-local fault isolation.

### WP6 — Warp and CTA synchronization

**Prerequisites:** participant rules and wait-state ownership are specified.

Before implementation, complete the execution-model wait API:

- observable WaitReason;
- entering Waiting with a reason;
- returning to Ready while clearing the reason;
- explicit PC rule for arrival, blocking, release, and re-execution.

Warp synchronization then uses `WarpSyncState`; CTA synchronization first
converges participating lanes within each Warp and then updates
`CtaBarrierState` at warp granularity.

Required tests:

- partial arrival and later completion;
- repeated generations at the same PC;
- partial final Warp;
- waiting lanes are not issued;
- all released lanes resume exactly once;
- collective prepare failure does not leave an impossible participant set;
- reduction result writeback occurs only after barrier completion.

Do not implement CTA wakeup through `memory`; it belongs to executor/simulator
coordination.

### WP7 — Async memory and mbarrier integration

**Prerequisite:** simulator progress/wakeup ownership is defined.

The existing `AsyncMemoryEngine` remains topology-free. Executor/simulator
code owns the association between an async handle and waiting lanes.

Tasks:

- issue the minimum supported async copy operation;
- record its executor-side wait association;
- progress the memory engine deterministically;
- map completion/failure to lane status and mbarrier state;
- ensure an operation is neither completed nor woken twice.

Do not add host threads, wall-clock timing, or a third-party state-machine
dependency.

### WP8 — Calls and function-scoped resources

**Prerequisites:** a program/function model and call-state owner exist.

This work package must first resolve:

- authoritative current FunctionId per Thread;
- call/return PC storage;
- register/local-frame allocation and destruction;
- function-parameter binding, which LaunchRuntime does not currently expose;
- divergent call behavior.

Executor then implements call/return as explicit prepare/commit transitions.
Do not add a placeholder call stack merely to remove the probe FunctionId
parameter.

### WP9 — Packaging and simulator integration

After the executor public contract is stable:

- install/export `ptxsim::inst_execute_engine`;
- add it to the root package target list;
- add build-tree and installed-package consumer checks;
- compose it with the future scheduler/program source in `simulator`;
- add deterministic step-limit and unsupported-instruction handling to the
  simulator loop, not the arithmetic or memory modules;
- run GCC/Clang Debug/Release and sanitizer gates.

---

## 12. Test strategy

Each work package adds the smallest test that fails if its new sequencing rule
breaks. Prefer one executor test binary and focused GTest cases; do not create
one executable per instruction.

Required test layers:

```text
execution_model tests
  facade forwarding + masks + PC/status state

executor unit tests
  explicit issue + private/revised operation + prepare/commit

runtime integration tests
  topology-to-register/address bindings

later simulator tests
  scheduling/fetch/progress loop
```

For every mutating instruction test, capture the pre-step values and assert
both intended changes and required non-changes. A failing lane must prove that
its destination and PC were not partially committed.

Final verification for an implemented work package should include:

```text
cmake configure with BUILD_TESTING=ON
build the changed module and dependents
run execution_model/runtime/executor focused tests
run full CTest before commit
run ASan + UBSan for ownership/lifetime changes
inspect installed-package consumers once executor is exported
```

---

## 13. Explicit non-goals

The executor milestone does not authorize:

- regenerating the full PTX instruction universe;
- copying frontend instruction definitions;
- a virtual handler hierarchy or handler registry;
- a generic effect graph or rollback engine;
- a scheduler/reconvergence algorithm inside Warp;
- a ProgramImage, loader, source map, or call stack before its gate;
- execution_model knowledge of register/memory handles;
- memory knowledge of Thread/Warp/CTA;
- arithmetic knowledge of PTX instruction forms;
- asynchronous host execution or timing simulation;
- an installed executor API before Gate A stabilizes the consumer contract.

---

## 14. Review gates

Every executor change must answer:

1. Does it use `WarpIssueGroup` as the real execution unit?
2. Do both facades reach the same canonical engine call?
3. Is Thread still the only owner of current PC/status?
4. Does executor, rather than Thread, choose next PC?
5. Is every predictable failure checked before the first mutation, and is
   every commit result still handled?
6. Can a lane fault leave its destination or PC partially updated?
7. Are scalar and collective semantics kept distinct?
8. Does memory remain topology-free?
9. Does execution_model remain executor-free?
10. Is the new representation required by an implemented operation rather
    than a speculative future instruction?
11. Are template inputs constrained with concepts/requires?
12. Is one focused regression test shipped with the behavior?

If any answer is unclear, stop that work package and resolve the ownership or
commit rule before adding another operation.

---

## 15. Completion criteria

The executor foundation is ready to drive `exec_ir` design when WP0-WP3 and
Gate A are complete:

- Thread and Warp facades share one constrained warp-issue engine contract;
- scalar register execution performs warp prepare followed by deterministic
  commit;
- predicate, arithmetic, branch, exit, PC, and lane-fault behavior are tested;
- no `exec_ir`, frontend, program, or scheduler abstraction was invented to
  make the probe compile;
- the revised `exec_ir` shape is justified field-by-field by executor usage.

The complete executor module is ready for simulator integration when the
applicable later work packages are complete, its target is installable, and
all supported instruction families preserve the same validated prepare/commit
contract.
