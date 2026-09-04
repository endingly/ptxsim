#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <initializer_list>
#include <map>
#include <utility>
#include <variant>
#include <vector>

#include <ptxsim/inst_execute_engine/inst_execute_engine.hpp>

namespace ptxsim::inst_execute_engine::test {
namespace {

using common::CodeLocation;
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
constexpr FunctionId function{0};
constexpr ProgramCounter initial_pc{10};
constexpr ProgramCounter move_fallthrough{42};

/** @brief Build a generated move instruction with the requested predicate. */
auto mov(std::optional<exec_ir::Predicate> predicate, exec_ir::DataType type,
         RegisterSlot destination, RegisterSlot source)
    -> exec_ir::Instruction {
  exec_ir::Mov::Scalar::ScalarOperands operands{destination, source};
  exec_ir::Mov::Scalar form{type, operands};
  return exec_ir::Mov{std::move(predicate), exec_ir::Mov::Variant{form}};
}
/** @brief Build a generated integer add instruction with bound operands. */
auto add(std::optional<exec_ir::Predicate> predicate, exec_ir::DataType type,
         RegisterSlot destination, exec_ir::B32Operand lhs,
         exec_ir::B32Operand rhs) -> exec_ir::Instruction {
  exec_ir::Add::IntegerNoSat form{type, destination, std::move(lhs),
                                  std::move(rhs)};
  return exec_ir::Add{std::move(predicate), exec_ir::Add::Variant{form}};
}
/** @brief Build a generated scalar load using its selected address space. */
auto make_load(std::optional<exec_ir::Predicate> predicate,
               exec_ir::DataType type, exec_ir::AddressSpace space,
               RegisterSlot destination, RegisterSlot address)
    -> exec_ir::Instruction {
  if (space == exec_ir::AddressSpace::generic) {
    exec_ir::Ld::GenericScalar form{exec_ir::MemoryConsistency::omitted,
                                    exec_ir::MemoryScope::none,
                                    false,
                                    exec_ir::CacheOperator::unspecified,
                                    type,
                                    destination,
                                    address};
    return exec_ir::Ld{std::move(predicate), exec_ir::Ld::Variant{form}};
  }
  exec_ir::Ld::ExplicitScalar form{space,
                                   exec_ir::CacheOperator::unspecified,
                                   exec_ir::MemoryConsistency::omitted,
                                   exec_ir::MemoryScope::none,
                                   false,
                                   type,
                                   destination,
                                   address};
  return exec_ir::Ld{std::move(predicate), exec_ir::Ld::Variant{form}};
}
/** @brief Build a generated scalar store using its selected address space. */
auto make_store(std::optional<exec_ir::Predicate> predicate,
                exec_ir::DataType type, exec_ir::AddressSpace space,
                RegisterSlot address, RegisterSlot source)
    -> exec_ir::Instruction {
  if (space == exec_ir::AddressSpace::generic) {
    exec_ir::St::GenericScalar form{exec_ir::MemoryConsistency::omitted,
                                    exec_ir::MemoryScope::none,
                                    false,
                                    exec_ir::CacheOperator::unspecified,
                                    type,
                                    address,
                                    source};
    return exec_ir::St{std::move(predicate), exec_ir::St::Variant{form}};
  }
  exec_ir::St::ExplicitScalar form{space,
                                   exec_ir::CacheOperator::unspecified,
                                   exec_ir::MemoryConsistency::omitted,
                                   exec_ir::MemoryScope::none,
                                   false,
                                   type,
                                   address,
                                   source};
  return exec_ir::St{std::move(predicate), exec_ir::St::Variant{form}};
}
/** @brief Build a generated warp-synchronization instruction. */
auto make_bar(std::optional<exec_ir::Predicate> predicate,
              exec_ir::B32Operand mask) -> exec_ir::Instruction {
  exec_ir::Bar::WarpSync form{std::move(mask)};
  return exec_ir::Bar{std::move(predicate), exec_ir::Bar::Variant{form}};
}
/** @brief Build a generated direct branch instruction. */
auto bra(std::optional<exec_ir::Predicate> predicate, ProgramCounter target)
    -> exec_ir::Instruction {
  exec_ir::Bra::Direct form{false, target};
  return exec_ir::Bra{std::move(predicate), exec_ir::Bra::Variant{form}};
}
/** @brief Build a generated exit instruction. */
auto exit(std::optional<exec_ir::Predicate> predicate = std::nullopt)
    -> exec_ir::Instruction {
  return exec_ir::Exit{std::move(predicate),
                       exec_ir::Exit::Variant{exec_ir::Exit::Bare{}}};
}

const auto move_instruction =
    mov(std::nullopt, exec_ir::DataType::b32, RegisterSlot{1}, RegisterSlot{0});

auto executable_move_program()
    -> std::expected<exec_ir::ExecutableProgram, exec_ir::ProgramError> {
  return exec_ir::ExecutableProgram::create({
      .instructions = {move_instruction, exit()},
      .functions = {{function, 0, 2, {RawWidth::b32, RawWidth::b32}}},
  });
}

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
                   const exec_ir::Instruction& instruction,
                   ProgramCounter fallthrough) noexcept
      : engine_(engine), instruction_(instruction), fallthrough_(fallthrough) {}

