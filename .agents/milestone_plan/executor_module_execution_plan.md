# PTXSim `inst_execute_engine` Module Execution Plan

> **Status:** WP0-WP5 and the minimal WP6 warp synchronization slice are implemented
> **Current prerequisite:** `execution_model`, `memory`, `runtime`, and `arith`
> **Current integration:** deterministic program fetch and issue orchestration in
> the [simulator module plan](simulator_module_execution_plan.md)
> **Language/build:** C++23 / CMake / GoogleTest
> **Primary objective:** preserve validated warp-issue prepare/commit behavior
> while extending the supported execution semantics

---

## Current follow-up — Generated Add execution family

The adopted [execution-family architecture](../arch/instruction_execution_families.md)
supersedes the historical handwritten-handler approach for the current Add
work. Scope is all nine Add variants and their declared types/controls in the
pinned frontend specification, not extended-precision `add.cc` / `addc`.
Other opcode implementations keep their existing behavior.

- [x] Generate ValueALU preparation and exhaustive Add form/type dispatch into
  private engine build artifacts from the existing projection.
- [x] Implement typed codecs, pure Add semantics and control mapping; reuse
  existing effect/commit and instruction-independent arithmetic primitives.
- [x] Remove the historical u32-only Add path and fix scalar lowering width
  preservation so real frontend input reaches all Add paths.
- [x] Verify structure failures, missing semantic definitions, installed-wheel
  execution, all declared type paths and independent numerical/control cases.
- [x] Pass engine/simulator/full CTest and package-consumer regressions.

Acceptance evidence (2026-09-07): GCC Debug build passed; 342 runtime and
build-tree/installed-consumer CTest cases passed, followed by the corrected
missing-semantics link-negative test (343 total). Python suites passed 18 tests.
Real PTX tests exercise all 23 type paths plus signed 16/64-bit and floating
32/64-bit immediates. Numerical tests cover wrapping, saturation, rounding,
FTZ, exceptional floating encodings and packed lane independence.

An sdist-built wheel installed outside the repository runs all three generators.
Its final engine artifacts match editable-build output byte for byte. CMake
regenerates after a generator-source timestamp change without rewriting
identical artifacts; the following codegen-target build performs no generation.
The previous 53 engine tests passed before this work. This acceptance covers
the pinned Add specification only, not extended-precision arithmetic or a
new target-SM compatibility claim. Sanitizer configurations were not rerun.

Template-maintenance follow-up: execution preparation now renders packaged
Jinja2 file/family templates; Python retains model validation and derives typed
render data. The ValueALU template accepts operation/semantic names rather than
embedding Add. No other execution families or generators were migrated.
Verification: generated C++ is unchanged apart from whitespace; GCC Debug build
and all 343 CTest cases passed, as did 22 Python tests. The sdist-built wheel
includes all three templates, runs the engine's 12 Python tests outside the
repository, and emits the same artifacts as the editable build.

## Sub execution follow-up

Extend the existing ValueALU generation to all eight Sub forms and 17 type paths
in the pinned frontend model, retaining Add behavior and the shared preparation/
commit boundary. No `sub.cc` / `subc`, frontend revision change, or new family
template is included.

- [x] Generate Sub dispatch and preparation from the same projected model and
  ValueALU template, with independent opcode-level bindings.
- [x] Implement pure Sub semantic adapters and share existing codecs/control
  mapping where applicable.
- [x] Validate all declared types through real PTX, including subtraction order,
  wrapping, saturation, packed lane isolation, rounding, FTZ and immediates.
- [x] Pass generated-structure, missing-semantics, installed-wheel and C++
  regression checks.

Acceptance evidence (2026-09-07): GCC Debug build and all 386 CTest cases passed,
including Add/Sub missing-semantics link checks and package-consumer tests.
The shared pipeline retains 27 Add cases and adds 34 Sub cases covering all
17 type paths and representative controls/immediates. Five Sub semantic tests
and three engine boundary tests cover numerical behavior, invalid IR rejection,
predicate suppression and lane fault isolation. The unsupported-op regression
now uses Mul because Sub is supported. Mixed saturation samples follow the
then-pinned YAML spelling `sub.f32.f16.sat`; the modifier-order dependency
upgrade below supersedes that spelling as the canonical test input.

