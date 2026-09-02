#include <gtest/gtest.h>

#include <initializer_list>
#include <map>
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
using execution_model::Warp;
using execution_model::WarpIssueGroup;

constexpr GridId grid_id{7};
constexpr FunctionId function{3};
constexpr ProgramCounter initial_pc{10};
constexpr ProgramCounter move_fallthrough{42};
constexpr MoveProbe move{
    .type = DataType::b32,
    .source = RegisterSlot{0},
    .destination = RegisterSlot{1},
};
constexpr ProbeInstruction move_instruction{
    .predicate = std::nullopt,
    .operation = move,
};

auto shape() -> execution_model::GridShape {
  return {
      .cta_dim = {1, 1, 1},
      .thread_dim = {2, 1, 1},
      .warp_size = 4,
  };
}

auto three_lane_shape() -> execution_model::GridShape {
  return {
      .cta_dim = {1, 1, 1},
      .thread_dim = {3, 1, 1},
      .warp_size = 4,
  };
}

auto ready_lanes_by_pc(const Warp& warp) -> std::map<ProgramCounter, LaneMask> {
  std::map<ProgramCounter, LaneMask> groups;
  for (const auto& thread : warp) {
    if (!thread.ready()) {
      continue;
    }
    auto [group, inserted] = groups.try_emplace(
        thread.pc(), LaneMask{warp.architectural_warp_size()});
    (void)inserted;
    group->second.set(thread.lane_id());
  }
  return groups;
}

auto issue(ProgramCounter pc, std::initializer_list<std::uint32_t> lanes)
    -> WarpIssueGroup {
  LaneMask mask{4};
  for (const auto lane : lanes) {
    mask.set(LaneId{lane});
  }
  return {.pc = pc, .lanes = std::move(mask)};
}

class BoundStepAdapter final {
 public:
  BoundStepAdapter(InstExecuteEngine& engine,
                   const ProbeInstruction& instruction,
                   ProgramCounter fallthrough) noexcept
      : engine_(engine), instruction_(instruction), fallthrough_(fallthrough) {}

  auto step(Warp& warp, const WarpIssueGroup& group)
      -> std::expected<StepReport, StepError> {
    return engine_.execute(warp, group, instruction_, fallthrough_);
  }

 private:
  InstExecuteEngine& engine_;
  const ProbeInstruction& instruction_;
  ProgramCounter fallthrough_;
};

class InstExecuteEngineTest : public ::testing::Test {
 protected:
  InstExecuteEngineTest()
      : runtime_(grid_id, shape()),
        arithmetic_(),
        engine_(runtime_, function, arithmetic_) {}

  auto warp() -> Warp& {
    return runtime_.grid().cta(CtaId{grid_id, 0}).warp(0);
  }

  auto bind(LaneId lane, std::vector<RawWidth> widths)
      -> memory::RegisterFrameHandle {
    const auto frame =
        runtime_.registers().create_frame({.slot_widths = widths});
    EXPECT_TRUE(frame);
    EXPECT_TRUE(runtime_.bind_register_frame(warp().thread(lane).id(), function,
                                             *frame));
    return *frame;
  }

  auto view(memory::RegisterFrameHandle frame) -> memory::RegisterView {
    const auto result = runtime_.registers().view(frame);
    EXPECT_TRUE(result);
    return *result;
  }

  void expect_rejected(const WarpIssueGroup& group, StepErrorCode code) {
    const auto frame = bind(LaneId{0}, {RawWidth::b32, RawWidth::b32});
    auto registers = view(frame);
    ASSERT_TRUE(registers.write(RegisterSlot{0}, RawValue::b32(7U)));
    ASSERT_TRUE(registers.write(RegisterSlot{1}, RawValue::b32(9U)));
    auto& lane = warp().thread(LaneId{0});
    lane.set_pc(initial_pc);
    const auto original_status = lane.status();

    const auto result =
        engine_.execute(warp(), group, move_instruction, move_fallthrough);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, code);
    EXPECT_EQ(lane.pc(), initial_pc);
    EXPECT_EQ(lane.status(), original_status);
    EXPECT_EQ(*registers.read(RegisterSlot{0}), RawValue::b32(7U));
    EXPECT_EQ(*registers.read(RegisterSlot{1}), RawValue::b32(9U));
  }

  runtime::LaunchRuntime runtime_;
  arith::context arithmetic_;
  InstExecuteEngine engine_;
};