  auto step(Warp& warp, const WarpIssueGroup& group)
      -> std::expected<StepReport, StepError> {
    return engine_.execute(warp, group, instruction_, fallthrough_);
  }

 private:
  InstExecuteEngine& engine_;
  const exec_ir::Instruction& instruction_;
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

TEST_F(InstExecuteEngineTest,
       ExecutesFetchedExecutableInstructionWithItsSuccessor) {
  const auto frame = bind(LaneId{0}, {RawWidth::b32, RawWidth::b32});
  auto registers = view(frame);
  ASSERT_TRUE(registers.write(RegisterSlot{0}, RawValue::b32(0x1234U)));
  auto program = executable_move_program();
  ASSERT_TRUE(program);
  const CodeLocation location{function, ProgramCounter{0}};
  const auto instruction = program->fetch(location);
  const auto successor = program->fallthrough(location);
  ASSERT_TRUE(instruction);
  ASSERT_TRUE(successor);
  warp().thread(LaneId{0}).set_pc(location.pc);

  const auto result = engine_.execute(warp(), issue(location.pc, {0}),
                                      instruction->get(), successor->pc);

  ASSERT_TRUE(result);
  EXPECT_TRUE(result->faults.empty());
  EXPECT_EQ(*registers.read(RegisterSlot{1}), RawValue::b32(0x1234U));
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), successor->pc);
}

TEST_F(InstExecuteEngineTest, MissingFallthroughRejectsBeforeAnyLaneMutation) {
  const auto frame = bind(LaneId{0}, {RawWidth::b32, RawWidth::b32});
  auto registers = view(frame);
  ASSERT_TRUE(registers.write(RegisterSlot{0}, RawValue::b32(7U)));
  ASSERT_TRUE(registers.write(RegisterSlot{1}, RawValue::b32(9U)));
  auto& thread = warp().thread(LaneId{0});
  thread.set_pc(initial_pc);

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}),
                                      move_instruction, std::nullopt);

  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code, StepErrorCode::missing_fallthrough);
  EXPECT_EQ(thread.pc(), initial_pc);
  EXPECT_EQ(thread.status(), ThreadStatus::Ready);
  EXPECT_EQ(*registers.read(RegisterSlot{0}), RawValue::b32(7U));
  EXPECT_EQ(*registers.read(RegisterSlot{1}), RawValue::b32(9U));
}

TEST_F(InstExecuteEngineTest,
       UnsupportedInstructionPrecedesMissingFallthroughWithoutMutation) {
  const auto frame =
      bind(LaneId{0}, {RawWidth::b32, RawWidth::b32, RawWidth::b32});
  auto registers = view(frame);
  ASSERT_TRUE(registers.write(RegisterSlot{0}, RawValue::b32(7U)));
  auto& thread = warp().thread(LaneId{0});
  thread.set_pc(initial_pc);
  const exec_ir::Sub::IntegerNoSat form{exec_ir::DataType::u32, RegisterSlot{0},
                                        RegisterSlot{1}, RegisterSlot{2}};
  const exec_ir::Instruction instruction{
      exec_ir::Sub{std::nullopt, exec_ir::Sub::Variant{form}}};

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}),
                                      instruction, std::nullopt);

  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code, StepErrorCode::unsupported_instruction);
  EXPECT_EQ(thread.pc(), initial_pc);
  EXPECT_EQ(thread.status(), ThreadStatus::Ready);
  EXPECT_EQ(*registers.read(RegisterSlot{0}), RawValue::b32(7U));
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
  warp().thread(LaneId{0}).mark_waiting(execution_model::WaitReason::Other);
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
  const exec_ir::Instruction instruction =
      mov(exec_ir::Predicate{.source = RegisterSlot{2}}, exec_ir::DataType::b32,
          RegisterSlot{1}, RegisterSlot{0});

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
  const exec_ir::Instruction instruction =
      mov(exec_ir::Predicate{.source = RegisterSlot{2}}, exec_ir::DataType::b32,
          RegisterSlot{1}, RegisterSlot{0});

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
  const exec_ir::Instruction instruction =
      mov(exec_ir::Predicate{.source = RegisterSlot{2}, .negated = true},
          exec_ir::DataType::b32, RegisterSlot{1}, RegisterSlot{0});

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
  const exec_ir::Instruction register_add =
      add(std::nullopt, exec_ir::DataType::u32, RegisterSlot{2},
          RegisterSlot{0}, RegisterSlot{1});

  const auto register_result = engine_.execute(
      warp(), issue(initial_pc, {0}), register_add, ProgramCounter{46});

  ASSERT_TRUE(register_result);
  EXPECT_TRUE(register_result->faults.empty());
  EXPECT_EQ(*registers.read(RegisterSlot{2}), RawValue::b32(42U));
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), ProgramCounter{46});

  warp().thread(LaneId{0}).set_pc(initial_pc);
  const exec_ir::Instruction immediate_add =
      add(std::nullopt, exec_ir::DataType::u32, RegisterSlot{2},
          RegisterSlot{0}, RawValue::b32(8U));

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
  const exec_ir::Instruction instruction =
      add(std::nullopt, exec_ir::DataType::u32, RegisterSlot{0},
          RawValue::b32(0xffff'ffffU), RawValue::b32(1U));

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}),
                                      instruction, ProgramCounter{48});

  ASSERT_TRUE(result);
  EXPECT_TRUE(result->faults.empty());
  EXPECT_EQ(*registers.read(RegisterSlot{0}), RawValue::b32(0U));
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), ProgramCounter{48});
}

