#include <gtest/gtest.h>

#include <ptxsim/arith/arith.hpp>

namespace ptxsim::arith::test {

template <typename P>
concept packed_add_invocable = requires(const context& c, P value) {
  add(c, value, value);
};

static_assert(packed_format_capability_v<float16_t, 2, dense_packed_layout>);
static_assert(packed_format_capability_v<ufloat8_e8m0_t, 2,
                                         dense_packed_layout>);
static_assert(packed_format_capability_v<fixed8_s2f6_t, 2,
                                         dense_packed_layout>);
static_assert(packed_format_capability_v<float6_e2m3_t, 2,
                                         byte_packed_layout>);
static_assert(packed_format_capability_v<float4_e2m1_t, 4,
                                         dense_packed_layout>);
static_assert(packed_format_capability_v<float6_e2m3_t, 4,
                                         byte_packed_layout>);
static_assert(!packed_format_capability_v<ufloat7_e4m3_t, 2,
                                          dense_packed_layout>);
static_assert(packed_operation_capability<packed_operation::add,
                                          float16x2_t>::value);
static_assert(packed_operation_capability<packed_operation::fma,
                                          bfloat16x2_t>::value);
static_assert(!packed_operation_capability<packed_operation::add,
                                           float32x2_t>::value);
static_assert(!packed_operation_capability<packed_operation::mul,
                                           float8_e4m3x2_t>::value);
static_assert(!packed_add_invocable<float8_e4m3x2_t>);
static_assert(operation_capability<
              scalar_operation::pack, float4_e2m1x4_t,
              std::array<float4_e2m1_t, 4>>::value);
static_assert(!operation_capability<scalar_operation::add, float32x2_t,
                                    float32x2_t, float32x2_t>::value);
static_assert(float16x2_t::from_bits(0x00121234u)[1].bits() == 0x12u);

TEST(PackedFormats, StorageAliasesKeepCanonicalPaddingAndLaneZeroAtLsb) {
  const auto ue8 = ufloat8_e8m0x2_t::from_bits(0xff7fu);
  EXPECT_EQ(unpack(ue8)[0].bits(), 0x7fu);
  EXPECT_EQ(unpack(ue8)[1].bits(), 0xffu);
  EXPECT_EQ(pack<ufloat8_e8m0x2_t>(unpack(ue8)).bits(), 0xff7fu);

  const auto s2f6 = pack<fixed8_s2f6x2_t>(
      {fixed8_s2f6_t{127}, fixed8_s2f6_t{-128}});
  EXPECT_EQ(s2f6.bits(), 0x807fu);
  EXPECT_EQ(unpack(s2f6)[0].rep, 127);
  EXPECT_EQ(unpack(s2f6)[1].rep, -128);

  const auto fp6 = float6_e2m3x2_t::from_bits(0xffffu);
  EXPECT_EQ(fp6.bits(), 0x3f3fu);
  EXPECT_EQ(unpack(fp6)[0].bits(), 0x3fu);
  EXPECT_EQ(unpack(fp6)[1].bits(), 0x3fu);

  const auto fp4x4 = float4_e2m1x4_t::from_bits(0xffffu);
  EXPECT_EQ(fp4x4.bits(), 0xffffu);
  const auto fp4_lanes = unpack(fp4x4);
  for (const auto lane : fp4_lanes)
    EXPECT_EQ(lane.bits(), 0x0fu);
  EXPECT_EQ(pack<float4_e2m1x4_t>(fp4_lanes).bits(), 0xffffu);

  const auto fp6x4 = float6_e3m2x4_t::from_bits(0xffffffffu);
  EXPECT_EQ(fp6x4.bits(), 0x3f3f3f3fu);
  const auto fp6_lanes = unpack(fp6x4);
  for (const auto lane : fp6_lanes)
    EXPECT_EQ(lane.bits(), 0x3fu);
  EXPECT_EQ(pack<float6_e3m2x4_t>(fp6_lanes).bits(), 0x3f3f3f3fu);
}

TEST(PackedFormats, LaneAccessChecksBoundsInDebug) {
  const auto value = float16x2_t::from_bits(0x00121234u);
  EXPECT_EQ(value[0].bits(), 0x1234u);
  EXPECT_EQ(value[1].bits(), 0x12u);
#ifndef NDEBUG
  EXPECT_DEATH((void)value[2], "lane < Lanes");
#endif
}

}  // namespace ptxsim::arith::test