TEST_F(InstExecuteEngineTest, MovesOneLaneAndCommitsExplicitFallthrough) {
  const auto frame = bind(LaneId{0}, {RawWidth::b32, RawWidth::b32});
  auto registers = view(frame);
  ASSERT_TRUE(registers.write(RegisterSlot{0}, RawValue::b32(0x1234'5678U)));
  warp().thread(LaneId{0}).set_pc(initial_pc);

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}),
                                      move_instruction, move_fallthrough);

  ASSERT_TRUE(result);
  EXPECT_TRUE(result->faults.empty());
  EXPECT_EQ(*registers.read(RegisterSlot{1}), RawValue::b32(0x1234'5678U));
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), move_fallthrough);
  EXPECT_EQ(warp().thread(LaneId{0}).status(), ThreadStatus::Ready);
}

TEST_F(InstExecuteEngineTest, MovesTwoLanesThroughIsolatedFrames) {
  const auto first = bind(LaneId{0}, {RawWidth::b32, RawWidth::b32});
  const auto second = bind(LaneId{1}, {RawWidth::b32, RawWidth::b32});
  auto first_registers = view(first);
  auto second_registers = view(second);
  ASSERT_TRUE(first_registers.write(RegisterSlot{0}, RawValue::b32(1U)));
  ASSERT_TRUE(second_registers.write(RegisterSlot{0}, RawValue::b32(2U)));
  warp().thread(LaneId{0}).set_pc(initial_pc);
  warp().thread(LaneId{1}).set_pc(initial_pc);

  const auto result = engine_.execute(warp(), issue(initial_pc, {0, 1}),
                                      move_instruction, move_fallthrough);

  ASSERT_TRUE(result);
  EXPECT_TRUE(result->faults.empty());
  EXPECT_EQ(*first_registers.read(RegisterSlot{1}), RawValue::b32(1U));
  EXPECT_EQ(*second_registers.read(RegisterSlot{1}), RawValue::b32(2U));
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), move_fallthrough);
  EXPECT_EQ(warp().thread(LaneId{1}).pc(), move_fallthrough);
}

TEST_F(InstExecuteEngineTest, PreparesAllLanesBeforeCommittingSuccessfulOnes) {
  const auto initialized = bind(LaneId{0}, {RawWidth::b32, RawWidth::b32});
  const auto uninitialized = bind(LaneId{1}, {RawWidth::b32, RawWidth::b32});
  auto initialized_registers = view(initialized);
  auto uninitialized_registers = view(uninitialized);
  ASSERT_TRUE(initialized_registers.write(RegisterSlot{0}, RawValue::b32(5U)));
  warp().thread(LaneId{0}).set_pc(initial_pc);
  warp().thread(LaneId{1}).set_pc(initial_pc);

  const auto result = engine_.execute(warp(), issue(initial_pc, {0, 1}),
                                      move_instruction, move_fallthrough);

  ASSERT_TRUE(result);
  ASSERT_EQ(result->faults.size(), 1u);
  EXPECT_EQ(result->faults.front().lane, LaneId{1});
  EXPECT_EQ(std::get<memory::RegisterError>(result->faults.front().cause).code,
            memory::RegisterErrorCode::uninitialized_read);
  EXPECT_EQ(*initialized_registers.read(RegisterSlot{1}), RawValue::b32(5U));
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), move_fallthrough);
  EXPECT_EQ(warp().thread(LaneId{1}).pc(), initial_pc);
  EXPECT_EQ(warp().thread(LaneId{1}).status(), ThreadStatus::Trapped);
  EXPECT_FALSE(*uninitialized_registers.initialized(RegisterSlot{1}));
}