TEST(InstExecuteEngineOperationTest, OpIdentityIgnoresDataType) {
  const auto move_b32 = mov(std::nullopt, exec_ir::DataType::b32,
                            RegisterSlot{1}, RegisterSlot{0});
  const auto move_u32 = mov(std::nullopt, exec_ir::DataType::u32,
                            RegisterSlot{1}, RegisterSlot{0});
  const auto add_u32 = add(std::nullopt, exec_ir::DataType::u32,
                           RegisterSlot{0}, RegisterSlot{1}, RegisterSlot{2});
  const auto add_b32 = add(std::nullopt, exec_ir::DataType::b32,
                           RegisterSlot{0}, RegisterSlot{1}, RegisterSlot{2});

  EXPECT_EQ(exec_ir::op(move_b32), exec_ir::Op::mov);
  EXPECT_EQ(exec_ir::op(move_u32), exec_ir::Op::mov);
  EXPECT_EQ(exec_ir::op(add_u32), exec_ir::Op::add);
  EXPECT_EQ(exec_ir::op(add_b32), exec_ir::Op::add);
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

  const exec_ir::Instruction instruction =
      add(std::nullopt, exec_ir::DataType::b32, RegisterSlot{2},
          RegisterSlot{0}, RegisterSlot{1});
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

TEST_F(InstExecuteEngineTest, UnpredicatedBranchDoesNotNeedSuccessor) {
  const exec_ir::Instruction instruction =
      bra(std::nullopt, ProgramCounter{80});
  warp().thread(LaneId{0}).set_pc(initial_pc);

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}),
                                      instruction, std::nullopt);

  ASSERT_TRUE(result);
  EXPECT_TRUE(result->faults.empty());
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), ProgramCounter{80});
  EXPECT_EQ(warp().thread(LaneId{0}).status(), ThreadStatus::Ready);
}

TEST_F(InstExecuteEngineTest,
       PredicatedBranchWithoutSuccessorRejectsBeforePreparation) {
  const auto frame = bind(LaneId{0}, {RawWidth::pred});
  auto registers = view(frame);
  ASSERT_TRUE(registers.write(RegisterSlot{0}, RawValue::pred(true)));
  const exec_ir::Instruction instruction =
      bra(exec_ir::Predicate{.source = RegisterSlot{0}}, ProgramCounter{80});
  auto& thread = warp().thread(LaneId{0});
  thread.set_pc(initial_pc);

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}),
                                      instruction, std::nullopt);

  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code, StepErrorCode::missing_fallthrough);
  EXPECT_EQ(thread.pc(), initial_pc);
  EXPECT_EQ(thread.status(), ThreadStatus::Ready);
}

TEST_F(InstExecuteEngineTest, FalsePredicateMakesBranchFallThrough) {
  const auto frame = bind(LaneId{0}, {RawWidth::pred});
  auto registers = view(frame);
  ASSERT_TRUE(registers.write(RegisterSlot{0}, RawValue::pred(false)));
  const exec_ir::Instruction instruction =
      bra(exec_ir::Predicate{.source = RegisterSlot{0}}, ProgramCounter{80});
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
  const exec_ir::Instruction instruction =
      bra(exec_ir::Predicate{.source = RegisterSlot{0}}, ProgramCounter{81});
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
  const exec_ir::Instruction instruction =
      bra(exec_ir::Predicate{.source = RegisterSlot{0}}, ProgramCounter{82});
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
  const exec_ir::Instruction instruction =
      bra(exec_ir::Predicate{.source = RegisterSlot{0}}, ProgramCounter{83});
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

TEST_F(InstExecuteEngineTest, UnpredicatedExitDoesNotNeedSuccessor) {
  const exec_ir::Instruction instruction = exit(std::nullopt);
  warp().thread(LaneId{0}).set_pc(initial_pc);

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}),
                                      instruction, std::nullopt);

  ASSERT_TRUE(result);
  EXPECT_TRUE(result->faults.empty());
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), initial_pc);
  EXPECT_EQ(warp().thread(LaneId{0}).status(), ThreadStatus::Exited);
  EXPECT_FALSE(warp().ready_mask().test(LaneId{0}));
  EXPECT_TRUE(warp().exited_mask().test(LaneId{0}));
}

