#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include <ptxsim/inst_execute_engine/inst_execute_engine.hpp>

namespace ptxsim::inst_execute_engine::test {
namespace {

using common::FunctionId;
using common::ProgramCounter;
using common::RawValue;
using common::RawWidth;
using common::RegisterSlot;
using execution_model::CtaId;
using execution_model::GridId;
using execution_model::LaneId;
using execution_model::LaneMask;
using execution_model::ThreadStatus;

/** @brief Exercise generated dispatch through the public engine, not its table. */
class EngineDispatchTest : public ::testing::Test {
 protected:
  /** @brief Own one ready lane and leave resources unbound until a test needs them. */
  EngineDispatchTest()
      : runtime_(GridId{1}, {.cta_dim = {1, 1, 1},
                             .thread_dim = {1, 1, 1},
                             .warp_size = 4}),
        engine_(runtime_, FunctionId{0}, arithmetic_) {
    warp().thread(LaneId{0}).set_pc(ProgramCounter{10});
  }

  /** @brief Return the single warp owned by this fixture's launch. */
  auto warp() -> execution_model::Warp& {
    return runtime_.grid().cta(CtaId{GridId{1}, 0}).warp(0);
  }

  /** @brief Issue only lane zero at its current PC, with an explicit successor. */
  auto execute(const exec_ir::Instruction& instruction,
               std::optional<ProgramCounter> successor = ProgramCounter{11})
      -> std::expected<StepReport, StepError> {
    LaneMask lanes{4};
    lanes.set(LaneId{0});
    return engine_.execute(warp(), {.pc = warp().thread(LaneId{0}).pc(),
                                    .lanes = std::move(lanes)},
                           instruction, successor);
  }

  /** @brief Bind one owned frame and return its non-owning register view. */
  auto bind(std::vector<RawWidth> widths) -> memory::RegisterView {
    const auto frame = runtime_.registers().create_frame({.slot_widths = widths});
    EXPECT_TRUE(frame);
    EXPECT_TRUE(runtime_.bind_register_frame(warp().thread(LaneId{0}).id(),
                                             FunctionId{0}, *frame));
    const auto view = runtime_.registers().view(*frame);
    EXPECT_TRUE(view);
    return *view;
  }

  /** @brief Rejection must precede resource resolution and all state mutation. */
  void expect_unsupported(const exec_ir::Instruction& instruction) {
    const auto result = execute(instruction);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, StepErrorCode::unsupported_instruction);
    EXPECT_EQ(warp().thread(LaneId{0}).pc(), ProgramCounter{10});
    EXPECT_EQ(warp().thread(LaneId{0}).status(), ThreadStatus::Ready);
  }

