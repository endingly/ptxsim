#include <ptxsim/fp/types.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <type_traits>

namespace ptxsim::fp::test {
namespace {

template <typename T>
void check_value_type() {
  static_assert(std::is_trivially_copyable_v<T>);
  static_assert(std::is_standard_layout_v<T>);
  static_assert(T{} == T{0});
}

FpClass expected_e4m3(std::uint8_t bits) {
  const auto exponent = bits & 0x78u;
  const auto fraction = bits & 0x07u;
  if (exponent == 0)
    return fraction == 0 ? FpClass::Zero : FpClass::Subnormal;
  return exponent == 0x78u && fraction == 7 ? FpClass::QuietNaN
                                            : FpClass::Normal;
}

FpClass expected_e5m2(std::uint8_t bits) {
  const auto exponent = bits & 0x7Cu;
  const auto fraction = bits & 0x03u;
  if (exponent == 0)
    return fraction == 0 ? FpClass::Zero : FpClass::Subnormal;
  if (exponent != 0x7Cu)
    return FpClass::Normal;
  if (fraction == 0)
    return FpClass::Infinity;
  return (fraction & 2) ? FpClass::QuietNaN : FpClass::SignalingNaN;
}

}  // namespace

TEST(TypesRefactor, StrongValueTypes) {
  check_value_type<Fp16>();
  check_value_type<Bf16>();
  check_value_type<Fp32>();
  check_value_type<Tf32>();
  check_value_type<Fp64>();
  check_value_type<Fp8E4M3>();
  check_value_type<Fp8E5M2>();
  check_value_type<Fp4E2M1>();
  static_assert(!std::is_same_v<Fp8E4M3, Fp8E5M2>);
}

TEST(TypesRefactor, EverySmallFormatEncodingIsClassified) {
  for (unsigned bits = 0; bits != 256; ++bits) {
    EXPECT_EQ(classify(Fp8E4M3{static_cast<std::uint8_t>(bits)}),
              expected_e4m3(bits))
        << bits;
    EXPECT_EQ(classify(Fp8E5M2{static_cast<std::uint8_t>(bits)}),
              expected_e5m2(bits))
        << bits;
  }
  for (unsigned bits = 0; bits != 16; ++bits) {
    const Fp4E2M1 value{static_cast<std::uint8_t>(bits)};
    EXPECT_EQ(classify(value),
              (bits & 6) == 0
                  ? ((bits & 1) ? FpClass::Subnormal : FpClass::Zero)
                  : FpClass::Normal)
        << bits;
    EXPECT_FALSE(is_nan(value));
    EXPECT_FALSE(is_infinity(value));
  }
  EXPECT_EQ(classify(Fp8E4M3{0x78}), FpClass::Normal);
  EXPECT_EQ(classify(Fp8E4M3{0x7E}), FpClass::Normal);
  EXPECT_EQ(classify(Fp8E4M3{0x7F}), FpClass::QuietNaN);
}

TEST(TypesRefactor, Tf32AndFp4CanonicalStoragePolicy) {
  EXPECT_FALSE(is_valid_encoding(Tf32{0x3F801234u}));
  EXPECT_EQ(normalize_encoding(Tf32{0x3F801234u}).bits, 0x3F800000u);
  EXPECT_FALSE(is_valid_encoding(Fp4E2M1{0xF1u}));
  EXPECT_EQ(normalize_encoding(Fp4E2M1{0xF1u}).bits, 0x01u);
}

}  // namespace ptxsim::fp::test
