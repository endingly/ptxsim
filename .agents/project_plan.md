# ptxsim Project Plan v2 (historical)

> **Status:** Historical planning record. The frontend/lowering, `exec_ir`,
> program, state, and bootstrap pipeline described below was retired; the
> surviving module boundaries are defined by the current source tree and
> `docs/execution_model.md`.
> **Supersedes:** `.agents/project_plan.md` v0.4  
> **Specification baseline:** NVIDIA PTX ISA 9.3  
> **Current work:** retained modules are `common`, `arith`,
> `execution_model`, `memory`, and `runtime`. V2-M1 arithmetic remediation was
> accepted when PR #3 merged with merge commit
> `7da3628c0463f586b190921b283b15ab059d2022` at 2026-08-30T13:08:30Z and
> Linux CI run `33313368339` completed successfully for that exact head. The
> required `GCC Debug`, `GCC Release`, `Clang Debug`, `Clang Release`, and
> `GCC ASan + UBSan` jobs all succeeded. Active default-branch ruleset
> `21723631` requires PRs and those exact strict checks.
> **Primary objective:** retain a deterministic, inspectable runtime topology,
> memory, and instruction-independent numerical semantics foundation.

---

## 1. Why v2 is required

The previous project plan is no longer an executable roadmap. It assumes:

```text
ptxsim::fp
fp::Environment
raw-bit per-type floating overloads
Milestone 4 as a small IEEE-only floating wrapper
```

The current architecture has deliberately moved to:

```text
ptxsim::arith
strong typed numeric formats
generic scalar/conversion/packed/tensor APIs
independent controls
private arithmetic backends
```

That is not a naming-only refactor. It changes module responsibility, API shape, testing strategy, and dependency ordering.

Arithmetic remediation is tracked in the [V2-M1 review audit chain](../docs/reviews/README.md):
[main review](../docs/reviews/v2-m1-main-review.md),
[remediation review](../docs/reviews/v2-m1-fix-review.md), and
[rereview](../docs/reviews/v2-m1-fix-rereview.r1.md); archive governance is
defined in [`.agents/review_policy.md`](review_policy.md).
V2 keeps an accepted `arith` boundary as the prerequisite for executor
integration.

### 1.1 Main changes from v0.4

| v0.4 assumption | v2 decision |
|---|---|
| Independent `ptxsim::fp` environment | Replace with pure `ptxsim::arith` numerical semantics library |
| FP module mainly wraps SoftFloat | SoftFloat is one private backend; BF16/FP8/FP6/FP4/fixed/tensor need format-specific or generic exact cores |
| FP public API models Fp16/Fp32/Fp64 overloads | Public API uses typed generic operations and capability traits |
| Floating milestone can integrate early | Arithmetic must pass format/capability/conversion/tensor conformance before integration |
| Roadmap baseline “PTX 9.x” | Pin semantic baseline to PTX ISA 9.3; future versions require explicit deltas |
| No dedicated frontend isolation target | Add a lowering/front-end adapter boundary; execution core never links frontend |
| Generic memory list is enough | TMEM remains a specialized storage resource, not a normal byte-addressed state space |
| Trace considered as a possible independent module | No separate trace submodule in the current plan; use a minimal debug/event-sink boundary |
| CI primarily proves ordinary build | Add capability, sanitizer, header consumer, install/export, concurrency and oracle-independence gates |

---

# 2. Project goals and non-goals

## 2.1 Goals

ptxsim v0.1 should provide:

1. deterministic PTX functional execution for a documented subset;
2. a formal `ptxsim::exec_ir` boundary after frontend resolution;
3. frontend-independent executable program images;
4. inspectable register, memory, specialized storage and scheduler state;
5. exact numerical behavior where PTX determines a unique result;
6. explicit deterministic reference profiles where PTX is target-dependent or underspecified;
7. structured unsupported/error reporting, with no silent fallback;
8. reproducible unit, conformance, differential and end-to-end tests;
9. a usable library API and command-line runner;
10. a support matrix that separates parsing, lowering, execution and validation status.

## 2.2 Non-goals for v0.1

The first release does not attempt to provide:

```text
SASS decoding or execution
physical register allocation
cycle-accurate timing
cache/coalescer/pipeline timing
GPU performance prediction
full CUDA runtime emulation
all PTX instructions
all target-SM availability rules
bit identity for behavior PTX explicitly leaves target-dependent
```

Instruction availability and target architecture gating belong outside `arith`; the first implementation may support a smaller documented target subset.

---

# 3. Architecture decisions

## 3.1 Retired execution pipeline (historical)

```text
PTX source
   |
   v
ptx_frontend
   |
   v
ptx_frontend::resolved_ir
   |
   |  lowering / validation
   v
ptxsim::exec_ir
   |
   v
ptxsim::program::ProgramImage
   |
   +--------------------+-------------------+
   |                    |                   |
   v                    v                   v
state                memory/TMEM          arith
   \                    |                  /
    \                   |                 /
     +-------------- semantics ----------+
                         |
                         v
                      executor
                         |
                         v
                      scheduler
                         |
                         v
                       runtime
                         |
                         v
                    debug/event sink
```

