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

| Format | exact scalar arithmetic | approximate/reference scalar operations | F32 conversion |
|---|---|---|---|
| F16 | add/sub/mul/FMA (`.rn`, FTZ), abs/neg/min/max/compare | `tanh_approx`, `ex2_approx` (preserve subnormals) | both directions |
| F32 | add/sub/mul/FMA/mad/div/sqrt/rcp, abs/neg/min/max/compare/copysign/testp | `div_approx`, `div_full`, `rcp_approx`, `sqrt_approx`, `rsqrt_approx`, `sin_approx`, `cos_approx`, `lg2_approx`, `ex2_approx`, `tanh_approx` | n/a |
| F64 | add/sub/mul/FMA/mad/div/sqrt/rcp, abs/neg/min/max/compare/copysign/testp | `rsqrt_approx`, `rcp_approx_ftz`, `rsqrt_approx_ftz` | existing F32↔F64 API |
| BF16 | add/sub/mul/FMA (exact single-round, `.rn`), abs/neg/min/max/compare | `tanh_approx`, `ex2_approx` (fixed FTZ) | both directions |
| TF32 | no scalar arithmetic | no scalar arithmetic | both directions |
| E4M3 | no scalar arithmetic | no scalar arithmetic | lane helper only |
| E5M2 | no scalar arithmetic | no scalar arithmetic | lane helper only |
| E2M1 | no scalar arithmetic | no scalar arithmetic | lane helper only |

Unsupported scalar operations have no overload. BF16 add/sub use a direct
integer significand/guard-round-sticky core and mul uses an exact integer
significand product. BF16 FMA aligns the exact integer product and addend,
performs signed integer addition/subtraction, then calls the BF16 packer once;
it never uses `f32_mulAdd` followed by BF16 narrowing. Its fixed 7×64-bit
integer accumulator covers the 400-bit maximum LSB-exponent separation plus
the 16-bit product and a carry. BF16 FTZ remains unsupported and is rejected.
F16 uses SoftFloat's native binary16 operations directly; it never widens
through F32 for scalar arithmetic. PTX scalar F16 and BF16 add/sub/mul/FMA
accept only nearest-even rounding, so other public arithmetic rounding modes
are rejected. F32-accumulator mixed `add`, `sub`, and `fma` overloads exact-
widen F16/BF16 inputs and perform the single F32 operation directly.

For modern PTX targets, `mad(Fp32/Fp64, ...)` is an alias for the fused
SoftFloat path, as specified for sm_20+; the legacy sm_1x non-fused behavior is
not modeled. `rcp(Fp32/Fp64, ...)` is exact `1/x` through the same SoftFloat
rounding and FTZ rules as division. It is separate from the explicitly named
approximation APIs.

`abs`, `neg`, `min`, `max`, and `compare` are available only for PTX scalar
arithmetic formats. `MinMaxControl::propagate_nan` maps to PTX `.NaN` and
returns canonical NaN for any NaN input; otherwise min/max ignore a lone NaN
and return the numeric operand, while two NaNs use the deterministic quiet-NaN
policy. `absolute` and `xor_sign` map to PTX
`.abs`/`.xorsign`; two-input F16/BF16/F32 requires them as a pair, while F32
three-input min/max accepts `.abs` but not `.xorsign`. Modifiers are applied
before NaN selection, so a lone NaN still returns the correctly modified
numeric operand; when the selected result is NaN, PTX ignores `.abs` and
`.xorsign`. `CompareOp` models ordered and unordered `setp` predicates;
executor-owned Boolean predicate composition is intentionally outside this
module. `copysign` and `testp` apply only to F32/F64. F64 min/max accepts no
modifiers. F32 three-input min/max folds operands in order.

Low-precision conversion scales linearly through constrained
`Environment::convert<To>(From, ConversionControl)` pairs with F32 as the hub.
Unsupported pairs fail constraints at compile time. Existing named
integer/F32/F64 conversions remain source-compatible; BF16 and TF32 also have
named convenience wrappers.

## Rounding, FTZ, exceptions, and overflow

F32/F64 arithmetic supports all four modes: nearest-even, toward-zero,
toward-negative, and toward-positive. F16/BF16 scalar arithmetic is PTX
nearest-even only. `ArithmeticControl::subnormal` explicitly selects preserve or
flush-to-signed-zero. The legacy `.flush_subnormal = true` aggregate member is
retained for source compatibility and centrally maps to flush-to-signed-zero.
F32 arithmetic flushes both inputs and output while preserving sign, exactly as
the previous module did. F16 arithmetic does the same. F64/BF16 arithmetic and
mixed low-precision-to-F32 operations reject FTZ.

`ApproximationControl` has only the PTX `.ftz` spelling. Only F32 approximate
operations that accept this control expose optional FTZ; F32 `tanh_approx` and
F64 `rsqrt_approx` have no FTZ control, and BF16 `ex2_approx` always flushes
input and output subnormals. `rcp_approx_ftz` and `rsqrt_approx_ftz` implement
the documented F64 FTZ algorithm: they flush input/output subnormals, clear the
low 32 input and result bits before computation/output respectively, and return
the PTX-specified `0x7fffffff00000000` NaN.

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

## Approximation APIs and result modifiers

APIs ending in `_approx` and `div_full` are intentionally named as PTX
error-contract/reference semantics, not as a GPU instruction bit model. Basic
approximate F32 operations use SoftFloat where that supplies a more accurate
reference. Transcendentals live in a separate approximation backend and use
host `<cmath>` only to produce reference values checked against representative
PTX error bounds. Host libm results are not claimed bit-exact with any GPU.
For these APIs `Result` flags are diagnostic only: special values and signaling
NaNs are diagnosed, while PTX does not expose an architectural exception-flag
result for approximate instructions.

`saturate(Result<T>)` and `relu(Result<T>)` are composable public result
modifiers rather than flags on `ArithmeticControl`. `saturate` is available for
`Fp16`/`Fp32`, clamps to `[0, 1]`, and maps NaN to `+0`; `relu` is available for
`Fp16`/`Bf16`, maps negative values to `+0`, and maps NaN to canonical NaN.
They preserve any diagnostic flags carried by the base operation.

## Validation and integration boundary

Deterministic operations use raw-bit comparison. Generic validation also
provides class comparison, ordered bits, ULP distance, and within-ULP checks for
all formats. Relative and absolute comparison remain F32/F64-only diagnostics
implemented with host floating point and never participate in execution.

This repository currently has no complete PTX executor path. Parsing, opcode
selection, packed lane scheduling, registers, memory, and trace remain outside
this module and must call it only through the public façade.

Packed lane adapters, the tensor-producer `oob` marker, and the remaining PTX
conversion matrix are deliberately not implemented yet. TF32/FP8/FP4 remain
conversion or packed/MMA operand formats rather than synthetic scalar
arithmetic types.