TEST_F(InstExecuteEngineTest,
       PredicatedExitWithoutSuccessorRejectsBeforePreparation) {
  const auto frame = bind(LaneId{0}, {RawWidth::pred});
  auto registers = view(frame);
  ASSERT_TRUE(registers.write(RegisterSlot{0}, RawValue::pred(true)));
  const exec_ir::Instruction instruction =
      exit(exec_ir::Predicate{.source = RegisterSlot{0}});
  auto& thread = warp().thread(LaneId{0});
  thread.set_pc(initial_pc);

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}),
                                      instruction, std::nullopt);

  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code, StepErrorCode::missing_fallthrough);
  EXPECT_EQ(thread.pc(), initial_pc);
  EXPECT_EQ(thread.status(), ThreadStatus::Ready);
}

TEST_F(InstExecuteEngineTest, PredicatedOffExitFallsThroughAndStaysReady) {
  const auto frame = bind(LaneId{0}, {RawWidth::pred});
  auto registers = view(frame);
  ASSERT_TRUE(registers.write(RegisterSlot{0}, RawValue::pred(false)));
  const exec_ir::Instruction instruction =
      exit(exec_ir::Predicate{.source = RegisterSlot{0}});
  warp().thread(LaneId{0}).set_pc(initial_pc);

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}),
                                      instruction, ProgramCounter{55});

  ASSERT_TRUE(result);
  EXPECT_TRUE(result->faults.empty());
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), ProgramCounter{55});
  EXPECT_EQ(warp().thread(LaneId{0}).status(), ThreadStatus::Ready);
}

TEST_F(InstExecuteEngineTest, LoadsAndStoresLittleEndianGlobalU32) {
  const auto global = runtime_.address_spaces().create_global({8});
  ASSERT_TRUE(runtime_.bind_global(global));
  auto memory = runtime_.address_spaces().view(global);
  ASSERT_TRUE(memory);
  ASSERT_TRUE(memory->initialize(memory::Address{0},
                                 std::array{std::byte{0x78}, std::byte{0x56},
                                            std::byte{0x34}, std::byte{0x12}}));
  const auto frame = bind(LaneId{0}, {RawWidth::b32, RawWidth::b64});
  auto registers = view(frame);
  ASSERT_TRUE(
      registers.write(RegisterSlot{1}, RawValue::b64(std::uint64_t{0})));
  warp().thread(LaneId{0}).set_pc(initial_pc);
  const exec_ir::Instruction load = make_load(
      std::nullopt, exec_ir::DataType::u32, exec_ir::AddressSpace::generic,
      RegisterSlot{0}, RegisterSlot{1});
  ASSERT_TRUE(engine_.execute(warp(), issue(initial_pc, {0}), load,
                              ProgramCounter{56}));
  EXPECT_EQ(*registers.read(RegisterSlot{0}), RawValue::b32(0x12345678U));

  warp().thread(LaneId{0}).set_pc(initial_pc);
  ASSERT_TRUE(registers.write(RegisterSlot{0}, RawValue::b32(0xaabbccddU)));
  const exec_ir::Instruction store = make_store(
      std::nullopt, exec_ir::DataType::u32, exec_ir::AddressSpace::global,
      RegisterSlot{1}, RegisterSlot{0});
  const auto result = engine_.execute(warp(), issue(initial_pc, {0}), store,
                                      ProgramCounter{57});
  ASSERT_TRUE(result);
  EXPECT_TRUE(result->faults.empty());
  EXPECT_EQ(*memory->snapshot(memory::Address{0}, 4),
            (std::vector<std::byte>{std::byte{0xdd}, std::byte{0xcc},
                                    std::byte{0xbb}, std::byte{0xaa}}));
}

TEST_F(InstExecuteEngineTest,
       GenericConstantStoreFaultsAndPredicateSuppressesAddress) {
  const auto constant = runtime_.address_spaces().create_constant({4});
  ASSERT_TRUE(runtime_.bind_constant(constant));
  auto constant_view = runtime_.address_spaces().view(constant);
  ASSERT_TRUE(constant_view);
  const std::array bytes{std::byte{1}, std::byte{2}, std::byte{3},
                         std::byte{4}};
  ASSERT_TRUE(constant_view->initialize(memory::Address{0}, bytes));
  const auto frame =
      bind(LaneId{0}, {RawWidth::b32, RawWidth::b64, RawWidth::pred});
  auto registers = view(frame);
  ASSERT_TRUE(registers.write(RegisterSlot{2}, RawValue::pred(false)));
  warp().thread(LaneId{0}).set_pc(initial_pc);
  const exec_ir::Instruction predicated_store = make_store(
      exec_ir::Predicate{RegisterSlot{2}}, exec_ir::DataType::u32,
      exec_ir::AddressSpace::generic, RegisterSlot{1}, RegisterSlot{0});
  const auto predicated = engine_.execute(warp(), issue(initial_pc, {0}),
                                          predicated_store, ProgramCounter{59});
  ASSERT_TRUE(predicated);
  EXPECT_TRUE(predicated->faults.empty());
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), ProgramCounter{59});

  ASSERT_TRUE(registers.write(
      RegisterSlot{1},
      RawValue::b64(memory::GenericAddressLayout::constant_base)));
  ASSERT_TRUE(registers.write(RegisterSlot{0}, RawValue::b32(9U)));
  warp().thread(LaneId{0}).set_pc(initial_pc);
  const exec_ir::Instruction store = make_store(
      std::nullopt, exec_ir::DataType::u32, exec_ir::AddressSpace::generic,
      RegisterSlot{1}, RegisterSlot{0});
  const auto result = engine_.execute(warp(), issue(initial_pc, {0}), store,
                                      ProgramCounter{58});
  ASSERT_TRUE(result);
  ASSERT_EQ(result->faults.size(), 1U);
  EXPECT_EQ(std::get<memory::AddressSpaceError>(result->faults[0].cause)
                .memory_error->code,
            memory::MemoryErrorCode::WriteToReadOnlyRegion);
  EXPECT_EQ(*constant_view->snapshot(memory::Address{0}, 4),
            (std::vector<std::byte>{bytes.begin(), bytes.end()}));
}