## 3.2 Dependency rules

The required dependency direction is:

```text
common
  |
  +--> arith
  +--> exec_ir
  +--> state
  +--> memory

ptx_frontend --> lowering --> exec_ir/program

exec_ir/program + state + memory + arith
             --> semantics --> executor --> scheduler --> runtime

runtime/executor/state/memory --> debug interfaces
```

Forbidden dependencies:

```text
arith -> exec_ir
arith -> ptx_frontend
arith -> state/memory/scheduler/runtime
exec_ir -> ptx_frontend
state/memory/semantics/executor -> ptx_frontend
memory -> executor/scheduler
scheduler -> ptx_frontend
```

Only the lowering/front-end adapter target may link `ptx_frontend`.

## 3.3 `arith` responsibility

`ptxsim::arith` owns only numerical semantics:

```text
typed scalar values
format classification
integer/floating/fixed conversions
integer and bit primitives
scalar arithmetic
packed lane arithmetic where capability exists
logical tensor arithmetic
numeric controls
numeric status/errors
model profiles for numerically unspecified behavior
```

It does not own:

```text
PTX mnemonic/opcode/form
modifier ordering or grammar
target-SM instruction availability
register file
predicate execution
PC or call stack
warp/lane fragments
memory descriptors
TMEM address/descriptor handling
scheduler state
```

## 3.4 `exec_ir` responsibility

`exec_ir` is a typed PTX functional execution IR, not SASS. It should:

- use resolved, compact operands;
- encode facts needed by execution after frontend validation;
- make PC targets and identifiers stable;
- use typed instruction records or a bounded variant;
- avoid frontend AST/IR ownership;
- avoid embedding numerical backend details;
- preserve source locations through side metadata.

## 3.5 Tensor boundary

```text
executor / tensor instruction adapter
  - decodes register fragments
  - resolves layout/shape/instruction qualifiers
  - reads shared memory/TMEM descriptors
  - builds logical tiles and scale views

arith::tensor
  - receives logical A/B/C values
  - applies explicit numeric scale model
  - computes logical D
  - returns numeric status
```

`arith::tensor` must never know lane IDs, warp fragments, TMEM addresses or PTX descriptor encoding.

## 3.6 TMEM decision

TMEM is a specialized execution resource. It is not added to a generic byte-addressed `StateSpace` enum as if it were global/shared/local memory.

Recommended boundary:

```cpp
class TmemState;
class TmemAddress;
class TmemDescriptorView;
```

TMEM instruction adapters may read/write this resource and transform data into logical tensor tiles. Generic load/store memory APIs must not accidentally accept TMEM addresses.

## 3.7 Trace/debug decision

No independent trace submodule is introduced in the initial roadmap. Instead use a minimal, optional event-sink interface under debug/runtime:

```cpp
struct ExecutionEventSink {
  virtual void on_step(const StepEvent&) = 0;
  virtual void on_memory(const MemoryEvent&) = 0;
  virtual void on_trap(const TrapEvent&) = 0;
};
```

The executor must not depend on a concrete tracer. The hook must remain optional and low-intrusion.

## 3.8 State-machine dependency decision

Do not add a third-party state-machine library for v0.1 scheduler/executor work.

Use explicit enums, transition functions and invariant checks while state graphs are small and performance/debugging requirements are evolving. Re-evaluate only if all are true:

- the project has multiple independently evolving complex protocols;
- transition duplication is measurable;
- generated diagrams/introspection materially help testing;
- the dependency does not infect public headers or core numeric paths;
- compile-time and binary-size cost is acceptable.

---

# 4. Proposed repository layout

```text
ptxsim/
├── CMakeLists.txt
├── CMakePresets.json
├── vcpkg.json
├── cmake/
│   ├── register_headers.cmake
│   ├── add_test.cmake
│   ├── sanitizers.cmake
│   └── install_package.cmake
├── docs/
│   ├── arith_module_design.md
│   ├── execution_model.md
│   ├── exec_ir.md
│   ├── memory_model.md
│   ├── tensor_boundary.md
│   ├── validation_policy.md
│   ├── support_matrix.md
│   └── adr/
├── submod/
│   ├── common/
│   ├── arith/
│   ├── exec_ir/
│   ├── lowering/
│   ├── program/
│   ├── state/
│   ├── memory/
│   ├── semantics/
│   ├── executor/
│   ├── scheduler/
│   ├── runtime/
│   └── debug/
├── tools/
│   └── ptxsim/
└── test/
    ├── e2e/
    ├── conformance/
    ├── differential/
    ├── consumer/
    └── data/
        ├── ptx_9_3/
        └── hardware_vectors/
```

Public aliases:

```text
ptxsim::common
ptxsim::arith
ptxsim::exec_ir
ptxsim::lowering
ptxsim::program
ptxsim::state
ptxsim::memory
ptxsim::semantics
ptxsim::executor
ptxsim::scheduler
ptxsim::runtime
ptxsim::debug
```

Real target names remain prefixed with `ptxsim_` to avoid collisions.

