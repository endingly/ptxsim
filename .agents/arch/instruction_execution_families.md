# Instruction execution families

## Adopted scope and ownership

Execution preparation is generated from the pinned frontend instruction model
and the existing execution-IR projection. A small handwritten execution-family
template owns the repeated read / destination-check / evaluation / stage flow.
Handwritten semantic adapters interpret arithmetic controls; existing engine
stages retain issue validation, predication, lazy resource access and commit.

The execution-family templates live in the engine Python package's `templates/`
directory and are rendered by Jinja2. Python owns structural validation and
derives typed operands before rendering; templates only lay out C++ preparation
and dispatch. Package loading, strict undefined-variable errors, disabled HTML
escaping and CMake template dependencies are part of the generation contract.
Only implemented families receive templates; this does not migrate the other
generators or introduce another YAML specification.

The first real opcode is **Add**, not a synthetic opcode or a u32-only capability.
The agreed scope is every Add variant currently described by
`ptx_frontend@cf1f32161890b04e1060095a96ad5d8ab996db27` and projected into
`exec_ir::Add`. All its declared types and controls are obligations. This is a
functional execution model across the forms in that fixed specification, not
a newly selected SM target. Target-SM/family availability validation is not
introduced by this change.

This pin retains owned entry-parameter metadata and completes the frontend FMA
contract. The C++ port and Python generator dependency use the same commit.

Extended-precision `add.cc` / `addc` and implicit condition-code state are
explicitly outside this task, as agreed by the maintainer. They require frontend
and architectural-state work. Neither this boundary nor a successful frontend
projection proves all of PTX ISA 9.3 is implemented.

## Single source of structural facts

The generator consumes `ProjectedInstruction` / `ProjectedForm`, their frontend
operand access and type expressions, and the existing C++ mapping/naming rules.
Do not maintain a second variant, modifier, layout or field-path specification
in YAML, Python dictionaries, JSON or C++ macros. A small **opcode-level**
binding selecting an execution family is allowed; operand count alone does not
establish arithmetic semantics.

Generate ordinary private `.hpp` / `.cpp` build artifacts and compile them into
`ptxsim_inst_execute_engine`. Do not generate into handwritten source files,
interpret specifications at runtime, or add a frontend C++ dependency to the
engine. Python code remains module-owned under
`submod/inst_execute_engine/python`, inside the single root-configured
`ptxsim-codegen` wheel.

## ValueALU contract

The first family prepares one lane with explicit typed inputs, one register
result, and no implicit state changes:

1. Read inputs in specification order after common predicate gating.
2. Validate the result register without reading its previous contents.
3. Call a pure semantic adapter with decoded values and read-only controls.
4. Retain the resulting `RawValue` and register view in `PreparedEffect`.
5. Let the shared commit stage write the register and then update the thread PC.

Input and output types are derived independently. Add/Sub/Mul have two sources;
FMA has three, captured before any destination write. Mixed-precision operands
retain independent types and widths. Physical bit-container bindings require an
unambiguous semantic type of matching width from the projected modifiers.
Signed values use bit-preserving interpretation, floating
values preserve their encodings, and packed lanes do not share carry or status.
Other width policies or effects require explicit family support, not silent
truncation or dropped writes.

The arithmetic adapter must not access registers, memory, launch state or PCs.
It maps PTX controls to `arith` controls. Packed forms may invoke scalar `arith`
primitives lane by lane; they must not rely on whole-container addition or host
floating arithmetic. Instruction-independent `arith` never receives PTX IR.

Different dataflows (memory, control flow, collectives or implicit state) should
get their own family when needed. Do not build a universal callback/effect DSL,
an inheritance framework or a template directory of unused future families.

## Completeness and error boundaries

The completion unit is an entire opcode in the declared specification scope.
Do not expose variant/modifier/layout support queries or treat a nonempty handler
as proof of support. A new source form must either be correctly generated and
implemented or fail generation, compilation, linking or acceptance; it must not
quietly acquire an unsupported/default-success stub.

Structural generation checks and successful linking are necessary but not
semantic proofs. Full linkage must reference the generated dispatch so static
library dead-code omission cannot conceal undefined semantic adapters.

