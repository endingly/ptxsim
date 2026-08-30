# ptxsim V2-M2 Next-Step Execution Plan

> **Document status:** Active execution plan  
> **Target location:** `.agents/milestone_plan/v2_m2_execution_plan.md`  
> **Project:** `ptxsim`  
> **Plan baseline:** `project_plan.md` / V2 roadmap  
> **PTX baseline:** NVIDIA PTX ISA 9.3  
> **Frontend dependency:** `endingly/ptx_frontend`  
> **Purpose:** guide the transition from completed V2-M1 arithmetic stabilization into V2-M2 `exec_ir` / lowering / `ProgramImage` / core state development

---

# 1. Current state

V2-M1 arithmetic remediation is accepted: PR #3 merged with merge commit
`7da3628c0463f586b190921b283b15ab059d2022` at 2026-08-30T13:08:30Z, and
Linux CI run `33313368339` completed successfully for that exact head. Its
required GCC Debug/Release, Clang Debug/Release, and GCC ASan + UBSan jobs all
succeeded.

The stabilized direction is:

```text
ptx_frontend::resolved_ir
        |
        v
ptxsim::lowering
        |
        v
ptxsim::exec_ir
        |
        v
ptxsim::program::ProgramImage
        |
        +------------------+
        |                  |
        v                  v
   ptxsim::state       ptxsim::arith
        |
        v
   later semantics/executor
```

Important boundaries already established by V2-M1 must remain unchanged:

```text
arith does not model PTX instructions
arith does not depend on exec_ir
exec_ir does not depend on ptx_frontend
state does not depend on ptx_frontend
only lowering may link ptx_frontend
SoftFloat remains private to arith
```

V2-M2 is the correct next full milestone.

---

# 2. Immediate actions before V2-M2 implementation

## 2.1 V2-M1 acceptance record

PR #3 merged with a **merge commit**, not a squash/rebase merge.

Reason:

- the V2-M1 review chain records issue → fix commit → regression-test mappings;
- existing documentation references specific commit hashes;
- preserving history keeps the remediation chain auditable.

The completed acceptance sequence was:

```text
1. wait for main push CI
2. verify all five required checks
3. record V2-M1 as accepted
4. branch from the new main
```

The M2 branch is:

```text
feat/v2-m2-exec-ir
```

---

## 2.2 Advance the ptx_frontend baseline before writing substantial lowering

Current ptxsim frontend pin predates significant newer frontend work.

V2-M2 should first move the exact frontend dependency to a modern, known-good baseline, preferably the current M12-complete frontend revision:

```text
ptx_frontend@1c4547f65c888ee92b1933a20f9a74b380b96953
```

Do not follow `ptx_frontend/main` continuously during M2.

After choosing the M2 baseline, freeze it for the milestone unless a confirmed lowering blocker requires another update.

### Required work

```text
- update overlay port REF
- update source SHA512
- regenerate checked-in resolved_ir snapshot
- update SHA256SUMS
- update generated provenance
- run frontend-lowering vcpkg smoke
- inventory public resolved_ir changes relevant to lowering
```

This work should be isolated from `exec_ir` implementation.

Recommended commit/PR title:

```text
build(frontend): establish V2-M2 frontend baseline
```

---

# 3. V2-M2 objective

V2-M2 must establish a frontend-independent executable representation and core thread state.

Target pipeline:

```text
PTX source
-> ptx_frontend
-> ptx_frontend::resolved_ir
-> ptxsim::lowering
-> ptxsim::program::ProgramImage
-> ptxsim::state::ThreadState
```

The key acceptance property is:

> Once lowering has completed, all frontend-owned objects may be destroyed without invalidating `ProgramImage`, instruction records, state metadata, branch targets, symbols, source-location data required for diagnostics, or any future execution input.

V2-M2 does **not** need to execute a complete PTX program yet.

Execution is primarily V2-M4.

---

# 4. V2-M2 architectural rules

## 4.1 Exec IR is not SASS

`exec_ir` is a typed functional execution IR.

It must not encode:

```text
SASS opcodes
physical registers
issue width
latency
pipelines
scoreboards
hardware scheduling
machine-code encoding
```

It should encode only execution facts required by the functional simulator.

---

## 4.2 Exec IR must not own frontend types

Forbidden:

```cpp
ptx_frontend::resolved_ir::Instruction*
std::reference_wrapper<ptx_frontend::...>
frontend NodeId reused as runtime identity
frontend string_view stored into ProgramImage
```

Required:

