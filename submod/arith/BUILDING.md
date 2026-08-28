# `ptxsim::arith` build and packaging contract

The current v0.1 supported build matrix is Linux with GCC (primary) and
Clang (conformance). The exact integer backend intentionally uses `__int128`;
CMake rejects compilers without that extension instead of changing overflow
semantics. macOS and MSVC are not release-gated yet.

Only headers directly under `include/` are supported includes. Headers under
`include/detail/` are implementation details and are not a supported API.
Some are installed only because templates in public headers require them to
compile; consumers must not include or depend on them directly.

`ptxsim::arith_validation` is an opt-in diagnostic target. It contains host
floating-point tolerance helpers and is intentionally separate from the
production `ptxsim::arith` target.

The project root owns install and package configuration. It exports only the
production `ptxsim::arith` and diagnostic `ptxsim::arith_validation` libraries,
installs the arithmetic headers below `include/ptxsim/arith`, and uses the
project package helper for build-tree and installed exports. `arith` owns the
build-tree and installed-package consumer CTest gates, in addition to its unit
and internal tests; none of those test targets are exported.

## Conversion ownership

The public `cvt<To>` API has one conversion path: validate controls, decode the
source to an exact canonical binary value, and encode the destination once.
Do not add a `(To, From)` dispatch layer such as `backend::convert` or
`convert_impl`; that would restore the pairwise/F32-hub architecture and can
introduce double rounding.

`src/detail/low_precision_backend.*` still owns low-precision helpers used by
BF16 arithmetic and approximation kernels. Functions such as
`narrow_from_f32` and `widen_to_f32` are therefore not deprecated merely
because public conversion moved to the canonical core. They are internal
numeric primitives, not an alternative public conversion route.
