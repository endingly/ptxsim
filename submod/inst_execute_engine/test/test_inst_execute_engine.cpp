#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
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
         RegisterSlot destination, exec_ir::MovSource source)
    -> exec_ir::Instruction {
  exec_ir::Mov::Scalar::ScalarOperands operands{destination, std::move(source)};
  exec_ir::Mov::Scalar form{type, operands};
  return exec_ir::Mov{std::move(predicate), exec_ir::Mov::Variant{form}};
}
/** @brief Build a generated low-element-first aggregate pack move. */
auto mov_pack(exec_ir::DataType type, RegisterSlot destination,
              std::vector<std::optional<RegisterSlot>> source)
    -> exec_ir::Instruction {
  exec_ir::Mov::Scalar::PackOperands operands{
      destination, exec_ir::RegisterVector{std::move(source)}};
  exec_ir::Mov::Scalar form{type, exec_ir::Mov::Scalar::Operands{operands}};
  return exec_ir::Mov{std::nullopt, exec_ir::Mov::Variant{form}};
}
/** @brief Build a generated low-element-first aggregate unpack move. */
auto mov_unpack(exec_ir::DataType type,
                std::vector<std::optional<RegisterSlot>> destination,
                RegisterSlot source) -> exec_ir::Instruction {
  exec_ir::Mov::Scalar::UnpackOperands operands{
      exec_ir::RegisterVector{std::move(destination)}, source};
  exec_ir::Mov::Scalar form{type, exec_ir::Mov::Scalar::Operands{operands}};
  return exec_ir::Mov{std::nullopt, exec_ir::Mov::Variant{form}};
}
/** @brief Build the projected four-component u32 topology move. */
auto mov_v4_u32(RegisterSlot destination, common::SpecialRegisterId source)
    -> exec_ir::Instruction {
  const exec_ir::Mov::V4U32 form{
      exec_ir::VectorArity::v4, exec_ir::DataType::u32,
      exec_ir::VectorRegisterRef{destination},
      exec_ir::VectorSpecialRegisterRef{source}};
  return exec_ir::Mov{std::nullopt, exec_ir::Mov::Variant{form}};
}
/** @brief Build the projected predicate-register move. */
auto mov_pred(exec_ir::Predicate destination, exec_ir::PredicateSource source)
    -> exec_ir::Instruction {
  const exec_ir::Mov::Pred form{std::move(destination), std::move(source)};
  return exec_ir::Mov{std::nullopt, exec_ir::Mov::Variant{form}};
}
/** @brief Build a generated integer add instruction with bound operands. */
auto add(std::optional<exec_ir::Predicate> predicate, exec_ir::DataType type,
         RegisterSlot destination, exec_ir::B32Operand lhs,
         exec_ir::B32Operand rhs) -> exec_ir::Instruction {
  exec_ir::Add::IntegerNoSat form{type, destination, std::move(lhs),
                                  std::move(rhs)};
  return exec_ir::Add{std::move(predicate), exec_ir::Add::Variant{form}};
}
/** @brief Build a generated integer subtraction instruction with bound operands. */
auto sub(std::optional<exec_ir::Predicate> predicate, exec_ir::DataType type,
         RegisterSlot destination, exec_ir::B32Operand lhs,
         exec_ir::B32Operand rhs) -> exec_ir::Instruction {
  exec_ir::Sub::IntegerNoSat form{type, destination, std::move(lhs),
                                  std::move(rhs)};
  return exec_ir::Sub{std::move(predicate), exec_ir::Sub::Variant{form}};
}
/** @brief Build a generated round-to-nearest scalar FMA instruction. */
auto fma_rn_f32(std::optional<exec_ir::Predicate> predicate,
                RegisterSlot destination, exec_ir::ScalarOperand lhs,
                exec_ir::ScalarOperand rhs, exec_ir::ScalarOperand addend)
    -> exec_ir::Instruction {
  exec_ir::Fma::RnF32 form{false, false, destination, std::move(lhs),
                            std::move(rhs), std::move(addend)};
  return exec_ir::Fma{std::move(predicate), exec_ir::Fma::Variant{form}};
}
/** @brief Build the implemented scalar unsigned less-than predicate comparison. */
auto setp_lt_u32(exec_ir::Predicate destination, exec_ir::ScalarOperand lhs,
                 exec_ir::ScalarOperand rhs) -> exec_ir::Instruction {
  exec_ir::Setp::LtU32 form{exec_ir::ComparisonOperator::lt,
                            std::move(destination), std::move(lhs),
                            std::move(rhs)};
  return exec_ir::Setp{std::nullopt, exec_ir::Setp::Variant{form}};
}
/** @brief Build a generated scalar load using its selected address space. */
auto make_load(std::optional<exec_ir::Predicate> predicate,
               exec_ir::DataType type, exec_ir::AddressSpace space,
               RegisterSlot destination, exec_ir::Address address)
    -> exec_ir::Instruction {
  if (space == exec_ir::AddressSpace::generic) {
    exec_ir::Ld::GenericScalar form{false,
                                    exec_ir::MemoryConsistency::omitted,
                                    exec_ir::MemoryScope::none,
                                    exec_ir::CacheOperator::unspecified,
                                    type,
                                    destination,
                                    address};
    return exec_ir::Ld{std::move(predicate), exec_ir::Ld::Variant{form}};
  }
  exec_ir::Ld::ExplicitScalar form{false,
                                   exec_ir::MemoryConsistency::omitted,
                                   exec_ir::MemoryScope::none,
                                   space,
                                   exec_ir::CacheOperator::unspecified,
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
    exec_ir::St::GenericScalar form{false,
                                    exec_ir::MemoryConsistency::omitted,
                                    exec_ir::MemoryScope::none,
                                    exec_ir::CacheOperator::unspecified,
                                    type,
                                    address,
                                    source};
    return exec_ir::St{std::move(predicate), exec_ir::St::Variant{form}};
  }
  exec_ir::St::ExplicitScalar form{false,
                                   exec_ir::MemoryConsistency::omitted,
                                   exec_ir::MemoryScope::none,
                                   space,
                                   exec_ir::CacheOperator::unspecified,
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
/** @brief Build a CTA sync form with immediate barrier and count operands. */
auto make_cta_sync(std::uint32_t barrier, std::uint32_t count)
    -> exec_ir::Instruction {
  exec_ir::Bar::Sync::BarrierAndThreadCountOperands operands{
      RawValue::b32(barrier), RawValue::b32(count)};
  exec_ir::Bar::Sync form{exec_ir::Bar::Sync::Operands{operands}};
  return exec_ir::Bar{std::nullopt, exec_ir::Bar::Variant{form}};
}

/** @brief Build the omitted-count CTA sync form, which covers its full CTA. */
auto make_cta_sync(std::uint32_t barrier) -> exec_ir::Instruction {
  exec_ir::Bar::Sync::ImmediateBarrierOperands operands{RawValue::b32(barrier)};
  exec_ir::Bar::Sync form{exec_ir::Bar::Sync::Operands{operands}};
  return exec_ir::Bar{std::nullopt, exec_ir::Bar::Variant{form}};
}

/** @brief Build a CTA arrive form with immediate barrier and count operands. */
auto make_cta_arrive(std::uint32_t barrier, std::uint32_t count)
    -> exec_ir::Instruction {
  exec_ir::Bar::Arrive form{RawValue::b32(barrier), RawValue::b32(count)};
  return exec_ir::Bar{std::nullopt, exec_ir::Bar::Variant{form}};
}

/** @brief Build a CTA population-count reduction with an immediate barrier ID. */
auto make_cta_red_popc(RegisterSlot destination, std::uint32_t barrier,
                       exec_ir::Predicate input) -> exec_ir::Instruction {
  exec_ir::Bar::RedPopcU32::WithoutThreadCountOperands operands{
      destination, RawValue::b32(barrier), input};
  exec_ir::Bar::RedPopcU32 form{exec_ir::Bar::RedPopcU32::Operands{operands}};
  return exec_ir::Bar{std::nullopt, exec_ir::Bar::Variant{form}};
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

TEST_F(InstExecuteEngineTest, MovesB64ImmediateAndAdvancesProgramCounter) {
  const auto frame = bind(LaneId{0}, {RawWidth::b64});
  auto registers = view(frame);
  warp().thread(LaneId{0}).set_pc(initial_pc);
  const auto instruction =
      mov(std::nullopt, exec_ir::DataType::b64, RegisterSlot{0},
          RawValue::b64(std::uint64_t{0}));

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}),
                                      instruction, move_fallthrough);

  ASSERT_TRUE(result);
  EXPECT_TRUE(result->faults.empty());
  EXPECT_EQ(*registers.read(RegisterSlot{0}), RawValue::b64(std::uint64_t{0}));
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), move_fallthrough);
}

TEST_F(InstExecuteEngineTest, B64MoveRejectsWrongDestinationWidthWithoutWrite) {
  const auto frame = bind(LaneId{0}, {RawWidth::b32});
  auto registers = view(frame);
  ASSERT_TRUE(registers.write(RegisterSlot{0}, RawValue::b32(7U)));
  auto& thread = warp().thread(LaneId{0});
  thread.set_pc(initial_pc);
  const auto instruction =
      mov(std::nullopt, exec_ir::DataType::b64, RegisterSlot{0},
          RawValue::b64(std::uint64_t{0}));

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}),
                                      instruction, move_fallthrough);

  ASSERT_TRUE(result);
  ASSERT_EQ(result->faults.size(), 1U);
  EXPECT_EQ(std::get<common::RawValueError>(result->faults.front().cause),
            (common::RawValueError{RawWidth::b64, RawWidth::b32}));
  EXPECT_EQ(*registers.read(RegisterSlot{0}), RawValue::b32(7U));
  EXPECT_EQ(thread.pc(), initial_pc);
  EXPECT_EQ(thread.status(), ThreadStatus::Trapped);
}