TEST_F(InstExecuteEngineTest, LoadFaultsRetainPcAndDestination) {
  const auto global = runtime_.address_spaces().create_global({4});
  ASSERT_TRUE(runtime_.bind_global(global));
  const auto frame = bind(LaneId{0}, {RawWidth::b32, RawWidth::b64});
  auto registers = view(frame);
  ASSERT_TRUE(registers.write(RegisterSlot{0}, RawValue::b32(77U)));
  const exec_ir::Instruction load = make_load(
      std::nullopt, exec_ir::DataType::u32, exec_ir::AddressSpace::global,
      RegisterSlot{0}, RegisterSlot{1});
  const auto expect_storage_fault = [&](std::uint64_t address,
                                        memory::MemoryErrorCode code) {
    ASSERT_TRUE(registers.write(RegisterSlot{1}, RawValue::b64(address)));
    auto& thread = warp().thread(LaneId{0});
    thread.mark_ready();
    thread.set_pc(initial_pc);
    const auto result = engine_.execute(warp(), issue(initial_pc, {0}), load,
                                        ProgramCounter{60});
    ASSERT_TRUE(result);
    ASSERT_EQ(result->faults.size(), 1U);
    const auto& error =
        std::get<memory::AddressSpaceError>(result->faults[0].cause);
    ASSERT_TRUE(error.memory_error);
    EXPECT_EQ(error.memory_error->code, code);
    EXPECT_EQ(*registers.read(RegisterSlot{0}), RawValue::b32(77U));
    EXPECT_EQ(thread.pc(), initial_pc);
    EXPECT_EQ(thread.status(), ThreadStatus::Trapped);
  };
  expect_storage_fault(0, memory::MemoryErrorCode::UninitializedRead);
  expect_storage_fault(1, memory::MemoryErrorCode::Misaligned);
  expect_storage_fault(4, memory::MemoryErrorCode::OutOfBounds);

  ASSERT_TRUE(runtime_.address_spaces().destroy(global));
  auto& thread = warp().thread(LaneId{0});
  thread.mark_ready();
  thread.set_pc(initial_pc);
  const auto stale =
      engine_.execute(warp(), issue(initial_pc, {0}), load, ProgramCounter{60});
  ASSERT_TRUE(stale);
  ASSERT_EQ(stale->faults.size(), 1U);
  EXPECT_EQ(std::get<memory::AddressSpaceError>(stale->faults[0].cause).code,
            memory::AddressSpaceErrorCode::stale_resource);
  EXPECT_EQ(*registers.read(RegisterSlot{0}), RawValue::b32(77U));
  EXPECT_EQ(thread.pc(), initial_pc);
  EXPECT_EQ(thread.status(), ThreadStatus::Trapped);
}

TEST_F(InstExecuteEngineTest, MissingGlobalBindingFaultsOnlyTheLane) {
  const auto frame = bind(LaneId{0}, {RawWidth::b32, RawWidth::b64});
  auto registers = view(frame);
  ASSERT_TRUE(registers.write(RegisterSlot{0}, RawValue::b32(77U)));
  ASSERT_TRUE(
      registers.write(RegisterSlot{1}, RawValue::b64(std::uint64_t{0})));
  warp().thread(LaneId{0}).set_pc(initial_pc);
  const exec_ir::Instruction load = make_load(
      std::nullopt, exec_ir::DataType::u32, exec_ir::AddressSpace::global,
      RegisterSlot{0}, RegisterSlot{1});
  const auto result =
      engine_.execute(warp(), issue(initial_pc, {0}), load, ProgramCounter{61});
  ASSERT_TRUE(result);
  ASSERT_EQ(result->faults.size(), 1U);
  EXPECT_EQ(
      std::get<runtime::RuntimeBindingError>(result->faults[0].cause).code,
      runtime::RuntimeBindingErrorCode::missing_binding);
  EXPECT_EQ(*registers.read(RegisterSlot{0}), RawValue::b32(77U));
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), initial_pc);
  EXPECT_EQ(warp().thread(LaneId{0}).status(), ThreadStatus::Trapped);
}

