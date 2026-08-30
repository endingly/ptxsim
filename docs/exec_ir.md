# Execution IR

## M2-01 status

M2-01 provides frontend-independent strong IDs from `ptxsim::common`:
`ProgramCounter`, `FunctionId`, `RegisterSlot`, `SymbolId`, `LabelId`,
`SourceLocationId`, `ThreadId`, `CtaId`, `WarpId`, and `LaneId`. They have
explicit `uint32_t` construction, ordering, and canonical strings such as
`pc:7`. This ASCII `<kind>:<decimal>` form is the stable ID dump contract and
does not use locale formatting.

`ptxsim::exec_ir` remains build-tree-only and has no public instruction,
operand, or value types. `ptxsim::common` is exported because IDs are now a
public core API.

## Boundary

`exec_ir` will own typed execution facts after lowering. It will not retain
`ptx_frontend` objects, PTX modifier spellings, arithmetic backend types, or
SASS/timing details.

## Non-goals in M2-01

M2-01 does not define raw values, instruction records, operands, or a verifier.
