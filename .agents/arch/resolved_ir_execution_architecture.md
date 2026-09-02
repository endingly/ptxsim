# Audit: Direct `resolved_ir` Execution Proposal

> **Status:** proposal audited; not adopted
> **Scope:** program representation, instruction dispatch, and simulator
> initialization
> **Current authority:** existing `exec_ir` and executor plans remain unchanged
> **Purpose:** preserve the audit evidence while the proposal is considered

## Context

`ptx_frontend::resolved_ir::ResolvedInstruction` is already a generated
`std::variant` whose top-level alternatives represent PTX opcodes. Each opcode
record owns another variant for its legal forms. This already provides the two
dispatch levels required by the execution engine:

```text
ResolvedInstruction
  -> Add / Mov / Bra / Exit / ...
       -> opcode-specific form, type, layout, and modifiers
```

Generating a second ptxsim `Op` enum, instruction payload hierarchy, and
frontend-derived schema would duplicate this structure without adding an
execution capability.

The resolved module owns its symbol table, functions, instruction vectors,
operand spellings, IDs, types, and source locations. A simulator may therefore
take ownership of a resolved module without retaining the parser, syntax AST,
or source buffer.

## Audited proposal

If adopted, the simulator would own one immutable
`ptx_frontend::resolved_ir::ResolvedModule` for its lifetime. The resolved
instruction objects are the canonical static instruction representation.

The simulator would perform one initialization pass that builds only the runtime
information absent from frontend IR:

- frontend function identity to `ptxsim::common::FunctionId`;
- frontend register identity and parameterized index to
  `ptxsim::common::RegisterSlot`;
- function-local label identity to `ptxsim::common::ProgramCounter`;
- register-frame layouts and other runtime resource bindings;
- an early diagnostic for legal frontend forms that ptxsim does not yet
  execute.

This data would be an execution index rather than another instruction IR and
would not copy complete opcode or operand records.

Each thread would remain the authority for its own function-local PC. Simulator
fetch would use the issued thread PC to index the owning resolved function body
and pass a `const ResolvedInstruction&` to `InstExecuteEngine`.

The engine could dispatch with visitors:

```text
std::visit over ResolvedInstruction     -> opcode handler
std::visit over opcode.variant          -> supported form handler
prepare all participating lanes         -> commit effects
```

This form would not require a ptxsim-generated first-level operation enum or
dispatch table. An unsupported visitor path would return an explicit
unsupported-instruction diagnostic.

## Checker boundary

Frontend resolution and checking establish PTX-facing facts such as symbol
binding, declaration validity, operand shape and type compatibility, modifier
legality, and target availability. They do not establish simulator runtime
invariants.

Under this proposal, the simulator initialization pass would still own
validation of:

- whether ptxsim implements the resolved opcode form;
- register-slot and function-ID allocation;
- branch target to PC conversion;
- memory and runtime resource binding;
- execution-model state required by barriers, calls, asynchronous operations,
  and other stateful instructions.

The execution engine would not reinterpret frontend legality, but checker
success alone would still not prove that the current ptxsim build can execute
the instruction.

## Required frontend completion

The current resolved function body retains instructions but not label
positions. A resolved branch target contains a bound symbol ID, while the
resolved module does not expose a reliable mapping from that symbol to a body
index.

Adopting direct branch execution would first require `ptx_frontend` to expose
one of:

- a function-local label-symbol to instruction-index table; or
- an already resolved instruction index in each direct branch target.

The first form would be preferable because it preserves symbol identity and can
also serve indirect branch metadata. Keeping the syntax AST or inferring label
positions from source ranges merely to reconstruct discarded information would
undermine the proposed simplification.

## Dependency and packaging consequences

Adopting this proposal would replace the current boundary in which execution IR
is frontend-independent. Simulator and instruction execution would acquire a
direct C++ dependency on `ptx_frontend::resolved_ir` and its public
dependencies.

It could then remove the separate `ptxsim-exec-ir-codegen` package, its frontend
wheel dependency, and ptxsim-owned execution-IR YAML. Building the frontend
itself could still require its Python generator dependencies; those would
remain the responsibility of the frontend package or vcpkg port rather than
ptxsim CMake.

`ptx_frontend` is pre-1.0 and its generated types may change. Ptxsim therefore
would need to pin a known frontend revision and confine direct type use to the
simulator and instruction-execution boundary. Conversely, independent ABI
stability, serialization, multiple frontends, or measured execution-cache
pressure are concrete reasons to retain a ptxsim-owned IR.

## Audit conclusion

Direct execution is feasible and can remove duplicated instruction structures,
but `ResolvedModule` is not yet a complete executable program representation.
At minimum it lacks label-to-PC and frontend-register-to-runtime-slot mappings,
and frontend legality checking does not replace simulator support validation.

If the proposal is later adopted, the smallest viable design is:

```text
immutable ResolvedModule + initialization-time execution index
  -> Simulator fetch by thread-owned PC
  -> direct top-level and opcode-form variant visitors
```

Until an explicit architecture decision adopts that trade-off:

- the existing ptxsim-owned `exec_ir` design remains authoritative;
- current `InstExecuteEngine` probe types and dispatch structure remain as
  implemented;
- no source, dependency, packaging, or milestone-plan change follows from this
  audit;
- this document does not supersede any execution plan.

## References

- [PTX Frontend README](https://github.com/endingly/ptx_frontend)
- [Resolved IR public types](https://github.com/endingly/ptx_frontend/blob/dev/submod/resolved_ir/include/ptx_resolved_ir.hpp)
- [Resolved module construction](https://github.com/endingly/ptx_frontend/blob/dev/submod/resolved_ir/src/ptx_resolved_module.cpp)
- [Resolved IR CMake target](https://github.com/endingly/ptx_frontend/blob/dev/submod/resolved_ir/CMakeLists.txt)