TEST_F(InstExecuteEngineTest, MissingBindingFaultsOnlyItsLane) {
  const auto frame = bind(LaneId{0}, {RawWidth::b32, RawWidth::b32});
  auto registers = view(frame);
  ASSERT_TRUE(registers.write(RegisterSlot{0}, RawValue::b32(5U)));
  warp().thread(LaneId{0}).set_pc(initial_pc);
  warp().thread(LaneId{1}).set_pc(initial_pc);

  const auto result = engine_.execute(warp(), issue(initial_pc, {0, 1}),
                                      move_instruction, move_fallthrough);

  ASSERT_TRUE(result);
  ASSERT_EQ(result->faults.size(), 1u);
  EXPECT_EQ(result->faults.front().lane, LaneId{1});
  EXPECT_EQ(
      std::get<runtime::RuntimeBindingError>(result->faults.front().cause).code,
      runtime::RuntimeBindingErrorCode::missing_binding);
  EXPECT_EQ(*registers.read(RegisterSlot{1}), RawValue::b32(5U));
  EXPECT_EQ(warp().thread(LaneId{1}).pc(), initial_pc);
  EXPECT_EQ(warp().thread(LaneId{1}).status(), ThreadStatus::Trapped);
}

TEST_F(InstExecuteEngineTest, StaleBindingFaultsWithoutWriting) {
  const auto frame = bind(LaneId{0}, {RawWidth::b32, RawWidth::b32});
  ASSERT_TRUE(runtime_.registers().destroy_frame(frame));
  warp().thread(LaneId{0}).set_pc(initial_pc);

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}),
                                      move_instruction, move_fallthrough);

  ASSERT_TRUE(result);
  ASSERT_EQ(result->faults.size(), 1u);
  EXPECT_EQ(result->faults.front().lane, LaneId{0});
  EXPECT_EQ(std::get<memory::RegisterError>(result->faults.front().cause).code,
            memory::RegisterErrorCode::stale_frame);
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), initial_pc);
  EXPECT_EQ(warp().thread(LaneId{0}).status(), ThreadStatus::Trapped);
}

TEST_F(InstExecuteEngineTest, RejectsEveryMalformedLocalIssueWithoutMutation) {
  WarpIssueGroup wrong_width{.pc = initial_pc, .lanes = LaneMask{3}};
  wrong_width.lanes.set(LaneId{0});
  expect_rejected(wrong_width, StepErrorCode::lane_mask_width);
}

TEST_F(InstExecuteEngineTest, RejectsEmptyIssueWithoutMutation) {
  expect_rejected({.pc = initial_pc, .lanes = LaneMask{4}},
                  StepErrorCode::empty_issue);
}

TEST_F(InstExecuteEngineTest, RejectsInvalidLaneWithoutMutation) {
  expect_rejected(issue(initial_pc, {2}), StepErrorCode::invalid_lane);
}

TEST_F(InstExecuteEngineTest, RejectsNonReadyLaneWithoutMutation) {
  warp().thread(LaneId{0}).mark_waiting();
  expect_rejected(issue(initial_pc, {0}), StepErrorCode::lane_not_ready);
}

TEST_F(InstExecuteEngineTest, RejectsPcMismatchWithoutMutation) {
  expect_rejected(issue(ProgramCounter{99}, {0}), StepErrorCode::pc_mismatch);
}

TEST(InstExecuteEngineForeignWarpTest,
     RejectsForeignRuntimeWarpWithoutMutation) {
  runtime::LaunchRuntime owner(grid_id, shape());
  runtime::LaunchRuntime foreign(GridId{8}, shape());
  arith::context arithmetic;
  InstExecuteEngine engine(owner, function, arithmetic);
  auto& warp = foreign.grid().cta(CtaId{GridId{8}, 0}).warp(0);
  warp.thread(LaneId{0}).set_pc(initial_pc);

  const auto result = engine.execute(warp, issue(initial_pc, {0}),
                                     move_instruction, move_fallthrough);

  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code, StepErrorCode::foreign_warp);
  EXPECT_EQ(warp.thread(LaneId{0}).pc(), initial_pc);
  EXPECT_EQ(warp.thread(LaneId{0}).status(), ThreadStatus::Ready);
}