All 24 Python tests passed. An sdist-built wheel installed outside the repository
passes the engine's 14 Python tests and generates byte-identical Add/Sub artifacts;
repeated generation preserves output timestamps. No frontend/package revision
or dependency changed. Sanitizer configurations were not rerun.

## Mul execution follow-up

Scope is the five Mul forms in the pinned frontend: `mul.rn.f32`, `mul.lo.u32`,
`mul.hi.u32`, `mul.wide.u32`, and `mul.wide.s32`. No frontend revision or
specification expansion is included.

- [x] Derive fixed-scalar and modifier-selected operand types for shared ValueALU
  preparation, preserving independent destination/source widths.
- [x] Add pure Mul semantic adapters under `src/semantics/`, using existing
  arithmetic multiplication and product-selection controls.
- [x] Exercise all five forms through real PTX, including high/low halves,
  signed/unsigned wide products, immediates and floating-point edge cases.
- [x] Pass generator, semantic, engine/simulator and missing-semantics checks.

Acceptance evidence (2026-09-07): GCC Debug build and all 404 CTest cases passed,
including 14 real-PTX Mul cases, two pure semantic tests, the wide-destination
width-error regression, Add/Sub/Mul missing-semantics link checks and package
consumers. The unsupported-op regression now uses Div because Mul is supported.
All 26 Python tests passed. The sdist-built wheel passes the engine's 16 tests
outside the repository, generates byte-identical artifacts and preserves output
timestamps on repeated generation. No frontend revision or dependency changed;
sanitizer configurations were not rerun.

## Test-target consolidation

The main engine build retains only `test_ptxsim_inst_execute_engine` as its test
executable. Per-op link-negative targets are replaced by one CTest-driven isolated
build checking the generated-preparation/semantic-definition link contract.
The representative omission is Add; existing generator and runtime tests retain
per-op coverage. Earlier per-op link-test counts above are historical evidence.

- [x] Verify that complete semantics link and omitted Add semantics fail at link.
- [x] Verify the main build exposes no per-op missing-semantics targets and
  rerun the engine tests.

Acceptance evidence (2026-09-07): GCC Debug engine build passed, followed by all
71 selected CTest cases (70 engine/semantic tests and the isolated link contract).
Ninja target inspection confirms the main build exposes only the normal engine
test executable. CTest now lists 402 total cases after replacing three link
checks with one; the full suite and sanitizer configurations were not rerun.

## Frontend modifier-order compatibility update

