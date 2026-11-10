# Inherited executable IR and complete frontend binding

> **Status:** Authority Sol approved design; implementation and actual-diff acceptance pending.
> **Baseline:** `origin/main` at `775f004af2c6861a053e47d686137fe08d8d29b0`.
> **Active plan:** [Inherited executable IR delivery](../milestone_plan/inherited_exec_ir_execution_plan.md).

## Authority and scope

This decision supersedes the instruction representation, generator ownership,
execution capability, and incomplete-leaf allowances in
[resolved IR execution architecture](resolved_ir_execution_architecture.md) and
[instruction execution families](instruction_execution_families.md). Their
identity-binding, static code location, predication, lane fault, resource,
storage, launch ABI, and prepare/commit contracts remain applicable unless this
decision explicitly replaces them. Historical review bodies remain evidence.

The target is the complete released frontend 0.8.0 normalized semantic model:
137 opcodes, 5,453 semantic forms, and 7,457 operand layouts. Breaking the old
C++ instruction and Python generator APIs is allowed. A retained compatibility
manifest, old form alias table, or selected old schema is not a completion path.
New semantic execution may remain unimplemented; full representation and full
frontend binding may not.

## Release provenance and model boundary

The frontend release is tag `v0.8.0`, commit
`14879089d2a0f8c5fcb962c621c43b6dc3de193c`. The source archive SHA512 is
`0cb40c2301d47633abca8f6ef2de570e1097fcfde6e9f904ad4bd2f84fda61753e7276d19bfc7709c07c6a2ba29ca02407e74fc937241bbd38b49d3ba9e0f824`.
The Python artifact is
[ptx_frontend-0.8.0-py3-none-any.whl](https://github.com/endingly/ptx_frontend/releases/download/v0.8.0/ptx_frontend-0.8.0-py3-none-any.whl),
SHA256 `9306243b6d66d172f050c898338fba186567b6bf86d1a428e778e4c1b16a5e68`.

The user's 2026-11-10 scope clarification explicitly retires
`fma.rn.oob.sat.f16` and `fma.rn.oob.sat.f16x2`, which this pinned frontend
rejects and does not represent as native forms with both controls. Retain both
spellings as source-resolution rejection tests and preserve the other 68
previous numeric execution cases and 16 native FMA forms. Do not add a parser
shim, native extension or legacy form to restore the retired combinations.
General arithmetic primitives remain outside this PTX combination retirement.

One immutable ptxsim projection consumes frontend public normalized
specification and resolved models. A version-local frontend C++ backend adapter
may import `GenerationContext`, `with_cpp_backend_field_names`, and `form_name`
helpers to obtain exact released C++ class/member/storage spellings. These are
0.8.0-limited adapter interfaces, not a promised stable public frontend API;
future frontend changes require adapter/provenance review.
The adapter does not replay raw PTX YAML or reproduce frontend legality checks.
The ptxsim YAML records typed leaf/domain/binder mappings. This single
projection drives IR declarations, structural validation/printing, frontend
binding, and execution dispatch. Execution capability never filters it.

The projection preserves every modifier and layout, fixed and dynamic controls,
optional/list operands, condition-code effects, completion kinds, WGMMA
protocol actions, matrix, tensor, TCGEN, texture, surface, fabric, video, stack,
and resource contracts needed for execution. Frontend diagnostic/source-only
state does not become runtime state. No opaque frontend payload or monostate
stand-in can satisfy a missing typed leaf.

## Owned runtime representation

The generated `Instruction` base has read-only dense `Kind` and `Opcode`, an
owned execution predicate, a virtual destructor, and typed queries. Every
semantic form is one generated final direct subclass of `Instruction`. Opcode
and Kind are ptxsim-owned identities fixed by closed generated construction,
which keeps both identities and the dynamic type consistent; callers cannot
forge an arbitrary base identity or mutate it afterward. Base construction and
copy/move are private to generated final friends; a user-derived class cannot
forge a tag by copying the base portion. Generated thunks
provide structural validation and printing. The base contains no executor,
runtime state, or frontend legality checker; its validation interface is
structural only.

Generate narrow per-opcode headers and bounded form/source shards where needed.
The broad aggregate is optional for consumers. Public headers must not carry a
full-model template dispatch or checking body merely to expose one form.

Operand leaves contain ptxsim-owned values and bound identities: register
slots, function-local label PCs, function IDs, parameter/storage/resource IDs,
and relocations. Register component views preserve declaration widths and
lane selection. Resource bindings preserve scope/kind/entry role, static opaque
objects, managed/unified UUIDs and arrays. Relocations retain address kind,
member, addend bits and byte mask against a ResourceId or FunctionId. Call
metadata retains canonical functions, prototypes, ABI and call-table slots.
Immediate values retain use-width bits and signed/integer-source metadata.
All referenced execution metadata outlives the frontend.
Binding coverage includes every projected form, layout, control, optional,
list, and leaf. An absent binder is unfinished implementation, even for an
instruction with no semantic handler.

`ExecutableProgram::create(const ProgramDefinition&)` freezes a fresh owned
snapshot using internal generated Kind-to-typed-copy thunks. Each thunk creates
an actual const final object and deep-value-copies nested containers. Metadata
tables are copied as well. Private const ProgramStorage exposes only const
instruction/metadata children; program copies share it. Legal mutable aliases
to caller inputs remain valid and cannot mutate the program. This is an
internal freeze boundary, not a public virtual clone API. Structural validation
does not reject an unexecuted instruction for missing semantics.

Every canonical callable function, including declarations, prototypes and
aliases, receives an owned FunctionId. Function bodies are optional; an absent
body is not an empty definition. Fetch on a declaration yields a structured
nonexecutable-function error, while entry launch requires a real definition.
Parameter declaration and definition scopes remain distinct. Strong storage,
resource, parameter, prototype, target-set and call-table identities preserve
indirect alternatives, ordered sparse slots, canonical targets and complete ABI.

Owned declarations preserve unknown extents and opaque widths rather than
requiring a runtime allocation. Sparse initializer constants and typed
relocations, managed/unified UUIDs and opaque static objects remain data.
Allocation views derive only from supported declarations; an unsupported runtime
allocation is separate from binding completeness. All nested operand
alternatives have typed binders. Semantic field identity includes form and
layout, independently of C++ member access. Complete form-static contracts may
use ptxsim-owned constexpr descriptors; absence represents a real model absence.

## Capability and issue dispatch

`execution.yaml` lists implemented semantic forms, their typed group handler,
and static type/layout/control ranges. Omitted forms are unsupported. Generate
one dense capability table; do not add an independent supported boolean.
Unknown or overlapping rules fail generation. A declared handler without its
implementation fails compilation or linking. The full IR and binder remain
independent of this registry.

Issue performs one Kind decode and one indirect group dispatch. A typed group
performs the participating-lane loop after that dispatch. Preserve predicate
gating, lazy operand/resource access, fault ordering, prepare-before-commit,
branch/exit, barriers, entry ABI and storage behavior. Unsupported issue is
reported before reading ordinary operands, changing any state, or requiring
fallthrough; unsupported instructions at unexecuted
PCs do not reject the whole program. Profile disabling and a PreparedProgram
cache boundary may be reserved without requiring caching or JIT in this change.

## Python and build ownership

All Python packaging belongs to `python/pyproject.toml`, with code under
`python/src/ptxsim_codegen/{model,frontend,binding,code_gen,resources}` and tests
under `python/tests`. C++ submodules retain only C++ and CMake content.
Templates ship under `code_gen/templates` and load through package resources.
CMake, CI and documentation use the consolidated package and complete inputs.

The ptxsim package declares the exact frontend wheel and no direct Jinja2
dependency, as requested by the user. Frontend production dependencies provide
Jinja2, PyYAML, jsonschema and jsonschema-rs. Build-isolation requirements for
setuptools/wheel are validated independently. The frontend overlay installs
only matching-source production dependencies with testing disabled; upstream
test-only requirements are not part of that build.