TEST(InstExecuteEngineFacadeTest, ThreadAndWarpSingletonFacadesAreEquivalent) {
  runtime::LaunchRuntime thread_runtime(grid_id, shape());
  runtime::LaunchRuntime warp_runtime(GridId{8}, shape());
  arith::context arithmetic;
  InstExecuteEngine thread_engine(thread_runtime, function, arithmetic);
  InstExecuteEngine warp_engine(warp_runtime, function, arithmetic);
  BoundStepAdapter thread_adapter(thread_engine, move_instruction,
                                  move_fallthrough);
  BoundStepAdapter warp_adapter(warp_engine, move_instruction,
                                move_fallthrough);
  auto& thread_warp = thread_runtime.grid().cta(CtaId{grid_id, 0}).warp(0);
  auto& explicit_warp = warp_runtime.grid().cta(CtaId{GridId{8}, 0}).warp(0);
  const auto thread_frame = thread_runtime.registers().create_frame(
      {.slot_widths = {RawWidth::b32, RawWidth::b32}});
  const auto warp_frame = warp_runtime.registers().create_frame(
      {.slot_widths = {RawWidth::b32, RawWidth::b32}});
  ASSERT_TRUE(thread_frame);
  ASSERT_TRUE(warp_frame);
  ASSERT_TRUE(thread_runtime.bind_register_frame(
      thread_warp.thread(LaneId{0}).id(), function, *thread_frame));
  ASSERT_TRUE(warp_runtime.bind_register_frame(
      explicit_warp.thread(LaneId{0}).id(), function, *warp_frame));
  auto thread_registers = thread_runtime.registers().view(*thread_frame);
  auto warp_registers = warp_runtime.registers().view(*warp_frame);
  ASSERT_TRUE(thread_registers);
  ASSERT_TRUE(warp_registers);
  ASSERT_TRUE(thread_registers->write(RegisterSlot{0}, RawValue::b32(11U)));
  ASSERT_TRUE(warp_registers->write(RegisterSlot{0}, RawValue::b32(11U)));
  thread_warp.thread(LaneId{0}).set_pc(initial_pc);
  explicit_warp.thread(LaneId{0}).set_pc(initial_pc);

  const auto thread_result = thread_warp.thread(LaneId{0}).step(thread_adapter);
  const auto warp_result =
      explicit_warp.step(warp_adapter, issue(initial_pc, {0}));

  ASSERT_TRUE(thread_result);
  ASSERT_TRUE(warp_result);
  EXPECT_TRUE(thread_result->faults.empty());
  EXPECT_TRUE(warp_result->faults.empty());
  EXPECT_EQ(*thread_registers->read(RegisterSlot{1}), RawValue::b32(11U));
  EXPECT_EQ(*warp_registers->read(RegisterSlot{1}), RawValue::b32(11U));
  EXPECT_EQ(thread_warp.thread(LaneId{0}).pc(), move_fallthrough);
  EXPECT_EQ(explicit_warp.thread(LaneId{0}).pc(), move_fallthrough);
}

TEST_F(InstExecuteEngineTest, PredicateTrueExecutesMove) {
  const auto frame =
      bind(LaneId{0}, {RawWidth::b32, RawWidth::b32, RawWidth::pred});
  auto registers = view(frame);
  ASSERT_TRUE(registers.write(RegisterSlot{0}, RawValue::b32(17U)));
  ASSERT_TRUE(registers.write(RegisterSlot{2}, RawValue::pred(true)));
  warp().thread(LaneId{0}).set_pc(initial_pc);
  const ProbeInstruction instruction{
      .predicate = PredicateProbe{.source = RegisterSlot{2}},
      .operation = move,
  };

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}),
                                      instruction, ProgramCounter{43});

  ASSERT_TRUE(result);
  EXPECT_TRUE(result->faults.empty());
  EXPECT_EQ(*registers.read(RegisterSlot{1}), RawValue::b32(17U));
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), ProgramCounter{43});
}