```text
frontend identity
    ↓ lowering
stable ptxsim identity
```

---

## 4.3 Do not create one giant dynamic instruction object

Avoid:

```cpp
struct Instruction {
    RuntimeOpcode opcode;
    RuntimeType type;
    std::vector<any_operand> operands;
    std::vector<any_modifier> modifiers;
};
```

Prefer bounded typed instruction records:

```cpp
using Instruction = std::variant<
    MovInst,
    IntegerBinaryInst,
    IntegerMulInst,
    BitInst,
    BranchInst,
    LoadInst,
    StoreInst
>;
```

Each record should expose only facts needed by the executor.

---

## 4.4 PTX text syntax must disappear during lowering

Do not carry raw modifier spellings such as:

```text
".rn"
".sat"
".ftz"
".lo"
".hi"
```

Lower them into typed facts.

Example:

```text
PTX .rn
-> exec_ir::Rounding::NearestEven

PTX .hi
-> exec_ir::ProductPart::High
```

Later, the semantics layer maps those facts into `arith` controls.

---

# 5. Recommended V2-M2 implementation sequence

---

# Phase M2-00 — Milestone bootstrap and frontend baseline

## Scope

```text
V2-M1 acceptance bookkeeping
frontend baseline upgrade
generated snapshot update
M2 documentation skeleton
target/CMake skeleton only
```

## Deliverables

Recommended documents:

```text
docs/exec_ir.md
docs/execution_model.md
docs/lowering_policy.md
```

Recommended module directories:

```text
submod/common/
submod/exec_ir/
submod/program/
submod/state/
submod/lowering/
```

Do not yet implement bulk lowering.

## Acceptance

```text
new frontend pin installs cleanly
generated snapshot integrity passes
frontend-lowering feature works
no new dependency leaks into arith/core
```

---

# Phase M2-01 — Common IDs and raw machine values

Corresponds mainly to:

```text
M2-01 Core IDs
M2-02 Raw machine value storage
```

This phase is the foundation for both Exec IR and state.

## 5.1 Strong IDs

Define strong types instead of raw integers:

```cpp
ProgramCounter
FunctionId
RegisterSlot
SymbolId
LabelId
SourceLocationId

ThreadId
CtaId
WarpId
LaneId
```

Recommended properties:

```text
- trivially copyable
- equality/order where meaningful
- explicit construction
- no implicit cross-ID conversion
- deterministic formatting
```

Do not reuse frontend IDs directly.

---

## 5.2 Raw machine value

Provide storage for:

```text
pred
b8
b16
b32
b64
b128
```

Suggested abstraction:

```cpp
class RawValue;
```

or a small strongly typed family.

Requirements:

```text
- no host float/double storage
- exact raw-bit preservation
- width known and checked
- no implicit truncation
- typed extraction helpers
- deterministic equality/dump
- b128 supported explicitly
```

Host endianness must not leak into logical register semantics.

Memory byte ordering belongs to the memory subsystem in V2-M3.

## Acceptance

```text
IDs and RawValue have independent unit tests
invalid width extraction is rejected
no frontend/arith dependencies are introduced
```

---

# Phase M2-02 — Operand model and initial typed instruction records

Corresponds to:

```text
M2-03 Operand model
M2-04 Initial instruction records
```

## 5.3 Operand model

Initial operands should include:

```cpp
RegisterOperand
ImmediateOperand
SpecialRegisterOperand
AddressOperand
BranchTarget
FunctionTarget
```

Prefer typed operands with explicit width/type facts.

Do not model every PTX source operand spelling.

---

## 5.4 Initial instruction subset

Recommended first IR records:

```text
mov
integer add
integer sub
integer mul
bit operations
branch
selected load
selected store
```

The selected `ld/st` records exist only to establish the IR/address boundary.

Full memory behavior belongs to M3/M4.

Suggested instruction family:

```cpp
MovInst
IntegerBinaryInst
IntegerMulInst
BitInst
BranchInst
LoadInst
StoreInst
```

The IR can use a bounded `std::variant`.

### Each record may contain

```text
destination/source operands
value width/type
numeric/control facts
predication information
resolved branch/function targets
state-space fact when already known
```

### Must not contain

```text
frontend pointers
source modifier strings
SoftFloat types
arith backend types
runtime parser structures
SASS encoding assumptions
```

## Acceptance

Each instruction record must have:

```text
constructor/invariant tests
positive shape tests
negative malformed-record tests
stable dump/format
```

---

# Phase M2-03 — ProgramImage and Exec IR verification

