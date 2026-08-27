#pragma once

#include <ptxsim/fp/controls.hpp>

#include <cstdint>

namespace ptxsim::fp {

struct Fp16 {
  std::uint16_t bits{};
  friend constexpr bool operator==(Fp16, Fp16) = default;
};

struct Bf16 {
  std::uint16_t bits{};
  friend constexpr bool operator==(Bf16, Bf16) = default;
};

struct Fp32 {
  std::uint32_t bits{};
  friend constexpr bool operator==(Fp32, Fp32) = default;
};

struct Tf32 {
  // Canonical binary32 layout. The low 13 fraction bits are padding and must
  // be zero. This is a module representation, not a PTX register layout.
  std::uint32_t bits{};
  friend constexpr bool operator==(Tf32, Tf32) = default;
};

struct Fp64 {
  std::uint64_t bits{};
  friend constexpr bool operator==(Fp64, Fp64) = default;
};

struct Fp8E4M3 {
  std::uint8_t bits{};
  friend constexpr bool operator==(Fp8E4M3, Fp8E4M3) = default;
};

struct Fp8E5M2 {
  std::uint8_t bits{};
  friend constexpr bool operator==(Fp8E5M2, Fp8E5M2) = default;
};

struct Fp4E2M1 {
  // Scalar lane semantics only; packed PTX x2 storage is intentionally not a
  // scalar floating-point format.
  std::uint8_t bits{};
  friend constexpr bool operator==(Fp4E2M1, Fp4E2M1) = default;
};

enum class FpClass {
  Zero,
  Subnormal,
  Normal,
  Infinity,
  QuietNaN,
  SignalingNaN,
};

}  // namespace ptxsim::fp

#include <ptxsim/fp/detail/format_traits.hpp>