---

# 5. Build and dependency contract

## 5.1 Supported build baseline

Initial required environment:

```text
CMake >= 3.28
C++23
Ninja
GCC on Linux as primary compiler
Clang on Linux as secondary conformance compiler
vcpkg manifest mode with project overlay ports
```

MSVC/macOS support is not a v0.1 release gate unless explicitly added later.

## 5.2 Third-party dependencies

### `ptx_frontend`

- consumed only by `ptxsim_lowering` and frontend-facing tools/tests;
- pinned to a known revision/version;
- no frontend types survive in `ProgramImage` or execution-core public APIs;
- Python/build-time dependencies belong to the frontend package contract, not every execution target.

### Berkeley SoftFloat

- installed through the project overlay port;
- pinned version/revision and license recorded;
- built with thread-local mutable state;
- linked `PRIVATE` by `ptxsim_arith`;
- never exposed through public headers or package interface;
- no other module calls SoftFloat directly.

### GTest

- test-only dependency;
- never linked by production targets.

### High-precision oracle dependencies

MPFR or another high-precision library may be introduced only for tests/differential tools. It must not become a production dependency of `ptxsim::arith`.

## 5.3 Required CMake options

```text
PTXSIM_BUILD_TESTING
PTXSIM_ENABLE_ASAN
PTXSIM_ENABLE_UBSAN
PTXSIM_ENABLE_TSAN          optional/non-blocking initially
PTXSIM_BUILD_TOOLS
PTXSIM_BUILD_CONSUMER_TESTS
PTXSIM_ENABLE_HARDWARE_ORACLE
```

## 5.4 Build gates

Every merge to `main` must prove:

```text
GCC Debug configure/build/test
GCC Release configure/build/test
Clang Debug configure/build/test
Clang Release configure/build/test
GCC ASan + UBSan configure/build/test
public header self-contained compile
build-tree consumer
install/export consumer
```

---

# 6. Validation policy

A universal epsilon is forbidden.

## 6.1 Validation classes

| Class | Meaning | Examples |
|---|---|---|
| V0 Structural | Shape/type/layout/invariant | exec IR, fragments, scale layouts |
| V1 Bit Exact | Unique integer/floating result | integer add, exact FMA, signed zero |
| V2 Spec-Bounded Numeric | ISA-defined error bound | approximate transcendental/div forms |
| V3 Allowed-Set | PTX permits several values/classes | unspecified NaN payload/target model |
| V4 Memory-Model | Outcome set under memory semantics | races/atomics/order |

## 6.2 Oracle hierarchy

```text
PTX 9.3 literal tables / pseudocode
          |
          +--> checked-in raw golden vectors
          |
          +--> independent reference model
          |       - SoftFloat for IEEE primitives
          |       - high precision for fused/approx/tensor
          |
          +--> NVIDIA hardware differential corpus
```

Production encode/decode/kernel code must not be reused as its own oracle.

## 6.3 Reproducibility

All randomized tests must emit:

```text
fixed seed
input raw bits
operation/capability
controls/profile
expected/actual raw bits
single-case replay command or test parameter
```

---

# 7. Delivery strategy

The roadmap uses milestones `V2-M0` through `V2-M9`. Numbering reflects the new dependency order and intentionally does not preserve v0.4 milestone numbers.

Critical path:

```text
V2-M0 build gate
   +
V2-M1 arithmetic conformance
   |
   +----> V2-M2 exec_ir/program/state
             |
             v
          V2-M3 memory/TMEM
             |
             v
          V2-M4 single-thread vertical slice
             |
             v
          V2-M5 floating/conversion integration
             |
             v
          V2-M6 SIMT/shared/barrier
             |
             v
          V2-M7 runtime/ABI/CLI/debug
             |
             v
          V2-M8 conformance and v0.1
             |
             v
          V2-M9 advanced PTX
```

V2-M2 consumes only the stabilized `arith` API established by V2-M1.

Legacy review artifacts and the retained `fix/m5-main-review` branch name
predate the V2 milestone reorder. They identify this V2-M1 arithmetic
remediation chain only; the branch remains unchanged to avoid pre-merge churn,
and V2-M5 remains the future floating/conversion integration milestone.

---

# 8. V2-M0 — Repository and build contract

**Goal:** make every later semantic change independently reproducible.

## 8.1 Tasks

| ID | Task | Done condition |
|---|---|---|
| M0-01 | Pin dependency revisions | frontend and SoftFloat revisions recorded and reproducible |
| M0-02 | Validate overlay ports | clean manifest install works without hidden machine state |
| M0-03 | Normalize target naming | all real targets prefixed; all aliases resolve |
| M0-04 | CMake presets | GCC/Clang Debug/Release and sanitizer presets configure |
| M0-05 | Test helper | CTest discovery works for each submodule |
| M0-06 | Sanitizer helper | ASan/UBSan can be enabled per preset without affecting third-party code incorrectly |
| M0-07 | Header compile gate | each public header compiles standalone |
| M0-08 | Install/export package | `find_package(ptxsim CONFIG)` consumer works |
| M0-09 | CI trigger repair | PR and all relevant main pushes run complete gates |
| M0-10 | License inventory | frontend, SoftFloat and test dependencies documented |