  /** Runtime owns the topology and every frame for the fixture's lifetime. */
  runtime::LaunchRuntime runtime_;
  /** Arithmetic policy is borrowed by the engine below. */
  arith::context arithmetic_;
  /** Engine lifetime is bounded by the runtime and arithmetic context. */
  InstExecuteEngine engine_;
};

/** @brief Unimplemented operand layouts must not reach a scalar std::get. */
TEST_F(EngineDispatchTest, RejectsMovePackAndUnpackBeforeBindingResources) {
  const exec_ir::RegisterVector vector{{RegisterSlot{1}, RegisterSlot{2}}};
  const exec_ir::Mov::Scalar pack{
      exec_ir::DataType::b64,
      exec_ir::Mov::Scalar::PackOperands{RegisterSlot{0}, vector}};
  expect_unsupported(exec_ir::Mov{std::nullopt, exec_ir::Mov::Variant{pack}});
  const exec_ir::Mov::Scalar unpack{
      exec_ir::DataType::b64,
      exec_ir::Mov::Scalar::UnpackOperands{vector, RegisterSlot{0}}};
  expect_unsupported(exec_ir::Mov{std::nullopt, exec_ir::Mov::Variant{unpack}});
}

/** @brief Each load qualifier is independently checked before any address read. */
TEST_F(EngineDispatchTest, RejectsEveryUnimplementedGenericLoadControl) {
  const exec_ir::Ld::GenericScalar base{
      exec_ir::MemoryConsistency::omitted, exec_ir::MemoryScope::none,
      false, exec_ir::CacheOperator::unspecified, exec_ir::DataType::u32,
      RegisterSlot{0}, RegisterSlot{1}};
  for (int field = 0; field != 5; ++field) {
    auto form = base;
    switch (field) {
      case 0: form.semantics = exec_ir::MemoryConsistency::weak; break;
      case 1: form.scope = exec_ir::MemoryScope::gpu; break;
      case 2: form.mmio = true; break;
      case 3: form.cache = exec_ir::CacheOperator::cg; break;
      case 4: form.type = exec_ir::DataType::b32; break;
    }
    expect_unsupported(exec_ir::Ld{std::nullopt, exec_ir::Ld::Variant{form}});
  }
}

/** @brief Scalar stores cannot silently acquire parameter-space support. */
TEST_F(EngineDispatchTest, RejectsParameterStoreBeforeBindingResources) {
  const exec_ir::St::ExplicitScalar form{
      exec_ir::AddressSpace::param, exec_ir::CacheOperator::unspecified,
      exec_ir::MemoryConsistency::omitted, exec_ir::MemoryScope::none, false,
      exec_ir::DataType::u32, RegisterSlot{0}, RegisterSlot{1}};
  expect_unsupported(exec_ir::St{std::nullopt, exec_ir::St::Variant{form}});
}

/** @brief A wider backend enum must not broaden a form's implemented type. */
TEST_F(EngineDispatchTest, RejectsUnimplementedIntegerAddType) {
  const exec_ir::Add::IntegerNoSat form{
      exec_ir::DataType::u64, RegisterSlot{0},
      RawValue::b64(std::uint64_t{1}), RawValue::b64(std::uint64_t{2})};
  expect_unsupported(exec_ir::Add{std::nullopt, exec_ir::Add::Variant{form}});
}

/** @brief Generated adapters retain arithmetic wrap and common register commit. */
TEST_F(EngineDispatchTest, AddWrapsAndCommitsThroughTheExistingProtocol) {
  auto registers = bind({RawWidth::b32});
  ASSERT_TRUE(registers.write(RegisterSlot{0}, RawValue::b32(7U)));
  const exec_ir::Add::IntegerNoSat form{
      exec_ir::DataType::u32, RegisterSlot{0},
      RawValue::b32(0xffff'ffffU), RawValue::b32(1U)};
  const auto result = execute(exec_ir::Add{std::nullopt, exec_ir::Add::Variant{form}});
  ASSERT_TRUE(result);
  EXPECT_TRUE(result->faults.empty());
  EXPECT_EQ(*registers.read(RegisterSlot{0}), RawValue::b32(0U));
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), ProgramCounter{11});
}

/** @brief A masked lane must not read an invalid ordinary source operand. */
TEST_F(EngineDispatchTest, PredicatedOffAdapterDoesNotReadSource) {
  auto registers = bind({RawWidth::pred, RawWidth::b32});
  ASSERT_TRUE(registers.write(RegisterSlot{0}, RawValue::pred(false)));
  ASSERT_TRUE(registers.write(RegisterSlot{1}, RawValue::b32(7U)));
  const exec_ir::Add::IntegerNoSat form{
      exec_ir::DataType::u32, RegisterSlot{1}, RegisterSlot{99}, RawValue::b32(1U)};
  const auto result = execute(exec_ir::Add{
      exec_ir::Predicate{RegisterSlot{0}, false}, exec_ir::Add::Variant{form}});
  ASSERT_TRUE(result);
  EXPECT_TRUE(result->faults.empty());
  EXPECT_EQ(*registers.read(RegisterSlot{1}), RawValue::b32(7U));
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), ProgramCounter{11});
}

/** @brief Control-only adapters must not eagerly resolve a register frame. */
TEST_F(EngineDispatchTest, UniformBranchAndExitNeedNoRegisterFrame) {
  const exec_ir::Bra::Direct form{true, ProgramCounter{20}};
  const auto branch = execute(exec_ir::Bra{std::nullopt, exec_ir::Bra::Variant{form}},
                              std::nullopt);
  ASSERT_TRUE(branch);
  EXPECT_TRUE(branch->faults.empty());
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), ProgramCounter{20});
  const auto exited = execute(exec_ir::Exit{
      std::nullopt, exec_ir::Exit::Variant{exec_ir::Exit::Bare{}}}, std::nullopt);
  ASSERT_TRUE(exited);
  EXPECT_TRUE(exited->faults.empty());
  EXPECT_EQ(warp().thread(LaneId{0}).status(), ThreadStatus::Exited);
}

/** @brief The registered warp-sync binding still selects collective commit. */
TEST_F(EngineDispatchTest, ImmediateWarpSyncNeedsNoRegisterFrame) {
  const exec_ir::Bar::WarpSync form{RawValue::b32(1U)};
  const auto result = execute(exec_ir::Bar{std::nullopt, exec_ir::Bar::Variant{form}});
  ASSERT_TRUE(result);
  EXPECT_TRUE(result->faults.empty());
  EXPECT_EQ(warp().thread(LaneId{0}).status(), ThreadStatus::Ready);
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), ProgramCounter{11});
}

}  // namespace
}  // namespace ptxsim::inst_execute_engine::test
