# Execution IR

## M2-03 status

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

M2-03 exports `ptxsim::exec_ir` with frontend-independent typed operands:
`RegisterOperand`, `ImmediateOperand`, `SpecialRegisterOperand`,
`AddressOperand`, `BranchTarget`, `FunctionTarget`, and the narrowly scoped
`ValueOperand` variant for register/immediate/special values. Addresses use
only a register slot or symbol base, `AddressWidth::bits32` or `bits64`, and a
signed byte offset. Operand dumps are stable ASCII strings; for example,
`register:7:b32`, `immediate:b16:0x0012`, and
`address:b64:symbol:9:-8`; targets prefix their resolved ID. `ptxsim::exec_ir`
depends only on `ptxsim::common` and is part of the installed package.
`SpecialRegisterId` is added in M2-03 specifically as the stable simulator
identity required by `SpecialRegisterOperand`.

## Boundary

`exec_ir` owns typed execution facts after lowering. It will not retain
`ptx_frontend` objects, PTX modifier spellings, arithmetic backend types, or
SASS/timing details.

## Non-goals in M2-03

M2-03 does not define instruction records, predication, register files, memory
objects or state spaces, byte serialization, arithmetic, lowering, or a
verifier.
