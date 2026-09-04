# PTXSim `inst_execute_engine` Module Execution Plan

> **Status:** WP0-WP5 and the minimal WP6 warp synchronization slice are implemented
> **Current prerequisite:** `execution_model`, `memory`, `runtime`, and `arith`
> **Next integration:** deterministic program fetch and issue orchestration in
> the [simulator module plan](simulator_module_execution_plan.md)
> **Language/build:** C++23 / CMake / GoogleTest
> **Primary objective:** establish how one warp issue is prepared and committed before fixing the shape of `exec_ir`

---

## 1. Decision summary

The executor is designed before the C++ `exec_ir` representation.

The stable execution unit is a scheduler-selected
`execution_model::WarpIssueGroup`, not an isolated Thread and not a complete
program. The future Simulator owns the immutable `ExecutableProgram` and
performs fetch; executor consumes the fetched instruction:

```text
scheduler selects Warp + WarpIssueGroup
                    |
                    v
     Simulator forms a common::CodeLocation and fetches
       ExecutableProgram::fetch(location)
       and derives the local successor
                    |
                    v
      executor validates / dispatches
                    |
                    v
        executor prepare / commit
                    |
        +-----------+-----------+
        v           v           v
     Thread       memory      Warp/CTA
     PC/status    resources   sync state
```

`Thread::step()` and `Warp::step()` remain as constrained thin facades. Both
reach the same step-provider contract, which the future Simulator implements:

```cpp
simulator.step(Warp&, const WarpIssueGroup&)
```

`Thread::step()` forms a single-lane issue from its authoritative current PC
and lane ID, then forwards to that same contract. Simulator fetches the
instruction and invokes the lower executor entry:

```cpp
executor.execute(warp, issue, instruction, fallthrough);
```

The facade is a convenience for lane-local execution and tests, not a second
instruction engine. Collective instructions may reject a single-lane issue.

Thread owns the sole authoritative current PC value. In the planned production
path, its function/activation state plus that local PC form
`common::CodeLocation`;
Simulator stores no duplicate authoritative PC. `ExecutableProgram` derives
the flat storage offset and same-function fallthrough; executor selects
target/fallthrough and commits the local PC through Thread.

The current executor implementation consumes an already-fetched `exec_ir`
instruction through its existing handwritten static dispatch table. It does not
add a production Simulator, a dynamic handler registry, or a general
transaction system.

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
- executor WP1-WP3 completed the control-flow and dispatch gate;
- revised `exec_ir` WP1 is implemented from the proven consumer contract;
- fully-bound `exec_ir` instruction types replace rather than preserve the
  private probe types in executor WP4.

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

Current gaps relevant to later integration:

- no production scheduler constructs `WarpIssueGroup` values;
- no Simulator owns/fetches an `ExecutableProgram` by `common::CodeLocation`;
- `Thread` owns the authoritative wait state: `Waiting` iff `WaitReason` is
  non-`None`. Entering, releasing, exiting, and trapping clear/set that reason
  through the Thread API without changing the authoritative PC;
- `Thread` has no current FunctionId, activation, or call stack.

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

Register and local-frame lookup currently requires `(ThreadId, FunctionId)`.
The explicit probe `FunctionId` and this binding are MVP assumptions: they are
sufficient only while a Thread has one live activation for a function. Until
call state exists, the probe executor receives that ID from its test/launch
context. It must not invent a current-function field, activation, or call
stack.

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

future exec_ir lowering ──> fully-bound ExecutableProgram
future scheduler ─────────> executor caller
future simulator ─────────> scheduler + program/fetch + executor + runtime
```

Forbidden direction:

```text
execution_model -X-> executor
memory          -X-> execution_model
memory          -X-> executor
runtime         -X-> frontend symbolic IR
executor        -X-> ptx_frontend
arith           -X-> executor/exec_ir/runtime
```

The frontend is mandatory at lowering input, but never a runtime executor
dependency. An already-built `ExecutableProgram` contains no frontend identity
needed for execution.

Do not add a separate `semantics` module for the first operations. Handwritten
executor handlers may call `arith` directly. Split a semantics target only
after multiple handlers demonstrate reusable instruction-independent code.

---

## 5. Step facades and canonical call path

### 5.1 Required step-provider contract

The execution-model facade should constrain the actual expression it invokes:

```cpp
template <typename Stepper>
concept WarpIssueStepper =
    requires(Stepper& stepper, execution_model::Warp& warp,
             const execution_model::WarpIssueGroup& issue) {
      stepper.step(warp, issue);
    };