Corresponds primarily to:

```text
M2-05 ProgramImage
```

## 5.5 ProgramImage ownership

Suggested conceptual layout:

```text
ProgramImage
  ├── instruction array
  ├── function table
  ├── symbol table
  ├── register-layout metadata
  ├── entry-point table
  └── source/debug side metadata
```

Prefer immutable ownership after construction.

---

## 5.6 Function layout

Functions should resolve to stable PC ranges:

```text
FunctionId
-> [begin_pc, end_pc)
```

Branches must use resolved targets.

Forbidden at runtime:

```text
lookup label name string
lookup function name string
consult frontend symbol object
```

---

## 5.7 Source metadata

Use side tables where possible:

```text
PC -> SourceLocationId
SourceLocationId -> copied source metadata
```

Execution records should not be bloated with debug strings.

---

## 5.8 IR verifier

Add a verifier before M2 is considered stable.

It should validate at minimum:

```text
instruction PC range
function PC range
branch target validity
register-slot validity
operand width compatibility
symbol ID validity
function ID validity
instruction-record invariants
```

Do not defer these structural checks to the future executor.

## Acceptance

```text
ProgramImage owns all records
ProgramImage can be deterministically dumped
invalid internal references are rejected
same logical input yields stable IDs/PCs
```

---

# Phase M2-04 — Core state

Corresponds to:

```text
M2-09 RegisterFile
M2-10 ThreadState
M2-11 Special register provider
```

This phase can run partially in parallel with M2-02/M2-03 after M2-01 is merged.

---

## 5.9 RegisterFile

Recommended structure:

```text
dense RawValue slots
+ declared width/layout metadata
+ initialization bitmap
```

Do not initialize all registers to architectural zero silently.

Uninitialized read should remain observable through a structured result/policy.

Potential interface:

```cpp
read(RegisterSlot)
write(RegisterSlot, RawValue)
is_initialized(RegisterSlot)
```

---

## 5.10 ThreadState

Initial ThreadState should contain only core execution state:

```text
current PC
thread lifecycle/status
RegisterFile
predicate/register values
call-frame placeholder
```

Do not add scheduler/barrier/shared-memory ownership here yet.

---

## 5.11 SpecialRegisterProvider

Define a testable abstraction independent of the future scheduler.

Example direction:

```cpp
class SpecialRegisterProvider {
public:
    virtual expected<RawValue, StateError>
    read(SpecialRegisterId, const ThreadState&) const = 0;
};
```

M2 establishes the interface.

Actual launch-derived values such as:

```text
%tid
%ntid
%ctaid
%nctaid
%laneid
%warpid
```

are primarily populated in V2-M6.

## Acceptance

```text
state has no ptx_frontend dependency
RegisterFile initialization is testable
ThreadState can be constructed from ProgramImage metadata
special-register provider can be mocked
```

---

# Phase M2-05 — Lowering diagnostics and lowering context

Corresponds to:

```text
M2-06 Lowering diagnostics
M2-07 Lowering context
```

`ptxsim_lowering` is the **only** production module that may link `ptx_frontend`.

---

## 5.12 Lowering diagnostics

Structured lowering errors should contain:

```text
error code
source location
function context
instruction/family context
unsupported feature detail
optional operand/control detail
```

Separate the following categories:

```text
FrontendInvalidInput
UnsupportedPtxFeature
UnsupportedPtxVariant
UnsupportedTypeCombination
LoweringInvariantViolation
MalformedResolvedIr
InternalLoweringError
```

Do not use one generic `"unsupported"` string.

---

## 5.13 LoweringContext

The context owns frontend-to-simulator mapping during lowering:

```text
frontend function identity -> FunctionId
frontend symbol identity   -> SymbolId
frontend register identity -> RegisterSlot
frontend label identity    -> PC / block target
```

These maps are temporary.

They must not survive inside `ProgramImage`.

## Acceptance

```text
unsupported-but-valid PTX gets structured unsupported diagnostics
invalid frontend objects are distinguishable
source location survives into diagnostics
```

---

# Phase M2-06 — Module lowering

Corresponds to:

```text
M2-08 Module lowering
```

Use a two-pass approach.

---

## 5.14 Pass 1 — Declare stable identities

First allocate simulator ownership:

```text
functions
symbols
register layouts
labels/basic blocks
entry points
```

Do not construct instructions while identities remain unresolved.

---

## 5.15 Pass 2 — Lower instructions

For each resolved instruction:

```text
1. inspect frontend variant
2. check ptxsim execution support
3. normalize operand/control facts
4. resolve stable simulator IDs
5. create typed exec_ir record
6. attach source metadata
```

Unsupported legal forms must fail explicitly.

Never lower an unsupported form into a nearby supported one.

---

## 5.16 Initial supported lowering subset

Keep the first lowering slice narrow:

```text
mov
integer add/sub/mul
selected bit ops
direct branch
selected ld/st records
basic predication
```

Do not expand immediately into:

```text
all floating
all conversions
tensor
atomics
async copy
warp collectives
WGMMA
TCGEN05
```

Those are later milestones.

---

# Phase M2-07 — Lifetime and end-to-end M2 acceptance

This is the formal V2-M2 exit phase.

## Required test pipeline

```text
PTX source
-> frontend parse/resolve
-> resolved_ir
-> ptxsim lowering
-> ProgramImage
-> ThreadState
```

Then explicitly destroy:

```text
source buffer
frontend CST
frontend AST
binding/semantic objects
resolved_ir
LoweringContext
```

After destruction the test must still be able to:

```text
walk instructions
dump ProgramImage
resolve function ranges
read copied source metadata
inspect symbols
create ThreadState
verify branch targets
run ProgramImage verifier
```

---

## Required deterministic tests

At minimum:

```text
same source -> stable ProgramImage dump
stable function IDs
stable register slots
stable PCs
forward branch lowering
backward branch lowering
multi-function isolation
unsupported instruction diagnostic
unsupported type/control diagnostic
source-location preservation
frontend lifetime independence
```

---

# 6. Recommended PR sequence

Do not implement V2-M2 as one large PR.

Recommended sequence:

| PR | Scope |
|---|---|
| M2-00 | frontend baseline + milestone bootstrap |
| M2-01 | strong IDs + RawValue |
| M2-02 | operand model + typed instruction records |
| M2-03 | ProgramImage + verifier |
| M2-04 | RegisterFile + ThreadState + special-register interface |
| M2-05 | lowering diagnostics + LoweringContext |
| M2-06 | minimal module/instruction lowering |
| M2-07 | lifetime/E2E acceptance + docs/API stabilization |

M2-04 may develop in parallel with M2-02/M2-03 after M2-01 lands.

Recommended dependency graph:

```text
M2-00
  |
  v
M2-01
  |
  +------------------+
  |                  |
  v                  v
M2-02             M2-04
  |
  v
M2-03
  |
  v
M2-05
  |
  v
M2-06
  |
  v
M2-07
```

---

# 7. V2-M2 explicit non-goals

The following should not be implemented as part of M2 unless required to establish an interface:

```text
actual instruction execution
global/shared/local memory backing storage
TMEM storage implementation
scheduler
warp/CTA execution
barriers
floating execution adapters
conversion execution adapters
kernel ABI
host launch API
CLI
calls/returns
atomics
async copy
WGMMA
TCGEN05
```

Avoid scope creep.

---

# 8. V2-M2 acceptance checklist

## Build / architecture

- [ ] `ptxsim_exec_ir` exists
- [ ] `ptxsim_program` exists
- [ ] `ptxsim_state` exists
- [ ] `ptxsim_lowering` exists
- [ ] only lowering links `ptx_frontend`
- [ ] exec_ir/program/state installed headers contain no frontend type
- [ ] arith has no new dependency on exec_ir/state/frontend

## Exec IR

- [ ] strong simulator IDs exist
- [ ] raw pred/b8/b16/b32/b64/b128 storage exists
- [ ] typed operands exist
- [ ] bounded typed instruction records exist
- [ ] branch targets are resolved
- [ ] verifier rejects malformed IR

## ProgramImage

- [ ] instructions are owned
- [ ] functions have stable PC ranges
- [ ] symbols use simulator IDs
- [ ] source metadata is copied/owned
- [ ] deterministic dump exists

## State

- [ ] dense RegisterFile exists
- [ ] initialization tracking exists
- [ ] ThreadState exists
- [ ] special-register provider is mockable

## Lowering

- [ ] two-pass lowering exists
- [ ] structured diagnostics exist
- [ ] unsupported legal PTX is explicit
- [ ] frontend IDs do not survive lowering

## E2E

- [ ] PTX -> resolved_ir -> ProgramImage works
- [ ] frontend objects can be destroyed
- [ ] ProgramImage remains valid
- [ ] same input produces deterministic IR
- [ ] GCC Debug/Release pass
- [ ] Clang Debug/Release pass
- [ ] ASan+UBSan pass
- [ ] installed core consumer does not require ptx_frontend
- [ ] lowering feature consumer/install path passes