TEST_F(InstExecuteEngineTest,
       StoresIsolateFaultsHonorPartialMasksAndCommitByAscendingLane) {
  const auto global = runtime_.address_spaces().create_global({4});
  ASSERT_TRUE(runtime_.bind_global(global));
  auto memory = runtime_.address_spaces().view(global);
  ASSERT_TRUE(memory);
  const auto first = bind(LaneId{0}, {RawWidth::b32, RawWidth::b64});
  const auto second = bind(LaneId{1}, {RawWidth::b32, RawWidth::b64});
  auto first_registers = view(first);
  auto second_registers = view(second);
  ASSERT_TRUE(first_registers.write(RegisterSlot{0}, RawValue::b32(1U)));
  ASSERT_TRUE(
      first_registers.write(RegisterSlot{1}, RawValue::b64(std::uint64_t{0})));
  ASSERT_TRUE(second_registers.write(RegisterSlot{0}, RawValue::b32(2U)));
  ASSERT_TRUE(
      second_registers.write(RegisterSlot{1}, RawValue::b64(std::uint64_t{4})));
  const exec_ir::Instruction store = make_store(
      std::nullopt, exec_ir::DataType::u32, exec_ir::AddressSpace::global,
      RegisterSlot{1}, RegisterSlot{0});
  warp().thread(LaneId{0}).set_pc(initial_pc);
  warp().thread(LaneId{1}).set_pc(initial_pc);
  const auto isolated = engine_.execute(warp(), issue(initial_pc, {0, 1}),
                                        store, ProgramCounter{62});
  ASSERT_TRUE(isolated);
  ASSERT_EQ(isolated->faults.size(), 1U);
  EXPECT_EQ(isolated->faults[0].lane, LaneId{1});
  EXPECT_EQ(*memory->snapshot(memory::Address{0}, 4),
            (std::vector<std::byte>{std::byte{1}, std::byte{0}, std::byte{0},
                                    std::byte{0}}));
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), ProgramCounter{62});
  EXPECT_EQ(warp().thread(LaneId{0}).status(), ThreadStatus::Ready);
  EXPECT_EQ(warp().thread(LaneId{1}).pc(), initial_pc);
  EXPECT_EQ(warp().thread(LaneId{1}).status(), ThreadStatus::Trapped);

  warp().thread(LaneId{0}).set_pc(initial_pc);
  warp().thread(LaneId{1}).mark_ready();
  warp().thread(LaneId{1}).set_pc(initial_pc);
  const auto partial = engine_.execute(warp(), issue(initial_pc, {0}), store,
                                       ProgramCounter{63});
  ASSERT_TRUE(partial);
  EXPECT_TRUE(partial->faults.empty());
  EXPECT_EQ(warp().thread(LaneId{1}).pc(), initial_pc);
  EXPECT_EQ(warp().thread(LaneId{1}).status(), ThreadStatus::Ready);

  ASSERT_TRUE(first_registers.write(RegisterSlot{0}, RawValue::b32(3U)));
  ASSERT_TRUE(second_registers.write(RegisterSlot{0}, RawValue::b32(4U)));
  ASSERT_TRUE(
      second_registers.write(RegisterSlot{1}, RawValue::b64(std::uint64_t{0})));
  warp().thread(LaneId{0}).set_pc(initial_pc);
  warp().thread(LaneId{1}).mark_ready();
  warp().thread(LaneId{1}).set_pc(initial_pc);
  const auto ordered = engine_.execute(warp(), issue(initial_pc, {0, 1}), store,
                                       ProgramCounter{64});
  ASSERT_TRUE(ordered);
  EXPECT_TRUE(ordered->faults.empty());
  EXPECT_EQ(*memory->snapshot(memory::Address{0}, 4),
            (std::vector<std::byte>{std::byte{4}, std::byte{0}, std::byte{0},
                                    std::byte{0}}));
}

TEST_F(InstExecuteEngineTest, RejectsInvalidAddressSpaceBeforePreparation) {
  warp().thread(LaneId{0}).set_pc(initial_pc);
  const exec_ir::Instruction load = make_load(
      std::nullopt, exec_ir::DataType::u32,
      static_cast<exec_ir::AddressSpace>(99), RegisterSlot{0}, RegisterSlot{1});
  const auto result =
      engine_.execute(warp(), issue(initial_pc, {0}), load, ProgramCounter{65});
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code, StepErrorCode::unsupported_instruction);
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), initial_pc);
  EXPECT_EQ(warp().thread(LaneId{0}).status(), ThreadStatus::Ready);
}

