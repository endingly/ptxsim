#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cmath>

#include <ptxsim/arith/arith.hpp>

namespace ptxsim::arith::test {

TEST(SpecialFunctions, ControlledDeterministicModel) {
  context c;
  const special_function_control approx{
      .approximation = approximation_mode::ptx_approximate};
  EXPECT_EQ(div(c, float32_t::from_bits(0x40400000),
                float32_t::from_bits(0x40000000), approx)
                ->value.bits(),
            0x3fc00000u);
  EXPECT_EQ(sqrt(c, float32_t::from_bits(0x40800000), approx)->value.bits(),
            0x40000000u);
  EXPECT_EQ(rcp(c, float32_t::from_bits(0x40000000), approx)->value.bits(),
            0x3f000000u);
  EXPECT_EQ(rsqrt(c, float32_t::from_bits(0x40800000), approx)->value.bits(),
            0x3f000000u);
  EXPECT_EQ(sin(c, float32_t::from_bits(0x80000000), approx)->value.bits(),
            0x80000000u);
  EXPECT_EQ(cos(c, float32_t{}, approx)->value.bits(), 0x3f800000u);
  EXPECT_EQ(lg2(c, float32_t{}, approx)->value.bits(), 0xff800000u);
  EXPECT_TRUE(lg2(c, float32_t::from_bits(0xbf800000), approx)->status.invalid);
  EXPECT_EQ(ex2(c, float32_t::from_bits(0xff800000), approx)->value.bits(), 0u);
  EXPECT_EQ(tanh(c, float32_t::from_bits(0xff800000), approx)->value.bits(),
            0xbf800000u);

  const auto as_float = [](float32_t value) {
    return std::bit_cast<float>(value.bits());
  };
  EXPECT_LE(
      std::abs(
          as_float(sin(c, float32_t::from_bits(0x3f000000), approx)->value) -
          0.47942553860420300538F),
      6.75e-7F);
  EXPECT_LE(
      std::abs(
          as_float(cos(c, float32_t::from_bits(0x3f000000), approx)->value) -
          0.87758256189037271612F),
      6.75e-7F);
  EXPECT_LE(
      std::abs(
          as_float(lg2(c, float32_t::from_bits(0x3fc00000), approx)->value) -
          0.58496250072115618145F),
      2.3841858e-7F);
  EXPECT_LE(
      std::abs(
          as_float(ex2(c, float32_t::from_bits(0x3f000000), approx)->value) -
          1.41421356237309504880F),
      2.3841858e-7F);
  const auto tanh_value =
      as_float(tanh(c, float32_t::from_bits(0x3f000000), approx)->value);
  EXPECT_LE(std::abs((tanh_value - 0.46211715726000975850F) /
                     0.46211715726000975850F),
            4.8828125e-4F);

  EXPECT_EQ(sin(c, float32_t::from_bits(1),
                {.approximation = approximation_mode::ptx_approximate,
                 .subnormal = subnormal_mode::flush_input})
                .error(),
            arithmetic_error::unsupported_subnormal_mode);
  EXPECT_EQ(sin(c, float32_t::from_bits(0x3f000000), {}).error(),
            arithmetic_error::unsupported_approximation_mode);
  EXPECT_EQ(rcp(c, float32_t::from_bits(0x3f800000),
                {.approximation = approximation_mode::ptx_full})
                .error(),
            arithmetic_error::unsupported_approximation_mode);
  model_profile other{};
  other.approximation.model = approximation_model::unavailable;
  EXPECT_EQ(
      sin(context{other}, float32_t::from_bits(0x3f000000), approx).error(),
      arithmetic_error::unsupported_approximation_mode);
  const auto first = sin(c, float32_t::from_bits(0x3f000000), approx);
  const auto second = sin(c, float32_t::from_bits(0x3f000000), approx);
  ASSERT_TRUE(first && second);
  EXPECT_EQ(first->value, second->value);
  EXPECT_TRUE(first->status.model_dependent);
}

TEST(SpecialFunctions, ApproximationCapabilityMatrix) {
  static_assert(special_function_operation_capability<
                scalar_operation::div, float32_t>::supports(
                approximation_mode::ptx_approximate));
  static_assert(special_function_operation_capability<
                scalar_operation::div, float32_t>::supports(
                approximation_mode::ptx_full));
  context c;
  const auto one = float32_t::from_bits(0x3f800000);
  EXPECT_TRUE(div(c, one, one,
                  {.approximation = approximation_mode::ptx_approximate}));
  EXPECT_TRUE(div(c, one, one,
                  {.approximation = approximation_mode::ptx_full}));
}

TEST(SpecialFunctions, DivApproxLargeDivisorPtxDomain) {
  context c;
  const special_function_control preserve{
      .approximation = approximation_mode::ptx_approximate};
  const special_function_control fio{
      .approximation = approximation_mode::ptx_approximate,
      .subnormal = subnormal_mode::flush_input_and_output};
  const auto f32 = [](std::uint32_t bits) { return float32_t::from_bits(bits); };

  for (const auto control : {preserve, fio}) {
    for (const auto [lhs, rhs, expected] :
         std::array{std::array{0x3f800000u, 0x7f000000u, 0x00000000u},
                    std::array{0x3f800000u, 0xff000000u, 0x80000000u},
                    std::array{0xbf800000u, 0x7f000000u, 0x80000000u},
                    std::array{0xbf800000u, 0xff000000u, 0x00000000u}}) {
      const auto result = div(c, f32(lhs), f32(rhs), control);
      ASSERT_TRUE(result);
      EXPECT_EQ(result->value.bits(), expected);
    }
  }

  EXPECT_EQ(div(c, f32(0x3f800000), f32(0x7e800000), preserve)
                ->value.bits(),
            0x00800000u);
  EXPECT_EQ(div(c, f32(0x3f800000), f32(0x7e800001), preserve)
                ->value.bits(),
            0x00000000u);
  EXPECT_EQ(div(c, f32(0x3f800000), f32(0x7f7fffff), preserve)
                ->value.bits(),
            0x00000000u);
  EXPECT_EQ(div(c, f32(0x3f800000), f32(0x7f800000), preserve)
                ->value.bits(),
            0x00000000u);

  for (const auto lhs : {0x7f800000u, 0xff800000u})
    for (const auto rhs : {0x7f000000u, 0xff000000u}) {
      const auto result = div(c, f32(lhs), f32(rhs), preserve);
      ASSERT_TRUE(result);
      EXPECT_EQ(result->value.bits(), 0x7fc00000u);
      EXPECT_TRUE(result->status.invalid);
    }

  EXPECT_TRUE(is_nan(div(c, f32(0x7fc00001), f32(0x7f000000), preserve)
                         ->value));
  EXPECT_TRUE(is_nan(div(c, f32(0x3f800000), f32(0x7fc00001), preserve)
                         ->value));
}

TEST(SpecialFunctions, LowPrecisionApproximationFtzCapabilities) {
  context c;
  const special_function_control approx{
      .approximation = approximation_mode::ptx_approximate};
  EXPECT_EQ(tanh(c, float16_t{}, approx)->value.bits(), 0u);
  EXPECT_EQ(ex2(c, float16_t::from_bits(0x3c00), approx)->value.bits(),
            0x4000u);
  EXPECT_EQ(tanh(c, float16_t::from_bits(0x3800), approx)->value.bits(),
            0x3765u);
  EXPECT_EQ(ex2(c, float16_t::from_bits(0x3800), approx)->value.bits(),
            0x3da8u);
  const special_function_control bf16_ftz{
      .approximation = approximation_mode::ptx_approximate,
      .subnormal = subnormal_mode::flush_input_and_output};
  EXPECT_EQ(tanh(c, bfloat16_t{}, bf16_ftz)->value.bits(), 0u);
  EXPECT_EQ(ex2(c, bfloat16_t::from_bits(0x3f80), bf16_ftz)->value.bits(),
            0x4000u);
  EXPECT_EQ(tanh(c, bfloat16_t::from_bits(0x3f00), bf16_ftz)->value.bits(),
            0x3eedu);
  EXPECT_EQ(ex2(c, bfloat16_t::from_bits(0x3f00), bf16_ftz)->value.bits(),
            0x3fb5u);
  EXPECT_EQ(ex2(c, bfloat16_t::from_bits(1), bf16_ftz)->value.bits(), 0x3f80u);
  EXPECT_EQ(ex2(c, bfloat16_t::from_bits(0x3f80), approx).error(),
            arithmetic_error::unsupported_subnormal_mode);
}

TEST(SpecialFunctions, ApproximateCornerCasesAndFtz) {
  context c;
  const special_function_control approx{
      .approximation = approximation_mode::ptx_approximate};
  struct unary_case {
    std::uint32_t input;
    std::uint32_t expected;
    bool nan = false;
  };
  const auto expect_unary = [&]<std::size_t N>(
                                const auto& function,
                                const std::array<unary_case, N>& cases) {
    for (const auto [input, expected, expect_nan] : cases) {
      const auto result = function(float32_t::from_bits(input));
      ASSERT_TRUE(result);
      if (expect_nan)
        EXPECT_TRUE(is_nan(result->value));
      else
        EXPECT_EQ(result->value.bits(), expected);
      EXPECT_TRUE(result->status.model_dependent);
    }
  };
  expect_unary([&](float32_t x) { return rcp(c, x, approx); },
               std::array{unary_case{0x00000000, 0x7f800000},
                          unary_case{0x80000000, 0xff800000},
                          unary_case{0x7f800000, 0x00000000},
                          unary_case{0xff800000, 0x80000000},
                          unary_case{0x7fc00001, 0, true}});
  expect_unary([&](float32_t x) { return sqrt(c, x, approx); },
               std::array{unary_case{0x00000000, 0x00000000},
                          unary_case{0x80000000, 0x80000000},
                          unary_case{0x7f800000, 0x7f800000},
                          unary_case{0xff800000, 0, true},
                          unary_case{0x7fc00001, 0, true}});
  expect_unary([&](float32_t x) { return rsqrt(c, x, approx); },
               std::array{unary_case{0x00000000, 0x7f800000},
                          unary_case{0x80000000, 0xff800000},
                          unary_case{0x7f800000, 0x00000000},
                          unary_case{0xff800000, 0, true},
                          unary_case{0x7fc00001, 0, true}});
  expect_unary([&](float32_t x) { return sin(c, x, approx); },
               std::array{unary_case{0x00000000, 0x00000000},
                          unary_case{0x80000000, 0x80000000},
                          unary_case{0x7f800000, 0, true},
                          unary_case{0xff800000, 0, true},
                          unary_case{0x7fc00001, 0, true}});
  expect_unary([&](float32_t x) { return cos(c, x, approx); },
               std::array{unary_case{0x00000000, 0x3f800000},
                          unary_case{0x80000000, 0x3f800000},
                          unary_case{0x7f800000, 0, true},
                          unary_case{0xff800000, 0, true},
                          unary_case{0x7fc00001, 0, true}});
  expect_unary([&](float32_t x) { return lg2(c, x, approx); },
               std::array{unary_case{0x00000000, 0xff800000},
                          unary_case{0x80000000, 0xff800000},
                          unary_case{0x7f800000, 0x7f800000},
                          unary_case{0xff800000, 0, true},
                          unary_case{0x7fc00001, 0, true}});
  expect_unary([&](float32_t x) { return ex2(c, x, approx); },
               std::array{unary_case{0x00000000, 0x3f800000},
                          unary_case{0x80000000, 0x3f800000},
                          unary_case{0x7f800000, 0x7f800000},
                          unary_case{0xff800000, 0x00000000},
                          unary_case{0x7fc00001, 0, true}});
  expect_unary([&](float32_t x) { return tanh(c, x, approx); },
               std::array{unary_case{0x00000000, 0x00000000},
                          unary_case{0x80000000, 0x80000000},
                          unary_case{0x7f800000, 0x3f800000},
                          unary_case{0xff800000, 0xbf800000},
                          unary_case{0x7fc00001, 0, true}});

  const auto negative = float32_t::from_bits(0xbf800000);
  EXPECT_TRUE(sqrt(c, negative, approx)->status.invalid);
  EXPECT_TRUE(rsqrt(c, negative, approx)->status.invalid);
  EXPECT_TRUE(lg2(c, negative, approx)->status.invalid);
  EXPECT_TRUE(rcp(c, float32_t{}, approx)->status.divide_by_zero);
  EXPECT_TRUE(rsqrt(c, float32_t{}, approx)->status.divide_by_zero);

  struct binary_case {
    std::uint32_t lhs, rhs, expected;
    bool nan = false;
  };
  for (const auto [lhs, rhs, expected, expect_nan] :
       std::array{binary_case{0x3f800000, 0x00000000, 0x7f800000},
                  binary_case{0xbf800000, 0x00000000, 0xff800000},
                  binary_case{0x7f800000, 0x7f800000, 0, true},
                  binary_case{0x7fc00001, 0x3f800000, 0, true},
                  binary_case{0x00000001, 0x3f800000, 0x00000001}}) {
    const auto result =
        div(c, float32_t::from_bits(lhs), float32_t::from_bits(rhs), approx);
    ASSERT_TRUE(result);
    if (expect_nan)
      EXPECT_TRUE(is_nan(result->value));
    else
      EXPECT_EQ(result->value.bits(), expected);
  }
  EXPECT_TRUE(div(c, float32_t::from_bits(0x7f800000),
                  float32_t::from_bits(0x7f800000), approx)
                  ->status.invalid);

  const special_function_control ftz{
      .approximation = approximation_mode::ptx_approximate,
      .subnormal = subnormal_mode::flush_input_and_output};
  const auto subnormal = float32_t::from_bits(1);
  EXPECT_EQ(rcp(c, subnormal, ftz)->value.bits(), 0x7f800000u);
  EXPECT_EQ(sqrt(c, subnormal, ftz)->value.bits(), 0u);
  EXPECT_EQ(rsqrt(c, subnormal, ftz)->value.bits(), 0x7f800000u);
  EXPECT_EQ(sin(c, subnormal, ftz)->value.bits(), 0u);
  EXPECT_EQ(cos(c, subnormal, ftz)->value.bits(), 0x3f800000u);
  EXPECT_EQ(lg2(c, subnormal, ftz)->value.bits(), 0xff800000u);
  EXPECT_EQ(ex2(c, subnormal, ftz)->value.bits(), 0x3f800000u);
  EXPECT_EQ(tanh(c, subnormal, ftz)->value.bits(), 0u);
  EXPECT_EQ(
      div(c, subnormal, float32_t::from_bits(0x3f800000), ftz)->value.bits(),
      0u);
}

TEST(SpecialFunctions, FixedDomainSweepsRespectApproximationBounds) {
  context c;
  const special_function_control approx{
      .approximation = approximation_mode::ptx_approximate};
  const auto as_float = [](float32_t value) {
    return std::bit_cast<float>(value.bits());
  };
  const auto ordered_bits = [](std::uint32_t bits) {
    return (bits & 0x80000000u) != 0 ? ~bits + 1u : bits | 0x80000000u;
  };
  const auto ulp_distance = [&](float32_t lhs, float32_t rhs) {
    const auto ordered_lhs = ordered_bits(lhs.bits());
    const auto ordered_rhs = ordered_bits(rhs.bits());
    return ordered_lhs < ordered_rhs ? ordered_rhs - ordered_lhs
                                     : ordered_lhs - ordered_rhs;
  };
  const auto expect_ulp_error = [&]<std::size_t N>(
                                    const auto& function, const auto& oracle,
                                    const std::array<std::uint32_t, N>& inputs,
                                    std::uint32_t bound) {
    for (const auto bits : inputs) {
      const auto input = float32_t::from_bits(bits);
      const auto result = function(input);
      ASSERT_TRUE(result);
      const auto reference = oracle(static_cast<long double>(as_float(input)));
      const auto expected = float32_t::from_bits(
          std::bit_cast<std::uint32_t>(static_cast<float>(reference)));
      if (std::isfinite(reference))
        EXPECT_LE(ulp_distance(result->value, expected), bound)
            << "input bits: 0x" << std::hex << bits;
      else
        EXPECT_EQ(result->value.bits(), expected.bits())
            << "input bits: 0x" << std::hex << bits;
    }
  };
  const auto expect_relative_error =
      [&]<std::size_t N>(const auto& function, const auto& oracle,
                         const std::array<std::uint32_t, N>& inputs,
                         long double bound) {
        for (const auto bits : inputs) {
          const auto input = float32_t::from_bits(bits);
          const auto result = function(input);
          ASSERT_TRUE(result);
          const auto reference =
              oracle(static_cast<long double>(as_float(input)));
          if (reference == 0.0L || !std::isfinite(reference)) {
            EXPECT_EQ(result->value.bits(), std::bit_cast<std::uint32_t>(
                                                static_cast<float>(reference)))
                << "input bits: 0x" << std::hex << bits;
          } else {
            EXPECT_LE(
                std::abs((static_cast<long double>(as_float(result->value)) -
                          reference) /
                         reference),
                bound)
                << "input bits: 0x" << std::hex << bits;
          }
        }
      };
  constexpr std::array trig_inputs{0xc0c90fdbu, 0xc0000000u, 0xbf000000u,
                                   0x3f000000u, 0x40000000u, 0x40c90fdbu};
  for (const auto bits : trig_inputs) {
    const auto input = float32_t::from_bits(bits);
    const auto sine = sin(c, input, approx), cosine = cos(c, input, approx);
    ASSERT_TRUE(sine && cosine);
    const auto x = static_cast<long double>(as_float(input));
    EXPECT_LE(
        std::abs(static_cast<long double>(as_float(sine->value)) - std::sin(x)),
        0x1.6a09e667f3bccp-21L)
        << "sin input bits: 0x" << std::hex << bits;
    EXPECT_LE(std::abs(static_cast<long double>(as_float(cosine->value)) -
                       std::cos(x)),
              0x1.6a09e667f3bccp-21L)
        << "cos input bits: 0x" << std::hex << bits;
  }
  constexpr std::array log_inner_inputs{0x3f400000u, 0x3f800000u, 0x3fc00000u};
  for (const auto bits : log_inner_inputs) {
    const auto input = float32_t::from_bits(bits);
    const auto result = lg2(c, input, approx);
    ASSERT_TRUE(result);
    EXPECT_LE(std::abs(static_cast<long double>(as_float(result->value)) -
                       std::log2(static_cast<long double>(as_float(input)))),
              0x1p-22L)
        << "lg2 input bits: 0x" << std::hex << bits;
  }
  constexpr std::array log_outer_inputs{0x3f000000u, 0x40000000u, 0x40400000u};
  expect_relative_error([&](float32_t x) { return lg2(c, x, approx); },
                        [](long double x) { return std::log2(x); },
                        log_outer_inputs, 0x1p-22L);
  constexpr std::array exp_inputs{0xc1200000u, 0xbf000000u, 0x00000000u,
                                  0x3f000000u, 0x40a00000u, 0x41200000u};
  expect_ulp_error([&](float32_t x) { return ex2(c, x, approx); },
                   [](long double x) { return std::exp2(x); }, exp_inputs, 2);
  constexpr std::array exact_inputs{0x3f000000u, 0x3fa00000u, 0x40000000u,
                                    0x40400000u, 0x40a00000u};
  expect_ulp_error([&](float32_t x) { return rcp(c, x, approx); },
                   [](long double x) { return 1.0L / x; }, exact_inputs, 1);
  expect_relative_error([&](float32_t x) { return sqrt(c, x, approx); },
                        [](long double x) { return std::sqrt(x); },
                        exact_inputs, 0x1p-23L);
  expect_relative_error([&](float32_t x) { return rsqrt(c, x, approx); },
                        [](long double x) { return 1.0L / std::sqrt(x); },
                        exact_inputs, std::exp2(-22.9L));
  for (const auto bits : exact_inputs) {
    const auto result = div(c, float32_t::from_bits(0x41200000),
                            float32_t::from_bits(bits), approx);
    ASSERT_TRUE(result);
    const auto expected = float32_t::from_bits(std::bit_cast<std::uint32_t>(
        static_cast<float>(10.0L / as_float(float32_t::from_bits(bits)))));
    EXPECT_LE(ulp_distance(result->value, expected), 2u)
        << "divisor bits: 0x" << std::hex << bits;
  }
  for (const auto bits : exact_inputs) {
    const auto input = float32_t::from_bits(bits);
    const auto first = tanh(c, input, approx), second = tanh(c, input, approx);
    ASSERT_TRUE(first && second);
    EXPECT_EQ(first->value.bits(), second->value.bits());
    const auto reference = std::tanh(static_cast<long double>(as_float(input)));
    EXPECT_LE(std::abs((static_cast<long double>(as_float(first->value)) -
                        reference) /
                       reference),
              0x1p-11L);
  }
}

TEST(SpecialFunctions, UnsupportedApproximationAndProfile) {
  context c;
  const special_function_control approx{
      .approximation = approximation_mode::ptx_approximate};
  const auto one = float32_t::from_bits(0x3f800000);
  EXPECT_EQ(rsqrt(c, one).error(),
            arithmetic_error::unsupported_approximation_mode);
  EXPECT_EQ(sin(c, one).error(),
            arithmetic_error::unsupported_approximation_mode);
  EXPECT_EQ(
      rcp(c, one, {.approximation = approximation_mode::ptx_full}).error(),
      arithmetic_error::unsupported_approximation_mode);
  model_profile unsupported{};
  unsupported.approximation.model = approximation_model::unavailable;
  const context other{unsupported};
  EXPECT_EQ(div(other, one, one, approx).error(),
            arithmetic_error::unsupported_approximation_mode);
  EXPECT_EQ(sqrt(other, one, approx).error(),
            arithmetic_error::unsupported_approximation_mode);
  EXPECT_EQ(rsqrt(other, one, approx).error(),
            arithmetic_error::unsupported_approximation_mode);
  EXPECT_EQ(sin(other, one, approx).error(),
            arithmetic_error::unsupported_approximation_mode);
  EXPECT_EQ(cos(other, one, approx).error(),
            arithmetic_error::unsupported_approximation_mode);
  EXPECT_EQ(lg2(other, one, approx).error(),
            arithmetic_error::unsupported_approximation_mode);
  EXPECT_EQ(ex2(other, one, approx).error(),
            arithmetic_error::unsupported_approximation_mode);
  EXPECT_EQ(tanh(other, one, approx).error(),
            arithmetic_error::unsupported_approximation_mode);
}

}  // namespace ptxsim::arith::test
