# Execution IR

## M2-05 status

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

M2-04 adds validated data-only instruction records: `MovInst`, signed or
unsigned `IntegerBinaryInst` add/sub, `IntegerMulInst`, `BitInst`, direct
`BranchInst`, and scalar `LoadInst`/`StoreInst`. This initial slice is
deliberately narrow: mov supports pred/b16/b32/b64; integer add/sub use
b32/b64; mul uses b32 inputs, b32 unsigned low/high results, or b64 signed or
unsigned wide results; and/or/xor use b32; loads use b32 values and b64
addresses in global or constant space; stores use b32 values and b64 global
addresses. Constant stores are structurally rejected as read-only.

`Instruction` is a bounded variant. `validate` returns structured error codes
for invalid controls, width mismatch, unsupported width, invalid multiply result
relations, and read-only store. Legal PTX forms outside this slice remain future
lowering rejections, not silently widened M2-04 records.

M2-05 exports `ptxsim::program` as a real library. `ProgramImage::create`
accepts one owning `ProgramImageData` input, verifies it, and moves its typed
IR, copied function/symbol/source names, dense per-function register layouts,
entry IDs, and `PC -> optional SourceLocationId` side table into an immutable
image. Runtime access is vector/PC based only: no frontend references, string
lookup, or mutation API is retained. Empty images are valid only with no
function records or entry points.

`verify(const ProgramImage&)` repeats the structural checks after construction:
canonical vector IDs, contiguous function partitions, register widths and
slots, symbols, entries, source side metadata, each `exec_ir::validate` result,
and branch targets confined to their owning function. `dump` walks the stored
vector order with ASCII ID formatting and stable escaped strings. Symbol
addresses/storage are intentionally absent until M3.

## Boundary

`exec_ir` owns typed execution facts after lowering. It will not retain
`ptx_frontend` objects, PTX modifier spellings, arithmetic backend types, or
SASS/timing details.

## Non-goals in M2-05

M2-05 does not define execution semantics, register files, memory objects or
symbol addresses, byte serialization, arithmetic, lowering, or a scheduler.