TEST_F(InstExecuteEngineTest, FalsePredicateSuppressesInvalidOperand) {
  const auto frame =
      bind(LaneId{0}, {RawWidth::b32, RawWidth::b32, RawWidth::pred});
  auto registers = view(frame);
  ASSERT_TRUE(registers.write(RegisterSlot{1}, RawValue::b32(9U)));
  ASSERT_TRUE(registers.write(RegisterSlot{2}, RawValue::pred(false)));
  warp().thread(LaneId{0}).set_pc(initial_pc);
  const ProbeInstruction instruction{
      .predicate = PredicateProbe{.source = RegisterSlot{2}},
      .operation = move,
  };

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}),
                                      instruction, ProgramCounter{44});

  ASSERT_TRUE(result);
  EXPECT_TRUE(result->faults.empty());
  EXPECT_EQ(*registers.read(RegisterSlot{1}), RawValue::b32(9U));
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), ProgramCounter{44});
  EXPECT_EQ(warp().thread(LaneId{0}).status(), ThreadStatus::Ready);
}

TEST_F(InstExecuteEngineTest, NegatedFalsePredicateExecutesMove) {
  const auto frame =
      bind(LaneId{0}, {RawWidth::b32, RawWidth::b32, RawWidth::pred});
  auto registers = view(frame);
  ASSERT_TRUE(registers.write(RegisterSlot{0}, RawValue::b32(23U)));
  ASSERT_TRUE(registers.write(RegisterSlot{2}, RawValue::pred(false)));
  warp().thread(LaneId{0}).set_pc(initial_pc);
  const ProbeInstruction instruction{
      .predicate = PredicateProbe{.source = RegisterSlot{2}, .negated = true},
      .operation = move,
  };

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}),
                                      instruction, ProgramCounter{45});

  ASSERT_TRUE(result);
  EXPECT_TRUE(result->faults.empty());
  EXPECT_EQ(*registers.read(RegisterSlot{1}), RawValue::b32(23U));
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), ProgramCounter{45});
}

TEST_F(InstExecuteEngineTest, DispatchesAddWithRegisterAndImmediateOperands) {
  const auto frame =
      bind(LaneId{0}, {RawWidth::b32, RawWidth::b32, RawWidth::b32});
  auto registers = view(frame);
  ASSERT_TRUE(registers.write(RegisterSlot{0}, RawValue::b32(12U)));
  ASSERT_TRUE(registers.write(RegisterSlot{1}, RawValue::b32(30U)));
  warp().thread(LaneId{0}).set_pc(initial_pc);
  const ProbeInstruction register_add{
      .predicate = std::nullopt,
      .operation = AddProbe{.type = DataType::u32,
                            .destination = RegisterSlot{2},
                            .lhs = RegisterSlot{0},
                            .rhs = RegisterSlot{1}},
  };

  const auto register_result = engine_.execute(
      warp(), issue(initial_pc, {0}), register_add, ProgramCounter{46});

  ASSERT_TRUE(register_result);
  EXPECT_TRUE(register_result->faults.empty());
  EXPECT_EQ(*registers.read(RegisterSlot{2}), RawValue::b32(42U));
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), ProgramCounter{46});

  warp().thread(LaneId{0}).set_pc(initial_pc);
  const ProbeInstruction immediate_add{
      .predicate = std::nullopt,
      .operation = AddProbe{.type = DataType::u32,
                            .destination = RegisterSlot{2},
                            .lhs = RegisterSlot{0},
                            .rhs = RawValue::b32(8U)},
  };

  const auto immediate_result = engine_.execute(
      warp(), issue(initial_pc, {0}), immediate_add, ProgramCounter{47});

  ASSERT_TRUE(immediate_result);
  EXPECT_TRUE(immediate_result->faults.empty());
  EXPECT_EQ(*registers.read(RegisterSlot{2}), RawValue::b32(20U));
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), ProgramCounter{47});
}