TEST_F(InstExecuteEngineTest,
       CompletesWarpSyncAcrossPartialArrivalsWithoutRegisterBindings) {
  const exec_ir::Instruction bar = make_bar(std::nullopt, RawValue::b32(3U));
  warp().thread(LaneId{0}).set_pc(initial_pc);
  warp().thread(LaneId{1}).set_pc(initial_pc);

  const auto first =
      engine_.execute(warp(), issue(initial_pc, {0}), bar, ProgramCounter{11});
  ASSERT_TRUE(first);
  EXPECT_TRUE(warp().execution_state().sync.active());
  EXPECT_TRUE(warp().thread(LaneId{0}).waiting());
  EXPECT_EQ(warp().thread(LaneId{0}).wait_reason(),
            execution_model::WaitReason::WarpSync);
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), initial_pc);

  const auto second =
      engine_.execute(warp(), issue(initial_pc, {1}), bar, ProgramCounter{11});
  ASSERT_TRUE(second);
  EXPECT_FALSE(warp().execution_state().sync.active());
  for (const auto lane : {LaneId{0}, LaneId{1}}) {
    EXPECT_TRUE(warp().thread(lane).ready());
    EXPECT_EQ(warp().thread(lane).wait_reason(),
              execution_model::WaitReason::None);
    EXPECT_EQ(warp().thread(lane).pc(), ProgramCounter{11});
  }

  EXPECT_EQ(warp().execution_state().sync.next_generation(), 1U);
  warp().thread(LaneId{0}).set_pc(initial_pc);
  warp().thread(LaneId{1}).set_pc(initial_pc);
  const auto repeated = engine_.execute(warp(), issue(initial_pc, {0, 1}), bar,
                                        ProgramCounter{12});
  ASSERT_TRUE(repeated);
  EXPECT_FALSE(warp().execution_state().sync.active());
  EXPECT_EQ(warp().execution_state().sync.next_generation(), 2U);
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), ProgramCounter{12});
  EXPECT_EQ(warp().thread(LaneId{1}).pc(), ProgramCounter{12});
}

TEST_F(InstExecuteEngineTest, RejectsInvalidWarpSyncMasksWithoutMutation) {
  bind(LaneId{0}, {RawWidth::b32});
  bind(LaneId{1}, {RawWidth::b32});
  warp().thread(LaneId{0}).set_pc(initial_pc);
  warp().thread(LaneId{1}).set_pc(initial_pc);
  const auto check = [&](RawValue mask,
                         std::initializer_list<std::uint32_t> lanes,
                         StepErrorCode code) {
    const exec_ir::Instruction bar = make_bar(std::nullopt, mask);
    const auto result = engine_.execute(warp(), issue(initial_pc, lanes), bar,
                                        ProgramCounter{11});
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, code);
    EXPECT_FALSE(warp().execution_state().sync.active());
    EXPECT_TRUE(warp().thread(LaneId{0}).ready());
    EXPECT_EQ(warp().thread(LaneId{0}).pc(), initial_pc);
  };
  check(RawValue::b32(0U), {0}, StepErrorCode::collective_invalid_mask);
  check(RawValue::b32(16U), {0}, StepErrorCode::collective_invalid_mask);
  check(RawValue::b32(1U), {1}, StepErrorCode::collective_invalid_mask);
}

TEST_F(InstExecuteEngineTest, DoesNotArriveWhenWarpSyncPrepareFaults) {
  const auto first = bind(LaneId{0}, {RawWidth::b32, RawWidth::b32});
  bind(LaneId{1}, {RawWidth::b32});
  ASSERT_TRUE(view(first).write(RegisterSlot{1}, RawValue::b32(3U)));
  warp().thread(LaneId{0}).set_pc(initial_pc);
  warp().thread(LaneId{1}).set_pc(initial_pc);
  const exec_ir::Instruction bar = make_bar(std::nullopt, RegisterSlot{1});
  const auto result = engine_.execute(warp(), issue(initial_pc, {0, 1}), bar,
                                      ProgramCounter{11});
  ASSERT_TRUE(result);
  ASSERT_EQ(result->faults.size(), 1U);
  EXPECT_EQ(result->faults.front().lane, LaneId{1});
  EXPECT_FALSE(warp().execution_state().sync.active());
  EXPECT_TRUE(warp().thread(LaneId{0}).ready());
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), initial_pc);
  EXPECT_TRUE(warp().thread(LaneId{1}).trapped());
}

TEST_F(InstExecuteEngineTest, RejectsDisagreeingWarpSyncMembermasks) {
  const auto first = bind(LaneId{0}, {RawWidth::b32});
  const auto second = bind(LaneId{1}, {RawWidth::b32});
  ASSERT_TRUE(view(first).write(RegisterSlot{0}, RawValue::b32(3U)));
  ASSERT_TRUE(view(second).write(RegisterSlot{0}, RawValue::b32(1U)));
  warp().thread(LaneId{0}).set_pc(initial_pc);
  warp().thread(LaneId{1}).set_pc(initial_pc);
  const exec_ir::Instruction bar = make_bar(std::nullopt, RegisterSlot{0});
  const auto result = engine_.execute(warp(), issue(initial_pc, {0, 1}), bar,
                                      ProgramCounter{11});
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code, StepErrorCode::collective_mask_mismatch);
  EXPECT_FALSE(warp().execution_state().sync.active());
  EXPECT_TRUE(warp().thread(LaneId{0}).ready());
  EXPECT_TRUE(warp().thread(LaneId{1}).ready());
}

