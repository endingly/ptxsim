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
`ptx_frontend@fdb5ef575087b530c2cd6db6cb3631cf430a8ce0` and projected into
`exec_ir::Add`. All its declared types and controls are obligations. This is a
functional execution model across the forms in that fixed specification, not
a newly selected SM target. Target-SM/family availability validation is not
introduced by this change.

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

Input and output types are derived independently; mixed-precision Add is not
treated as three operands of one type. The present Add operands require matching
container widths. Signed values use bit-preserving interpretation, floating
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

## Verification and build contracts

- Exercise every declared Add/Sub/Mul/Setp form/type through generated code, and cover its
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
