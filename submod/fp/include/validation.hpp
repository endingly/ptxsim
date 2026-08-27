#pragma once

#include <ptxsim/fp/types.hpp>

#include <cstdint>

namespace ptxsim::fp::validation {

[[nodiscard]] bool bit_exact(Fp32 expected, Fp32 actual) noexcept;
[[nodiscard]] bool bit_exact(Fp64 expected, Fp64 actual) noexcept;
[[nodiscard]] bool same_float_class(Fp32 expected, Fp32 actual) noexcept;
[[nodiscard]] bool same_float_class(Fp64 expected, Fp64 actual) noexcept;
[[nodiscard]] std::uint32_t ulp_distance(Fp32 lhs, Fp32 rhs) noexcept;
[[nodiscard]] std::uint64_t ulp_distance(Fp64 lhs, Fp64 rhs) noexcept;
[[nodiscard]] bool within_ulp(Fp32 expected, Fp32 actual,
                              std::uint32_t maximum) noexcept;
[[nodiscard]] bool within_ulp(Fp64 expected, Fp64 actual,
                              std::uint64_t maximum) noexcept;

// Validation-only host floating-point comparisons; never use for execution.
[[nodiscard]] bool within_relative(Fp32 expected, Fp32 actual,
                                   float maximum) noexcept;
[[nodiscard]] bool within_relative(Fp64 expected, Fp64 actual,
                                   double maximum) noexcept;
[[nodiscard]] bool within_absolute(Fp32 expected, Fp32 actual,
                                   float maximum) noexcept;
[[nodiscard]] bool within_absolute(Fp64 expected, Fp64 actual,
                                   double maximum) noexcept;

}  // namespace ptxsim::fp::validation
