#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <limits>

#include <ptxsim/arith/arith.hpp>

namespace ptxsim::arith::test {
namespace {

constexpr std::uint32_t not_found = std::numeric_limits<std::uint32_t>::max();

TEST(BitOperations, BitExtractUnsignedUsesPtxOperandAndPaddingRules) {
  constexpr std::uint32_t bits32 = 0xc000'0020u;
  EXPECT_EQ(bit_extract_unsigned(bits32, 0, 0), 0u);
  EXPECT_EQ(bit_extract_unsigned(bits32, 32, 4), 0u);
  EXPECT_EQ(bit_extract_unsigned(bits32, 30, 4), 3u);
  EXPECT_EQ(bit_extract_unsigned(bits32, 261, 1), 1u);
  EXPECT_EQ(bit_extract_unsigned(bits32, 5, 257), 1u);

  constexpr std::uint64_t bits64 = 0xc000'0000'0000'0000ull;
  EXPECT_EQ(bit_extract_unsigned(bits64, 63, 2), 1ull);
  EXPECT_EQ(bit_extract_unsigned(bits64, 62, 4), 3ull);
  EXPECT_EQ(bit_extract_unsigned(bits64, 64, 1), 0ull);
}

TEST(BitOperations, BitExtractSignedUsesPtxSignBitBeyondSourceMsb) {
  constexpr auto negative32 = std::bit_cast<std::int32_t>(0x8000'0000u);
  constexpr auto positive32 = std::bit_cast<std::int32_t>(0x4000'0000u);
  EXPECT_EQ(bit_extract_signed(negative32, 0, 0), 0);
  EXPECT_EQ(bit_extract_signed(negative32, 31, 1), -1);
  EXPECT_EQ(bit_extract_signed(negative32, 30, 4), -2);
  EXPECT_EQ(bit_extract_signed(negative32, 32, 4), -1);
  EXPECT_EQ(bit_extract_signed(negative32, 255, 1), -1);
  EXPECT_EQ(bit_extract_signed(positive32, 32, 4), 0);
  EXPECT_EQ(bit_extract(negative32, 31, 1), -1);

  constexpr auto negative64 =
      std::bit_cast<std::int64_t>(0x8000'0000'0000'0000ull);
  constexpr auto positive64 =
      std::bit_cast<std::int64_t>(0x4000'0000'0000'0000ull);
  EXPECT_EQ(bit_extract_signed(negative64, 63, 2), -1);
  EXPECT_EQ(bit_extract_signed(negative64, 62, 4), -2);
  EXPECT_EQ(bit_extract_signed(negative64, 64, 8), -1);
  EXPECT_EQ(bit_extract_signed(positive64, 64, 8), 0);
}

TEST(BitOperations, BfindReturnsPtxNonSignPositionAndSentinel) {
  EXPECT_EQ(find_most_significant_non_sign(std::uint32_t{}), not_found);
  EXPECT_EQ(find_most_significant_non_sign(0x8000'1000u), 31u);
  EXPECT_EQ(find_most_significant_non_sign(1u), 0u);
  EXPECT_EQ(find_most_significant_non_sign(std::uint64_t{}), not_found);
  EXPECT_EQ(find_most_significant_non_sign(0x8000'0000'0000'0000ull), 63u);

  EXPECT_EQ(find_most_significant_non_sign(std::int32_t{}), not_found);
  EXPECT_EQ(find_most_significant_non_sign(std::int32_t{-1}), not_found);
  EXPECT_EQ(
      find_most_significant_non_sign(std::numeric_limits<std::int32_t>::min()),
      30u);
  EXPECT_EQ(find_most_significant_non_sign(std::int32_t{-2}), 0u);
  EXPECT_EQ(find_most_significant_non_sign(std::int32_t{0x400}), 10u);
  EXPECT_EQ(find_most_significant_non_sign(std::int64_t{-1}), not_found);
  EXPECT_EQ(
      find_most_significant_non_sign(std::numeric_limits<std::int64_t>::min()),
      62u);

  EXPECT_EQ(find_most_significant(std::int32_t{-1}), -1);
  EXPECT_EQ(find_most_significant(std::numeric_limits<std::int32_t>::min()),
            30);
}

TEST(BitOperations, BfindShiftAmountUsesPtxReturnForm) {
  EXPECT_EQ(find_shift_amount(std::uint32_t{}), not_found);
  EXPECT_EQ(find_shift_amount(0x8000'0000u), 0u);
  EXPECT_EQ(find_shift_amount(1u), 31u);
  EXPECT_EQ(find_shift_amount(std::int32_t{-1}), not_found);
  EXPECT_EQ(find_shift_amount(std::numeric_limits<std::int32_t>::min()), 1u);
  EXPECT_EQ(find_shift_amount(std::int32_t{-2}), 31u);
  EXPECT_EQ(find_shift_amount(std::int64_t{-1}), not_found);
  EXPECT_EQ(find_shift_amount(std::numeric_limits<std::int64_t>::min()), 1u);
  EXPECT_EQ(find_shift_amount(std::int64_t{1}), 63u);
}

}  // namespace
}  // namespace ptxsim::arith::test