## 8.2 Acceptance

- fresh checkout succeeds without manually installed project dependencies other than documented build tools;
- no production target links GTest or test-only oracle dependencies;
- installed `ptxsim::arith` does not expose SoftFloat;
- CI has no path filter that can skip ordinary source changes on `main`.

---

# 9. V2-M1 — Arithmetic conformance and API stabilization

**Goal:** turn `refactor/arith-module` into a truthful, deterministic numerical library before simulator integration.

**Accepted remediation status:** the audit chain and map below record local
fixes, including `05b0af2` (F64 FTZ projection), `419e2e6` (S2F6 finite
saturation), `def4b8a` (frontend snapshot integrity), and `a407c8e`
(pre-rounding S2F6 status). Acceptance evidence is the exact merge and hosted
CI run recorded at the top of this plan.

## 9.1 Work packages

| ID | Work package | Review issues | Done condition |
|---|---|---|---|
| M1-01 | Independent PTX format goldens | P0-001, P1-009 | raw vectors exist independently of production traits |
| M1-02 | Correct format descriptors | P0-001, P1-008 | UE8M0/UE4M3 and all classification metadata match PTX 9.3 |
| M1-03 | Central operation capability | P0-002 | every true concept has an implementation; unsupported combinations fail at compile time where possible |
| M1-04 | Canonical conversion pipeline | P0-003 | public `cvt` no longer carries pairwise type list |
| M1-05 | Conversion controls | P1-001, P1-002 | directed/RNA/stochastic/satfinite/ReLU/subnormal legality and semantics implemented where specified |
| M1-06 | Scalar compound semantics | P0-004, P1-003, P1-007 | `mad`, status composition, error mapping and rsqrt contract fixed |
| M1-07 | Signed bit semantics | P0-005 | BFE/BFIND match PTX pseudocode for 32/64-bit signed/unsigned |
| M1-08 | Packed capability/layout | P1-004 | operation/lane/layout capabilities and required aliases are explicit |
| M1-09 | Tensor type combinations | P0-006 | numeric MMA capability matches PTX matrix data types |
| M1-10 | Block scale model | P0-007 | A row chunks/B column chunks, scale types and layouts validated |
| M1-11 | Widened tensor MAC | P0-008 | no premature F32 overflow/underflow or low-format rounding |
| M1-12 | Typed model profile | P1-005 | PTX 9.3 reference profile explicitly controls unspecified choices |
| M1-13 | Approximation model | P1-006 | corner behavior, deterministic model and error bounds proven |
| M1-14 | SoftFloat stress | P1-010 | nested/exception/concurrent TLS isolation passes |
| M1-15 | Packaging cleanup | P2 issues | public/private headers, validation target and consumer gates fixed |
| M1-16 | Support matrix | all | generated/documented public capability matrix matches tests |

## 9.2 Required public API shape

The stable direction is:

```cpp
context ctx{model_profile::ptx_9_3_reference()};

auto x = add(ctx, a, b, floating_control{...});
auto y = fma<Result>(ctx, a, b, c, floating_control{...});
auto z = cvt<To>(ctx, from, conversion_control{...});
auto s = cvt<To>(ctx, from,
                 conversion_control{.rounding = rounding_mode::stochastic},
                 stochastic_rounding_input{random_bits});
```

No public instruction form/opcode dispatch is added.

## 9.3 Acceptance

V2-M1 was accepted with the merge and exact-head hosted CI evidence recorded
at the top of this plan. The map below records remediation targets and checks.

## 9.4 MAIN remediation and regression map

The five local workflows are GCC Debug, GCC Release, Clang Debug, Clang
Release, and GCC ASan + UBSan. They remain required final integrated-tree gates;
the review audit chain is under [`docs/reviews`](../docs/reviews/).