TEST_F(InstExecuteEngineTest, MovesExactB16AndB64RawBitsWithoutConversion) {
  const auto frame = bind(LaneId{0}, {RawWidth::b16, RawWidth::b16,
                                     RawWidth::b64, RawWidth::b64});
  auto registers = view(frame);
  ASSERT_TRUE(registers.write(
      RegisterSlot{0}, RawValue::b16(std::uint16_t{0xabcdU})));
  ASSERT_TRUE(registers.write(RegisterSlot{2},
                             RawValue::b64(std::uint64_t{0x0123'4567'89ab'cdefULL})));
  auto& thread = warp().thread(LaneId{0});
  thread.set_pc(initial_pc);

  const auto b16 = engine_.execute(
      warp(), issue(initial_pc, {0}),
      mov(std::nullopt, exec_ir::DataType::s16, RegisterSlot{1}, RegisterSlot{0}),
      move_fallthrough);
  ASSERT_TRUE(b16);
  EXPECT_TRUE(b16->faults.empty());
  EXPECT_EQ(*registers.read(RegisterSlot{1}),
            RawValue::b16(std::uint16_t{0xabcdU}));

  thread.set_pc(initial_pc);
  const auto b64 = engine_.execute(
      warp(), issue(initial_pc, {0}),
      mov(std::nullopt, exec_ir::DataType::f64, RegisterSlot{3}, RegisterSlot{2}),
      move_fallthrough);
  ASSERT_TRUE(b64);
  EXPECT_TRUE(b64->faults.empty());
  EXPECT_EQ(*registers.read(RegisterSlot{3}),
            RawValue::b64(std::uint64_t{0x0123'4567'89ab'cdefULL}));
}

TEST_F(InstExecuteEngineTest, MaterializesAddressIntoRequestedRawWidth) {
  const auto frame = bind(LaneId{0}, {RawWidth::b32, RawWidth::b64});
  auto registers = view(frame);
  auto& thread = warp().thread(LaneId{0});
  const exec_ir::Address address{
      RawValue::b64(std::uint64_t{0x0123'4567'89ab'cdefULL}),
      exec_ir::AddressOffset{false, RawValue::b64(std::uint64_t{0x31U})}};

  thread.set_pc(initial_pc);
  const auto b32 = engine_.execute(
      warp(), issue(initial_pc, {0}),
      mov(std::nullopt, exec_ir::DataType::b32, RegisterSlot{0}, address),
      move_fallthrough);
  ASSERT_TRUE(b32);
  EXPECT_TRUE(b32->faults.empty());
  EXPECT_EQ(*registers.read(RegisterSlot{0}), RawValue::b32(0x89ab'ce20U));

  thread.set_pc(initial_pc);
  const auto b64 = engine_.execute(
      warp(), issue(initial_pc, {0}),
      mov(std::nullopt, exec_ir::DataType::b64, RegisterSlot{1}, address),
      move_fallthrough);
  ASSERT_TRUE(b64);
  EXPECT_TRUE(b64->faults.empty());
  EXPECT_EQ(*registers.read(RegisterSlot{1}),
            RawValue::b64(std::uint64_t{0x0123'4567'89ab'ce20ULL}));
}

TEST_F(InstExecuteEngineTest, PacksAndUnpacksLowElementsFirstWithDestinationSinks) {
  const auto frame = bind(LaneId{0}, {RawWidth::b16, RawWidth::b16,
                                     RawWidth::b32, RawWidth::b32,
                                     RawWidth::b64});
  auto registers = view(frame);
  ASSERT_TRUE(registers.write(
      RegisterSlot{0}, RawValue::b16(std::uint16_t{0x1122U})));
  ASSERT_TRUE(registers.write(
      RegisterSlot{1}, RawValue::b16(std::uint16_t{0x3344U})));
  auto& thread = warp().thread(LaneId{0});
  thread.set_pc(initial_pc);
  const auto packed = engine_.execute(
      warp(), issue(initial_pc, {0}),
      mov_pack(exec_ir::DataType::b32, RegisterSlot{2},
               {RegisterSlot{0}, RegisterSlot{1}}),
      move_fallthrough);
  ASSERT_TRUE(packed);
  EXPECT_TRUE(packed->faults.empty());
  EXPECT_EQ(*registers.read(RegisterSlot{2}), RawValue::b32(0x3344'1122U));

  ASSERT_TRUE(registers.write(RegisterSlot{4},
                             RawValue::b64(std::uint64_t{0x5566'7788'1122'3344ULL})));
  thread.set_pc(initial_pc);
  const auto unpacked = engine_.execute(
      warp(), issue(initial_pc, {0}),
      mov_unpack(exec_ir::DataType::b64, {RegisterSlot{3}, std::nullopt},
                 RegisterSlot{4}),
      move_fallthrough);
  ASSERT_TRUE(unpacked);
  EXPECT_TRUE(unpacked->faults.empty());
  EXPECT_EQ(*registers.read(RegisterSlot{3}), RawValue::b32(0x1122'3344U));
}

TEST_F(InstExecuteEngineTest, MovesTopologyVectorIntoFourContiguousSlots) {
  const auto frame = bind(LaneId{1}, {RawWidth::b32, RawWidth::b32,
                                     RawWidth::b32, RawWidth::b32});
  auto registers = view(frame);
  warp().thread(LaneId{1}).set_pc(initial_pc);

  const auto result = engine_.execute(
      warp(), issue(initial_pc, {1}),
      mov_v4_u32(RegisterSlot{0}, exec_ir::kThreadIdSpecialRegister),
      move_fallthrough);

  ASSERT_TRUE(result);
  EXPECT_TRUE(result->faults.empty());
  EXPECT_EQ(*registers.read(RegisterSlot{0}), RawValue::b32(1U));
  EXPECT_EQ(*registers.read(RegisterSlot{1}), RawValue::b32(0U));
  EXPECT_EQ(*registers.read(RegisterSlot{2}), RawValue::b32(0U));
  EXPECT_EQ(*registers.read(RegisterSlot{3}), RawValue::b32(0U));
}

TEST_F(InstExecuteEngineTest, MovesAliasedPredicateSource) {
  const auto frame = bind(LaneId{0}, {RawWidth::pred});
  auto registers = view(frame);
  ASSERT_TRUE(registers.write(RegisterSlot{0}, RawValue::pred(true)));
  warp().thread(LaneId{0}).set_pc(initial_pc);

  const auto result = engine_.execute(
      warp(), issue(initial_pc, {0}),
      mov_pred(exec_ir::Predicate{RegisterSlot{0}},
               exec_ir::PredicateSource{exec_ir::Predicate{RegisterSlot{0}}}),
      move_fallthrough);

  ASSERT_TRUE(result);
  EXPECT_TRUE(result->faults.empty());
  EXPECT_EQ(*registers.read(RegisterSlot{0}), RawValue::pred(true));
}

TEST_F(InstExecuteEngineTest, InvertsAliasedPredicateMoveSource) {
  const auto frame = bind(LaneId{0}, {RawWidth::pred});
  auto registers = view(frame);
  ASSERT_TRUE(registers.write(RegisterSlot{0}, RawValue::pred(false)));
  auto& thread = warp().thread(LaneId{0});
  thread.set_pc(initial_pc);

  const auto result = engine_.execute(
      warp(), issue(initial_pc, {0}),
      mov_pred(exec_ir::Predicate{RegisterSlot{0}},
               exec_ir::PredicateSource{exec_ir::Predicate{RegisterSlot{0}, true}}),
      move_fallthrough);

  ASSERT_TRUE(result);
  EXPECT_TRUE(result->faults.empty());
  EXPECT_EQ(*registers.read(RegisterSlot{0}), RawValue::pred(true));
  EXPECT_EQ(thread.status(), ThreadStatus::Ready);
  EXPECT_EQ(thread.pc(), move_fallthrough);
}

TEST_F(InstExecuteEngineTest,
       RejectsMalformedAggregateAndSpecialMovesBeforeResourcePreparation) {
  warp().thread(LaneId{0}).set_pc(initial_pc);
  const auto bad_special = mov(
      std::nullopt, exec_ir::DataType::f32, RegisterSlot{0},
      exec_ir::SpecialRegisterRef{exec_ir::kThreadIdSpecialRegister, 0U});
  const auto bad_grid = mov(
      std::nullopt, exec_ir::DataType::f64, RegisterSlot{0},
      exec_ir::SpecialRegisterRef{exec_ir::kGridIdSpecialRegister, std::nullopt});
  const std::array malformed{
      mov_pack(exec_ir::DataType::u32, RegisterSlot{0},
               {RegisterSlot{1}, RegisterSlot{2}}),
      mov_pack(exec_ir::DataType::b16, RegisterSlot{0},
               {RegisterSlot{1}, RegisterSlot{2}, RegisterSlot{3}, RegisterSlot{4}}),
      mov_pack(exec_ir::DataType::b32, RegisterSlot{0},
               {RegisterSlot{1}, std::nullopt}),
      bad_special,
      bad_grid,
  };
  for (const auto& operation : malformed) {
    const auto result = engine_.execute(warp(), issue(initial_pc, {0}), operation,
                                        move_fallthrough);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, StepErrorCode::invalid_instruction);
    EXPECT_EQ(warp().thread(LaneId{0}).pc(), initial_pc);
    EXPECT_EQ(warp().thread(LaneId{0}).status(), ThreadStatus::Ready);
  }
}

TEST_F(InstExecuteEngineTest,
       SuppressesUnsupportedMoveSourceWhenExecutionPredicateIsFalse) {
  const auto frame = bind(LaneId{0}, {RawWidth::pred, RawWidth::b32});
  auto registers = view(frame);
  ASSERT_TRUE(registers.write(RegisterSlot{0}, RawValue::pred(false)));
  ASSERT_TRUE(registers.write(RegisterSlot{1}, RawValue::b32(77U)));
  auto& thread = warp().thread(LaneId{0});
  thread.set_pc(initial_pc);

  const auto result = engine_.execute(
      warp(), issue(initial_pc, {0}),
      mov(exec_ir::Predicate{RegisterSlot{0}}, exec_ir::DataType::b32,
          RegisterSlot{1}, exec_ir::SymbolRef{common::SymbolId{9}}),
      move_fallthrough);

  ASSERT_TRUE(result);
  EXPECT_TRUE(result->faults.empty());
  EXPECT_EQ(*registers.read(RegisterSlot{1}), RawValue::b32(77U));
  EXPECT_EQ(thread.pc(), move_fallthrough);
}

TEST_F(InstExecuteEngineTest, UnsupportedSymbolMoveSourceFaultsEligibleLane) {
  const auto frame = bind(LaneId{0}, {RawWidth::b32});
  auto registers = view(frame);
  auto& thread = warp().thread(LaneId{0});
  thread.set_pc(initial_pc);

  const auto result = engine_.execute(
      warp(), issue(initial_pc, {0}),
      mov(std::nullopt, exec_ir::DataType::b32, RegisterSlot{0},
          exec_ir::SymbolRef{common::SymbolId{9}}),
      move_fallthrough);

  ASSERT_TRUE(result);
  ASSERT_EQ(result->faults.size(), 1U);
  EXPECT_TRUE(std::holds_alternative<UnsupportedMovSource>(
      result->faults.front().cause));
  EXPECT_EQ(thread.status(), ThreadStatus::Trapped);
  EXPECT_FALSE(*registers.initialized(RegisterSlot{0}));
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
  const exec_ir::Div::U32 form{RegisterSlot{0}, RegisterSlot{1},
                               RegisterSlot{2}};
  const exec_ir::Instruction instruction{
      exec_ir::Div{std::nullopt, exec_ir::Div::Variant{form}}};

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}),
                                      instruction, std::nullopt);

  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code, StepErrorCode::unsupported_instruction);
  EXPECT_EQ(thread.pc(), initial_pc);
  EXPECT_EQ(thread.status(), ThreadStatus::Ready);
  EXPECT_EQ(*registers.read(RegisterSlot{0}), RawValue::b32(7U));
}

TEST_F(InstExecuteEngineTest, WideMulRejectsB32DestinationWithoutWrite) {
  const auto frame =
      bind(LaneId{0}, {RawWidth::b32, RawWidth::b32, RawWidth::b32});
  auto registers = view(frame);
  ASSERT_TRUE(registers.write(RegisterSlot{0}, RawValue::b32(0xfeedfaceU)));
  ASSERT_TRUE(registers.write(RegisterSlot{1}, RawValue::b32(3U)));
  ASSERT_TRUE(registers.write(RegisterSlot{2}, RawValue::b32(7U)));
  auto& thread = warp().thread(LaneId{0});
  thread.set_pc(initial_pc);
  const exec_ir::Mul::WideU32 form{RegisterSlot{0}, RegisterSlot{1},
                                   RegisterSlot{2}};
  const exec_ir::Instruction instruction{
      exec_ir::Mul{std::nullopt, exec_ir::Mul::Variant{form}}};

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}),
                                      instruction, ProgramCounter{56});

  ASSERT_TRUE(result);
  ASSERT_EQ(result->faults.size(), 1U);
  EXPECT_EQ(std::get<common::RawValueError>(result->faults.front().cause),
            (common::RawValueError{RawWidth::b64, RawWidth::b32}));
  EXPECT_EQ(*registers.read(RegisterSlot{0}), RawValue::b32(0xfeedfaceU));
  EXPECT_EQ(*registers.read(RegisterSlot{1}), RawValue::b32(3U));
  EXPECT_EQ(*registers.read(RegisterSlot{2}), RawValue::b32(7U));
  EXPECT_EQ(thread.pc(), initial_pc);
  EXPECT_EQ(thread.status(), ThreadStatus::Trapped);
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

TEST_F(InstExecuteEngineTest, ComparesUnsignedU32ForEachIssuedLane) {
  const auto first = bind(LaneId{0}, {RawWidth::pred, RawWidth::b32});
  const auto second = bind(LaneId{1}, {RawWidth::pred, RawWidth::b32});
  auto first_registers = view(first);
  auto second_registers = view(second);
  ASSERT_TRUE(first_registers.write(RegisterSlot{1}, RawValue::b32(0U)));
  ASSERT_TRUE(second_registers.write(RegisterSlot{1}, RawValue::b32(1U)));
  warp().thread(LaneId{0}).set_pc(initial_pc);
  warp().thread(LaneId{1}).set_pc(initial_pc);
  const auto instruction = setp_lt_u32(exec_ir::Predicate{RegisterSlot{0}},
                                       RegisterSlot{1}, RawValue::b32(1U));

  const auto result = engine_.execute(warp(), issue(initial_pc, {0, 1}),
                                      instruction, move_fallthrough);

  ASSERT_TRUE(result);
  EXPECT_TRUE(result->faults.empty());
  EXPECT_EQ(*first_registers.read(RegisterSlot{0}), RawValue::pred(true));
  EXPECT_EQ(*second_registers.read(RegisterSlot{0}), RawValue::pred(false));
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), move_fallthrough);
  EXPECT_EQ(warp().thread(LaneId{1}).pc(), move_fallthrough);
}

TEST_F(InstExecuteEngineTest,
       RejectsMalformedPredicateComparisonBeforeMutation) {
  const auto frame = bind(LaneId{0}, {RawWidth::pred, RawWidth::b32});
  auto registers = view(frame);
  ASSERT_TRUE(registers.write(RegisterSlot{0}, RawValue::pred(false)));
  ASSERT_TRUE(registers.write(RegisterSlot{1}, RawValue::b32(7U)));
  auto& thread = warp().thread(LaneId{0});
  thread.set_pc(initial_pc);
  const exec_ir::Setp::GeS32 form{
      exec_ir::ComparisonOperator::lt,
      exec_ir::Predicate{RegisterSlot{0}},
      RegisterSlot{1},
      RawValue::b32(0U),
  };
  const exec_ir::Instruction instruction{
      exec_ir::Setp{std::nullopt, exec_ir::Setp::Variant{form}}};

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}),
                                      instruction, move_fallthrough);

  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code, StepErrorCode::invalid_instruction);
  EXPECT_EQ(thread.pc(), initial_pc);
  EXPECT_EQ(thread.status(), ThreadStatus::Ready);
  EXPECT_EQ(*registers.read(RegisterSlot{0}), RawValue::pred(false));
  EXPECT_EQ(*registers.read(RegisterSlot{1}), RawValue::b32(7U));
}

TEST_F(InstExecuteEngineTest,
       SetpRejectsNegatedDestinationsAndForbiddenCombine) {
  const auto frame =
      bind(LaneId{0}, {RawWidth::pred, RawWidth::pred, RawWidth::b32});
  auto registers = view(frame);
  ASSERT_TRUE(registers.write(RegisterSlot{0}, RawValue::pred(false)));
  ASSERT_TRUE(registers.write(RegisterSlot{1}, RawValue::pred(true)));
  ASSERT_TRUE(registers.write(RegisterSlot{2}, RawValue::b32(1U)));
  auto& thread = warp().thread(LaneId{0});
  thread.set_pc(initial_pc);
  const exec_ir::Setp::LtU32 negated_destination{
      exec_ir::ComparisonOperator::lt,
      exec_ir::Predicate{RegisterSlot{0}, true}, RegisterSlot{2},
      RawValue::b32(2U)};
  const exec_ir::Instruction negated_instruction{
      exec_ir::Setp{std::nullopt, exec_ir::Setp::Variant{negated_destination}}};
  const auto negated_result = engine_.execute(
      warp(), issue(initial_pc, {0}), negated_instruction, move_fallthrough);
  ASSERT_FALSE(negated_result);
  EXPECT_EQ(negated_result.error().code, StepErrorCode::invalid_instruction);

  const exec_ir::Setp::LtAndU32 invalid_boolean{
      exec_ir::ComparisonOperator::lt,
      exec_ir::BooleanOperator::or_,
      exec_ir::Predicate{RegisterSlot{0}},
      RegisterSlot{2},
      RawValue::b32(2U),
      exec_ir::Predicate{RegisterSlot{1}}};
  const exec_ir::Instruction boolean_instruction{
      exec_ir::Setp{std::nullopt, exec_ir::Setp::Variant{invalid_boolean}}};
  const auto boolean_result = engine_.execute(
      warp(), issue(initial_pc, {0}), boolean_instruction, move_fallthrough);
  ASSERT_FALSE(boolean_result);
  EXPECT_EQ(boolean_result.error().code, StepErrorCode::invalid_instruction);

  const exec_ir::Setp::LtAndS32Pair negated_combine{
      exec_ir::ComparisonOperator::lt,
      exec_ir::BooleanOperator::and_,
      exec_ir::PredicatePair{exec_ir::Predicate{RegisterSlot{0}},
                             exec_ir::Predicate{RegisterSlot{1}}},
      RegisterSlot{2},
      RawValue::b32(2U),
      exec_ir::Predicate{RegisterSlot{1}, true}};
  const exec_ir::Instruction combine_instruction{
      exec_ir::Setp{std::nullopt, exec_ir::Setp::Variant{negated_combine}}};
  const auto combine_result = engine_.execute(
      warp(), issue(initial_pc, {0}), combine_instruction, move_fallthrough);
  ASSERT_FALSE(combine_result);
  EXPECT_EQ(combine_result.error().code, StepErrorCode::invalid_instruction);
  EXPECT_EQ(thread.pc(), initial_pc);
  EXPECT_EQ(*registers.read(RegisterSlot{0}), RawValue::pred(false));
  EXPECT_EQ(*registers.read(RegisterSlot{1}), RawValue::pred(true));
}

TEST_F(InstExecuteEngineTest, PairSetpStagesBothDestinationsBeforeCommit) {
  const auto frame =
      bind(LaneId{0}, {RawWidth::pred, RawWidth::b32, RawWidth::b32});
  auto registers = view(frame);
  ASSERT_TRUE(registers.write(RegisterSlot{0}, RawValue::pred(false)));
  ASSERT_TRUE(registers.write(RegisterSlot{1}, RawValue::b32(4U)));
  ASSERT_TRUE(registers.write(RegisterSlot{2}, RawValue::b32(4U)));
  auto& thread = warp().thread(LaneId{0});
  thread.set_pc(initial_pc);
  const exec_ir::Setp::EqU32Pair form{
      exec_ir::ComparisonOperator::eq,
      exec_ir::PredicatePair{exec_ir::Predicate{RegisterSlot{0}},
                             exec_ir::Predicate{RegisterSlot{2}}},
      RegisterSlot{1}, RegisterSlot{2}};
  const exec_ir::Instruction instruction{
      exec_ir::Setp{std::nullopt, exec_ir::Setp::Variant{form}}};

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}),
                                      instruction, move_fallthrough);

  ASSERT_TRUE(result);
  ASSERT_EQ(result->faults.size(), 1U);
  EXPECT_EQ(*registers.read(RegisterSlot{0}), RawValue::pred(false));
  EXPECT_EQ(thread.pc(), initial_pc);
  EXPECT_EQ(thread.status(), ThreadStatus::Trapped);
}

TEST_F(InstExecuteEngineTest, PredicatedOffSetpSkipsItsSources) {
  const auto frame =
      bind(LaneId{0}, {RawWidth::pred, RawWidth::pred, RawWidth::b32});
  auto registers = view(frame);
  ASSERT_TRUE(registers.write(RegisterSlot{0}, RawValue::pred(false)));
  ASSERT_TRUE(registers.write(RegisterSlot{1}, RawValue::pred(false)));
  auto& thread = warp().thread(LaneId{0});
  thread.set_pc(initial_pc);
  const exec_ir::Setp::LtU32 form{exec_ir::ComparisonOperator::lt,
                                  exec_ir::Predicate{RegisterSlot{0}},
                                  RegisterSlot{2}, RawValue::b32(1U)};
  const exec_ir::Instruction instruction{exec_ir::Setp{
      exec_ir::Predicate{RegisterSlot{1}}, exec_ir::Setp::Variant{form}}};

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}),
                                      instruction, move_fallthrough);

  ASSERT_TRUE(result);
  EXPECT_TRUE(result->faults.empty());
  EXPECT_EQ(*registers.read(RegisterSlot{0}), RawValue::pred(false));
  EXPECT_EQ(thread.pc(), move_fallthrough);
}

TEST_F(InstExecuteEngineTest, SetpCapturesPredicateSourceBeforeAliasedWrite) {
  const auto frame = bind(LaneId{0}, {RawWidth::pred, RawWidth::b32});
  auto registers = view(frame);
  ASSERT_TRUE(registers.write(RegisterSlot{0}, RawValue::pred(true)));
  ASSERT_TRUE(registers.write(RegisterSlot{1}, RawValue::b32(1U)));
  warp().thread(LaneId{0}).set_pc(initial_pc);
  const exec_ir::Setp::LtAndS32Pair form{
      exec_ir::ComparisonOperator::lt,
      exec_ir::BooleanOperator::and_,
      exec_ir::PredicatePair{exec_ir::Predicate{RegisterSlot{0}},
                             exec_ir::Predicate{RegisterSlot{0}}},
      RegisterSlot{1},
      RawValue::b32(2U),
      exec_ir::Predicate{RegisterSlot{0}}};
  const exec_ir::Instruction instruction{
      exec_ir::Setp{std::nullopt, exec_ir::Setp::Variant{form}}};

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}),
                                      instruction, move_fallthrough);

  ASSERT_TRUE(result);
  EXPECT_TRUE(result->faults.empty());
  EXPECT_EQ(*registers.read(RegisterSlot{0}), RawValue::pred(false));
}

