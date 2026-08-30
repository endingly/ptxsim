#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <type_traits>

#include <ptxsim/common/raw_value.hpp>

namespace ptxsim::common::test {
namespace {

template <typename T>
concept CanMakeB8 = requires(T value) { RawValue::b8(value); };

template <typename T>
concept CanMakeB16 = requires(T value) { RawValue::b16(value); };

template <typename T>
concept CanMakeB32 = requires(T value) { RawValue::b32(value); };

template <typename T>
concept CanMakeB64 = requires(T value) { RawValue::b64(value); };

template <typename T>
concept CanMakePred = requires(T value) { RawValue::pred(value); };

template <typename T>
concept CanMakeB128 = requires(T value) { RawValue::b128(value); };

TEST(RawValue, ExactFactoriesAndDeterministicDump) {
  static_assert(!std::is_default_constructible_v<RawValue>);
  static_assert(CanMakePred<bool>);
  static_assert(CanMakeB8<std::uint8_t>);
  static_assert(!CanMakePred<int>);
  static_assert(!CanMakeB8<int>);
  static_assert(!CanMakeB8<double>);
  static_assert(!CanMakeB8<std::uint16_t>);
  static_assert(!CanMakeB16<int>);
  static_assert(!CanMakeB32<int>);
  static_assert(!CanMakeB64<int>);
  static_assert(!CanMakeB128<std::uint64_t>);

  EXPECT_EQ(to_string(RawValue::pred(false)), "pred:false");
  EXPECT_EQ(to_string(RawValue::pred(true)), "pred:true");
  EXPECT_EQ(to_string(RawValue::b8(std::uint8_t{0})), "b8:0x00");
  EXPECT_EQ(to_string(RawValue::b8(std::numeric_limits<std::uint8_t>::max())),
            "b8:0xff");
  EXPECT_EQ(to_string(RawValue::b16(std::uint16_t{0})), "b16:0x0000");
  EXPECT_EQ(to_string(RawValue::b16(std::numeric_limits<std::uint16_t>::max())),
            "b16:0xffff");
  EXPECT_EQ(to_string(RawValue::b32(std::uint32_t{1})), "b32:0x00000001");
  EXPECT_EQ(to_string(RawValue::b32(std::numeric_limits<std::uint32_t>::max())),
            "b32:0xffffffff");
  EXPECT_EQ(to_string(RawValue::b64(std::uint64_t{1})),
            "b64:0x0000000000000001");
  EXPECT_EQ(to_string(RawValue::b64(std::numeric_limits<std::uint64_t>::max())),
            "b64:0xffffffffffffffff");
  EXPECT_EQ(to_string(RawValue::b128(Bits128{.low = 0, .high = 0})),
            "b128:0x00000000000000000000000000000000");
  EXPECT_EQ(to_string(RawValue::b128(
                Bits128{.low = std::numeric_limits<std::uint64_t>::max(),
                        .high = std::numeric_limits<std::uint64_t>::max()})),
            "b128:0xffffffffffffffffffffffffffffffff");
}

TEST(RawValue, RoundTripAndWidthErrors) {
  const auto pred = RawValue::pred(true);
  const auto b8 = RawValue::b8(std::uint8_t{0x12});
  const auto b16 = RawValue::b16(std::uint16_t{0x1234});
  const auto b32 = RawValue::b32(std::uint32_t{0x1234'5678});
  const auto b64 = RawValue::b64(std::uint64_t{0x0123'4567'89ab'cdef});
  const auto b128 = RawValue::b128(
      Bits128{.low = 0x0123'4567'89ab'cdef, .high = 0xfedc'ba98'7654'3210});

  EXPECT_EQ(*pred.as_pred(), true);
  EXPECT_EQ(*b8.as_b8(), 0x12u);
  EXPECT_EQ(*b16.as_b16(), 0x1234u);
  EXPECT_EQ(*b32.as_b32(), 0x1234'5678u);
  EXPECT_EQ(*b64.as_b64(), 0x0123'4567'89ab'cdefu);
  EXPECT_EQ(*b128.as_b128(), (Bits128{.low = 0x0123'4567'89ab'cdef,
                                      .high = 0xfedc'ba98'7654'3210}));
  EXPECT_EQ(to_string(b128), "b128:0xfedcba98765432100123456789abcdef");
  EXPECT_EQ(b32, RawValue::b32(std::uint32_t{0x1234'5678}));
  EXPECT_NE(b32, b64);

  const auto wrong_width = b8.as_b64();
  ASSERT_FALSE(wrong_width.has_value());
  EXPECT_EQ(wrong_width.error(),
            (RawValueError{.expected = RawWidth::b64, .actual = RawWidth::b8}));
}

}  // namespace
}  // namespace ptxsim::common::test