TEST_F(InstExecuteEngineTest, AddWrapsU32WithoutArchitecturalStatus) {
  const auto frame = bind(LaneId{0}, {RawWidth::b32});
  auto registers = view(frame);
  warp().thread(LaneId{0}).set_pc(initial_pc);
  const ProbeInstruction instruction{
      .predicate = std::nullopt,
      .operation = AddProbe{.type = DataType::u32,
                            .destination = RegisterSlot{0},
                            .lhs = RawValue::b32(0xffff'ffffU),
                            .rhs = RawValue::b32(1U)},
  };

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}),
                                      instruction, ProgramCounter{48});

  ASSERT_TRUE(result);
  EXPECT_TRUE(result->faults.empty());
  EXPECT_EQ(*registers.read(RegisterSlot{0}), RawValue::b32(0U));
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), ProgramCounter{48});
}

TEST(InstExecuteEngineOperationTest, OpIdentityIgnoresDataType) {
  const ProbeOperation move_b32 = MoveProbe{
      .type = DataType::b32,
      .source = RegisterSlot{0},
      .destination = RegisterSlot{1},
  };
  const ProbeOperation move_u32 = MoveProbe{
      .type = DataType::u32,
      .source = RegisterSlot{0},
      .destination = RegisterSlot{1},
  };
  const ProbeOperation add_u32 = AddProbe{
      .type = DataType::u32,
      .destination = RegisterSlot{0},
      .lhs = RegisterSlot{1},
      .rhs = RegisterSlot{2},
  };
  const ProbeOperation add_b32 = AddProbe{
      .type = DataType::b32,
      .destination = RegisterSlot{0},
      .lhs = RegisterSlot{1},
      .rhs = RegisterSlot{2},
  };

  EXPECT_EQ(op(move_b32), Op::mov);
  EXPECT_EQ(op(move_u32), Op::mov);
  EXPECT_EQ(op(add_u32), Op::add);
  EXPECT_EQ(op(add_b32), Op::add);
}

TEST_F(InstExecuteEngineTest,
       UnsupportedAddTypeRejectsBeforeAnyLaneStateMutation) {
  const auto frame =
      bind(LaneId{0}, {RawWidth::b32, RawWidth::b32, RawWidth::b32});
  auto registers = view(frame);
  ASSERT_TRUE(registers.write(RegisterSlot{0}, RawValue::b32(7U)));
  ASSERT_TRUE(registers.write(RegisterSlot{1}, RawValue::b32(9U)));
  ASSERT_TRUE(registers.write(RegisterSlot{2}, RawValue::b32(11U)));
  auto& thread = warp().thread(LaneId{0});
  thread.set_pc(initial_pc);

  const ProbeInstruction instruction{
      .predicate = std::nullopt,
      .operation = AddProbe{.type = DataType::b32,
                            .destination = RegisterSlot{2},
                            .lhs = RegisterSlot{0},
                            .rhs = RegisterSlot{1}},
  };
  const auto result = engine_.execute(warp(), issue(initial_pc, {0}),
                                      instruction, ProgramCounter{56});

  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code, StepErrorCode::unsupported_instruction);
  EXPECT_EQ(thread.pc(), initial_pc);
  EXPECT_EQ(thread.status(), ThreadStatus::Ready);
  EXPECT_EQ(*registers.read(RegisterSlot{0}), RawValue::b32(7U));
  EXPECT_EQ(*registers.read(RegisterSlot{1}), RawValue::b32(9U));
  EXPECT_EQ(*registers.read(RegisterSlot{2}), RawValue::b32(11U));
}