---

# 9. Development order after V2-M2

The recommended roadmap after M2 is:

```text
V2-M3 Memory / TMEM foundation
    ↓
V2-M4 Single-thread integer vertical slice
    ↓
V2-M5 Floating / conversion / packed integration
    ↓
V2-M6 CTA / SIMT / shared memory / barriers
    ↓
V2-M7 ABI / calls / runtime API / CLI / inspection
    ↓
V2-M8 Conformance hardening / v0.1
    ↓
V2-M9 Advanced PTX
```

---

# 10. Why this order should be preserved

## V2-M3 before V2-M4

M4 includes actual `ld/st` execution.

Therefore memory ownership, address spaces, lifetimes, bounds/alignment policy and TMEM separation must already exist.

---

## V2-M4 before V2-M5

The first executable vertical slice should use integer/bit operations.

This isolates executor correctness from floating complexity.

M4 stabilizes:

```text
PC update
fetch
predication
operand reads
register writes
branching
memory errors
commit order
step result
step limit
```

Only after that should floating controls be introduced.

---

## V2-M5 after executor mechanics are stable

V2-M5 maps instruction-level facts into the already-stable `arith` API:

```text
rounding
FTZ
sat
satfinite
ReLU
stochastic random bits
approximation profile
```

This prevents simultaneous debugging of:

```text
lowering
state
executor
arith control mapping
floating semantics
```

---

## V2-M6 only after deterministic single-thread execution

SIMT adds:

```text
thread grouping
warp state
CTA state
shared memory
barriers
blocking
scheduler ordering
deadlock detection
```

Do not introduce this before single-thread execution is reliable.

---

# 11. ptx_frontend update policy after M2 begins

Do not continuously follow frontend `main`.

Recommended policy:

```text
V2-M2 start:
    pin frontend M12-complete baseline

V2-M2 .. V2-M5:
    keep pin stable
    upgrade only for a demonstrated lowering blocker

V2-M6:
    evaluate newer frontend only when cluster/mbarrier coverage is required

V2-M9:
    deliberately adopt frontend M14-M18 domains for TMA/WGMMA/TCGEN05
```

The isolation rule makes this feasible:

```text
ptx_frontend
    ↓
lowering
    ↓
stable ptxsim exec_ir
```

Frontend API churn should be absorbed by `ptxsim_lowering`, not propagated into execution modules.

---

# 12. Agent implementation rules

All agents working on V2-M2 should follow these constraints.

## Required

```text
small reviewable PRs
unit tests with each public abstraction
stable deterministic dumps
typed errors
no frontend ownership leaks
explicit unsupported handling
header self-contained tests
consumer/install tests
```

## Forbidden

```text
instruction parsing inside arith
frontend types in exec_ir
frontend types in state
runtime string label lookup
one generic untyped instruction bag
host float/double register storage
silent operand truncation
silent unsupported instruction fallback
building executor behavior inside M2 IR objects
adding scheduler/barrier state early
```

---

# 13. Recommended first three concrete tasks

Immediately after V2-M1 merges:

### Task A — M2 baseline

```text
advance ptx_frontend pin
regenerate snapshot
validate new resolved_ir surface
update M2 docs
```

### Task B — common identity layer

```text
strong IDs
RawValue
format/dump helpers
tests
```

### Task C — Exec IR skeleton

```text
operand types
instruction variant
basic invariants
ProgramCounter/target model
```

Do not begin broad lowering before Task B and Task C are accepted.

---

# 14. Final execution recommendation

The recommended next sequence is:

```text
merge V2-M1
↓
establish frontend M12 baseline
↓
IDs / RawValue
↓
typed operands / instruction records
↓
ProgramImage / verifier
↓
core state
↓
lowering context / diagnostics
↓
module lowering
↓
lifetime + deterministic E2E
↓
V2-M2 acceptance
↓
V2-M3
↓
V2-M4
↓
V2-M5
↓
V2-M6
↓
V2-M7
↓
V2-M8
↓
V2-M9
```

The most important architectural principle for the next stage is:

> **Treat `exec_ir` as the stable simulator boundary, and treat `ptxsim_lowering` as the replaceable compatibility layer between an evolving frontend and a stable execution core.**

If that boundary is kept strict, future ptx_frontend M13-M19 work can be adopted without repeatedly redesigning `ProgramImage`, `ThreadState`, memory, executor or arithmetic semantics.