| Issue | Fix commit | Regression target / check |
|---|---|---|
| MAIN-P0-004 | `173a951` | `test_scalar.cpp`, `test_special.cpp` |
| MAIN-P0-001 | `04c67c1` | `test_scalar.cpp` |
| MAIN-P0-002 | `6adc5f7` | `test_special.cpp` |
| MAIN-P0-003 | `6be18ec` | `test_special.cpp` |
| MAIN-P1-001 | `e48fba2` | `test_special.cpp` |
| MAIN-P1-002 | `93147b9` | `test_conversion.cpp` |
| MAIN-P1-003 | `1aa80f9` | `test_conversion.cpp` |
| MAIN-P1-004 | `7dd0e71` | `test_conversion.cpp` |
| MAIN-P1-005 | `97eab10` | `test_bit.cpp` |
| MAIN-P1-006 | `ad44346` | `test_special.cpp` |
| MAIN-P1-007 | `3b50b45` | `test_scalar.cpp` |
| MAIN-P1-008 | `de1ab28` | `test_bit.cpp`, `test_scalar.cpp` |
| MAIN-P1-009 | `35873ec` | `test_tensor.cpp` |
| MAIN-P1-010 | `2c59b86` | `test_packed.cpp` |
| MAIN-P2-001 | `d2b2ad9` | `test_scalar.cpp` |
| MAIN-P2-002 | `d3850cf` | five workflow presets; manifest feature dry-run/config with tests enabled and frontend omitted |
| MAIN-P2-003 | `20e0e37` + `def4b8a` | `frontend-lowering` feature install; automatic snapshot integrity check + documented manual regeneration/byte comparison |
| MAIN-P2-004 | external / configured | ruleset `21723631` exact five strict contexts; run `33313368339` passed the merge head `7da3628c0463f586b190921b283b15ab059d2022` |
| MAIN-P2-005 | `57f3593` | `ptxsim_arith_public_header_check` |
| MAIN-P2-006 | `24dd230` | plan/document consistency searches |
| FIX-P0-001 | `05b0af2` | F64 FTZ upper-word projection; `test_special.cpp` |
| FIX-P0-002 + FIX-P1-001 | `419e2e6` | S2F6 finite saturation/status; `test_conversion.cpp` |
| FIX-P2-002 | `def4b8a` | automatic snapshot integrity CTest + manual byte comparison |
| REREVIEW-P1-001 | `a407c8e` | S2F6 pre-rounding finite-range status; `test_conversion.cpp` |
| REREVIEW-P2-002 | `8dd6ef5` | plan consistency search and diff check |
| REREVIEW-P3-001 | `d43cc2d` | pinned workflow action refs and YAML check |
| REREVIEW-P2-003 | this archival commit | V2-M1 review audit-chain preservation/link checks |
| FIX-P2-003 | `777ac67` | [`docs/reviews`](../docs/reviews/) provenance and archival links |

---

# 10. Retired V2-M2 record — Exec IR, lowering, ProgramImage and core state

**Goal:** establish the formal execution boundary and frontend-independent program ownership.

**Current phase (M2 review/remediation before M3):** M2-00 pinned frontend baseline
`992fc36527e1ffe2d1b3dd2a07de2b6d721e7898`, upstream native CMake codegen, and
the feature-on acceptance gates under remediation. `ptxsim::common` exports stable,
frontend-independent IDs and exact-width raw pred/b8/b16/b32/b64/b128 values.
`ptxsim::exec_ir` now exports typed register, immediate, special-register,
address, branch, and function operands, with no frontend or arith dependency.
It now also exports the validated initial data-only instruction slice: mov,
integer add/sub/mul, b32 and/or/xor, direct branch, and selected scalar
global/constant loads and global stores. `ptxsim::program` now owns verified,
frontend-independent `ProgramImage` data: instruction PC order, contiguous
function ranges with dense register layouts, copied symbols/source metadata,
entries, and a PC source side table. It has no executor, state, lowering, or
symbol storage/address implementation. `ptxsim::state` now exports a static,
frontend-independent `RegisterFile` with caller-supplied, RegisterFile-owned
dense `RawWidth` layout, exact-width writes, structured uninitialized reads,
and deterministic dumps.
It now also exports a frontend-independent `ThreadState` that owns
caller-supplied IDs, initial PC, RegisterFile, ready status, and an empty
call-frame placeholder. The frontend-independent `ptxsim::bootstrap`
production adapter validates an entry against its owning `ProgramImage`, then
creates `ThreadState` from the canonical function PC and register layout
without retaining the image. M2-11 now adds only the mockable
`SpecialRegisterProvider` interface;
its production values and launch configuration remain absent. Executor PC/status
transitions are M4+ work, and call semantics are M7 work. `%tid`, `%ntid`,
`%ctaid`, `%nctaid`, `%laneid`, and `%warpid` values are M6 work.
`ptxsim::lowering` is an optional `frontend-lowering` target and
now owns structured, fully copied lowering diagnostics plus a temporary dense
frontend-ID-to-simulator-ID/PC `LoweringContext`, plus module
lowering from AST placement/source facts and resolved-IR semantics into verified
`ProgramImage` data. It is installed/exported as the explicit `lowering`
component, and no frontend object or identity survives in `ProgramImage`. The
supported M2-08 slice is scalar/pred
mov, selected integer/bit/branch instructions, and selected explicit scalar
global/constant memory operations; unsupported legal forms are diagnostics.
The feature-on acceptance test destroys all frontend and temporary lowering
objects before verifying/dumping the resulting image and creating a ready
`ThreadState` through the production bootstrap API; no production
lowering-to-state dependency is introduced. Default package consumers remain
frontend-free;
lowering consumers explicitly request the component and its frontend dependency.

## 10.1 Exec IR principles

- typed instruction records;
- resolved register/function/label identifiers;
- explicit operands and control facts needed by execution;
- no SoftFloat/arith backend details;
- no frontend node pointers/references;
- no SASS assumptions;
- source location stored as side metadata where practical.

## 10.2 Tasks