TEST_F(InstExecuteEngineTest, WidthMismatchFaultsBeforeWrite) {
  const auto frame = bind(LaneId{0}, {RawWidth::b32, RawWidth::b16});
  auto registers = view(frame);
  ASSERT_TRUE(registers.write(RegisterSlot{0}, RawValue::b32(5U)));
  ASSERT_TRUE(
      registers.write(RegisterSlot{1}, RawValue::b16(std::uint16_t{9})));
  warp().thread(LaneId{0}).set_pc(initial_pc);

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}),
                                      move_instruction, move_fallthrough);

  ASSERT_TRUE(result);
  ASSERT_EQ(result->faults.size(), 1u);
  EXPECT_EQ(std::get<common::RawValueError>(result->faults.front().cause),
            (common::RawValueError{RawWidth::b32, RawWidth::b16}));
  EXPECT_EQ(*registers.read(RegisterSlot{1}), RawValue::b16(std::uint16_t{9}));
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), initial_pc);
  EXPECT_EQ(warp().thread(LaneId{0}).status(), ThreadStatus::Trapped);
}

TEST_F(InstExecuteEngineTest, UnboundBranchTakesItsDirectTarget) {
  const ProbeInstruction instruction{
      .predicate = std::nullopt,
      .operation = BranchProbe{.target = ProgramCounter{80}},
  };
  warp().thread(LaneId{0}).set_pc(initial_pc);

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}),
                                      instruction, ProgramCounter{49});

  ASSERT_TRUE(result);
  EXPECT_TRUE(result->faults.empty());
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), ProgramCounter{80});
  EXPECT_EQ(warp().thread(LaneId{0}).status(), ThreadStatus::Ready);
}

TEST_F(InstExecuteEngineTest, FalsePredicateMakesBranchFallThrough) {
  const auto frame = bind(LaneId{0}, {RawWidth::pred});
  auto registers = view(frame);
  ASSERT_TRUE(registers.write(RegisterSlot{0}, RawValue::pred(false)));
  const ProbeInstruction instruction{
      .predicate = PredicateProbe{.source = RegisterSlot{0}},
      .operation = BranchProbe{.target = ProgramCounter{80}},
  };
  warp().thread(LaneId{0}).set_pc(initial_pc);

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}),
                                      instruction, ProgramCounter{50});

  ASSERT_TRUE(result);
  EXPECT_TRUE(result->faults.empty());
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), ProgramCounter{50});
  EXPECT_EQ(warp().thread(LaneId{0}).status(), ThreadStatus::Ready);
}

TEST(InstExecuteEngineBranchTest, DivergentBranchLeavesReadyLanesGroupedByPc) {
  runtime::LaunchRuntime runtime(grid_id, three_lane_shape());
  arith::context arithmetic;
  const ProbeInstruction instruction{
      .predicate = PredicateProbe{.source = RegisterSlot{0}},
      .operation = BranchProbe{.target = ProgramCounter{81}},
  };
  InstExecuteEngine engine(runtime, function, arithmetic);
  auto& warp = runtime.grid().cta(CtaId{grid_id, 0}).warp(0);
  for (const auto lane : {LaneId{0}, LaneId{1}}) {
    const auto frame =
        runtime.registers().create_frame({.slot_widths = {RawWidth::pred}});
    ASSERT_TRUE(frame);
    ASSERT_TRUE(
        runtime.bind_register_frame(warp.thread(lane).id(), function, *frame));
    auto registers = runtime.registers().view(*frame);
    ASSERT_TRUE(registers);
    ASSERT_TRUE(
        registers->write(RegisterSlot{0}, RawValue::pred(lane == LaneId{0})));
    warp.thread(lane).set_pc(initial_pc);
  }
  warp.thread(LaneId{2}).set_pc(ProgramCounter{77});

  const auto result = engine.execute(warp, issue(initial_pc, {0, 1}),
                                     instruction, ProgramCounter{51});

  ASSERT_TRUE(result);
  EXPECT_TRUE(result->faults.empty());
  EXPECT_EQ(warp.thread(LaneId{0}).pc(), ProgramCounter{81});
  EXPECT_EQ(warp.thread(LaneId{1}).pc(), ProgramCounter{51});
  EXPECT_EQ(warp.thread(LaneId{2}).pc(), ProgramCounter{77});
  const auto groups = ready_lanes_by_pc(warp);
  ASSERT_EQ(groups.size(), 3u);
  EXPECT_TRUE(groups.at(ProgramCounter{81}).test(LaneId{0}));
  EXPECT_TRUE(groups.at(ProgramCounter{51}).test(LaneId{1}));
  EXPECT_TRUE(groups.at(ProgramCounter{77}).test(LaneId{2}));
}

