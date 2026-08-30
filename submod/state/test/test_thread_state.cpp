#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include <ptxsim/program/program_image.hpp>
#include <ptxsim/state/thread_state.hpp>

namespace ptxsim::state::test {
namespace {

using common::FunctionId;
using common::ProgramCounter;
using common::RawValue;
using common::RawWidth;
using common::RegisterSlot;
using common::ThreadId;

TEST(ThreadState, CreatesEmptyReadyState) {
  const auto thread =
      ThreadState::create(ThreadId{7}, FunctionId{3}, ProgramCounter{11}, {});
  ASSERT_TRUE(thread);
  EXPECT_EQ(thread->thread_id(), ThreadId{7});
  EXPECT_EQ(thread->current_function(), FunctionId{3});
  EXPECT_EQ(thread->current_pc(), ProgramCounter{11});
  EXPECT_EQ(thread->status(), ThreadStatus::ready);
  EXPECT_EQ(thread->registers().size(), 0U);
  EXPECT_TRUE(thread->call_frames().empty());
  EXPECT_EQ(dump(*thread),
            "thread:7 function:3 pc:11 status:ready call-depth:0\n");
}

TEST(ThreadState, RegistersStartUninitializedAndWritesAppearInDump) {
  auto thread =
      *ThreadState::create(ThreadId{0}, FunctionId{1}, ProgramCounter{2},
                           {RawWidth::b32, RawWidth::pred});
  EXPECT_FALSE(thread.registers().read(RegisterSlot{0}));
  EXPECT_FALSE(thread.registers().is_initialized(RegisterSlot{1}).value());
  ASSERT_TRUE(thread.registers().write(RegisterSlot{0}, RawValue::b32(9U)));
  EXPECT_EQ(dump(thread),
            "thread:0 function:1 pc:2 status:ready call-depth:0\n"
            "register:0 b32 b32:0x00000009\n"
            "register:1 pred uninitialized\n");
}

TEST(ThreadState, AcceptsCopiedProgramImageFunctionMetadata) {
  const auto image = program::ProgramImage::create({
      .instructions = {exec_ir::MovInst{
          {RegisterSlot{0}, RawWidth::b16},
          exec_ir::ValueOperand{
              exec_ir::ImmediateOperand{RawValue::b16(std::uint16_t{1})}},
          std::nullopt}},
      .functions = {{FunctionId{0},
                     "entry",
                     ProgramCounter{0},
                     ProgramCounter{1},
                     {{RegisterSlot{0}, RawWidth::b16},
                      {RegisterSlot{1}, RawWidth::b128}}}},
      .entry_points = {FunctionId{0}},
      .source_locations_by_pc = {std::nullopt},
  });
  ASSERT_TRUE(image);
  const auto& function = image->functions().front();
  std::vector<RawWidth> layout;
  for (const auto& register_layout : function.registers)
    layout.push_back(register_layout.width);

  const auto thread = ThreadState::create(ThreadId{5}, function.id,
                                          function.begin_pc, std::move(layout));
  ASSERT_TRUE(thread);
  EXPECT_EQ(thread->current_function(), function.id);
  EXPECT_EQ(thread->current_pc(), function.begin_pc);
  EXPECT_EQ(thread->registers().size(), function.registers.size());
  EXPECT_EQ(*thread->registers().declared_width(RegisterSlot{1}),
            RawWidth::b128);
}

TEST(ThreadState, PropagatesInvalidRegisterLayout) {
  const auto thread =
      ThreadState::create(ThreadId{0}, FunctionId{0}, ProgramCounter{0},
                          {static_cast<RawWidth>(99)});
  ASSERT_FALSE(thread);
  EXPECT_EQ(thread.error().code, RegisterErrorCode::invalid_layout_width);
  EXPECT_EQ(thread.error().actual, static_cast<RawWidth>(99));
  EXPECT_EQ(thread.error().index, 0U);
}

TEST(ThreadState, CopyAndMovePreserveState) {
  auto thread = *ThreadState::create(ThreadId{4}, FunctionId{2},
                                     ProgramCounter{8}, {RawWidth::b8});
  ASSERT_TRUE(
      thread.registers().write(RegisterSlot{0}, RawValue::b8(std::uint8_t{4})));
  auto copy = thread;
  auto moved = std::move(copy);
  EXPECT_EQ(moved.thread_id(), ThreadId{4});
  EXPECT_EQ(*moved.registers().read(RegisterSlot{0}),
            RawValue::b8(std::uint8_t{4}));
}

}  // namespace
}  // namespace ptxsim::state::test