Maintain issue validation, instruction binding and required-successor checks
before lane preparation. Malformed directly constructed IR is distinct from
an unimplemented opcode. Register bindings, source initialization and width
failures remain lane-local faults. A predicate-disabled lane must not read
unused ordinary operands. Prepare all issued lanes before any scalar commit;
retain existing fault ordering and collective behavior. No new multi-effect
transactional guarantees are implied.

Other historical handlers (`mov`, `setp`, `ld`, `st`, `bar`, `bra`, `exit`) retain
their existing behavior. They are not automatically certified by the Add work;
their full-op audits and eventual family migration remain separate tasks.

## Mov and topology reads

Mov uses a separate movement family. The generator derives its three projected
variants and five operand layouts from the frontend model: scalar copy, pack,
unpack, vector topology read, and predicate copy. Movement preserves raw bits
and does not invoke arithmetic conversion or floating-point evaluation.

Scalar copies cover b16/u16/s16, b32/u32/s32/f32 and b64/u64/s64/f64 with exact
register widths. The wider-register relaxation for load/store and conversion
instructions does not apply to Mov. Predicate copies apply the source negation
flag before staging the result, including when source and destination alias.
This negation is supported in bound IR; the pinned frontend layout matcher
currently rejects negated Mov predicate sources in PTX text.
Bit-size pack/unpack forms concatenate or split two/four components from low
to high significance, including b128 containers. A source cannot be a sink;
unpack destinations can contain sinks but require at least one actual register.
All sources are captured and all destinations checked before any write, including
aliases. Common predicate gating suppresses ordinary reads and resource access.

Executable special-register identities are owned by `exec_ir`; lowering maps
frontend identities explicitly. The reusable engine reader obtains the following
values from the existing execution topology:

| PTX source | Authoritative value |
| --- | --- |
| `%tid.{x,y,z}` | Thread coordinates within the CTA |
| `%ntid.{x,y,z}` | CTA thread dimensions |
| `%ctaid.{x,y,z}` | CTA coordinates within the grid |
| `%nctaid.{x,y,z}` | Grid CTA dimensions |
| `%laneid` | Lane position within the warp |
| `%warpid` | Warp index within the CTA under the fixed functional scheduler |
| `%gridid` | Caller-assigned 64-bit launch identity |
| `%lanemask_{eq,le,lt,ge,gt}` | 32-bit positional masks relative to the lane ID |

The launch owner supplies distinct grid identities when distinct launches must
be distinguishable. Legacy 16-bit topology-component reads and 16/32-bit grid-ID
reads retain the low bits. Lane masks describe positions, independent of active
lanes or the configured number of lanes; lane IDs above 31 cannot be represented
and produce a structured fault. Vector topology moves write x, y, z and zero
into four contiguous b32 component slots. Register storage remains memory-owned;
execution-model nodes do not acquire memory handles or instruction semantics.