TEST_F(InstExecuteEngineTest,
       RejectsUnreachableWarpSyncParticipantWithoutBeginning) {
  warp().thread(LaneId{0}).set_pc(initial_pc);
  warp().thread(LaneId{1}).set_pc(initial_pc);
  warp().thread(LaneId{1}).mark_exited();
  const exec_ir::Instruction bar = make_bar(std::nullopt, RawValue::b32(3U));
  const auto result =
      engine_.execute(warp(), issue(initial_pc, {0}), bar, ProgramCounter{11});
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code,
            StepErrorCode::collective_unreachable_participant);
  EXPECT_EQ(result.error().lane, LaneId{1});
  EXPECT_FALSE(warp().execution_state().sync.active());
  EXPECT_TRUE(warp().thread(LaneId{0}).ready());
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), initial_pc);
}

TEST_F(InstExecuteEngineTest, RejectsDirectlyPredicatedWarpSyncBeforePrepare) {
  warp().thread(LaneId{0}).set_pc(initial_pc);
  const exec_ir::Instruction bar =
      make_bar(exec_ir::Predicate{RegisterSlot{0}, false}, RawValue::b32(1U));
  const auto result =
      engine_.execute(warp(), issue(initial_pc, {0}), bar, ProgramCounter{11});
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code, StepErrorCode::unsupported_instruction);
  EXPECT_FALSE(warp().execution_state().sync.active());
  EXPECT_TRUE(warp().thread(LaneId{0}).ready());
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), initial_pc);
}

TEST_F(InstExecuteEngineTest,
       RejectsPendingWarpSyncMismatchAndDuplicateArrival) {
  const exec_ir::Instruction mask_three =
      make_bar(std::nullopt, RawValue::b32(3U));
  const exec_ir::Instruction mask_two =
      make_bar(std::nullopt, RawValue::b32(2U));
  bind(LaneId{0}, {RawWidth::b32});
  bind(LaneId{1}, {RawWidth::b32});
  warp().thread(LaneId{0}).set_pc(initial_pc);
  warp().thread(LaneId{1}).set_pc(initial_pc);
  ASSERT_TRUE(engine_.execute(warp(), issue(initial_pc, {0}), mask_three,
                              ProgramCounter{11}));

  const auto mismatch = engine_.execute(warp(), issue(initial_pc, {1}),
                                        mask_two, ProgramCounter{11});
  ASSERT_FALSE(mismatch);
  EXPECT_EQ(mismatch.error().code, StepErrorCode::collective_pending_mismatch);
  EXPECT_TRUE(warp().execution_state().sync.active());

  warp().thread(LaneId{0}).mark_ready();
  const auto duplicate = engine_.execute(warp(), issue(initial_pc, {0, 1}),
                                         mask_three, ProgramCounter{11});
  ASSERT_FALSE(duplicate);
  EXPECT_EQ(duplicate.error().code,
            StepErrorCode::collective_duplicate_arrival);
  EXPECT_TRUE(warp().execution_state().sync.active());
  EXPECT_TRUE(
      warp().execution_state().sync.pending().arrivals().test(LaneId{0}));
  EXPECT_FALSE(
      warp().execution_state().sync.pending().arrivals().test(LaneId{1}));
  EXPECT_TRUE(warp().thread(LaneId{1}).ready());
  EXPECT_EQ(warp().thread(LaneId{1}).pc(), initial_pc);
}

TEST(InstExecuteEngineWarpSyncTest, SupportsTheFinalPartialWarp) {
  runtime::LaunchRuntime runtime{grid_id, three_lane_shape()};
  arith::context arithmetic;
  InstExecuteEngine engine{runtime, function, arithmetic};
  auto& warp = runtime.grid().cta(CtaId{grid_id, 0}).warp(0);
  for (const auto lane : {LaneId{0}, LaneId{1}, LaneId{2}}) {
    const auto frame =
        runtime.registers().create_frame({.slot_widths = {RawWidth::b32}});
    ASSERT_TRUE(frame);
    ASSERT_TRUE(
        runtime.bind_register_frame(warp.thread(lane).id(), function, *frame));
    warp.thread(lane).set_pc(initial_pc);
  }
  const exec_ir::Instruction bar = make_bar(std::nullopt, RawValue::b32(7U));
  const auto result = engine.execute(warp, issue(initial_pc, {0, 1, 2}), bar,
                                     ProgramCounter{11});
  ASSERT_TRUE(result);
  EXPECT_FALSE(warp.execution_state().sync.active());
  EXPECT_EQ(warp.thread(LaneId{2}).pc(), ProgramCounter{11});
}

}  // namespace
}  // namespace ptxsim::inst_execute_engine::test