| ID | Task | Done condition |
|---|---|---|
| M2-01 | Core IDs | PC, function, register slot, symbol, thread/CTA/warp IDs |
| M2-02 | Raw machine value storage | pred, b8/b16/b32/b64/b128 and typed extraction helpers |
| M2-03 | Operand model | register, immediate, special, address and target operands |
| M2-04 | Initial instruction records | mov, integer add/sub/mul, bit ops, bra, selected ld/st |
| M2-05 | ProgramImage | owned instruction sequence, functions, symbols and metadata |
| M2-06 | Lowering diagnostics | structured error with source location and unsupported feature |
| M2-07 | Lowering context | frontend IDs map to stable simulator IDs/PCs |
| M2-08 | Module lowering | resolved module can outlive frontend objects after conversion |
| M2-09 | RegisterFile | dense raw storage and initialization tracking |
| M2-10 | ThreadState | PC, status, registers, call metadata placeholder; validated ProgramImage entry bootstrap |
| M2-11 | Special register provider | testable interface independent of scheduler implementation |

## 10.3 Acceptance

```text
PTX source
-> ptx_frontend::resolved_ir
-> lowering
-> ProgramImage + ThreadState
```

works for a minimal typed instruction subset, and deleting the frontend object does not invalidate execution data.

---

# 11. V2-M3 — Memory, storage lifetimes and TMEM foundation

**Goal:** implement inspectable addressable memory while preserving specialized storage boundaries.

## 11.1 Generic memory tasks

| ID | Task | Done condition |
|---|---|---|
| M3-01 | StateSpace/VirtualAddress | global/const/param/shared/local represented explicitly |
| M3-02 | MemoryRegion | bounds, alignment and read/write policy |
| M3-03 | Global/const storage | initialization and mutability rules |
| M3-04 | Param storage | per-launch parameter region |
| M3-05 | Shared/local factories | per-CTA/per-thread isolation |
| M3-06 | Initialized-byte tracking | uninitialized reads observable through policy/status |
| M3-07 | Symbol layout | ProgramImage symbols receive stable simulator addresses |
| M3-08 | Snapshots/dumps | deterministic immutable range/symbol/scope output |

## 11.2 TMEM tasks

| ID | Task | Done condition |
|---|---|---|
| M3-09 | TmemState skeleton | specialized allocation/storage object exists outside generic StateSpace |
| M3-10 | Tmem address/descriptor types | cannot be passed to generic memory API by accident |
| M3-11 | Logical tile bridge | test adapter maps TMEM-like storage to logical tensor tiles |
| M3-12 | Inspectability | TMEM state can be dumped without pretending to be byte-addressed PTX memory |

## 11.3 Acceptance

- memory scopes/lifetimes are deterministic and isolated;
- generic load/store cannot access TMEM;
- all implemented storage has stable diagnostic snapshots;
- no scheduler or frontend dependency leaks into memory core.

---

# 12. V2-M4 — Single-thread integer execution vertical slice

**Goal:** execute a complete minimal PTX program through lowering, state, memory and executor.

## 12.1 Tasks

| ID | Task | Done condition |
|---|---|---|
| M4-01 | Step contract | continue/branch/exit/trap/unsupported/step-limit modeled |
| M4-02 | Operand read/write | typed register/immediate/special access |
| M4-03 | Predication | true/false/negated guard behavior |
| M4-04 | Integer semantics adapter | exec_ir controls map to `arith` integer/bit primitives |
| M4-05 | Mov semantics | selected scalar forms |
| M4-06 | Branch semantics | direct PC target, no runtime label lookup |
| M4-07 | Load/store semantics | initial global forms with bounds/alignment errors |
| M4-08 | ThreadExecutor | fetch, guard, execute, commit and PC update |
| M4-09 | Step limit | deterministic runaway protection |
| M4-10 | E2E corpus | source-to-register and source-to-memory goldens |

## 12.2 Acceptance

At least one parameter-free PTX function must execute:

```text
source -> frontend -> lowering -> exec_ir -> executor
       -> integer/bit arith -> register/memory result -> dump
```

with deterministic raw output and structured unsupported errors.

---

# 13. V2-M5 — Floating, conversion and packed integration

**Goal:** integrate the stabilized arithmetic API without leaking instruction modeling into `arith`.

## 13.1 Integration rule

The semantics layer maps instruction facts to numeric controls:

```text
exec_ir rounding fact       -> arith::rounding_mode
exec_ir .ftz fact           -> arith::subnormal_mode
exec_ir .sat/.satfinite     -> arith::saturation_mode
exec_ir .relu               -> arith::activation_mode
exec_ir random-bit operand  -> stochastic_rounding_input
```

`arith` never parses or stores PTX modifier spelling.

## 13.2 Tasks

| ID | Task | Done condition |
|---|---|---|
| M5-01 | Typed value bridge | raw register bits convert to/from `arith` strong types without host FP |
| M5-02 | Exact F32 subset | add/sub/mul/fma/div/sqrt selected forms |
| M5-03 | Exact F64 subset | selected forms and directed rounding |
| M5-04 | F16/BF16 subset | capability-driven scalar/packed forms |
| M5-05 | Conversion subset | integer/floating/alternate formats selected PTX forms |
| M5-06 | Control mapping tests | each modifier mapping explicit and negative cases rejected |
| M5-07 | Status policy | executor uses value; diagnostics may retain numeric status without creating FP architecture registers |
| M5-08 | Exact conformance | bit-exact reference vectors and optional hardware differential |
| M5-09 | Approximate subset | only operations whose V2-M1 profiles/error bounds are complete |
| M5-10 | E2E FTZ/sat/ReLU/stochastic | source-level cases preserve raw bits and explicit random operand |