These rules follow the [PTX ISA Mov and special-register sections](https://docs.nvidia.com/cuda/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-mov).
Structural coverage is not whole-ISA certification. Entry-input parameter
addresses reuse the existing ABI byte offsets and are consumed by indirect
`ld.param`; address materialization does not load the parameter value. Other
symbolic storage address materialization still needs
executable resource bindings and allocation. Device-function formal addresses
need activation-owned local storage, and function addresses need a defined
executable code-address model. Physical SM placement and device capacities,
clustering, timers, performance/environment registers, graph execution and shared
resource statistics need their corresponding runtime contracts. They are not
replaced by thread/CTA indices or arbitrary constants. Unbacked frontend source
categories currently fail lowering, even in a predicated instruction; predicate
suppression does not make such a module lowerable. Already-bound execution IR
applies predicate gating before attempting
an unavailable source. Remaining frontend and runtime acceptance prerequisites
are recorded in the active executor plan.

## Add coverage obligations

The pinned projection has nine forms and 23 type paths (control combinations
are additional obligations, not extra opcode identities):

| Form | Types | Main semantic controls |
| --- | --- | --- |
| `FloatF32` | f32 | Four rounding modes, FTZ, saturation |
| `FloatF32x2` | f32x2 | Four rounding modes, FTZ |
| `FloatF64` | f64 | Four rounding modes |
| `Half` | f16, f16x2 | Round-nearest-even, FTZ, saturation |
| `Bfloat` | bf16, bf16x2 | Round-nearest-even |
| `MixedF32` | f32 result, f16/bf16 input, f32 addend | Four rounding modes, saturation |
| `IntegerNoSat` | u16/u32/u64, s16/s32/s64, u16x2/s16x2 | Wrapping |
| `Sat` | s32/u32, u16x2/s16x2 | Per-element integer saturation |
| `PackedOptionalSat` | u8x4/s8x4 | Per-element wrapping or saturation |

The general scalar lowering binder must preserve resolved source widths and
bit patterns; the earlier b32/u32-only binder cannot carry this full set.
The frontend already encodes negative integers in two's-complement form, so
lowering must not negate them again based on the original spelling flag.

The semantic reference is the [PTX ISA 9.3 manual](https://docs.nvidia.com/cuda/parallel-thread-execution/index.html),
specifically its ordinary integer, floating, half/bfloat and mixed-precision
Add sections. The manual documents packed operations per element and treats
extended-precision carry operations separately. In this change the pinned
frontend model defines the accepted structural boundary; the numerical tests
must independently check the stated arithmetic behavior.

## Sub scope

Handwritten per-op semantic adapters live in
`submod/inst_execute_engine/src/semantics/<op>_semantics.hpp/.cpp`.
Shared codecs, floating controls and execution-stage implementation remain in
`src/`; generated preparation includes the adapters via `semantics/` paths.

Sub reuses the same ValueALU template as Add, with its own opcode binding and
pure subtraction semantics. Its topology is derived independently from the same
pinned frontend specification; it must not inherit Add's integer type list.
The eight Sub forms have 17 type paths:

| Form | Types | Controls |
| --- | --- | --- |
| `FloatF32` | f32 | Four rounding modes, FTZ, saturation |
| `FloatF32x2` | f32x2 | Four rounding modes, FTZ |
| `FloatF64` | f64 | Four rounding modes |
| `Half` | f16, f16x2 | Round-nearest-even, FTZ, saturation |
| `Bfloat` | bf16, bf16x2 | Round-nearest-even |
| `MixedF32` | f32 result, f16/bf16 first input, f32 second input | Four rounding modes, saturation |
| `IntegerNoSat` | u16/u32/u64, s16/s64 | Wrapping |
| `OptionalSat` | s32, u8x4/s8x4 | Wrapping or saturation |

Subtraction preserves operand order and calls `arith::sub` directly. Packed
lanes do not propagate borrow to adjacent lanes. `sub.cc` / `subc` and implicit
condition-code state remain outside the implemented scope; no frontend revision
or target-SM policy change is required.

Mixed Add/Sub diagnostics and primary PTX tests use the frontend's canonical
`{.rnd}{.sat}.f32.{f16|bf16}` order. Historical trailing `.sat` is an explicitly
declared frontend alias, not a simulator-side rewrite or a general permission
to reorder modifiers. C++ and Python frontend revisions must be upgraded
together because canonical slot changes can reorder generated aggregate members.

## Mul scope

The pinned specification declares five Mul forms: `RnF32`, `LoU32`, `HiU32`,
`WideU32`, and `WideS32`. They reuse ValueALU preparation with handwritten
`src/semantics/mul_semantics.hpp/.cpp` adapters calling `arith::mul`.
Low/high forms select the corresponding 32-bit product half; wide forms produce
the full unsigned/signed 64-bit product of two 32-bit inputs. Floating multiply
is register-only f32 with fixed round-to-nearest-even and preserved subnormals.
Other PTX Mul forms are outside this pinned specification and this integration.

Operand type paths come from projected modifier-field or fixed-scalar type
expressions. The destination need not share the source type or width. Exact
and same-width register policies must retain their declared width checks;
unsupported type expressions or policies must fail generation rather than
silently falling back to a same-width operation.

## FMA scope

The frontend pin declares 16 FMA variants and 70 canonical type/control
combinations. All reuse ValueALU preparation and the pure
`src/semantics/fma_semantics.hpp` adapter:

| Types | Controls |
| --- | --- |
| f32 | Four rounding modes, FTZ, saturation |
| f64 | Four rounding modes |
| f32x2 | Four rounding modes, FTZ |
| f16/f16x2 | Round-nearest-even; FTZ or OOB handling; optional saturation or ReLU |
| bf16/bf16x2 | Round-nearest-even, optional OOB handling and ReLU |
| f32 result, f16/bf16 multiplicands, f32 accumulator | Four rounding modes, saturation |

Every path calls fused `arith::fma`; an independently rounded multiplication
followed by addition is not equivalent. Mixed forms widen their multiplicands
exactly and round the fused result once to f32. Packed forms operate per lane
without sharing result bits. PTX controls are translated at the semantic adapter;
the arithmetic library retains no PTX, register or runtime dependency.

OOB handling recognizes exactly the positive raw marker `0x7ff7` for f16 and
bf16, as disclosed in NVIDIA
[US20240168765A1, paragraph 0343 and Table 89](https://patents.google.com/patent/US20240168765A1/en).
A matching multiplicand forces positive zero independently per lane. Other NaNs,
including negative `0xfff7`, retain ordinary FMA handling. Recognition occurs
before arithmetic or NaN canonicalization can alter the marker's bits.

The accumulator alone does not trigger the rule. This operand-position choice
follows NVIDIA CUTLASS's
[`guarded_multiply_add`](https://github.com/NVIDIA/cutlass/blob/f74fea9ce35868d3ae9f8d1dce1969d7250d3f90/include/cutlass/functional.h);
its broader all-NaN software guard is not used. The patent discloses an embodiment,
not an exhaustive hardware predicate. Exact sign-sensitive matching and the
accumulator exclusion remain explicit model choices pending hardware validation
or an authoritative ISA clarification. Strict whole-op hardware acceptance
therefore remains open. ReLU canonicalizes half/bfloat NaN results to CUDA's
`0x7fff`, distinct from the OOB marker.

The generated form validator rejects malformed rounding/type controls before
ordinary lane resources are accessed. Source initialization and destination
width errors retain the shared lane-local failure-before-commit contract.
PTX pipeline tests supply operands through entry parameters and read global
output bytes through the public runtime APIs, including aliases, legal
immediates and all canonical modifier combinations.

## Setp scope

The pinned frontend exposes five predicate-comparison forms: `LtU32`, `GeS32`,
`LtAndU32`, `EqU32Pair`, and `LtAndS32Pair`. A predicate-comparison family derives
operand access, source types, predicate destination shape, combine-input shape
and modifier constraints from that projection. It must not force predicate
destinations into the numeric ValueALU family. Handwritten comparison semantics
live under `src/semantics/` and use exact native signed/unsigned integer
comparisons; the current `arith::compare` API supports floating types only.
The shared lowering leaf binder also supports predicate pairs by binding both
members through the existing predicate binder; generated Setp lowering requires
no opcode-specific implementation.

For comparison result `t`, a bare pair yields `(t, !t)`. Boolean combination
yields `(t && c, !t && c)`, so the second result is not generally the negation of
the first final result. Only the single-destination unsigned AND form exposes
a negatable combine predicate in this specification. Destination negation and
out-of-form comparison/Boolean selectors are invalid IR, not ignored flags.

Both predicate destinations are validated before either write. All comparison
and combine inputs are captured before commit, including when an input aliases
a destination. A bounded second staged register write extends the existing
synchronous prepare/commit contract; this does not introduce general transactions
or concurrent register-frame mutation. Existing execution-predicate suppression,
lane fault isolation and authoritative thread PC behavior remain unchanged.
The pinned frontend does not require pair destinations to be distinct. The
engine retains aliases and commits in destination order (p then q), so q is
the final value when both destinations name the same slot.

Floating comparisons, other comparison/Boolean combinations and predicate sinks
are not exposed by the pinned Setp projection and are outside this integration.

## Ordinary load/store scope

`ld` and `st` use a separate memory-transfer execution family, not numeric
ValueALU semantics. Projection supplies form identity, operand roles, types,
vector arity and modifier domains. Generated adapters call common handwritten
address/byte-transfer helpers. No frontend/package revision change is needed.

The integration covers ordinary scalar and supported v2/v4/v8 transfers with
the pinned 8/16/32/64-bit integer/bit types and f32/f64. Register storage width
is separate from memory width: loads extend signed integers with the sign bit
and other encodings with zero bits; stores retain low bits. Floating encodings
are copied without host floating-point conversion. Natural alignment is the
complete vector byte size, with at most 32 bytes per supported instruction.

Reuse launch bindings for global, constant, shared, local and entry-parameter
resources, including existing generic-address windows. Explicit stores cannot
target read-only spaces. Generic stores retain the memory subsystem's permission
checks. Numeric b32/b64 address registers, immediate addresses and checked byte
offsets are supported. Entry-parameter symbols use the source-ordered layouts
owned by `exec_ir`; the simulator packs arguments and automatically binds their
region. This does not allocate symbolic global/shared/local declarations.

Preparation captures every source and validates all destinations and the full
memory span before architectural mutation. Vector load writes commit in operand
order; vector stores stage one contiguous byte span. This is the existing
synchronous per-lane prepare/commit guarantee, not a new cross-lane atomicity
or concurrent register-frame mutation contract. Predicated-off lanes must not
read inputs or resolve memory resources.

Only omitted/weak consistency without scope, MMIO or cache controls is included.
Acquire/release/relaxed/volatile, MMIO, cache-policy/eviction/non-coherent forms
and function-parameter resources remain explicitly unsupported. Do not silently
execute these as plain accesses. Fences, atomics, asynchronous copies and `ldu`
are separate integrations.

## Branch, exit and named barriers

The control-flow family covers the pinned `bra` and `exit` forms. A branch
updates the authoritative thread PC; `.uni` is the producer's uniformity
guarantee, not a separate opcode or a different branch algorithm. Common
predicate gating handles untaken branches and conditional exits. Calls,
returns and indirect branches are separate integrations.

The barrier family covers the pinned `bar` forms: warp synchronization,
CTA sync/arrive, and population-count/AND/OR reductions, including the `.cta`
spellings. Projection supplies operand layouts and fixed modifier tokens;
generated adapters normalize operands into handwritten collective preparation.
The separate `barrier` opcode and asynchronous barriers are not included.

`Simulator` owns a persistent instruction engine. The engine retains deferred
CTA collective continuations and reduction destinations across issue calls; it must
outlive those waits. `CtaBarrierState` owns protocol, generation, arrival counts
and warp masks only. Neither execution-model nodes nor the memory subsystem
acquire dependencies on engine effects or on each other.

CTA arrival is counted once per converged non-exited warp. Local arrivals must
agree on the dynamic instruction and barrier controls. Different warps may
combine sync and arrive on the same barrier generation; incompatible counts or
reduction protocols are rejected before recording an arrival. Sync and
reduction wait for completion; arrive continues after local convergence.
Explicit counts must be positive warp-size multiples; an explicit zero is
rejected, not interpreted as the omitted CTA-wide count. The PTX text does not
establish that zero is an alias for omission.
Reduction inputs are captured before waiting and results are written before
waiters resume. Resources retained for deferred writes must remain valid until
completion, and write failures must identify the actual affected warp/lane.

Exit processing also reconciles active barriers: a barrier cannot remain blocked
solely on threads that have exited. Release must never revive exited threads
or count a trapped thread as an exit. Explicit-count barriers must not count
unrelated exits as ordinary arrivals. These rules follow the
[PTX barrier and exit semantics](https://docs.nvidia.com/cuda/parallel-thread-execution/index.html#parallel-synchronization-and-communication-instructions-bar-barrier).

Memory transfers currently complete synchronously before PC advancement. Barrier
ordering uses that existing execution contract; this is not a host-parallel
memory model or an implementation of asynchronous memory completion.

## Verification and build contracts

- Exercise every declared Add/Sub/Mul/FMA/Setp form/type through generated code, and cover its
  controls, boundary encodings, signed zero, NaNs, subnormals, saturation and
  packed lanes with independently specified expected values.
- Parse/resolve/lower/execute real PTX samples to catch unreachable engine paths.
- Check generation determinism, renamed operands, independent result/source
  types, unsupported structure failures, and missing semantic-definition failure.
- Keep one normal engine test executable in the main build. A single CTest
  link-contract check drives an isolated build using actual generated preparation:
  complete semantics must link, while omitting representative Add semantics must
  fail with an unresolved Add semantic symbol. This checks the linking mechanism,
  not every opcode; generated dispatch and execution tests cover opcode completeness.
  New operations must not add another link-negative target to the main build.
- Build from an editable package and a wheel outside the source tree. Track
  generator scripts, shared helpers, backend resources and pinned package
  metadata in CMake. Changed dependencies rerun generation; identical output
  must preserve artifact timestamps through atomic publication.
- Retain existing engine, simulator and package-consumer regressions. Do not
  silently replace the established tests with generated snapshots.

Current implementation progress and outstanding acceptance gates belong in the
[executor plan](../milestone_plan/executor_module_execution_plan.md).
