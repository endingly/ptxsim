#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <utility>

#include <ptxsim/bootstrap/thread_bootstrap.hpp>

namespace ptxsim::bootstrap::test {
namespace {

using common::FunctionId;
using common::ProgramCounter;
using common::RawValue;
using common::RawWidth;
using common::RegisterSlot;
using common::ThreadId;

auto valid_image() -> program::ProgramImage {
  auto image = program::ProgramImage::create({
      .instructions =
          {
              exec_ir::MovInst{{RegisterSlot{0}, RawWidth::b16},
                               exec_ir::ValueOperand{exec_ir::ImmediateOperand{
                                   RawValue::b16(std::uint16_t{1})}},
                               std::nullopt},
              exec_ir::MovInst{{RegisterSlot{0}, RawWidth::b32},
                               exec_ir::ValueOperand{exec_ir::ImmediateOperand{
                                   RawValue::b32(std::uint32_t{2})}},
                               std::nullopt},
          },
      .functions = {{FunctionId{0},
                     "entry",
                     ProgramCounter{0},
                     ProgramCounter{1},
                     {{RegisterSlot{0}, RawWidth::b16},
                      {RegisterSlot{1}, RawWidth::b128}}},
                    {FunctionId{1},
                     "helper",
                     ProgramCounter{1},
                     ProgramCounter{2},
                     {{RegisterSlot{0}, RawWidth::b32}}}},
      .entry_points = {FunctionId{0}},
      .source_locations_by_pc = {std::nullopt, std::nullopt},
  });
  EXPECT_TRUE(image);
  return std::move(*image);
}

TEST(ThreadBootstrap, CreatesEntryThreadFromCanonicalMetadata) {
  const auto image = valid_image();
  const auto thread = create_entry_thread(image, ThreadId{7}, FunctionId{0});
  ASSERT_TRUE(thread);
  EXPECT_EQ(thread->status(), state::ThreadStatus::ready);
  EXPECT_EQ(thread->thread_id(), ThreadId{7});
  EXPECT_EQ(thread->current_function(), FunctionId{0});
  EXPECT_EQ(thread->current_pc(), ProgramCounter{0});
  ASSERT_EQ(thread->registers().size(), 2U);
  EXPECT_EQ(*thread->registers().declared_width(RegisterSlot{0}),
            RawWidth::b16);
  EXPECT_EQ(*thread->registers().declared_width(RegisterSlot{1}),
            RawWidth::b128);
}

TEST(ThreadBootstrap, RejectsUnknownFunction) {
  const auto result =
      create_entry_thread(valid_image(), ThreadId{0}, FunctionId{9});
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code, ThreadBootstrapErrorCode::unknown_function);
  EXPECT_EQ(result.error().function, FunctionId{9});
}

TEST(ThreadBootstrap, RejectsNonEntryFunction) {
  const auto result =
      create_entry_thread(valid_image(), ThreadId{0}, FunctionId{1});
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code, ThreadBootstrapErrorCode::not_entry_function);
  EXPECT_EQ(result.error().function, FunctionId{1});
}

TEST(ThreadBootstrap, ThreadOutlivesProgramImage) {
  std::optional<state::ThreadState> thread;
  {
    const auto image = valid_image();
    auto result = create_entry_thread(image, ThreadId{3}, FunctionId{0});
    ASSERT_TRUE(result);
    thread.emplace(std::move(*result));
  }
  ASSERT_TRUE(thread);
  EXPECT_EQ(thread->current_pc(), ProgramCounter{0});
  ASSERT_TRUE(thread->registers().write(RegisterSlot{0},
                                        RawValue::b16(std::uint16_t{5})));
  EXPECT_EQ(*thread->registers().read(RegisterSlot{0}),
            RawValue::b16(std::uint16_t{5}));
}

}  // namespace
}  // namespace ptxsim::bootstrap::test
