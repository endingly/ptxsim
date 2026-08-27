#include <ptxsim/fp/environment.hpp>

#include <gtest/gtest.h>

namespace ptxsim::fp::test {
namespace {

template <typename T>
concept HasFma = requires(const Environment& environment, T value) {
  environment.fma(value, value, value);
};

template <typename To, typename From>
concept CanConvert = requires(const Environment& environment, From value) {
  environment.template convert<To>(value);
};

static_assert(HasFma<Fp32>);
static_assert(HasFma<Fp64>);
static_assert(HasFma<Bf16>);
static_assert(CanConvert<Bf16, Fp32>);
static_assert(CanConvert<Fp32, Fp8E4M3>);
static_assert(!CanConvert<Fp8E4M3, Fp8E5M2>);
static_assert(ConversionTraits<Bf16, Fp32>::supported);
static_assert(ConversionTraits<Bf16, Fp32>::accepts_rounding);
static_assert(ConversionTraits<Fp8E4M3, Fp32>::inherent_saturation);
static_assert(!ConversionTraits<Fp32, Bf16>::accepts_rounding);
static_assert(!ConversionTraits<Fp8E4M3, Fp8E5M2>::supported);

constexpr ArithmeticControl positional_legacy{RoundingMode::TowardZero, true};
constexpr ArithmeticControl designated_legacy{.flush_subnormal = true};
constexpr ArithmeticControl explicit_subnormal{
    .subnormal = SubnormalMode::FlushToSignedZero};
static_assert(positional_legacy.flush_subnormal);
static_assert(positional_legacy.subnormal == SubnormalMode::Preserve);
static_assert(designated_legacy.flush_subnormal);
static_assert(explicit_subnormal.subnormal ==
              SubnormalMode::FlushToSignedZero);

}  // namespace

TEST(Capabilities, CompileTimeMatrix) {
  SUCCEED();
}

}  // namespace ptxsim::fp::test
