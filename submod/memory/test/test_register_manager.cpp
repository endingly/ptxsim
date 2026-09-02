#include <gtest/gtest.h>

#include <concepts>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <ptxsim/memory/register/register_manager.hpp>

namespace ptxsim::memory::test {
namespace {

using common::Bits128;
using common::RawValue;
using common::RawWidth;
using common::RegisterSlot;

template <typename View>
concept WritableRegisterView = requires(
    View view, RegisterSlot slot, RawValue value) { view.write(slot, value); };

using ConstViewResult = decltype(std::declval<const RegisterManager&>().view(
    std::declval<RegisterFrameHandle>()));
static_assert(
    std::same_as<typename ConstViewResult::value_type, ConstRegisterView>);
static_assert(!WritableRegisterView<ConstRegisterView>);

auto frame(RegisterManager& manager, std::vector<RawWidth> widths)
    -> RegisterFrameHandle {
  auto result = manager.create_frame({std::move(widths)});
  EXPECT_TRUE(result);
  return *result;
}
}  // namespace

TEST(RegisterManager, EmptyFrameAndEveryDeclaredWidth) {
  RegisterManager manager;
  const auto empty = frame(manager, {});
  auto empty_view = manager.view(empty);
  ASSERT_TRUE(empty_view);
  EXPECT_EQ(*empty_view->slot_count(), 0U);
  EXPECT_EQ(empty_view->read(RegisterSlot{0}).error().code,
            RegisterErrorCode::slot_out_of_range);

  const std::vector widths{RawWidth::pred, RawWidth::b8,  RawWidth::b16,
                           RawWidth::b32,  RawWidth::b64, RawWidth::b128};
  const auto handle = frame(manager, widths);
  auto view = manager.view(handle);
  ASSERT_TRUE(view);
  EXPECT_EQ(*view->slot_count(), widths.size());
  for (std::uint32_t index = 0; index < widths.size(); ++index) {
    EXPECT_EQ(*view->declared_width(RegisterSlot{index}), widths[index]);
    EXPECT_FALSE(*view->initialized(RegisterSlot{index}));
    const auto read = view->read(RegisterSlot{index});
    ASSERT_FALSE(read);
    EXPECT_EQ(read.error().code, RegisterErrorCode::uninitialized_read);
  }
}

TEST(RegisterManager, ReadsWritesWidthsAndBits128) {
  RegisterManager manager;
  const auto handle =
      frame(manager, {RawWidth::pred, RawWidth::b8, RawWidth::b16,
                      RawWidth::b32, RawWidth::b64, RawWidth::b128});
  auto view = *manager.view(handle);
  const std::vector values{RawValue::pred(true),
                           RawValue::b8(std::uint8_t{1}),
                           RawValue::b16(std::uint16_t{2}),
                           RawValue::b32(3U),
                           RawValue::b64(std::uint64_t{4}),
                           RawValue::b128(Bits128{5, 6})};
  for (std::uint32_t index = 0; index < values.size(); ++index) {
    ASSERT_TRUE(view.write(RegisterSlot{index}, values[index]));
    EXPECT_TRUE(*view.initialized(RegisterSlot{index}));
    EXPECT_EQ(*view.read(RegisterSlot{index}), values[index]);
  }
}

TEST(RegisterManager, ReportsInvalidLayoutSlotsAndWidthMismatches) {
  RegisterManager manager;
  const auto invalid = manager.create_frame({{static_cast<RawWidth>(99)}});
  ASSERT_FALSE(invalid);
  EXPECT_EQ(invalid.error().code, RegisterErrorCode::invalid_layout_width);
  EXPECT_EQ(invalid.error().index, 0U);

  const auto handle = frame(manager, {RawWidth::b32});
  auto view = *manager.view(handle);
  const auto out_of_range =
      view.write(RegisterSlot{1}, RawValue::b64(std::uint64_t{7}));
  ASSERT_FALSE(out_of_range);
  EXPECT_EQ(out_of_range.error().code, RegisterErrorCode::slot_out_of_range);

  const auto mismatch =
      view.write(RegisterSlot{0}, RawValue::b64(std::uint64_t{7}));
  ASSERT_FALSE(mismatch);
  EXPECT_EQ(mismatch.error().code, RegisterErrorCode::width_mismatch);
  EXPECT_EQ(mismatch.error().expected, RawWidth::b32);
  EXPECT_EQ(mismatch.error().actual, RawWidth::b64);
  EXPECT_FALSE(*view.initialized(RegisterSlot{0}));
}

TEST(RegisterManager, KeepsIndependentFramesAndAllowsMultipleFrames) {
  RegisterManager manager;
  const auto first = frame(manager, {RawWidth::b32});
  const auto second = frame(manager, {RawWidth::b32});
  const auto other = frame(manager, {RawWidth::b32});
  EXPECT_NE(first, second);
  EXPECT_NE(first, other);

  ASSERT_TRUE(manager.view(first)->write(RegisterSlot{0}, RawValue::b32(1U)));
  ASSERT_TRUE(manager.view(second)->write(RegisterSlot{0}, RawValue::b32(2U)));
  EXPECT_EQ(*manager.view(first)->read(RegisterSlot{0}), RawValue::b32(1U));
  EXPECT_EQ(*manager.view(second)->read(RegisterSlot{0}), RawValue::b32(2U));
  EXPECT_FALSE(*manager.view(other)->initialized(RegisterSlot{0}));
}

TEST(RegisterManager, RejectsDestroyedCrossManagerAndReusedHandles) {
  RegisterManager manager;
  const auto old = frame(manager, {RawWidth::b32});
  auto old_view = *manager.view(old);
  ASSERT_TRUE(manager.destroy_frame(old));
  EXPECT_EQ(manager.view(old).error().code, RegisterErrorCode::stale_frame);
  EXPECT_EQ(old_view.read(RegisterSlot{0}).error().code,
            RegisterErrorCode::stale_frame);

  const auto replacement = frame(manager, {RawWidth::b32});
  EXPECT_NE(old, replacement);
  EXPECT_EQ(old_view.write(RegisterSlot{0}, RawValue::b32(1U)).error().code,
            RegisterErrorCode::stale_frame);

  RegisterManager other_manager;
  EXPECT_EQ(other_manager.view(replacement).error().code,
            RegisterErrorCode::stale_frame);
}

TEST(RegisterManager, ConstViewsRemainReadOnlyAndViewsOutliveManagersSafely) {
  {
    RegisterManager manager;
    const auto local_handle = frame(manager, {RawWidth::b16});
    const RegisterManager& const_manager = manager;
    auto const_view = const_manager.view(local_handle);
    EXPECT_TRUE(const_view);
    EXPECT_EQ(*const_view->declared_width(RegisterSlot{0}), RawWidth::b16);
  }

  RegisterView escaped = [] {
    auto manager = std::make_unique<RegisterManager>();
    const auto local_handle = frame(*manager, {RawWidth::b8});
    auto view = *manager->view(local_handle);
    manager.reset();
    return view;
  }();
  EXPECT_EQ(escaped.slot_count().error().code, RegisterErrorCode::stale_frame);
}

TEST(RegisterManager, HasNoFixedPhysicalRegisterLimit) {
  RegisterManager manager;
  constexpr std::uint32_t slot_count = 4096;
  const auto handle =
      frame(manager, std::vector<RawWidth>(slot_count, RawWidth::b32));
  auto view = *manager.view(handle);
  ASSERT_TRUE(view.write(RegisterSlot{slot_count - 1}, RawValue::b32(9U)));
  EXPECT_EQ(*view.read(RegisterSlot{slot_count - 1}), RawValue::b32(9U));
}

}  // namespace ptxsim::memory::test
