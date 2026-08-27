# Floating-point execution

`ptxsim::fp` is the deterministic scalar floating-point semantic layer. Values
are always represented by integer bit patterns; host `float` and `double` are
used only by diagnostic relative/absolute validation. Berkeley SoftFloat is a
private binary32/binary64 backend, not public API or semantic specification.

## Architecture

The dependency direction is:

```text
Environment façade
  -> OperationTraits and centralized control validation
    -> SoftFloatBackend or Bf16Backend / conversion backend
      -> value types, FormatTraits, generic classification
```

`Environment` contains no arithmetic implementation. `SoftFloatContext`
installs rounding and tininess-after-rounding, clears flags, captures operation
flags, and restores the caller's state. The configured SoftFloat port declares
these state variables thread-local, so simultaneous threads using different
rounding modes are isolated without a process-wide lock. `Result<T>` carries
the value and flags; `Environment` retains no state.

This isolation is a boundary of the current `softfloat::softfloat` CMake target:
it relies on that target selecting the thread-local SoftFloat port. It is not a
general promise for an arbitrary externally substituted SoftFloat library; a
replacement target must provide the same TLS state contract.

`FormatTraits<T>` is the single source for masks, precision, bias, and special
value capabilities. Classification, predicates, encoding normalization, and
bit-exact/class/ordered/ULP validation are templates shared by every format.

## Public formats and representation

| Type | Semantic encoding | Specials |
|---|---|---|
| `Fp16` | IEEE binary16 | subnormal, infinity, qNaN, sNaN |
| `Bf16` | 1/8/7 | subnormal, infinity, qNaN, sNaN |
| `Fp32` | IEEE binary32 | subnormal, infinity, qNaN, sNaN |
| `Fp64` | IEEE binary64 | subnormal, infinity, qNaN, sNaN |
| `Tf32` | canonical F32 layout, low 13 fraction bits zero | binary32 range, 10 stored fraction bits |
| `Fp8E4M3` | PTX E4M3 | no infinity; only `0x7f`/`0xff` are NaN |
| `Fp8E5M2` | IEEE-like E5M2 | subnormal, infinity, qNaN, sNaN |
| `Fp4E2M1` | PTX E2M1 lane value | finite only; no infinity or NaN |

All are independent trivial, standard-layout strong types with default `+0`
and raw-bit equality. `Fp4E2M1` represents one scalar lane, not PTX packed x2
storage. Only its low nibble is canonical. TF32 is a canonical binary32-shaped
module representation; PTX says register-internal layout is
implementation-defined, so this makes no hardware register-layout claim.
`is_valid_encoding()` detects nonzero padding and `normalize_encoding()` clears
it. Execution normalizes incoming TF32/FP4 padding before interpretation.

## Operation capability matrix

| Format | add/sub/mul | fused FMA | div/sqrt | F32 conversion |
|---|---:|---:|---:|---:|
| F32 | yes | yes | yes | n/a |
| F64 | yes | yes | yes | existing F32↔F64 API |
| BF16 | yes | yes (exact single-round) | no | both directions |
| TF32 | no | no | no | both directions |
| E4M3 | no | no | no | both directions |
| E5M2 | no | no | no | both directions |
| E2M1 | no | no | no | both directions |
| F16 | no | no | no | not yet |

Unsupported scalar operations have no overload. BF16 add/sub use a direct
integer significand/guard-round-sticky core and mul uses an exact integer
significand product. BF16 FMA aligns the exact integer product and addend,
performs signed integer addition/subtraction, then calls the BF16 packer once;
it never uses `f32_mulAdd` followed by BF16 narrowing. Its fixed 7×64-bit
integer accumulator covers the 400-bit maximum LSB-exponent separation plus
the 16-bit product and a carry. BF16 FTZ remains unsupported and is rejected.

Low-precision conversion scales linearly through constrained
`Environment::convert<To>(From, ConversionControl)` pairs with F32 as the hub.
Unsupported pairs fail constraints at compile time. Existing named
integer/F32/F64 conversions remain source-compatible; BF16 and TF32 also have
named convenience wrappers.

## Rounding, FTZ, exceptions, and overflow

All four modes are supported: nearest-even, toward-zero, toward-negative, and
toward-positive. `ArithmeticControl::subnormal` explicitly selects preserve or
flush-to-signed-zero. The legacy `.flush_subnormal = true` aggregate member is
retained for source compatibility and centrally maps to flush-to-signed-zero.
F32 arithmetic flushes both inputs and output while preserving sign, exactly as
the previous module did. Other arithmetic formats reject FTZ.

Narrow conversions report `Inexact` whenever discarded information is nonzero,
`Underflow` for an inexact result that remains subnormal/zero after rounding,
and `Overflow|Inexact` when the rounded result exceeds the format. IEEE-like
formats produce infinity unless `satfinite` or directed rounding requires the
maximum finite value. E4M3 and E2M1 cannot produce pseudo-infinity and always
saturate overflow to maximum finite: this is their inherent saturation, not an
effect of `satfinite`. `ConversionTraits::inherent_saturation` records exactly
that format property. For formats that do encode infinity, `satfinite` requests
maximum-finite output on overflow (including an infinity input), independently
of the selected rounding mode. Exact widening rejects non-default
rounding or saturation controls because they have no widening semantic effect.

## NaN policy

Arithmetic selects the first NaN operand in operand order (for FMA: `a`, `b`,
then `c`) and preserves its sign and payload; a selected signaling NaN is
quieted. Any signaling NaN among the operands raises `Invalid`, even when an
earlier quiet NaN supplies the result. The one deliberate precedence exception
is FMA `infinity * zero + cNaN`: it returns the canonical quiet NaN and raises
`Invalid`, rather than propagating `cNaN`. FMA also returns canonical quiet NaN
with `Invalid` for an infinite product plus an opposite-signed infinite c.

Conversion preserves sign and the most significant payload bits, truncates low
payload bits, and quiets signaling NaNs. A signaling input reports `Invalid`;
quiet NaN conversion does not report `Inexact`. E4M3 canonicalizes to its sole
signed NaN encoding. E2M1 has no NaN, so NaN conversion returns signed maximum
finite and reports `Invalid`. Infinity converted to a finite-only format
saturates and reports `Overflow|Inexact`.

F32-to-TF32 follows this centralized conversion policy. It is a deterministic
module policy, not a claim about unspecified hardware NaN payload bits.

## Validation and integration boundary

Deterministic operations use raw-bit comparison. Generic validation also
provides class comparison, ordered bits, ULP distance, and within-ULP checks for
all formats. Relative and absolute comparison remain F32/F64-only diagnostics
implemented with host floating point and never participate in execution.

This repository currently has no complete PTX executor path. Parsing, opcode
selection, packed lane scheduling, registers, memory, and trace remain outside
this module and must call it only through the public façade.
