# Execution IR

## M2-02 status

M2-01 provides frontend-independent strong IDs from `ptxsim::common`:
`ProgramCounter`, `FunctionId`, `RegisterSlot`, `SymbolId`, `LabelId`,
`SourceLocationId`, `ThreadId`, `CtaId`, `WarpId`, and `LaneId`. They have
explicit `uint32_t` construction, ordering, and canonical strings such as
`pc:7`. This ASCII `<kind>:<decimal>` form is the stable ID dump contract and
does not use locale formatting.

M2-02 adds `RawValue` to `ptxsim::common`. It exactly stores pred, b8, b16,
b32, b64, or b128; factories accept only the matching fixed-width type and
width-mismatched extraction returns `RawValueError`. Its stable dump is lower
case fixed-width hex; `Bits128{low, high}` dumps high then low. This describes
logical words only, not host endianness or memory byte order.

`ptxsim::exec_ir` remains build-tree-only and has no public instruction or
operand types. `ptxsim::common` is exported because IDs and raw values are
public core APIs.

## Boundary

`exec_ir` will own typed execution facts after lowering. It will not retain
`ptx_frontend` objects, PTX modifier spellings, arithmetic backend types, or
SASS/timing details.

## Non-goals in M2-02

M2-02 does not define instruction records, operands, register files, byte
serialization, arithmetic, or a verifier.