```

The exact concept location and namespace may be chosen during WP0. There must
not be an unconstrained public `template <typename Engine>` facade after WP0.
The future production Stepper is Simulator; `InstExecuteEngine` instead exposes
the lower `execute(..., instruction, fallthrough)` operation and does not
perform fetch.

### 5.2 Warp facade

Conceptual API:

```cpp
template <WarpIssueStepper Stepper>
decltype(auto) Warp::step(Stepper& stepper, const WarpIssueGroup& issue)
    noexcept(noexcept(stepper.step(*this, issue))) {
  return stepper.step(*this, issue);
}
```

It must not choose a PC, build a different lane group, fetch an instruction,
or implement prepare/commit.

### 5.3 Thread facade

Conceptual behavior:

```text
read this Thread's current PC
build one correctly-sized LaneMask containing only lane_id()
forward stepper.step(warp(), singleton issue)
```

This facade does not require a production `stepper.step(Thread&)` overload.
Both facades reach the canonical warp-issue path directly.

Because `Thread` sees Warp through a forward declaration, implementation must
avoid a circular include. A small non-template helper implemented where Warp
is complete may construct the singleton issue; the public template remains a
thin constrained forwarder.

### 5.4 Meaning of Thread PC ownership

PC ownership and transition policy are distinct:

- Thread stores and exposes the authoritative current PC;
- Simulator derives `common::CodeLocation` from the issued Thread/activation
  state and local issue PC, but stores no duplicate authoritative PC;
- `ExecutableProgram` derives flat storage offsets and local fallthrough;
  the branch record carries an explicit function-local target;
- executor receives both the fetched instruction and fallthrough;
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

`bar.warp.sync` is the implemented collective instruction. It prepares every
issued lane as a group and records no arrival until all group validation
succeeds. Other collective forms remain deferred until their participant and
fault semantics are specified.

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
| warp sync, incomplete arrival | group prepare succeeds | retain synchronization PC; Waiting(WarpSync) |
| warp sync, release | all participants arrive | successor PC; Ready |
| CTA sync / async wait | deferred | deferred |

No executor path may assume `ProgramCounter` is a byte address or increment it
implicitly. In the planned production path it is a checked function-local index;
`ExecutableProgram` supplies the same-function successor and executor receives
that fallthrough value.

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
- unsupported fetched opcode/type/modifier combination;
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
- forward both directly to `stepper.step(Warp&, const WarpIssueGroup&)`;
- add the missing Warp facade test;
- replace the existing Thread facade tests so they verify canonical forwarding,
  issue PC, mask width, and selected lane;
- leave all instruction semantics outside execution_model.

Acceptance:

- an engine without the canonical step expression is rejected at compile time;
- Thread and Warp facades reach the same fake-engine overload;
- Thread stepping selects exactly itself and its current PC;
- no production `stepper.step(Thread&)` path is required;
- `execution_model` still depends only on `common`.

### WP1 — Register-move executor probe

**Goal:** prove warp-wide prepare/commit through runtime register bindings.

Use one private handwritten operation equivalent to:

```text
MoveProbe {
  type b32
  source RegisterSlot
  destination RegisterSlot
}
```

The probe executor is bound to:

- one `LaunchRuntime`;
- one explicit FunctionId;
- one immutable `arith::context` after WP2.

The already-fetched probe instruction and explicit fallthrough are arguments
to `execute()`, not constructor-bound executor state.

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
- one private `AddProbe` whose separate data-type field currently accepts
  `u32`, with register/immediate sources as required by the test;
- an explicit fallthrough argument supplied by the future fetch owner.

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

- add one direct branch probe with an explicit target; fallthrough remains a
  separate executor input;
- reuse the same per-lane predicate logic;
- add one exit probe;
- commit different next PCs for lanes in the same issue group;
- use a test-only pure grouping helper to demonstrate that Ready lanes can be
  regrouped by their committed PCs without stored active/reconvergence masks;
  do not add a scheduler target or scheduling policy.

Acceptance tests:

- unpredicated and predicate-true branches take the target;
- a predicate-false branch is predicated off and takes fallthrough;
- divergent lanes commit different target/fallthrough PCs;
- non-participating lanes remain unchanged;
- exited lanes disappear from `ready_mask()`;
- a faulting branch predicate retains the source PC;
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

Gate A and the follow-up dispatch audit decided:

- `Thread` is the only authoritative PC owner;
- the current explicit `FunctionId` and `(ThreadId, FunctionId)` runtime
  bindings are MVP probe assumptions, not a call-capable execution context;
- the future Simulator owns an immutable `ExecutableProgram`, derives a
  `common::CodeLocation` from Thread/activation state and `WarpIssueGroup::pc`,
  fetches by that semantic location, derives function-local fallthrough, and
  calls executor;
- executor stores no instruction and exposes
  `execute(warp, issue, instruction, fallthrough)`;
- top-level `Op` contains only `mov`, `add`, `bra`, and `exit` opcode identity;
- data type, modifier, and operand form remain per-op record fields and may
  drive a second dispatch inside that opcode handler;
- the existing handwritten static table performs first-level dispatch; static
  generation remains conditional on demonstrated repetition, and no dynamic
  registry or duplicated `Op`/payload tag is permitted.

### WP4 — Fully-bound `exec_ir` consumption and dispatch (implemented)

**Prerequisite:** Gate A plus `exec_ir` WP1/WP2 are complete.

Tasks are intentionally bounded by the revised plan:

- consume fully-bound `exec_ir::Instruction` values, never frontend symbolic
  IR;
- receive an already-fetched instruction and an optional function-local
  successor; use `exec_ir::may_fallthrough()` to reject a missing required
  successor before any lane mutation;
- dispatch once by pure `exec_ir::Op` through the existing handwritten static
  table; static generation remains conditional on demonstrated repetition;
- dispatch within the selected handler by normalized type/form/modifier only
  when the implemented opcode requires it;
- do not encode type/modifier combinations in `Op`;
- do not add a dynamic registry/factory;
- keep all runtime handles and topology objects outside `exec_ir`;
- preserve the prepare/commit behavior proved by WP1-WP3 and the
  constructor-bound MVP `FunctionId` frame context;
- reject a missing successor before mutation when predication or operation
  semantics can select fallthrough;
- reject unsupported operations before mutation.

Do not add program loading, frontend ownership, or source ownership to
executor.

### WP5 — Scalar load/store (implemented)

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

Implemented scope is four-byte-aligned, little-endian `ld.u32`/`st.u32`.
Explicit global treats the b64 register value as a region-relative address;
generic resolves through the bound `ExecutionAddressContext`, including any
bound byte-addressable space (and therefore naturally reports read-only
constant writes). Load preparation reads memory then stages register writeback.
Store preparation reads operands, resolves the address, and calls
`validate_write` before staging its sole memory write. Commit is ascending
`LaneId`; same-address stores therefore leave the highest lane's value, without
claiming PTX data-race semantics. Explicit local/shared forms and address
offsets remain deferred.

### WP6 — Warp and CTA synchronization (minimal warp sync implemented)

**Prerequisites:** wait-state ownership and synchronization PC semantics are
specified.

The Thread wait-state prerequisite is complete: a waiting Thread records a
non-`None` `WaitReason`, all non-waiting statuses record `None`, and the
transition APIs preserve PC. Thread owns this state and scheduler eligibility.

The synchronization PC rule is also decided:

- the Thread transition API itself never changes PC;
- when a synchronization arrival does not complete its generation, the owner
  marks each participating Thread Waiting and retains that synchronization
  instruction's current PC;
- when the generation completes, the synchronization owner/executor writes
  that instruction's successor PC to every participant and calls
  `mark_ready`; participants do not re-execute the synchronization
  instruction;
- scheduler issue excludes waiting Threads; and
- failed collective prepare validation must not begin or mutate a rendezvous.

The minimal `bar.warp.sync` instruction is implemented. It validates one
non-empty, identical b32 membership mask across an issue, records partial
arrivals in `WarpSyncState`, leaves arriving lanes Waiting at the instruction
PC, and releases all participants to the supplied successor on completion.
CTA synchronization, reductions, and scheduler-owned wakeup policy remain
deferred.

At first arrival, the executor accepts only participants that are currently
Ready. Later deadlock detection and trap propagation remain simulator policy;
the executor only guarantees that a failed collective prepare does not begin a
rendezvous or partially add arrivals.

Warp synchronization uses `WarpSyncState`; future CTA synchronization first
converges participating lanes within each Warp and then updates
`CtaBarrierState` at warp granularity.

Required tests:

- partial arrival and later completion;
- repeated generations at the same PC;
- partial final Warp;
- waiting lanes are not issued;
- all released lanes resume exactly once;
- collective prepare failure does not begin a rendezvous or partially add arrivals;
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

**Prerequisites:** `ExecutableProgram` fetch is integrated and a call-state
owner exists.

This work package must first resolve:

- `common::CodeLocation` as authoritative static current location per Thread;
- `ActivationId` and `CallStack` ownership;
- `common::CodeLocation return_to` plus caller activation in each call frame;
- activation-owned register/local/parameter frame allocation and destruction;
- function-parameter binding, which LaunchRuntime does not currently expose;
- divergent call behavior.

Executor then implements call/return as explicit prepare/commit transitions.
Do not add a placeholder call stack merely to remove the MVP probe FunctionId.

### WP9 — Packaging and simulator integration (moved)

The executor contract is stable enough for composition. Program fetch,
deterministic issue, step limits, packaging, and installed-consumer checks are
now owned by the [simulator module plan](simulator_module_execution_plan.md).

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
- a module loader, function/source metadata image, or call stack before its
  gate;
- execution_model knowledge of register/memory handles;
- memory knowledge of Thread/Warp/CTA;
- arithmetic knowledge of PTX instruction forms;
- asynchronous host execution or timing simulation;
- an installed executor API before generated `exec_ir` replaces probe types.

---

## 14. Review gates

Every executor change must answer:

1. Does it use `WarpIssueGroup` as the real execution unit?
2. Do both facades reach the same canonical step-provider call?
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

- Thread and Warp facades share one constrained warp-issue step-provider
  contract;
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