The C++ overlay port and Python dependency are pinned together to
`fdb5ef575087b530c2cd6db6cb3631cf430a8ce0`, the fix for frontend issue #48
([PR #49](https://github.com/endingly/ptx_frontend/pull/49)). Mixed Add/Sub now use
`{.rnd}{.sat}.f32.{f16|bf16}` as the canonical modifier order. Explicit frontend
aliases preserve the historical trailing `.sat` spelling; accepting that alias
does not imply arbitrary modifier permutations are valid.

- [x] Regenerate against the new Python package and rebuild against the same
  frontend C++ revision, adapting aggregate initializers where member order changed.
- [x] Use canonical mixed saturation in primary tests and retain focused legacy
  alias compatibility checks for Add/Sub.
- [x] Pass Python and C++ regressions and verify both installed dependency pins.

Acceptance evidence (2026-09-07): the overlay rebuilt and installed frontend
Debug/Release libraries from the SHA512-verified fix archive; GCC Debug ptxsim
build and all 405 CTest cases passed. All 26 Python tests passed. Four mixed
Add/Sub f16/bf16 saturation cases assert canonical diagnostics, legacy/canonical
lowered-instruction equality, and numerical execution. Aggregate initializers
for mixed arithmetic and scalar ld/st were updated; arithmetic semantics were
unchanged. The port REF, setup.cfg, installed Python VCS metadata and built
wheel dependency metadata all identify the same fix hash. Because the frontend
package version remains 0.0.1b0, the existing Python environment required a
forced reinstall of the pinned Git dependency. Sanitizers were not rerun.

## Setp execution follow-up

Complete the five Setp forms in the pinned frontend, replacing the historical
handwritten `setp.lt.u32` path with generated predicate-comparison preparation.
No frontend revision or specification expansion is included.

- [x] Generate all five form adapters with model-derived operand shapes/types and
  validation of supported comparison/Boolean selectors and predicate negation.
- [x] Add pure predicate semantics and safely stage/commit up to two register
  writes, preserving the existing execution-predicate and lane fault contracts.
- [x] Exercise signed/unsigned comparisons, predicate combination/negation, dual
  destinations, input/output aliases and invalid second destinations.
- [x] Pass generator, engine, simulator, package and link-contract regressions.

Acceptance evidence (2026-09-07): GCC Debug build and all 429 CTest cases passed,
including 17 real PTX Setp pipeline cases and the existing semantic link contract.
The shared lowering leaf binder now binds both members of predicate pairs;
generated lowering requires no opcode-specific implementation. Integer semantics
use exact native comparisons because the current arithmetic comparison API
supports floating types only. Engine regressions cover invalid controls and
negation, predicate suppression, second-destination failure and pair aliases.

All 28 Python tests passed. A fresh sdist-built wheel contains the Setp model
and family template, installs against the exact frontend pin outside the source
tree, and generates byte-identical artifacts while preserving unchanged output
timestamps. No frontend dependency changed. Sanitizer configurations were not rerun.

## Ordinary load/store execution follow-up

Replace the historical u32-only load/store path with the memory execution
family described in the [architecture](../arch/instruction_execution_families.md).
This supersedes the scalar memory limitations in the historical implementation
sections below, without changing dependency pins or introducing memory ordering.

- [x] Generate ordinary scalar/vector form adapters and fail-closed validation.
- [x] Implement width-aware transfers, checked numeric addresses and existing
  explicit address-space bindings; retain prepare/commit fault isolation.
- [x] Bind register vectors and numeric address offsets in shared lowering.
- [x] Verify real PTX type/vector paths, signed extension, truncation, alignment,
  permissions, predication and failure-before-mutation behavior.
- [x] Pass C++/Python/package and existing link-contract regressions.

Acceptance evidence (2026-09-07): GCC Debug build and all 463 CTest cases passed.
The 28 real PTX memory pipeline cases cover all 14 scalar types, representative
v2/v4/v8 forms, narrow signed/unsigned extension, store truncation, floating bit
preservation, numeric offsets/immediate addresses and explicit shared/local
bindings. Five new engine regressions cover vector failure-before-mutation,
invalid vectors, constant s64-to-b128 loads and effective-address overflow.
Existing predication, permission, initialization, alignment, stale-resource and
lane-isolation regressions remain passing. Shared lowering tests cover sinks,
offsets and signed-spelling immediate address bit preservation.

All 31 Python tests passed. A fresh sdist-built wheel includes the memory model
and template, runs all three generators outside the source tree, and emits
byte-identical artifacts while preserving unchanged output timestamps. The
frontend pin is unchanged. Sanitizer configurations were not rerun.

## Branch, exit and named-barrier execution follow-up

Complete the pinned `bar`, `bra` and `exit` forms under the
[execution-family architecture](../arch/instruction_execution_families.md#branch-exit-and-named-barriers).
Keep the existing compact arithmetic/predicate/memory pipeline fixtures.

- [x] Generate control-flow and barrier preparation from projected frontend records.
- [x] Support CTA sync/arrive and popc/AND/OR reduction, preserving local
  convergence, generation reuse and failure-before-arrival validation.
- [x] Keep deferred continuations/writebacks in a persistent engine owned by
  Simulator; retain the execution_model/memory dependency boundary.
- [x] Reconcile thread exit with CTA and warp barrier release without reviving
  exited threads or treating trapped threads as exited.
- [x] Verify real PTX control-flow/collective paths, invalid collective contracts,
  and existing C++/Python/package/link regressions.

Acceptance evidence (2026-09-07): GCC Debug build and all 476 CTest cases passed,
including link-contract and installed/build-tree consumer checks. The new
pipeline fixtures exercise 23 barrier scenarios and nine branch/exit scenarios
from real PTX, including reduction readback, generation reuse, shared-memory
visibility, divergent branches and conditional exits. Engine regressions cover
partial-warp convergence, conflicting resources/protocols/counts/successors,
exit-aware release and deferred cross-warp writeback fault ownership. Shared
lowering now binds standalone resolved immediates through its existing scalar
binder; the former rejection test checks the resulting barrier operand instead.

All 37 Python tests passed. A fresh sdist-built wheel runs all three generators
outside the checkout with byte-identical output and stable unchanged timestamps.
Independent barrier-semantics review findings were fixed and the final review
reported no actionable findings. GCC ASan+UBSan builds and all 305 tests in
execution_model, runtime, inst_execute_engine and simulator passed with leak
detection and halt-on-error enabled. This sanitizer run covers the new persistent
collective ownership and deferred writeback paths; other sanitizer test binaries
and release/Clang configurations were not rerun.

## FMA execution follow-up

The C++ overlay and Python dependency advance together to frontend
`cf1f32161890b04e1060095a96ad5d8ab996db27` (frontend PR #56). FMA uses the
existing ValueALU family with three independently typed sources, including
mixed f16/bf16 multiplicands and an f32 accumulator/result.

- [x] Generate all 16 variants and 70 canonical type/control combinations.
- [x] Reuse true fused arithmetic and add f32x2 arithmetic/control support.
- [x] Exercise the complete contract through PTX parsing, resolution, lowering,
  public entry-argument binding, execution and global-memory readback.
- [x] Check independent rounding/cancellation encodings, signed zeros,
  subnormals, NaNs, saturation/ReLU and packed lane independence.
- [x] Retain source aliases, predicate suppression, malformed-control rejection
  and third-source lane-fault isolation without partial destination/PC commit.
- [x] Verify the sdist-built installed wheel, all Python suites, identical
  generator output and stable unchanged timestamps outside the source tree.
- [x] Complete GCC/Clang CTest, sanitizer and installed-consumer verification.
- [ ] Establish strict hardware OOB-NaN behavior before whole-op ISA acceptance.

The current OOB behavior matches raw `0x7ff7` multiplicands and produces positive
zero independently per lane. The marker follows NVIDIA US20240168765A1's
disclosed embodiment. Ordinary NaNs retain normal FMA handling; negative
`0xfff7` and a marker in the accumulator alone do not trigger the rule in this
model. These sign/operand-position choices have not been established as an
exhaustive hardware predicate. The
[architecture](../arch/instruction_execution_families.md#fma-scope) records the
reference and limitation. This item keeps issue #25 open for strict whole-op
acceptance; normal fused arithmetic and all declared execution paths are present.

Initial verification evidence (2026-09-08, before the OOB marker refinement):
GCC 15.2 and Clang 21.1 Debug/Release each
passed all 574 CTest cases, including build-tree/installed consumers and the
isolated semantic-link contract. GCC ASan+UBSan also passed all 574 cases with
leak detection and halt-on-error enabled. The final 88 focused FMA cases passed.
The installed consumer parses and executes a fused f32 cancellation case and
checks the exact global output bytes. Independent review reported no remaining
actionable defect within the documented compatibility scope.

All 38 Python tests passed from both the editable package and an sdist-built
wheel installed outside the checkout. All three installed generators emitted
byte-identical build artifacts and retained timestamps on unchanged output.
Installed Python VCS metadata and wheel dependency metadata confirm the same
frontend revision as the SHA512-verified C++ port.

OOB marker refinement (2026-09-08): replaced all-NaN matching with exact raw
`0x7ff7` recognition. Scalar tests cover both f16/bf16, quiet/signaling NaNs,
negative `0xfff7`, canonical `0x7fff`, all three source positions, disabled OOB,
ReLU and invalid status. Packed PTX cases distinguish ordinary NaNs from an
adjacent OOB lane. GCC Debug passed all 574 CTest cases, including consumers
and the link contract; GCC Debug, Clang Debug and GCC ASan+UBSan each passed
all 88 focused FMA cases after the refinement. Sanitizers used leak detection
and halt-on-error. Independent review found no actionable defect. Release
configurations and Python/wheel checks were not rerun for this arithmetic-only
refinement; their preceding evidence is retained above.

## Mov and topology execution follow-up

Implements the movement and topology work tracked in
[ptxsim #21](https://github.com/endingly/ptxsim/issues/21), from main `2ef3f8f`.
The frontend C++/Python pin remains `cf1f32161890b04e1060095a96ad5d8ab996db27`;
the later storage-declaration frontend PR does not itself supply a simulator
resource allocator or change the movement projection.

- [x] Lower scalar immediates, predicates, all topology components and declared
  vector destinations to owned executable values and contiguous component slots.
- [x] Generate the movement family from all three variants/five operand layouts,
  retaining bit-copy, pack/unpack, predicate and vector semantics.
- [x] Read topology special registers from execution-model state, including grid
  identity and fixed-width lane masks; preserve malformed-IR and lane-fault details.
- [x] Verify real PTX through public launch and global readback over multiple CTAs
  and multidimensional thread blocks, plus aliases, sinks and predicate suppression.
- [x] Complete compiler, sanitizer, generator, wheel and installed-consumer checks.
- [ ] Reconcile remaining frontend and runtime prerequisites before whole-ISA Mov
  certification; handler presence alone does not satisfy that boundary.

Validation (2026-09-08): all five GCC/Clang Debug/Release and GCC ASan+UBSan
presets cover 597 passing tests each. Four full runs initially rejected the
negated-predicate public fixture at the frontend boundary; after correcting that
fixture, all seven affected Mov pipeline tests passed in each preset. The final
Clang Release full run passed 597/597 directly. All 43 Python tests pass both
from source and from a freshly installed wheel outside the checkout. All three
installed generators match build output and preserve timestamps on repeated
generation; the exact frontend pin remains consistent. Diff and documentation
link checks pass. Assembly acceptance is not GPU execution validation.

Source categories requiring future architectural resources remain explicit:
symbolic storage needs address-space bindings and the allocation work in #22;
device-function formal addresses need activation-local materialization, while
entry-input parameter addresses reuse the existing launch ABI offsets. Function
addresses need an executable code-address contract. Physical SM IDs and device
capacities, cluster topology, timers, performance/environment state, graph
execution and shared-resource
statistics must not be synthesized from unrelated thread indices. The whole-op
frontend audit in endingly/ptx_frontend#51 remains the cross-op baseline.

Frontend audit boundary (PTX ISA 9.3, current pin and frontend `5c6e42c`):
ordinary `.v2/.v4` register-vector copies and brace destinations, ordinary vector
component references, and the documented bracketed address spelling `A[5]` are
not represented or accepted by the current frontend path. Predicate literal
sources such as `mov.pred %p, 0/1` are also absent from the projected source
union, despite the ISA's predicate-constant rule and acceptance by `ptxas`.
Negated predicate sources are represented by the resolved operand type but
rejected by the current frontend's `Pred` layout matcher. Their executor
semantics are covered through bound IR; public PTX tests use plain predicates.
The minimal reproduction and matcher diagnosis were
[reported upstream](https://github.com/endingly/ptx_frontend/issues/51#issuecomment-5581859774).
These are frontend-owned completion items under the scope of
endingly/ptx_frontend#51. No simulator-side parser or parallel instruction
specification is introduced to bypass them.

Unbacked source categories still produce `unsupported_operand` during lowering,
including inside predicated-off instructions; there is no claim that such PTX
modules execute. Supporting them requires their owned binding/state contracts.
For structurally valid already-bound execution IR, unavailable source evaluation
occurs only after predicate gating and returns a structured lane fault.

The projected `mov.v4.u32` destination can still be allocated and written by the
production pipeline. Until ordinary component references are available upstream,
its result is observed using the public post-run register view; scalar topology
acceptance independently uses PTX global stores and full buffer readback. This
observation boundary must not be reported as general vector-kernel support.

Bound-IR negated predicate sources retain logical inversion, including aliases. Offline
`ptxas 13.3.33 -arch=sm_90` accepts a PTX 9.3 kernel with `mov.pred %p1, !%p0`
and observable global output. This assembly check establishes toolchain
acceptance; it does not provide GPU execution evidence.

The PTX manual's `mov.u32` example using `%pm0_64` also conflicts with that
register's declared u64 type and the general type rule. Performance-monitor
state is not implemented here; the cross-op audit should reconcile that example
before choosing a width exception.

## 1. Decision summary

The executor was designed before the C++ `exec_ir` representation; the
completed probe work packages below are retained as historical evidence.

The stable execution unit is a scheduler-selected
`execution_model::WarpIssueGroup`, not an isolated Thread and not a complete
program. `Simulator` owns the immutable `ExecutableProgram` and
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

`Thread::step()` and `Warp::step()` remain as constrained thin facades. The
historical probe contract they share is:

```cpp
stepper.step(Warp&, const WarpIssueGroup&)
```

`Thread::step()` forms a single-lane issue from its authoritative current PC
and lane ID, then forwards to that same contract. The current `Simulator`
fetches the instruction and invokes the lower executor entry directly:

```cpp
executor.execute(warp, issue, instruction, fallthrough);
```

The facade is a convenience for lane-local execution and tests, not a second
instruction engine. Collective instructions may reject a single-lane issue.

Thread owns the sole authoritative current PC value. In the current no-call
path, `Simulator` combines its configured entry `FunctionId` with that local
PC to form `common::CodeLocation`; call/activation state is deferred.
Simulator stores no duplicate authoritative PC. `ExecutableProgram` derives
the flat storage offset and same-function fallthrough; executor selects
target/fallthrough and commits the local PC through Thread.

The current executor implementation consumes an already-fetched `exec_ir`
instruction through its existing handwritten static dispatch table. `Simulator`
is the separate production composition root; neither module adds a dynamic
handler registry or general transaction system.

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

Current execution facts and gaps relevant to later extensions:

- `Simulator` constructs `WarpIssueGroup` values by a deterministic
  topology-order scan and fetches `ExecutableProgram` by `common::CodeLocation`;
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
The explicit entry `FunctionId` passed by `Simulator` and this binding are
MVP assumptions: they are sufficient only while a Thread has one live
activation for a function. Until call state exists, executor must not invent a
current-function field, activation, or call stack.

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

exec_ir_lowering ─────────> fully-bound ExecutableProgram
Simulator ────────────────> program/fetch + executor + runtime
future scheduler policy ──> Simulator issue selection
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
`Simulator` is the current production fetch owner; `InstExecuteEngine` exposes
the lower `execute(..., instruction, fallthrough)` operation and does not
perform fetch. A Simulator step-provider adapter is not required by the
current public API.

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
- Simulator derives `common::CodeLocation` from its entry `FunctionId` and the
  local issue PC, but stores no duplicate authoritative PC; call activation is
  deferred;
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

The original collective implementation covered `bar.warp.sync`. The named-barrier
follow-up above extends that contract to CTA collectives: prepare issued lanes
as a group and record no arrival until all group validation succeeds. Deferred
writeback and exit reconciliation follow the execution-family architecture.

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
implicitly. In the current production path it is a checked function-local index;
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

The target is installed/exported after the post-control-flow gate and generated
`exec_ir` integration. The historical private probe operations were replaced by
fully-bound `exec_ir` records.

The historical private probe operations impose no separate executor ABI or
generator contract.

---

## 11. Work packages

WP0-WP3 and Gate A below are completed historical probe records; WP4 and later
describe the resulting implemented executor contract and its remaining scope.

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

This historical gate authorized the later `exec_ir` WP1 rewrite. The probe
operation types were replaced rather than preserved for compatibility.

Gate A and the follow-up dispatch audit decided:

- `Thread` is the only authoritative PC owner;
- the current explicit `FunctionId` and `(ThreadId, FunctionId)` runtime
  bindings are MVP probe assumptions, not a call-capable execution context;
- `Simulator` owns an immutable `ExecutableProgram`, derives a
  `common::CodeLocation` from its entry `FunctionId` and `WarpIssueGroup::pc`,
  fetches by that semantic location, derives function-local fallthrough, and
  calls executor; activation state remains deferred;
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

simulator integration tests
  scheduling/fetch/progress loop
```

For every mutating instruction test, capture the pre-step values and assert
both intended changes and required non-changes. A failing lane must prove that
its destination and PC were not partially committed.

Final verification for an implemented executor work package should include:

```text
cmake configure with BUILD_TESTING=ON
build the changed module and dependents
run execution_model/runtime/executor focused tests
run full CTest before commit
run ASan + UBSan for ownership/lifetime changes
inspect installed-package consumers for public API/build/export changes
```

These are implementation acceptance gates, not a requirement to rebuild for
documentation edits or repeat unchanged checks at each agent handoff. Reuse
passing evidence for unchanged inputs; rerun affected checks after corrections.

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
- asynchronous host execution or timing simulation.

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

The complete executor module is composed by `Simulator`, is installable, and
preserves the same validated prepare/commit contract for every supported
instruction family.
