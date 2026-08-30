#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include <ptxsim/state/register_file.hpp>

#include "register_file_detail.hpp"

namespace ptxsim::state::test {
namespace {

using common::RawValue;
using common::RawWidth;
using common::RegisterSlot;

static_assert(detail::layout_size_representable(0));
static_assert(detail::layout_size_representable(
    static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())));
static_assert(!detail::size_type_can_exceed_register_slots ||
              detail::layout_size_representable(
                  static_cast<std::size_t>(detail::register_slot_count)));
static_assert(!detail::size_type_can_exceed_register_slots ||
              !detail::layout_size_representable(
                  static_cast<std::size_t>(detail::register_slot_count + 1)));

TEST(RegisterFile, EmptyLayoutIsUsable) {
  const auto registers = RegisterFile::create({});
  ASSERT_TRUE(registers);
  EXPECT_EQ(registers->size(), 0U);
  EXPECT_TRUE(dump(*registers).empty());
}

TEST(RegisterFile, AllWidthsStartUninitialized) {
  const std::vector layout{RawWidth::pred, RawWidth::b8,  RawWidth::b16,
                           RawWidth::b32,  RawWidth::b64, RawWidth::b128};
  const auto registers = RegisterFile::create(layout);
  ASSERT_TRUE(registers);
  for (std::uint32_t index = 0; index < registers->size(); ++index) {
    const auto slot = RegisterSlot{index};
    EXPECT_EQ(*registers->declared_width(slot), layout[index]);
    EXPECT_FALSE(*registers->is_initialized(slot));
    const auto value = registers->read(slot);
    ASSERT_FALSE(value);
    EXPECT_EQ(value.error().code, RegisterErrorCode::uninitialized_read);
    EXPECT_EQ(value.error().slot, slot);
  }
  EXPECT_EQ(dump(*RegisterFile::create({RawWidth::b32})),
            "register:0 b32 uninitialized\n");
}

TEST(RegisterFile, ReadsAndWritesEachDeclaredWidth) {
  const std::vector<RawWidth> layout{RawWidth::pred, RawWidth::b8,
                                     RawWidth::b16,  RawWidth::b32,
                                     RawWidth::b64,  RawWidth::b128};
  const std::vector<RawValue> values{
      RawValue::pred(true),
      RawValue::b8(std::uint8_t{1}),
      RawValue::b16(std::uint16_t{2}),
      RawValue::b32(std::uint32_t{3}),
      RawValue::b64(std::uint64_t{4}),
      RawValue::b128(common::Bits128{5, 6}),
  };
  auto registers = *RegisterFile::create(layout);
  for (std::uint32_t index = 0; index < values.size(); ++index) {
    const auto slot = RegisterSlot{index};
    ASSERT_TRUE(registers.write(slot, values[index]));
    EXPECT_EQ(*registers.read(slot), values[index]);
  }
}

TEST(RegisterFile, ReadsWritesOverwritesAndDumps) {
  auto registers =
      *RegisterFile::create({RawWidth::b32, RawWidth::pred, RawWidth::b128});
  EXPECT_TRUE(registers.write(RegisterSlot{0}, RawValue::b32(7U)));
  EXPECT_TRUE(registers.write(RegisterSlot{1}, RawValue::pred(true)));
  EXPECT_TRUE(
      registers.write(RegisterSlot{2}, RawValue::b128(common::Bits128{1, 2})));
  EXPECT_EQ(*registers.read(RegisterSlot{0}), RawValue::b32(7U));
  EXPECT_EQ(*registers.read(RegisterSlot{1}), RawValue::pred(true));
  EXPECT_EQ(*registers.read(RegisterSlot{2}),
            RawValue::b128(common::Bits128{1, 2}));
  EXPECT_TRUE(registers.write(RegisterSlot{0}, RawValue::b32(9U)));
  EXPECT_EQ(*registers.read(RegisterSlot{0}), RawValue::b32(9U));
  EXPECT_EQ(dump(registers),
            "register:0 b32 b32:0x00000009\n"
            "register:1 pred pred:true\n"
            "register:2 b128 b128:0x00000000000000020000000000000001\n");
}

TEST(RegisterFile, MismatchedWriteDoesNotInitializeOrMutate) {
  auto registers = *RegisterFile::create({RawWidth::b32});
  const auto mismatch =
      registers.write(RegisterSlot{0}, RawValue::b64(std::uint64_t{9}));
  ASSERT_FALSE(mismatch);
  EXPECT_EQ(mismatch.error().code, RegisterErrorCode::width_mismatch);
  EXPECT_EQ(mismatch.error().expected, RawWidth::b32);
  EXPECT_EQ(mismatch.error().actual, RawWidth::b64);
  EXPECT_FALSE(*registers.is_initialized(RegisterSlot{0}));

  ASSERT_TRUE(registers.write(RegisterSlot{0}, RawValue::b32(3U)));
  const auto second_mismatch =
      registers.write(RegisterSlot{0}, RawValue::b8(std::uint8_t{1}));
  ASSERT_FALSE(second_mismatch);
  EXPECT_EQ(*registers.read(RegisterSlot{0}), RawValue::b32(3U));
}

TEST(RegisterFile, InvalidSlotsAndLayoutAreStructuredErrors) {
  auto registers = *RegisterFile::create({RawWidth::b8});
  const auto read = registers.read(RegisterSlot{1});
  ASSERT_FALSE(read);
  EXPECT_EQ(read.error().code, RegisterErrorCode::slot_out_of_range);
  EXPECT_EQ(read.error().slot, RegisterSlot{1});
  EXPECT_EQ(read.error().index, 1U);
  const auto write =
      registers.write(RegisterSlot{1}, RawValue::b8(std::uint8_t{1}));
  ASSERT_FALSE(write);
  EXPECT_EQ(write.error().code, RegisterErrorCode::slot_out_of_range);
  EXPECT_EQ(write.error().slot, RegisterSlot{1});
  EXPECT_EQ(write.error().index, 1U);
  const auto initialized = registers.is_initialized(RegisterSlot{1});
  ASSERT_FALSE(initialized);
  EXPECT_EQ(initialized.error().code, RegisterErrorCode::slot_out_of_range);
  EXPECT_EQ(initialized.error().slot, RegisterSlot{1});
  EXPECT_EQ(initialized.error().index, 1U);
  const auto width = registers.declared_width(RegisterSlot{1});
  ASSERT_FALSE(width);
  EXPECT_EQ(width.error().code, RegisterErrorCode::slot_out_of_range);
  EXPECT_EQ(width.error().slot, RegisterSlot{1});
  EXPECT_EQ(width.error().index, 1U);

  const auto invalid = RegisterFile::create({static_cast<RawWidth>(99)});
  ASSERT_FALSE(invalid);
  EXPECT_EQ(invalid.error().code, RegisterErrorCode::invalid_layout_width);
  EXPECT_EQ(invalid.error().index, 0U);
  EXPECT_EQ(invalid.error().actual, static_cast<RawWidth>(99));
}

TEST(RegisterFile, CopyAndMovePreserveState) {
  auto registers = *RegisterFile::create({RawWidth::b16});
  ASSERT_TRUE(
      registers.write(RegisterSlot{0}, RawValue::b16(std::uint16_t{5})));
  auto copy = registers;
  auto moved = std::move(copy);
  EXPECT_EQ(*moved.read(RegisterSlot{0}), RawValue::b16(std::uint16_t{5}));
}

}  // namespace
}  // namespace ptxsim::state::test
