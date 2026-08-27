# Floating-point execution

`ptxsim::fp` is the canonical scalar floating-point primitive layer.  Its
public API uses only `Fp16`, `Fp32`, and `Fp64` raw IEEE bit values; it does
not expose host `float`/`double` or Berkeley SoftFloat headers.  Berkeley
SoftFloat is a private arithmetic backend, not the PTX semantic specification.

## Rounding and state isolation

PTX rounding maps to SoftFloat as follows: `.rn` → nearest-even, `.rz` →
toward-zero, `.rm` → toward-negative, and `.rp` → toward-positive.  Every
`Environment` operation saves the calling thread's SoftFloat rounding,
tininess, and exception state; selects `tininess_afterRounding`; clears flags;
collects its diagnostic flags; then restores the saved state.  `Environment`
is stateless and does not use `fesetround()` or a global lock.

`Result<T>` always carries both the raw result and `ExceptionFlags`.  Invalid,
divide-by-zero, overflow, underflow, and inexact are IEEE diagnostic flags,
not operation failures.  SoftFloat's `infinite` flag is presented as the IEEE
name `DivideByZero`.

## FTZ and supported operations

For F32 operations with `flush_subnormal`, subnormal inputs are converted to a
signed zero before arithmetic and subnormal outputs to a signed zero after it.
This is PTX-specific processing outside SoftFloat.  F64 `.ftz` is unsupported:
passing `flush_subnormal=true` to an F64 `Environment` method throws
`std::invalid_argument`, rather than silently changing semantics.

Exact F32 and F64 add, sub, mul, fused multiply-add, div, and sqrt are
supported, as are i32/u32 ↔ F32/F64 and F32 ↔ F64 selected conversions.  F16
currently provides only raw storage and classification.  BF16, TF32, FP8,
approximate instructions, packed forms, saturation, tensor arithmetic, and
their PTX-specific rules are unsupported.

## Validation

Exact instructions use raw-bit comparison.  Validation also provides
float-class, ULP, relative, and absolute policies.  Relative and absolute
comparators deliberately use host `float`/`double` in `validation.cpp`; they
are validation-only helpers and never participate in canonical execution.
NaN policies may compare class without requiring a payload match; signed-zero
requirements use bit-exact comparison.

## Current integration boundary

This repository presently has no `exec_ir`, `semantics`, executor, or GPU
harness.  Consequently no PTX source-to-FP E2E path, FP instruction lowering,
semantics integration, or NVIDIA differential result is claimed here.  Those
layers must call this module only through its public raw-bit API.
