#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <string>

#include <ptx_frontend/base/base.hpp>
#include <ptxsim/exec_ir/exec_ir_types.hpp>

namespace ptxsim::exec_ir_lowering::test {
namespace {
TEST(OwnedImmediateMaterialization, AllNativeContainerWidthsMatchPinnedPublicMetadata) {
  const auto metadata = ptx_frontend::base::scalar_type_metadata();
  ASSERT_EQ(metadata.size(), 50);
  for (const auto& row : metadata) {
    const auto spelling = row.source_spelling.substr(1);
    std::optional<exec_ir::DataType> matched;
    for (std::uint16_t id = 0; id < 50; ++id) {
      const auto type = static_cast<exec_ir::DataType>(id);
      if (exec_ir::to_string(type) == spelling) matched = type;
    }
    ASSERT_TRUE(matched) << row.source_spelling;
    const auto width = exec_ir::data_type_width(*matched);
    ASSERT_TRUE(width) << row.source_spelling;
    if (*matched == exec_ir::DataType::pred) {
      EXPECT_EQ(*width, common::RawWidth::pred);
      continue;
    }
    const std::array widths{common::RawWidth::b8, common::RawWidth::b16,
        common::RawWidth::b32, common::RawWidth::b64, common::RawWidth::b128};
    const std::array sizes{1,2,4,8,16};
    bool found = false;
    for (std::size_t index = 0; index < widths.size(); ++index)
      if (*width == widths[index]) { EXPECT_EQ(row.size_bytes, sizes[index]); found = true; }
    EXPECT_TRUE(found);
  }
}
TEST(OwnedImmediateMaterialization, PreservesUseBitsAndSourceSignednessWithoutHostArithmetic) {
  using exec_ir::DataType;
  const auto max = std::numeric_limits<std::uint64_t>::max();
  EXPECT_EQ(*exec_ir::immediate_value({max,DataType::u8}), common::RawValue::b8(std::uint8_t{255}));
  EXPECT_EQ(*exec_ir::immediate_value({max,DataType::s16,true,max}), common::RawValue::b16(std::uint16_t{65535}));
  EXPECT_EQ(*exec_ir::immediate_value({0x80000000,DataType::f32}), common::RawValue::b32(0x80000000));
  EXPECT_EQ(*exec_ir::immediate_value({max,DataType::b128,false,max}),
            common::RawValue::b128(common::Bits128{max,0}));
  EXPECT_EQ(*exec_ir::immediate_value({max,DataType::b128,true,max}),
            common::RawValue::b128(common::Bits128{max,max}));
  EXPECT_FALSE(exec_ir::immediate_value({0,static_cast<DataType>(65535)}));
}
}  // namespace
}  // namespace ptxsim::exec_ir_lowering::test