TEST_F(InstExecuteEngineTest, BadBranchPredicateTrapsWithoutChangingPc) {
  const auto frame = bind(LaneId{0}, {RawWidth::b32});
  auto registers = view(frame);
  ASSERT_TRUE(registers.write(RegisterSlot{0}, RawValue::b32(1U)));
  const ProbeInstruction instruction{
      .predicate = PredicateProbe{.source = RegisterSlot{0}},
      .operation = BranchProbe{.target = ProgramCounter{82}},
  };
  warp().thread(LaneId{0}).set_pc(initial_pc);

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}),
                                      instruction, ProgramCounter{52});

  ASSERT_TRUE(result);
  ASSERT_EQ(result->faults.size(), 1u);
  EXPECT_EQ(std::get<common::RawValueError>(result->faults.front().cause),
            (common::RawValueError{RawWidth::pred, RawWidth::b32}));
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), initial_pc);
  EXPECT_EQ(warp().thread(LaneId{0}).status(), ThreadStatus::Trapped);
}

TEST_F(InstExecuteEngineTest,
       UninitializedBranchPredicateTrapsWithoutChangingPc) {
  bind(LaneId{0}, {RawWidth::pred});
  const ProbeInstruction instruction{
      .predicate = PredicateProbe{.source = RegisterSlot{0}},
      .operation = BranchProbe{.target = ProgramCounter{83}},
  };
  warp().thread(LaneId{0}).set_pc(initial_pc);

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}),
                                      instruction, ProgramCounter{53});

  ASSERT_TRUE(result);
  ASSERT_EQ(result->faults.size(), 1u);
  EXPECT_EQ(std::get<memory::RegisterError>(result->faults.front().cause).code,
            memory::RegisterErrorCode::uninitialized_read);
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), initial_pc);
  EXPECT_EQ(warp().thread(LaneId{0}).status(), ThreadStatus::Trapped);
}

TEST_F(InstExecuteEngineTest, UnboundExitExitsWithoutChangingPc) {
  const ProbeInstruction instruction{
      .predicate = std::nullopt,
      .operation = ExitProbe{},
  };
  warp().thread(LaneId{0}).set_pc(initial_pc);

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}),
                                      instruction, ProgramCounter{54});

  ASSERT_TRUE(result);
  EXPECT_TRUE(result->faults.empty());
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), initial_pc);
  EXPECT_EQ(warp().thread(LaneId{0}).status(), ThreadStatus::Exited);
  EXPECT_FALSE(warp().ready_mask().test(LaneId{0}));
  EXPECT_TRUE(warp().exited_mask().test(LaneId{0}));
}

TEST_F(InstExecuteEngineTest, PredicatedOffExitFallsThroughAndStaysReady) {
  const auto frame = bind(LaneId{0}, {RawWidth::pred});
  auto registers = view(frame);
  ASSERT_TRUE(registers.write(RegisterSlot{0}, RawValue::pred(false)));
  const ProbeInstruction instruction{
      .predicate = PredicateProbe{.source = RegisterSlot{0}},
      .operation = ExitProbe{},
  };
  warp().thread(LaneId{0}).set_pc(initial_pc);

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}),
                                      instruction, ProgramCounter{55});

  ASSERT_TRUE(result);
  EXPECT_TRUE(result->faults.empty());
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), ProgramCounter{55});
  EXPECT_EQ(warp().thread(LaneId{0}).status(), ThreadStatus::Ready);
}

}  // namespace
}  // namespace ptxsim::inst_execute_engine::test