## 13.3 Acceptance

No executor/semantics source directly includes SoftFloat or private `arith` backend headers. Exact operations agree bit-for-bit wherever PTX 9.3 defines a unique reference result.

---

# 14. V2-M6 — CTA, SIMT, shared memory and barriers

**Goal:** deterministic multi-thread functional execution.

## 14.1 Tasks

| ID | Task | Done condition |
|---|---|---|
| M6-01 | LaunchConfig | 1D/2D/3D grid and block enumeration |
| M6-02 | Special registers | tid/ntid/ctaid/nctaid and lane/warp facts |
| M6-03 | Warp grouping | partial warp supported |
| M6-04 | Deterministic scheduler | fixed documented selection order |
| M6-05 | CTA lifecycle | per-CTA state/memory creation and teardown |
| M6-06 | Barrier state | generation-aware arrive/wait/release |
| M6-07 | Barrier semantics | waiting threads do not advance |
| M6-08 | Shared memory E2E | write -> barrier -> read |
| M6-09 | Deadlock/no-progress detection | structured diagnostic with blocked reasons |
| M6-10 | Debug events | optional sink observes scheduling without controlling it |

## 14.2 Acceptance

- repeated runs produce identical schedule and state dumps;
- CTA shared state is isolated;
- barrier misuse/deadlock produces diagnostics, not host hangs;
- no third-party FSM dependency is required.

---

# 15. V2-M7 — ABI, calls, runtime API, CLI and inspection

**Goal:** expose a usable simulator and parameterized kernel path.

## 15.1 Tasks

| ID | Task | Done condition |
|---|---|---|
| M7-01 | Entry parameter layout | scalar/pointer/alignment subset documented |
| M7-02 | Host buffer import/export | deterministic global-memory exchange |
| M7-03 | Call frame | return PC, function, register/local metadata |
| M7-04 | Local frame lifetime | function-scoped local memory lifecycle |
| M7-05 | Call/ret/exit lowering | typed exec_ir and structured validation |
| M7-06 | Generic address/CVTA subset | explicit conversions with state-space checks |
| M7-07 | Simulator API | load, instantiate, launch, run, step, inspect |
| M7-08 | CLI | source/entry/grid/block/params/dump/step-limit |
| M7-09 | Debug snapshots | registers, memory, scheduler, calls and TMEM where applicable |
| M7-10 | Event output | optional stable event stream format without separate trace submodule |

## 15.2 Acceptance

A parameterized kernel can consume host input, execute, return host-visible output and emit deterministic inspection data through both library API and CLI.

---

# 16. V2-M8 — Conformance hardening and v0.1

**Goal:** make the implemented subset externally verifiable and maintainable.

## 16.1 Tasks

| ID | Task | Done condition |
|---|---|---|
| M8-01 | Support matrix | parse/lower/execute/validate status separated per instruction/type/control |
| M8-02 | Unit coverage map | every public capability has positive and negative tests |
| M8-03 | E2E corpus | each supported family has source-level regression |
| M8-04 | Exact numeric corpus | raw-bit vectors for each exact arithmetic form |
| M8-05 | Approximate policy table | operation domain, corner cases and bound documented |
| M8-06 | Hardware oracle tool | optional GPU differential path with captured vectors |
| M8-07 | Determinism suite | repeated execution and dump identity |
| M8-08 | Unsupported audit | no silent control/type/instruction downgrade |
| M8-09 | Dependency audit | frontend and SoftFloat isolation proven by target/link checks |
| M8-10 | Security/robustness | malformed inputs and limits return structured errors |
| M8-11 | Documentation | clean-checkout build, execution model and known limitations |
| M8-12 | Release checklist | versions, licenses, support matrix and all CI gates green |

## 16.2 v0.1 release bar

- deterministic single-thread and selected CTA execution;
- global/param/shared/local subset with inspection;
- exact integer/bit and selected floating/conversion support;
- explicit unsupported behavior;
- no dependency leaks;
- clean GCC/Clang/sanitizer gates;
- no open P0/P1 correctness issues in supported surface.

---

# 17. V2-M9 — Advanced PTX semantics

This milestone starts only after v0.1 has a stable support matrix.

Potential workstreams:

```text
atomics and memory ordering
warp vote/shuffle/match/redux
advanced barriers and async copy
expanded address/state-space semantics
sparse tensor operations
instruction-fragment and TMEM tensor integration
advanced block scaling
additional low-precision conversions
texture/surface support
more complete calls/ABI
allowed-set memory-model validation
hardware differential expansion
```

Each advanced feature must preserve the existing boundary:

```text
instruction/layout/storage handling outside arith
logical numeric kernel inside arith when reusable
```