TEST_F(InstExecuteEngineTest, MovesThreadIdXForEachIssuedLane) {
  const auto first = bind(LaneId{0}, {RawWidth::b32});
  const auto second = bind(LaneId{1}, {RawWidth::b32});
  auto first_registers = view(first);
  auto second_registers = view(second);
  warp().thread(LaneId{0}).set_pc(initial_pc);
  warp().thread(LaneId{1}).set_pc(initial_pc);
  const exec_ir::Instruction instruction =
      mov(std::nullopt, exec_ir::DataType::u32, RegisterSlot{0},
          exec_ir::SpecialRegisterRef{.id = exec_ir::kThreadIdSpecialRegister,
                                      .component = 0U});

  const auto result = engine_.execute(warp(), issue(initial_pc, {0, 1}),
                                      instruction, move_fallthrough);

  ASSERT_TRUE(result);
  EXPECT_TRUE(result->faults.empty());
  EXPECT_EQ(*first_registers.read(RegisterSlot{0}), RawValue::b32(0U));
  EXPECT_EQ(*second_registers.read(RegisterSlot{0}), RawValue::b32(1U));
}

TEST_F(InstExecuteEngineTest, MovesGridIdAndUnclippedLaneMasks) {
  const auto frame = bind(LaneId{1}, {RawWidth::b64, RawWidth::b16,
                                     RawWidth::b32, RawWidth::b32,
                                     RawWidth::b32, RawWidth::b32,
                                     RawWidth::b32});
  auto registers = view(frame);
  auto& thread = warp().thread(LaneId{1});
  const auto execute_special = [&](RegisterSlot destination,
                                   exec_ir::DataType type,
                                   common::SpecialRegisterId source) {
    thread.set_pc(initial_pc);
    return engine_.execute(
        warp(), issue(initial_pc, {1}),
        mov(std::nullopt, type, destination,
            exec_ir::SpecialRegisterRef{source, std::nullopt}),
        move_fallthrough);
  };

  ASSERT_TRUE(execute_special(RegisterSlot{0}, exec_ir::DataType::u64,
                              exec_ir::kGridIdSpecialRegister));
  ASSERT_TRUE(execute_special(RegisterSlot{1}, exec_ir::DataType::u16,
                              exec_ir::kGridIdSpecialRegister));
  EXPECT_EQ(*registers.read(RegisterSlot{0}),
            RawValue::b64(std::uint64_t{7U}));
  EXPECT_EQ(*registers.read(RegisterSlot{1}),
            RawValue::b16(std::uint16_t{7U}));

  ASSERT_TRUE(execute_special(RegisterSlot{2}, exec_ir::DataType::u32,
                              exec_ir::kLaneMaskEqSpecialRegister));
  ASSERT_TRUE(execute_special(RegisterSlot{3}, exec_ir::DataType::u32,
                              exec_ir::kLaneMaskLtSpecialRegister));
  ASSERT_TRUE(execute_special(RegisterSlot{4}, exec_ir::DataType::u32,
                              exec_ir::kLaneMaskLeSpecialRegister));
  ASSERT_TRUE(execute_special(RegisterSlot{5}, exec_ir::DataType::u32,
                              exec_ir::kLaneMaskGeSpecialRegister));
  ASSERT_TRUE(execute_special(RegisterSlot{6}, exec_ir::DataType::u32,
                              exec_ir::kLaneMaskGtSpecialRegister));
  EXPECT_EQ(*registers.read(RegisterSlot{2}), RawValue::b32(0x0000'0002U));
  EXPECT_EQ(*registers.read(RegisterSlot{3}), RawValue::b32(0x0000'0001U));
  EXPECT_EQ(*registers.read(RegisterSlot{4}), RawValue::b32(0x0000'0003U));
  EXPECT_EQ(*registers.read(RegisterSlot{5}), RawValue::b32(0xffff'fffeU));
  EXPECT_EQ(*registers.read(RegisterSlot{6}), RawValue::b32(0xffff'fffcU));
}

TEST(InstExecuteEngineOperationTest, LaneMaskAtLaneThirtyTwoFaultsWithoutShift) {
  runtime::LaunchRuntime runtime{
      execution_model::GridId{9},
      execution_model::GridShape{.cta_dim = {1, 1, 1},
                                 .thread_dim = {33, 1, 1},
                                 .warp_size = 64}};
  arith::context arithmetic;
  InstExecuteEngine engine{runtime, function, arithmetic};
  auto& warp = runtime.grid().cta(CtaId{execution_model::GridId{9}, 0}).warp(0);
  const auto frame = runtime.registers().create_frame({.slot_widths = {RawWidth::b32}});
  ASSERT_TRUE(frame);
  ASSERT_TRUE(runtime.bind_register_frame(warp.thread(LaneId{32}).id(), function,
                                          *frame));
  warp.thread(LaneId{32}).set_pc(initial_pc);
  LaneMask lanes{64};
  lanes.set(LaneId{32});

  const auto result = engine.execute(
      warp, WarpIssueGroup{.pc = initial_pc, .lanes = std::move(lanes)},
      mov(std::nullopt, exec_ir::DataType::u32, RegisterSlot{0},
          exec_ir::SpecialRegisterRef{exec_ir::kLaneMaskEqSpecialRegister,
                                      std::nullopt}),
      move_fallthrough);

  ASSERT_TRUE(result);
  ASSERT_EQ(result->faults.size(), 1U);
  EXPECT_TRUE(std::holds_alternative<UnsupportedSpecialRegister>(
      result->faults.front().cause));
  EXPECT_EQ(warp.thread(LaneId{32}).status(), ThreadStatus::Trapped);
}

TEST_F(InstExecuteEngineTest, TrapsUnsupportedSpecialMoveSource) {
  const auto frame = bind(LaneId{0}, {RawWidth::b32});
  auto registers = view(frame);
  warp().thread(LaneId{0}).set_pc(initial_pc);
  const exec_ir::Instruction instruction =
      mov(std::nullopt, exec_ir::DataType::u32, RegisterSlot{0},
          exec_ir::SpecialRegisterRef{.id = common::SpecialRegisterId{999},
                                      .component = 0U});

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}),
                                      instruction, move_fallthrough);

  ASSERT_TRUE(result);
  ASSERT_EQ(result->faults.size(), 1U);
  EXPECT_EQ(result->faults.front().lane, LaneId{0});
  EXPECT_EQ(std::get<UnsupportedSpecialRegister>(result->faults.front().cause),
            (UnsupportedSpecialRegister{common::SpecialRegisterId{999}, 0U}));
  EXPECT_EQ(warp().thread(LaneId{0}).status(), ThreadStatus::Trapped);
  EXPECT_FALSE(*registers.initialized(RegisterSlot{0}));
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

TEST_F(InstExecuteEngineTest, InvalidAddTypeRejectsBeforeAnyLaneStateMutation) {
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
  EXPECT_EQ(result.error().code, StepErrorCode::invalid_instruction);
  EXPECT_EQ(thread.pc(), initial_pc);
  EXPECT_EQ(thread.status(), ThreadStatus::Ready);
  EXPECT_EQ(*registers.read(RegisterSlot{0}), RawValue::b32(7U));
  EXPECT_EQ(*registers.read(RegisterSlot{1}), RawValue::b32(9U));
  EXPECT_EQ(*registers.read(RegisterSlot{2}), RawValue::b32(11U));
}

TEST_F(InstExecuteEngineTest, InvalidSubTypeRejectsBeforeAnyLaneStateMutation) {
  const auto frame =
      bind(LaneId{0}, {RawWidth::b32, RawWidth::b32, RawWidth::b32});
  auto registers = view(frame);
  ASSERT_TRUE(registers.write(RegisterSlot{0}, RawValue::b32(7U)));
  ASSERT_TRUE(registers.write(RegisterSlot{1}, RawValue::b32(9U)));
  ASSERT_TRUE(registers.write(RegisterSlot{2}, RawValue::b32(11U)));
  auto& thread = warp().thread(LaneId{0});
  thread.set_pc(initial_pc);

  const exec_ir::Instruction instruction =
      sub(std::nullopt, exec_ir::DataType::b32, RegisterSlot{2},
          RegisterSlot{0}, RegisterSlot{1});
  const auto result = engine_.execute(warp(), issue(initial_pc, {0}),
                                      instruction, ProgramCounter{57});

  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code, StepErrorCode::invalid_instruction);
  EXPECT_EQ(thread.pc(), initial_pc);
  EXPECT_EQ(thread.status(), ThreadStatus::Ready);
  EXPECT_EQ(*registers.read(RegisterSlot{0}), RawValue::b32(7U));
  EXPECT_EQ(*registers.read(RegisterSlot{1}), RawValue::b32(9U));
  EXPECT_EQ(*registers.read(RegisterSlot{2}), RawValue::b32(11U));
}

TEST_F(InstExecuteEngineTest, FalsePredicateSuppressesInvalidSubOperands) {
  const auto frame = bind(
      LaneId{0}, {RawWidth::b32, RawWidth::b32, RawWidth::b32, RawWidth::pred});
  auto registers = view(frame);
  ASSERT_TRUE(registers.write(RegisterSlot{2}, RawValue::b32(9U)));
  ASSERT_TRUE(registers.write(RegisterSlot{3}, RawValue::pred(false)));
  warp().thread(LaneId{0}).set_pc(initial_pc);
  const exec_ir::Instruction instruction =
      sub(exec_ir::Predicate{.source = RegisterSlot{3}}, exec_ir::DataType::u32,
          RegisterSlot{2}, RegisterSlot{0}, RegisterSlot{1});

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}),
                                      instruction, ProgramCounter{58});

  ASSERT_TRUE(result);
  EXPECT_TRUE(result->faults.empty());
  EXPECT_EQ(*registers.read(RegisterSlot{2}), RawValue::b32(9U));
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), ProgramCounter{58});
  EXPECT_EQ(warp().thread(LaneId{0}).status(), ThreadStatus::Ready);
}

TEST_F(InstExecuteEngineTest, FalsePredicateSuppressesAllThreeFmaSources) {
  const auto frame = bind(
      LaneId{0}, {RawWidth::b32, RawWidth::b32, RawWidth::b32, RawWidth::b32,
                  RawWidth::pred});
  auto registers = view(frame);
  ASSERT_TRUE(registers.write(RegisterSlot{0}, RawValue::b32(0x1234'5678U)));
  ASSERT_TRUE(registers.write(RegisterSlot{4}, RawValue::pred(false)));
  auto& thread = warp().thread(LaneId{0});
  thread.set_pc(initial_pc);
  const auto instruction = fma_rn_f32(
      exec_ir::Predicate{RegisterSlot{4}}, RegisterSlot{0}, RegisterSlot{1},
      RegisterSlot{2}, RegisterSlot{3});

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}),
                                      instruction, ProgramCounter{59});

  ASSERT_TRUE(result);
  EXPECT_TRUE(result->faults.empty());
  EXPECT_EQ(*registers.read(RegisterSlot{0}), RawValue::b32(0x1234'5678U));
  EXPECT_EQ(thread.pc(), ProgramCounter{59});
  EXPECT_EQ(thread.status(), ThreadStatus::Ready);
}

TEST_F(InstExecuteEngineTest,
       DirectedFmaRejectsRoundNearestBeforeLaneMutation) {
  const auto frame =
      bind(LaneId{0}, {RawWidth::b32, RawWidth::b32, RawWidth::b32, RawWidth::b32});
  auto registers = view(frame);
  ASSERT_TRUE(registers.write(RegisterSlot{0}, RawValue::b32(0xdecafbadU)));
  ASSERT_TRUE(registers.write(RegisterSlot{1}, RawValue::b32(0x3f80'0000U)));
  ASSERT_TRUE(registers.write(RegisterSlot{2}, RawValue::b32(0x3f80'0000U)));
  ASSERT_TRUE(registers.write(RegisterSlot{3}, RawValue::b32(0U)));
  auto& thread = warp().thread(LaneId{0});
  thread.set_pc(initial_pc);
  const exec_ir::Fma::DirectedF32 form{
      exec_ir::RoundingMode::rn, false, false, RegisterSlot{0},
      RegisterSlot{1}, RegisterSlot{2}, RegisterSlot{3}};
  const exec_ir::Instruction instruction{
      exec_ir::Fma{std::nullopt, exec_ir::Fma::Variant{form}}};

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}),
                                      instruction, ProgramCounter{60});

  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code, StepErrorCode::invalid_instruction);
  EXPECT_EQ(*registers.read(RegisterSlot{0}), RawValue::b32(0xdecafbadU));
  EXPECT_EQ(thread.pc(), initial_pc);
  EXPECT_EQ(thread.status(), ThreadStatus::Ready);
}

TEST_F(InstExecuteEngineTest, ThirdFmaSourceFaultDoesNotStageItsDestination) {
  const auto initialized =
      bind(LaneId{0}, {RawWidth::b32, RawWidth::b32, RawWidth::b32, RawWidth::b32});
  const auto uninitialized =
      bind(LaneId{1}, {RawWidth::b32, RawWidth::b32, RawWidth::b32, RawWidth::b32});
  auto initialized_registers = view(initialized);
  auto uninitialized_registers = view(uninitialized);
  ASSERT_TRUE(initialized_registers.write(RegisterSlot{0}, RawValue::b32(0U)));
  ASSERT_TRUE(initialized_registers.write(RegisterSlot{1}, RawValue::b32(0x3f80'0000U)));
  ASSERT_TRUE(initialized_registers.write(RegisterSlot{2}, RawValue::b32(0x4000'0000U)));
  ASSERT_TRUE(initialized_registers.write(RegisterSlot{3}, RawValue::b32(0x4040'0000U)));
  ASSERT_TRUE(uninitialized_registers.write(RegisterSlot{0}, RawValue::b32(0xa5a5'5a5aU)));
  ASSERT_TRUE(uninitialized_registers.write(RegisterSlot{1}, RawValue::b32(0x3f80'0000U)));
  ASSERT_TRUE(uninitialized_registers.write(RegisterSlot{2}, RawValue::b32(0x4000'0000U)));
  warp().thread(LaneId{0}).set_pc(initial_pc);
  warp().thread(LaneId{1}).set_pc(initial_pc);
  const auto instruction = fma_rn_f32(std::nullopt, RegisterSlot{0},
                                      RegisterSlot{1}, RegisterSlot{2},
                                      RegisterSlot{3});

  const auto result = engine_.execute(warp(), issue(initial_pc, {0, 1}),
                                      instruction, ProgramCounter{61});

  ASSERT_TRUE(result);
  ASSERT_EQ(result->faults.size(), 1U);
  EXPECT_EQ(result->faults.front().lane, LaneId{1});
  EXPECT_EQ(std::get<memory::RegisterError>(result->faults.front().cause).code,
            memory::RegisterErrorCode::uninitialized_read);
  EXPECT_EQ(*initialized_registers.read(RegisterSlot{0}), RawValue::b32(0x40a0'0000U));
  EXPECT_EQ(*uninitialized_registers.read(RegisterSlot{0}), RawValue::b32(0xa5a5'5a5aU));
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), ProgramCounter{61});
  EXPECT_EQ(warp().thread(LaneId{1}).pc(), initial_pc);
  EXPECT_EQ(warp().thread(LaneId{1}).status(), ThreadStatus::Trapped);
}

TEST_F(InstExecuteEngineTest, SubLaneFaultDoesNotBlockOtherLane) {
  const auto initialized =
      bind(LaneId{0}, {RawWidth::b32, RawWidth::b32, RawWidth::b32});
  const auto uninitialized =
      bind(LaneId{1}, {RawWidth::b32, RawWidth::b32, RawWidth::b32});
  auto initialized_registers = view(initialized);
  auto uninitialized_registers = view(uninitialized);
  ASSERT_TRUE(initialized_registers.write(RegisterSlot{0}, RawValue::b32(5U)));
  ASSERT_TRUE(initialized_registers.write(RegisterSlot{1}, RawValue::b32(2U)));
  ASSERT_TRUE(
      uninitialized_registers.write(RegisterSlot{0}, RawValue::b32(5U)));
  warp().thread(LaneId{0}).set_pc(initial_pc);
  warp().thread(LaneId{1}).set_pc(initial_pc);
  const exec_ir::Instruction instruction =
      sub(std::nullopt, exec_ir::DataType::u32, RegisterSlot{2},
          RegisterSlot{0}, RegisterSlot{1});

  const auto result = engine_.execute(warp(), issue(initial_pc, {0, 1}),
                                      instruction, ProgramCounter{59});

  ASSERT_TRUE(result);
  ASSERT_EQ(result->faults.size(), 1u);
  EXPECT_EQ(result->faults.front().lane, LaneId{1});
  EXPECT_EQ(std::get<memory::RegisterError>(result->faults.front().cause).code,
            memory::RegisterErrorCode::uninitialized_read);
  EXPECT_EQ(*initialized_registers.read(RegisterSlot{2}), RawValue::b32(3U));
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), ProgramCounter{59});
  EXPECT_EQ(warp().thread(LaneId{1}).pc(), initial_pc);
  EXPECT_EQ(warp().thread(LaneId{1}).status(), ThreadStatus::Trapped);
  EXPECT_FALSE(*uninitialized_registers.initialized(RegisterSlot{2}));
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

/** @brief Construct an ordinary vector transfer for focused boundary checks. */
auto vector_memory(bool load, exec_ir::DataType type,
                   exec_ir::VectorArity arity,
                   exec_ir::RegisterVector registers, exec_ir::Address address,
                   exec_ir::AddressSpace space = exec_ir::AddressSpace::global)
    -> exec_ir::Instruction {
  if (load) {
    return exec_ir::Ld{std::nullopt,
                       exec_ir::Ld::ExplicitVector{
                           .semantics = exec_ir::MemoryConsistency::omitted,
                           .scope = exec_ir::MemoryScope::none,
                           .state_space = space,
                           .cache = exec_ir::CacheOperator::unspecified,
                           .vector = arity,
                           .type = type,
                           .dst = std::move(registers),
                           .address = std::move(address)}};
  }
  return exec_ir::St{std::nullopt,
                     exec_ir::St::ExplicitVector{
                         .semantics = exec_ir::MemoryConsistency::omitted,
                         .scope = exec_ir::MemoryScope::none,
                         .state_space = space,
                         .cache = exec_ir::CacheOperator::unspecified,
                         .vector = arity,
                         .type = type,
                         .address = std::move(address),
                         .src = std::move(registers)}};
}

TEST_F(InstExecuteEngineTest, VectorLoadPreflightsEveryDestination) {
  const auto global = runtime_.address_spaces().create_global({8});
  ASSERT_TRUE(runtime_.bind_global(global));
  auto memory = runtime_.address_spaces().view(global);
  ASSERT_TRUE(memory);
  ASSERT_TRUE(
      memory->initialize(memory::Address{0}, std::array<std::byte, 8>{}));
  auto registers = view(bind(LaneId{0}, {RawWidth::b32, RawWidth::b16}));
  ASSERT_TRUE(registers.write(RegisterSlot{0}, RawValue::b32(77U)));
  warp().thread(LaneId{0}).set_pc(initial_pc);
  const auto load =
      vector_memory(true, exec_ir::DataType::u32, exec_ir::VectorArity::v2,
                    {{RegisterSlot{0}, RegisterSlot{1}}},
                    exec_ir::Address{RawValue::b64(std::uint64_t{0})});
  const auto result =
      engine_.execute(warp(), issue(initial_pc, {0}), load, move_fallthrough);
  ASSERT_TRUE(result);
  ASSERT_EQ(result->faults.size(), 1U);
  EXPECT_EQ(*registers.read(RegisterSlot{0}), RawValue::b32(77U));
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), initial_pc);
}

TEST_F(InstExecuteEngineTest, VectorStoreChecksWholeSpanBeforeMutation) {
  const auto global = runtime_.address_spaces().create_global({4});
  ASSERT_TRUE(runtime_.bind_global(global));
  auto memory = runtime_.address_spaces().view(global);
  ASSERT_TRUE(memory);
  const std::array original{std::byte{77}, std::byte{0}, std::byte{0},
                            std::byte{0}};
  ASSERT_TRUE(memory->initialize(memory::Address{0}, original));
  auto registers = view(bind(LaneId{0}, {RawWidth::b32, RawWidth::b32}));
  ASSERT_TRUE(registers.write(RegisterSlot{0}, RawValue::b32(1U)));
  ASSERT_TRUE(registers.write(RegisterSlot{1}, RawValue::b32(2U)));
  warp().thread(LaneId{0}).set_pc(initial_pc);
  const auto store =
      vector_memory(false, exec_ir::DataType::u32, exec_ir::VectorArity::v2,
                    {{RegisterSlot{0}, RegisterSlot{1}}},
                    exec_ir::Address{RawValue::b64(std::uint64_t{0})});
  const auto result =
      engine_.execute(warp(), issue(initial_pc, {0}), store, move_fallthrough);
  ASSERT_TRUE(result);
  ASSERT_EQ(result->faults.size(), 1U);
  EXPECT_EQ(*memory->snapshot(memory::Address{0}, 4),
            (std::vector<std::byte>{original.begin(), original.end()}));
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), initial_pc);
}

TEST_F(InstExecuteEngineTest,
       RejectsInvalidMemoryVectorsBeforeResolvingResources) {
  warp().thread(LaneId{0}).set_pc(initial_pc);
  const exec_ir::Address address{RawValue::b64(std::uint64_t{0})};
  const exec_ir::RegisterVector pair{{RegisterSlot{0}, RegisterSlot{1}}};
  for (const auto& operation :
       {vector_memory(true, exec_ir::DataType::u8, exec_ir::VectorArity::v8,
                      {{RegisterSlot{0}, RegisterSlot{1}, RegisterSlot{2},
                        RegisterSlot{3}, RegisterSlot{4}, RegisterSlot{5},
                        RegisterSlot{6}, RegisterSlot{7}}},
                      address),
        vector_memory(true, exec_ir::DataType::u32, exec_ir::VectorArity::v2,
                      {{RegisterSlot{0}}}, address),
        vector_memory(true, exec_ir::DataType::u32, exec_ir::VectorArity::v2,
                      {{RegisterSlot{0}, std::nullopt}}, address),
        vector_memory(true, exec_ir::DataType::u64, exec_ir::VectorArity::v4,
                      {{RegisterSlot{0}, RegisterSlot{1}, RegisterSlot{2},
                        RegisterSlot{3}}},
                      address, exec_ir::AddressSpace::shared),
        vector_memory(false, exec_ir::DataType::u32, exec_ir::VectorArity::v2,
                      pair, address, exec_ir::AddressSpace::param)}) {
    const auto result = engine_.execute(warp(), issue(initial_pc, {0}),
                                        operation, move_fallthrough);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, StepErrorCode::unsupported_instruction);
    EXPECT_EQ(warp().thread(LaneId{0}).pc(), initial_pc);
    EXPECT_EQ(warp().thread(LaneId{0}).status(), ThreadStatus::Ready);
  }
}

TEST_F(InstExecuteEngineTest, LoadsConstantSigned64Into128BitRegister) {
  const auto constant = runtime_.address_spaces().create_constant({8});
  ASSERT_TRUE(runtime_.bind_constant(constant));
  auto memory = runtime_.address_spaces().view(constant);
  ASSERT_TRUE(memory);
  std::array<std::byte, 8> negative_one;
  negative_one.fill(std::byte{255});
  ASSERT_TRUE(memory->initialize(memory::Address{0}, negative_one));
  auto registers = view(bind(LaneId{0}, {RawWidth::b128}));
  warp().thread(LaneId{0}).set_pc(initial_pc);
  const auto load = make_load(
      std::nullopt, exec_ir::DataType::s64, exec_ir::AddressSpace::const_,
      RegisterSlot{0}, exec_ir::Address{RawValue::b64(std::uint64_t{0})});
  const auto result =
      engine_.execute(warp(), issue(initial_pc, {0}), load, move_fallthrough);
  ASSERT_TRUE(result);
  EXPECT_TRUE(result->faults.empty());
  EXPECT_EQ(*registers.read(RegisterSlot{0}),
            RawValue::b128(common::Bits128{UINT64_MAX, UINT64_MAX}));
}

TEST_F(InstExecuteEngineTest, AddressOffsetCannotWrapIntoValidMemory) {
  const auto global = runtime_.address_spaces().create_global({4});
  ASSERT_TRUE(runtime_.bind_global(global));
  auto registers = view(bind(LaneId{0}, {RawWidth::b32}));
  ASSERT_TRUE(registers.write(RegisterSlot{0}, RawValue::b32(77U)));
  for (const bool subtract : {false, true}) {
    auto& thread = warp().thread(LaneId{0});
    thread.mark_ready();
    thread.set_pc(initial_pc);
    const exec_ir::Address address{
        RawValue::b64(subtract ? std::uint64_t{0} : UINT64_MAX),
        exec_ir::AddressOffset{subtract, RawValue::b64(std::uint64_t{1})}};
    const auto load =
        make_load(std::nullopt, exec_ir::DataType::u32,
                  exec_ir::AddressSpace::global, RegisterSlot{0}, address);
    const auto result =
        engine_.execute(warp(), issue(initial_pc, {0}), load, move_fallthrough);
    ASSERT_TRUE(result);
    ASSERT_EQ(result->faults.size(), 1U);
    EXPECT_TRUE(std::holds_alternative<memory::AddressResolutionError>(
        result->faults[0].cause));
    EXPECT_EQ(*registers.read(RegisterSlot{0}), RawValue::b32(77U));
    EXPECT_EQ(thread.pc(), initial_pc);
  }
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

TEST_F(InstExecuteEngineTest, LoadsEntryParameterU32FromImmediateOffset) {
  const auto parameter = runtime_.address_spaces().create_entry_parameter({4});
  ASSERT_TRUE(runtime_.bind_entry_parameter(parameter));
  auto memory = runtime_.address_spaces().view(parameter);
  ASSERT_TRUE(memory);
  ASSERT_TRUE(memory->initialize(memory::Address{0},
                                 std::array{std::byte{0x78}, std::byte{0x56},
                                            std::byte{0x34}, std::byte{0x12}}));
  const auto frame = bind(LaneId{0}, {RawWidth::b32});
  auto registers = view(frame);
  warp().thread(LaneId{0}).set_pc(initial_pc);
  const exec_ir::Instruction load = make_load(
      std::nullopt, exec_ir::DataType::u32, exec_ir::AddressSpace::param,
      RegisterSlot{0}, exec_ir::Address{RawValue::b64(std::uint64_t{0})});

  const auto result =
      engine_.execute(warp(), issue(initial_pc, {0}), load, ProgramCounter{57});

  ASSERT_TRUE(result);
  EXPECT_TRUE(result->faults.empty());
  EXPECT_EQ(*registers.read(RegisterSlot{0}), RawValue::b32(0x12345678U));
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), ProgramCounter{57});
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

TEST_F(InstExecuteEngineTest, ReleasesWarpSyncWhenMissingMemberExited) {
  warp().thread(LaneId{0}).set_pc(initial_pc);
  warp().thread(LaneId{1}).set_pc(initial_pc);
  warp().thread(LaneId{1}).mark_exited();
  const exec_ir::Instruction bar = make_bar(std::nullopt, RawValue::b32(3U));
  const auto result =
      engine_.execute(warp(), issue(initial_pc, {0}), bar, ProgramCounter{11});
  ASSERT_TRUE(result);
  EXPECT_FALSE(warp().execution_state().sync.active());
  EXPECT_TRUE(warp().thread(LaneId{0}).ready());
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), ProgramCounter{11});
}

TEST_F(InstExecuteEngineTest, PredicatedOffWarpSyncFallsThrough) {
  const auto frame = bind(LaneId{0}, {RawWidth::pred});
  ASSERT_TRUE(view(frame).write(RegisterSlot{0}, RawValue::pred(false)));
  warp().thread(LaneId{0}).set_pc(initial_pc);
  const exec_ir::Instruction bar =
      make_bar(exec_ir::Predicate{RegisterSlot{0}, false}, RawValue::b32(1U));
  const auto result =
      engine_.execute(warp(), issue(initial_pc, {0}), bar, ProgramCounter{11});
  ASSERT_TRUE(result);
  EXPECT_FALSE(warp().execution_state().sync.active());
  EXPECT_TRUE(warp().thread(LaneId{0}).ready());
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), ProgramCounter{11});
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

TEST(InstExecuteEngineWarpSyncTest, FullMaskIgnoresAbsentFinalWarpLanes) {
  runtime::LaunchRuntime runtime{
      grid_id,
      {.cta_dim = {1, 1, 1}, .thread_dim = {3, 1, 1}, .warp_size = 32}};
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
  const exec_ir::Instruction bar =
      make_bar(std::nullopt, RawValue::b32(0xffffffffU));
  LaneMask lanes{32};
  for (const auto lane : {LaneId{0}, LaneId{1}, LaneId{2}}) {
    lanes.set(lane);
  }
  const auto result =
      engine.execute(warp, WarpIssueGroup{.pc = initial_pc, .lanes = lanes},
                     bar, ProgramCounter{11});
  ASSERT_TRUE(result);
  EXPECT_FALSE(warp.execution_state().sync.active());
  EXPECT_EQ(warp.thread(LaneId{2}).pc(), ProgramCounter{11});
}

TEST(InstExecuteEngineCtaBarrierTest,
     SynchronizesTwoWarpsAndMixesArriveWithSync) {
  runtime::LaunchRuntime runtime{
      grid_id, {.cta_dim = {1, 1, 1}, .thread_dim = {4, 1, 1}, .warp_size = 2}};
  arith::context arithmetic;
  InstExecuteEngine engine{runtime, function, arithmetic};
  auto& cta = runtime.grid().cta(CtaId{grid_id, 0});
  auto& first_warp = cta.warp(0);
  auto& second_warp = cta.warp(1);
  const auto all_lanes = [](ProgramCounter pc) {
    LaneMask lanes{2};
    lanes.set(LaneId{0});
    lanes.set(LaneId{1});
    return WarpIssueGroup{.pc = pc, .lanes = std::move(lanes)};
  };
  for (auto* warp : {&first_warp, &second_warp}) {
    for (auto& thread : *warp) {
      thread.set_pc(initial_pc);
    }
  }

  const auto arrive = engine.execute(first_warp, all_lanes(initial_pc),
                                     make_cta_arrive(0, 4), ProgramCounter{11});
  ASSERT_TRUE(arrive);
  for (const auto& thread : first_warp) {
    EXPECT_TRUE(thread.ready());
    EXPECT_EQ(thread.pc(), ProgramCounter{11});
  }
  const auto sync = engine.execute(second_warp, all_lanes(initial_pc),
                                   make_cta_sync(0, 4), ProgramCounter{11});
  ASSERT_TRUE(sync);
  EXPECT_FALSE(cta.execution_state()
                   .barriers.barrier(execution_model::CtaBarrierId{0})
                   .active());
  for (auto* warp : {&first_warp, &second_warp}) {
    for (const auto& thread : *warp) {
      EXPECT_TRUE(thread.ready());
      EXPECT_EQ(thread.pc(), ProgramCounter{11});
    }
  }
}

TEST(InstExecuteEngineCtaBarrierTest,
     RejectsInvalidIdAndZeroCountBeforeArrival) {
  runtime::LaunchRuntime runtime{
      grid_id, {.cta_dim = {1, 1, 1}, .thread_dim = {2, 1, 1}, .warp_size = 2}};
  arith::context arithmetic;
  InstExecuteEngine engine{runtime, function, arithmetic};
  auto& warp = runtime.grid().cta(CtaId{grid_id, 0}).warp(0);
  LaneMask lanes{2};
  lanes.set(LaneId{0});
  lanes.set(LaneId{1});
  const WarpIssueGroup group{.pc = initial_pc, .lanes = lanes};
  for (auto& thread : warp) {
    thread.set_pc(initial_pc);
  }
  for (const auto instruction : {make_cta_sync(16, 2), make_cta_sync(0, 0)}) {
    const auto rejected =
        engine.execute(warp, group, instruction, ProgramCounter{11});
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code, StepErrorCode::collective_invalid_mask);
    EXPECT_FALSE(warp.cta()
                     .execution_state()
                     .barriers.barrier(execution_model::CtaBarrierId{0})
                     .active());
    for (const auto& thread : warp) {
      EXPECT_TRUE(thread.ready());
      EXPECT_EQ(thread.pc(), initial_pc);
    }
  }
}

TEST(InstExecuteEngineCtaBarrierTest,
     CompletionDoesNotReleaseAnotherWarpBeforeLocalConvergence) {
  runtime::LaunchRuntime runtime{
      grid_id, {.cta_dim = {1, 1, 1}, .thread_dim = {4, 1, 1}, .warp_size = 2}};
  arith::context arithmetic;
  InstExecuteEngine engine{runtime, function, arithmetic};
  auto& cta = runtime.grid().cta(CtaId{grid_id, 0});
  auto& completing_warp = cta.warp(0);
  auto& partial_warp = cta.warp(1);
  const auto group = [](ProgramCounter pc,
                        std::initializer_list<unsigned> ids) {
    LaneMask lanes{2};
    for (const auto id : ids) {
      lanes.set(LaneId{id});
    }
    return WarpIssueGroup{.pc = pc, .lanes = std::move(lanes)};
  };
  for (auto& thread : completing_warp) {
    thread.set_pc(initial_pc);
  }
  partial_warp.thread(LaneId{0}).set_pc(initial_pc);
  partial_warp.thread(LaneId{1}).set_pc(ProgramCounter{20});

  ASSERT_TRUE(engine.execute(partial_warp, group(initial_pc, {0}),
                             make_cta_sync(0, 2), ProgramCounter{11}));
  ASSERT_TRUE(engine.execute(completing_warp, group(initial_pc, {0, 1}),
                             make_cta_sync(0, 2), ProgramCounter{11}));
  EXPECT_TRUE(partial_warp.thread(LaneId{0}).waiting());
  EXPECT_EQ(partial_warp.thread(LaneId{0}).pc(), initial_pc);
  EXPECT_TRUE(cta.execution_state()
                  .barriers.barrier(execution_model::CtaBarrierId{0})
                  .active());

  partial_warp.thread(LaneId{1}).set_pc(initial_pc);
  ASSERT_TRUE(engine.execute(partial_warp, group(initial_pc, {1}),
                             make_cta_sync(0, 2), ProgramCounter{11}));
  EXPECT_FALSE(cta.execution_state()
                   .barriers.barrier(execution_model::CtaBarrierId{0})
                   .active());
  for (const auto& thread : partial_warp) {
    EXPECT_TRUE(thread.ready());
    EXPECT_EQ(thread.pc(), ProgramCounter{11});
  }
}

TEST_F(InstExecuteEngineTest, RejectsSiblingWarpBarrierAtAnotherResource) {
  warp().thread(LaneId{0}).set_pc(initial_pc);
  warp().thread(LaneId{1}).set_pc(ProgramCounter{20});
  ASSERT_TRUE(engine_.execute(warp(), issue(initial_pc, {0}), make_cta_sync(0),
                              ProgramCounter{11}));
  const auto rejected = engine_.execute(warp(), issue(ProgramCounter{20}, {1}),
                                        make_cta_sync(1), ProgramCounter{21});
  ASSERT_FALSE(rejected);
  EXPECT_EQ(rejected.error().code, StepErrorCode::collective_pending_mismatch);
  EXPECT_TRUE(warp().thread(LaneId{0}).waiting());
  EXPECT_TRUE(warp().thread(LaneId{1}).ready());
  EXPECT_FALSE(warp()
                   .cta()
                   .execution_state()
                   .barriers.barrier(execution_model::CtaBarrierId{1})
                   .active());
}

TEST_F(InstExecuteEngineTest, ExitReleasesAWaitingCtaBarrierWarp) {
  warp().thread(LaneId{0}).set_pc(initial_pc);
  warp().thread(LaneId{1}).set_pc(ProgramCounter{20});
  const auto first = engine_.execute(warp(), issue(initial_pc, {0}),
                                     make_cta_sync(0), ProgramCounter{11});
  ASSERT_TRUE(first);
  EXPECT_TRUE(warp().thread(LaneId{0}).waiting());
  const auto exit_result = engine_.execute(
      warp(), issue(ProgramCounter{20}, {1}), exit(), std::nullopt);
  ASSERT_TRUE(exit_result);
  EXPECT_TRUE(warp().thread(LaneId{1}).exited());
  EXPECT_TRUE(warp().thread(LaneId{0}).ready());
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), ProgramCounter{11});
  EXPECT_FALSE(warp()
                   .cta()
                   .execution_state()
                   .barriers.barrier(execution_model::CtaBarrierId{0})
                   .active());
}

TEST_F(InstExecuteEngineTest,
       RejectsSamePcCtaBarrierArrivalWithDifferentSuccessorWithoutMutation) {
  warp().thread(LaneId{0}).set_pc(initial_pc);
  warp().thread(LaneId{1}).set_pc(initial_pc);
  ASSERT_TRUE(engine_.execute(warp(), issue(initial_pc, {0}), make_cta_sync(0),
                              ProgramCounter{11}));
  const auto rejected = engine_.execute(warp(), issue(initial_pc, {1}),
                                        make_cta_sync(0), ProgramCounter{12});
  ASSERT_FALSE(rejected);
  EXPECT_EQ(rejected.error().code, StepErrorCode::collective_pending_mismatch);
  EXPECT_TRUE(warp().thread(LaneId{0}).waiting());
  EXPECT_TRUE(warp().thread(LaneId{1}).ready());
  EXPECT_EQ(warp().thread(LaneId{1}).pc(), initial_pc);
}

TEST_F(InstExecuteEngineTest, RejectsCtaBarrierWhileWarpSyncIsPending) {
  warp().thread(LaneId{0}).set_pc(initial_pc);
  warp().thread(LaneId{1}).set_pc(initial_pc);
  ASSERT_TRUE(engine_.execute(warp(), issue(initial_pc, {0}),
                              make_bar(std::nullopt, RawValue::b32(3U)),
                              ProgramCounter{11}));
  const auto rejected = engine_.execute(warp(), issue(initial_pc, {1}),
                                        make_cta_sync(0), ProgramCounter{12});
  ASSERT_FALSE(rejected);
  EXPECT_EQ(rejected.error().code, StepErrorCode::collective_pending_mismatch);
  EXPECT_TRUE(warp().thread(LaneId{0}).waiting());
  EXPECT_TRUE(warp().thread(LaneId{1}).ready());
  EXPECT_FALSE(warp()
                   .cta()
                   .execution_state()
                   .barriers.barrier(execution_model::CtaBarrierId{0})
                   .active());
}

TEST(InstExecuteEngineCtaBarrierTest,
     RejectsMixedProtocolCountMismatchAndDuplicateArrival) {
  runtime::LaunchRuntime runtime{
      grid_id, {.cta_dim = {1, 1, 1}, .thread_dim = {2, 1, 1}, .warp_size = 1}};
  arith::context arithmetic;
  InstExecuteEngine engine{runtime, function, arithmetic};
  auto& cta = runtime.grid().cta(CtaId{grid_id, 0});
  auto& first_warp = cta.warp(0);
  auto& second_warp = cta.warp(1);
  const auto group = [](ProgramCounter pc) {
    LaneMask lanes{1};
    lanes.set(LaneId{0});
    return WarpIssueGroup{.pc = pc, .lanes = std::move(lanes)};
  };
  for (auto* warp : {&first_warp, &second_warp}) {
    warp->thread(LaneId{0}).set_pc(initial_pc);
  }
  const auto first = engine.execute(first_warp, group(initial_pc),
                                    make_cta_sync(0, 2), ProgramCounter{11});
  ASSERT_TRUE(first);

  const auto second_frame = runtime.registers().create_frame(
      {.slot_widths = {RawWidth::b32, RawWidth::pred}});
  ASSERT_TRUE(second_frame);
  ASSERT_TRUE(runtime.bind_register_frame(second_warp.thread(LaneId{0}).id(),
                                          function, *second_frame));
  auto registers = runtime.registers().view(*second_frame);
  ASSERT_TRUE(registers);
  ASSERT_TRUE(registers->write(RegisterSlot{1}, RawValue::pred(true)));
  const auto mixed = engine.execute(
      second_warp, group(initial_pc),
      make_cta_red_popc(RegisterSlot{0}, 0,
                        exec_ir::Predicate{RegisterSlot{1}, false}),
      ProgramCounter{11});
  ASSERT_FALSE(mixed);
  EXPECT_EQ(mixed.error().code, StepErrorCode::collective_pending_mismatch);
  EXPECT_TRUE(first_warp.thread(LaneId{0}).waiting());
  EXPECT_TRUE(second_warp.thread(LaneId{0}).ready());

  const auto mismatched = engine.execute(
      second_warp, group(initial_pc), make_cta_sync(0, 1), ProgramCounter{11});
  ASSERT_FALSE(mismatched);
  EXPECT_EQ(mismatched.error().code,
            StepErrorCode::collective_pending_mismatch);
  first_warp.thread(LaneId{0}).mark_ready();
  const auto duplicate = engine.execute(
      first_warp, group(initial_pc), make_cta_sync(0, 2), ProgramCounter{11});
  ASSERT_FALSE(duplicate);
  EXPECT_EQ(duplicate.error().code,
            StepErrorCode::collective_duplicate_arrival);
  EXPECT_TRUE(cta.execution_state()
                  .barriers.barrier(execution_model::CtaBarrierId{0})
                  .active());
}

TEST(InstExecuteEngineCtaBarrierTest,
     DeferredRemoteReductionFaultUsesOwningWarpAndTrapsOwningLane) {
  runtime::LaunchRuntime runtime{
      grid_id, {.cta_dim = {1, 1, 1}, .thread_dim = {2, 1, 1}, .warp_size = 1}};
  arith::context arithmetic;
  InstExecuteEngine engine{runtime, function, arithmetic};
  auto& cta = runtime.grid().cta(CtaId{grid_id, 0});
  auto& first_warp = cta.warp(0);
  auto& second_warp = cta.warp(1);
  const auto group = [](ProgramCounter pc) {
    LaneMask lanes{1};
    lanes.set(LaneId{0});
    return WarpIssueGroup{.pc = pc, .lanes = std::move(lanes)};
  };
  const auto bind = [&](Warp& warp) {
    const auto frame = runtime.registers().create_frame(
        {.slot_widths = {RawWidth::b32, RawWidth::pred}});
    EXPECT_TRUE(frame);
    EXPECT_TRUE(runtime.bind_register_frame(warp.thread(LaneId{0}).id(),
                                            function, *frame));
    auto registers = runtime.registers().view(*frame);
    EXPECT_TRUE(registers);
    EXPECT_TRUE(registers->write(RegisterSlot{1}, RawValue::pred(true)));
    return *frame;
  };
  const auto first_frame = bind(first_warp);
  bind(second_warp);
  first_warp.thread(LaneId{0}).set_pc(initial_pc);
  second_warp.thread(LaneId{0}).set_pc(initial_pc);
  const auto reduction = make_cta_red_popc(
      RegisterSlot{0}, 0, exec_ir::Predicate{RegisterSlot{1}, false});
  ASSERT_TRUE(engine.execute(first_warp, group(initial_pc), reduction,
                             ProgramCounter{11}));
  ASSERT_TRUE(runtime.registers().destroy_frame(first_frame));
  const auto released = engine.execute(second_warp, group(initial_pc),
                                       reduction, ProgramCounter{11});
  ASSERT_TRUE(released);
  ASSERT_EQ(released->faults.size(), 1U);
  EXPECT_EQ(released->faults.front().lane, LaneId{0});
  EXPECT_EQ(released->faults.front().warp, first_warp.id());
  EXPECT_TRUE(first_warp.thread(LaneId{0}).trapped());
  EXPECT_FALSE(second_warp.thread(LaneId{0}).trapped());
}

TEST(InstExecuteEngineCtaBarrierTest,
     ExitCompletesExplicitSubsetWithoutWaitingForAnUnrelatedLiveWarp) {
  runtime::LaunchRuntime runtime{
      grid_id, {.cta_dim = {1, 1, 1}, .thread_dim = {4, 1, 1}, .warp_size = 2}};
  arith::context arithmetic;
  InstExecuteEngine engine{runtime, function, arithmetic};
  auto& cta = runtime.grid().cta(CtaId{grid_id, 0});
  auto& unrelated = cta.warp(0);
  auto& participating = cta.warp(1);
  const auto group = [](ProgramCounter pc,
                        std::initializer_list<unsigned> ids) {
    LaneMask lanes{2};
    for (const auto id : ids) {
      lanes.set(LaneId{id});
    }
    return WarpIssueGroup{.pc = pc, .lanes = std::move(lanes)};
  };
  for (auto& thread : unrelated) {
    thread.set_pc(ProgramCounter{30});
  }
  participating.thread(LaneId{0}).set_pc(initial_pc);
  participating.thread(LaneId{1}).set_pc(ProgramCounter{20});
  ASSERT_TRUE(engine.execute(participating, group(initial_pc, {0}),
                             make_cta_sync(0, 2), ProgramCounter{11}));
  ASSERT_TRUE(engine.execute(participating, group(ProgramCounter{20}, {1}),
                             exit(), std::nullopt));
  EXPECT_TRUE(unrelated.thread(LaneId{0}).ready());
  EXPECT_TRUE(participating.thread(LaneId{0}).ready());
  EXPECT_EQ(participating.thread(LaneId{0}).pc(), ProgramCounter{11});
  EXPECT_FALSE(cta.execution_state()
                   .barriers.barrier(execution_model::CtaBarrierId{0})
                   .active());
}

}  // namespace
}  // namespace ptxsim::inst_execute_engine::test