---

# 18. Cross-cutting engineering rules

## 18.1 Unsupported behavior

Never silently:

```text
change rounding to nearest-even
ignore FTZ/saturation/ReLU
replace stochastic rounding with deterministic rounding
promote an unsupported type combination
use host FP as a substitute
return an arbitrary tensor layout
```

Return a typed error or reject at compile time.

## 18.2 Numerical status

Statuses are diagnostics, not simulated architectural registers unless a PTX instruction explicitly exposes a result such as carry/borrow.

Compound operations must merge all contributing status according to an explicit helper. No stage status may be accidentally dropped.

## 18.3 Host floating point

Production semantic paths must not use host `float`/`double` arithmetic, host fenv or `std::fma`.

Host FP is allowed only in:

```text
formatting
diagnostics
validation-only tolerance helpers
hardware-oracle tooling
```

Validation helpers should be a separate target from core arithmetic.

## 18.4 Determinism

Any unspecified behavior selected by the reference simulator must come from an immutable profile. Global mutable numeric/scheduler random state is forbidden.

Stochastic PTX operations receive random bits explicitly from executor/runtime so runs can replay exactly.

## 18.5 Documentation synchronization

Every semantic PR must update at least one of:

```text
support_matrix.md
arith implementation status
exec_ir instruction table
validation policy/golden source
known limitations
```

A passing implementation test without a support-matrix update is not complete.

---

# 19. Branch and PR strategy

## 19.1 Arithmetic integration

No single PR should mix a new oracle, a rewritten production core, and
rewritten expected values without a clearly reviewable red-to-green sequence.

## 19.2 General PR size

Each PR should have one semantic thesis and include:

- public capability delta;
- dependency delta;
- rounding points;
- special-value policy;
- independent oracle source;
- positive/negative tests;
- support-matrix delta.

## 19.3 Merge ordering

Foundation changes merge before dependents:

```text
format facts
-> capabilities
-> conversion
-> scalar/tensor consumers
-> integration
```

Avoid long-lived branches that copy capability or format tables; rebase instead.

---

# 20. Risk register

| Risk | Failure mode | Mitigation |
|---|---|---|
| Oracle contamination | tests duplicate production mistake | literal PTX goldens + independent reference implementation |
| Capability drift | concept true but runtime unsupported | single operation-capability source + generated coverage tests |
| Scope explosion | advanced PTX blocks first vertical slice | strict support matrix and milestone gates |
| Frontend leakage | runtime tied to frontend object lifetime | isolated lowering target and owned ProgramImage |
| SoftFloat state leak | concurrent results depend on other calls | TLS build check, RAII, nested/concurrent stress |
| Host FP leakage | host/compiler changes semantics | target/link/code audit and no-fast-math policy |
| Tensor boundary erosion | arith starts modeling fragments/TMEM | logical tile API and separate instruction adapters |
| Target-dependent claims | one model presented as universal NVIDIA behavior | typed reference profiles + model-dependent status |
| Build-only confidence | green unit tests miss packaging/UB | compiler matrix, sanitizers, consumer and install gates |
| Premature abstraction | third-party FSM/large framework obscures semantics | explicit transitions first; add dependency only with measured need |
| Performance pressure | correctness backend replaced too early | optimize only after differential equivalence and profiling |

---

# 21. Definition of Done for a semantic feature

A feature is complete only when all applicable boxes are checked:

- [ ] PTX 9.3 section/table/pseudocode identified;
- [ ] module owner is correct;
- [ ] public capability is explicit;
- [ ] unsupported combinations are compile-time or typed errors;
- [ ] controls and defaults are explicit;
- [ ] exact intermediate and rounding points documented;
- [ ] zero/subnormal/Inf/qNaN/sNaN behavior documented;
- [ ] status composition documented;
- [ ] implementation does not use host FP in production;
- [ ] oracle is independent;
- [ ] boundary and randomized/replay tests exist;
- [ ] sanitizer build passes;
- [ ] support matrix updated;
- [ ] no forbidden dependency introduced;
- [ ] clean external consumer still builds.

---

# 22. Immediate next actions

1. Create or update a pull request for the final current head and run hosted
   Linux CI (user-owned cloud execution).
2. Confirm its five required contexts: GCC Debug, GCC Release, Clang Debug,
   Clang Release, and GCC ASan + UBSan.
3. Begin V2-M2 exec IR/lowering work without bypassing the established
   `arith` boundary.

---

# 23. Final architecture invariant

The project should always be explainable by this separation:

```text
ptx_frontend tells us what valid PTX means syntactically and structurally.
exec_ir tells the simulator what operation must execute.
state/memory/TMEM hold machine-visible data.
arith computes typed numerical meaning without knowing the instruction.
semantics maps execution facts to pure primitives.
executor commits state changes.
scheduler chooses deterministic progress.
runtime exposes launch and inspection.
```

If a proposed change makes `arith` aware of PTX instruction forms, makes the execution core depend on frontend IR, or makes tensor arithmetic aware of lane/TMEM descriptor encoding, the change violates this plan even if it appears locally convenient.
