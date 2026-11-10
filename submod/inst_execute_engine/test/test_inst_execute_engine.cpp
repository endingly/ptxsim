#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <memory>
#include <limits>
#include <stdexcept>
#include <concepts>
#include <ptxsim/exec_ir/exec_ir.gen.hpp>
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

/** @brief Raw fixture inputs remain separate from owned execution operands. */
using ScalarInput = std::variant<RegisterSlot,RawValue>;
/** @brief A raw lane list used only to construct exact owned register views. */
using RegisterInputs = std::vector<std::optional<RegisterSlot>>;
/** @brief Construct one declared slot view; frame width validation remains lane-local. */
auto reg(RegisterSlot slot,exec_ir::DataType type) -> exec_ir::RegisterRef {
  return {.slot=slot,.register_class=type==exec_ir::DataType::pred?exec_ir::RegisterClass::Predicate:exec_ir::RegisterClass::General,.declared_type=type};
}
/** @brief Retain fixture raw bits at their actual width so width-fault tests stay meaningful. */
auto immediate(RawValue value) -> exec_ir::Immediate {
  switch(value.width()) {
    case RawWidth::pred:return {static_cast<std::uint64_t>(*value.as_pred()),exec_ir::DataType::pred};
    case RawWidth::b8:return {*value.as_b8(),exec_ir::DataType::b8};
    case RawWidth::b16:return {*value.as_b16(),exec_ir::DataType::b16};
    case RawWidth::b32:return {*value.as_b32(),exec_ir::DataType::b32};
    case RawWidth::b64:return {*value.as_b64(),exec_ir::DataType::b64};
    case RawWidth::b128: {
      const auto bits=*value.as_b128();
      if(bits.high!=0 && bits.high!=std::numeric_limits<std::uint64_t>::max())
        throw std::invalid_argument("not an owning frontend immediate representation");
      return {bits.low,exec_ir::DataType::b128,bits.high!=0,bits.low};
    }
  }
  throw std::invalid_argument("unknown raw width");
}
/** @brief Bind a test arithmetic register or preserve an immediate's physical width. */
auto scalar(ScalarInput input,exec_ir::DataType type) -> exec_ir::ScalarOperand {
  if(const auto* slot=std::get_if<RegisterSlot>(&input)) return reg(*slot,type);
  return immediate(std::get<RawValue>(input));
}
/** @brief Construct a non-owning register address with real typed storage identity. */
auto make_address(RegisterSlot slot) -> exec_ir::Address {return {reg(slot,exec_ir::DataType::b64)};}
/** @brief Construct a raw numeric address while retaining its original bits and width. */
auto make_address(RawValue value) -> exec_ir::Address {return {immediate(value)};}
/** @brief Bind source-order register lanes, preserving actual sinks. */
auto register_vector(RegisterInputs inputs,exec_ir::DataType type=exec_ir::DataType::b32) -> exec_ir::RegisterVector {
  exec_ir::RegisterVector result;
  for(const auto& slot:inputs) result.elements.push_back(slot?std::optional{reg(*slot,type)}:std::nullopt);
  return result;
}
/** @brief Build canonical intrinsic metadata for direct execution fixtures. */
auto intrinsic(exec_ir::SpecialRegisterId id,std::optional<std::uint8_t> component=std::nullopt) -> exec_ir::SpecialRegisterRef {
  const bool vector=id.kind==exec_ir::SpecialRegisterKind::Tid || id.kind==exec_ir::SpecialRegisterKind::NTid ||
      id.kind==exec_ir::SpecialRegisterKind::CtaId || id.kind==exec_ir::SpecialRegisterKind::NCtaId;
  return {id,component,id.kind==exec_ir::SpecialRegisterKind::GridId?exec_ir::DataType::u64:exec_ir::DataType::u32,
      static_cast<std::uint8_t>(vector?4:1)};
}
/** @brief Construction-only movement inputs; the resulting instruction has only native typed leaves. */
using MoveInput = std::variant<RegisterSlot,RawValue,exec_ir::SpecialRegisterRef,exec_ir::Address,
    exec_ir::SymbolRef,exec_ir::FunctionRef,exec_ir::OpaqueSymbolRef>;
/** @brief Construct an exact scalar move whose lifetime is explicit at each call site. */
auto mov(std::optional<exec_ir::Predicate> predicate,exec_ir::DataType type,RegisterSlot destination,MoveInput source)
    -> std::unique_ptr<exec_ir::Instruction> {
  auto owned=std::visit([type](const auto& input)->exec_ir::MovSource {
    using T=std::remove_cvref_t<decltype(input)>;
    if constexpr(std::same_as<T,RegisterSlot>) return reg(input,type);
    else if constexpr(std::same_as<T,RawValue>) return immediate(input);
    else return input;
  },source);
  return std::make_unique<exec_ir::MovScalar>(exec_ir::MovScalar::ScalarOperands{reg(destination,type),std::move(owned)},
      exec_ir::MovScalar::Controls{type},predicate);
}
/** @brief Build an aggregate pack with the real native semantic form, including split b128. */
auto mov_pack(exec_ir::DataType type,RegisterSlot destination,RegisterInputs source) -> std::unique_ptr<exec_ir::Instruction> {
  auto lanes=register_vector(std::move(source));
  if(type==exec_ir::DataType::b128) return std::make_unique<exec_ir::MovB128PackUnpack>(
      exec_ir::MovB128PackUnpack::PackOperands{reg(destination,type),std::move(lanes)});
  return std::make_unique<exec_ir::MovScalar>(exec_ir::MovScalar::PackOperands{reg(destination,type),std::move(lanes)},exec_ir::MovScalar::Controls{type});
}
/** @brief Build an aggregate unpack with actual ordered typed destinations and sinks. */
auto mov_unpack(exec_ir::DataType type,RegisterInputs destination,RegisterSlot source) -> std::unique_ptr<exec_ir::Instruction> {
  auto lanes=register_vector(std::move(destination));
  if(type==exec_ir::DataType::b128) return std::make_unique<exec_ir::MovB128PackUnpack>(
      exec_ir::MovB128PackUnpack::UnpackOperands{std::move(lanes),reg(source,type)});
  return std::make_unique<exec_ir::MovScalar>(exec_ir::MovScalar::UnpackOperands{std::move(lanes),reg(source,type)},exec_ir::MovScalar::Controls{type});
}
/** @brief Build the existing four-lane topology movement without contiguous-slot aliases. */
auto mov_v4_u32(RegisterSlot destination,exec_ir::SpecialRegisterId source) -> std::unique_ptr<exec_ir::Instruction> {
  RegisterInputs slots;
  for(std::uint32_t lane=0;lane<4;++lane) slots.push_back(RegisterSlot{destination.value()+lane});
  return std::make_unique<exec_ir::MovV4U32>(exec_ir::MovV4U32::Operands{
      register_vector(std::move(slots),exec_ir::DataType::u32),exec_ir::VectorSpecialRegisterRef{intrinsic(source)}},
      exec_ir::MovV4U32::Controls{exec_ir::VectorArity::v4,exec_ir::DataType::u32});
}
/** @brief Construct a predicate copy without banning source inversion. */
auto mov_pred(exec_ir::Predicate destination,exec_ir::PredicateSource source) -> std::unique_ptr<exec_ir::Instruction> {
  return std::make_unique<exec_ir::MovPred>(exec_ir::MovPred::Operands{destination,source});
}
/** @brief Construct bound integer addition inputs with unchanged test values. */
auto add(std::optional<exec_ir::Predicate> predicate,exec_ir::DataType type,RegisterSlot destination,ScalarInput lhs,ScalarInput rhs)
    -> std::unique_ptr<exec_ir::Instruction> {
  return std::make_unique<exec_ir::AddIntegerNoSat>(exec_ir::AddIntegerNoSat::Operands{
      reg(destination,type),scalar(lhs,type),scalar(rhs,type)},exec_ir::AddIntegerNoSat::Controls{type},predicate);
}
/** @brief Construct bound integer subtraction inputs with unchanged test values. */
auto sub(std::optional<exec_ir::Predicate> predicate,exec_ir::DataType type,RegisterSlot destination,ScalarInput lhs,ScalarInput rhs)
    -> std::unique_ptr<exec_ir::Instruction> {
  return std::make_unique<exec_ir::SubIntegerNoSat>(exec_ir::SubIntegerNoSat::Operands{
      reg(destination,type),scalar(lhs,type),scalar(rhs,type)},exec_ir::SubIntegerNoSat::Controls{type},predicate);
}
/** @brief Construct the existing FMA RN form with three independent typed sources. */
auto fma_rn_f32(std::optional<exec_ir::Predicate> predicate,RegisterSlot destination,ScalarInput lhs,ScalarInput rhs,ScalarInput addend)
    -> std::unique_ptr<exec_ir::Instruction> {
  return std::make_unique<exec_ir::FmaRnF32>(exec_ir::FmaRnF32::Operands{reg(destination,exec_ir::DataType::f32),
      scalar(lhs,exec_ir::DataType::f32),scalar(rhs,exec_ir::DataType::f32),scalar(addend,exec_ir::DataType::f32)},predicate);
}
/** @brief Construct the existing unsigned comparison in its full source form. */
auto setp_lt_u32(exec_ir::Predicate destination,ScalarInput lhs,ScalarInput rhs) -> std::unique_ptr<exec_ir::Instruction> {
  return std::make_unique<exec_ir::SetpUnsigned>(exec_ir::SetpUnsigned::SingleOperands{exec_ir::PredicateOrSink{destination},
      scalar(lhs,exec_ir::DataType::u32),scalar(rhs,exec_ir::DataType::u32)},
      exec_ir::SetpUnsigned::Controls{.comparison=exec_ir::ComparisonOperator::lt,.type=exec_ir::DataType::u32});
}
/** @brief Exact comparison forms used by the existing execution fixtures. */
template <typename Form>
concept ComparisonFixtureForm = std::same_as<Form,exec_ir::SetpSigned> || std::same_as<Form,exec_ir::SetpUnsigned> ||
    std::same_as<Form,exec_ir::SetpSignedBoolean> || std::same_as<Form,exec_ir::SetpUnsignedBoolean>;
/** @brief Build a selected single/pair comparison without an opcode/form compatibility tree. */
template <ComparisonFixtureForm Form>
auto comparison(exec_ir::ComparisonOperator relation,std::variant<exec_ir::Predicate,exec_ir::PredicatePair> destination,
                ScalarInput lhs,ScalarInput rhs,std::optional<exec_ir::Predicate> combine=std::nullopt,
                exec_ir::BooleanOperator boolean=exec_ir::BooleanOperator::and_)
    -> std::unique_ptr<exec_ir::Instruction> {
  constexpr bool signed_type=std::same_as<Form,exec_ir::SetpSigned> || std::same_as<Form,exec_ir::SetpSignedBoolean>;
  constexpr bool combines=std::same_as<Form,exec_ir::SetpSignedBoolean> || std::same_as<Form,exec_ir::SetpUnsignedBoolean>;
  const auto type=signed_type?exec_ir::DataType::s32:exec_ir::DataType::u32;
  typename Form::Controls controls{.comparison=relation,.type=type};
  if constexpr(combines) {controls.boolean=boolean;if(!combine)throw std::invalid_argument("missing comparison source predicate");}
  if(const auto* single=std::get_if<exec_ir::Predicate>(&destination)) {
    typename Form::SingleOperands operands{exec_ir::PredicateOrSink{*single},scalar(lhs,type),scalar(rhs,type)};
    if constexpr(combines)operands.combine=exec_ir::PredicateSource{*combine};
    return std::make_unique<Form>(std::move(operands),controls);
  }
  const auto& pair=std::get<exec_ir::PredicatePair>(destination);
  typename Form::PairOperands operands{exec_ir::PredicatePairOrSink{pair.first,pair.second},scalar(lhs,type),scalar(rhs,type)};
  if constexpr(combines)operands.combine=exec_ir::PredicateSource{*combine};
  return std::make_unique<Form>(std::move(operands),controls);
}

/** @brief Construct a scalar load while leaving bindings and widths for lazy preparation. */
auto make_load(std::optional<exec_ir::Predicate> predicate,exec_ir::DataType type,exec_ir::AddressSpace space,RegisterSlot destination,exec_ir::Address source)
    -> std::unique_ptr<exec_ir::Instruction> {
  if(space==exec_ir::AddressSpace::generic) return std::make_unique<exec_ir::LdGenericScalar>(
      exec_ir::LdGenericScalar::Operands{reg(destination,type),source},exec_ir::LdGenericScalar::Controls{.type=type},predicate);
  return std::make_unique<exec_ir::LdExplicitScalar>(exec_ir::LdExplicitScalar::Operands{reg(destination,type),source},
      exec_ir::LdExplicitScalar::Controls{.state_space=space,.type=type},predicate);
}
/** @brief Accept a fixture address slot without recreating a runtime compatibility operand. */
auto make_load(std::optional<exec_ir::Predicate> predicate,exec_ir::DataType type,exec_ir::AddressSpace space,RegisterSlot destination,RegisterSlot source)
    -> std::unique_ptr<exec_ir::Instruction> {return make_load(predicate,type,space,destination,make_address(source));}
/** @brief Construct a scalar store with exact owned address and value operands. */
auto make_store(std::optional<exec_ir::Predicate> predicate,exec_ir::DataType type,exec_ir::AddressSpace space,RegisterSlot destination,RegisterSlot source)
    -> std::unique_ptr<exec_ir::Instruction> {
  if(space==exec_ir::AddressSpace::generic) return std::make_unique<exec_ir::StGenericScalar>(
      exec_ir::StGenericScalar::Operands{make_address(destination),reg(source,type)},exec_ir::StGenericScalar::Controls{.type=type},predicate);
  return std::make_unique<exec_ir::StExplicitScalar>(exec_ir::StExplicitScalar::Operands{make_address(destination),reg(source,type)},
      exec_ir::StExplicitScalar::Controls{.state_space=space,.type=type},predicate);
}
/** @brief Construct a warp synchronization with an owned mask operand. */
auto make_bar(std::optional<exec_ir::Predicate> predicate,ScalarInput mask) -> std::unique_ptr<exec_ir::Instruction> {
  return std::make_unique<exec_ir::BarWarpSync>(exec_ir::BarWarpSync::Operands{scalar(mask,exec_ir::DataType::b32)},predicate);
}
/** @brief Construct a CTA sync with explicit count and barrier ID. */
auto make_cta_sync(std::uint32_t barrier,std::uint32_t count) -> std::unique_ptr<exec_ir::Instruction> {
  return std::make_unique<exec_ir::BarSync>(exec_ir::BarSync::BarrierAndThreadCountOperands{
      exec_ir::ScalarOperand{exec_ir::Immediate{barrier,exec_ir::DataType::u32,false,barrier}},
      exec_ir::ScalarOperand{exec_ir::Immediate{count,exec_ir::DataType::u32,false,count}}});
}
/** @brief Construct the whole-CTA omitted-count sync. */
auto make_cta_sync(std::uint32_t barrier) -> std::unique_ptr<exec_ir::Instruction> {
  return std::make_unique<exec_ir::BarSync>(exec_ir::BarSync::ImmediateBarrierOperands{exec_ir::Immediate{barrier,exec_ir::DataType::u32,false,barrier}});
}
/** @brief Construct the existing asynchronous CTA arrival. */
auto make_cta_arrive(std::uint32_t barrier,std::uint32_t count) -> std::unique_ptr<exec_ir::Instruction> {
  return std::make_unique<exec_ir::BarArrive>(exec_ir::BarArrive::Operands{
      exec_ir::ScalarOperand{exec_ir::Immediate{barrier,exec_ir::DataType::u32,false,barrier}},
      exec_ir::ScalarOperand{exec_ir::Immediate{count,exec_ir::DataType::u32,false,count}}});
}
/** @brief Construct a waiting population-count barrier with its real predicate input. */
auto make_cta_red_popc(RegisterSlot destination,std::uint32_t barrier,exec_ir::Predicate input) -> std::unique_ptr<exec_ir::Instruction> {
  return std::make_unique<exec_ir::BarRedPopcU32>(exec_ir::BarRedPopcU32::WithoutThreadCountOperands{
      reg(destination,exec_ir::DataType::u32),exec_ir::ScalarOperand{exec_ir::Immediate{barrier,exec_ir::DataType::u32,false,barrier}},input});
}
/** @brief Construct a true local branch target with its canonical owner. */
auto bra(std::optional<exec_ir::Predicate> predicate,ProgramCounter target) -> std::unique_ptr<exec_ir::Instruction> {
  return std::make_unique<exec_ir::BraDirect>(exec_ir::BraDirect::Operands{exec_ir::BranchTarget{function,target}},predicate);
}
/** @brief Construct an exact bare exit with an optional execution guard. */
auto exit(std::optional<exec_ir::Predicate> predicate=std::nullopt) -> std::unique_ptr<exec_ir::Instruction> {
  return std::make_unique<exec_ir::ExitBare>(exec_ir::ExitBare::Operands{},predicate);
}
const auto move_instruction=mov(std::nullopt,exec_ir::DataType::b32,RegisterSlot{1},RegisterSlot{0});
/** @brief Freeze a fresh typed move/exit body for warp-step API checks. */
auto executable_move_program() -> std::expected<exec_ir::ExecutableProgram,exec_ir::ProgramError> {
  exec_ir::ProgramDefinition input;
  input.instructions.push_back(mov(std::nullopt,exec_ir::DataType::b32,RegisterSlot{1},RegisterSlot{0}));
  input.instructions.push_back(exit());
  input.function_declarations={{.id=exec_ir::FunctionDeclarationId{0},.function=function}};
  input.functions={{.id=function,.declarations={exec_ir::FunctionDeclarationId{0}},.definition=exec_ir::FunctionDeclarationId{0},
      .body=exec_ir::FunctionLayout{.id=function,.instruction_count=2,.register_widths={RawWidth::b32,RawWidth::b32}}}};
  return exec_ir::ExecutableProgram::create(input);
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
        engine_.execute(warp(), group, *(move_instruction), move_fallthrough);

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

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}), *(move_instruction), move_fallthrough);

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

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}), *(instruction), move_fallthrough);

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

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}), *(instruction), move_fallthrough);

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
      warp(), issue(initial_pc, {0}), *(mov(std::nullopt, exec_ir::DataType::s16, RegisterSlot{1}, RegisterSlot{0})),
      move_fallthrough);
  ASSERT_TRUE(b16);
  EXPECT_TRUE(b16->faults.empty());
  EXPECT_EQ(*registers.read(RegisterSlot{1}),
            RawValue::b16(std::uint16_t{0xabcdU}));

  thread.set_pc(initial_pc);
  const auto b64 = engine_.execute(
      warp(), issue(initial_pc, {0}), *(mov(std::nullopt, exec_ir::DataType::f64, RegisterSlot{3}, RegisterSlot{2})),
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
      immediate(RawValue::b64(std::uint64_t{0x0123'4567'89ab'cdefULL})),
      exec_ir::AddressOffset{exec_ir::AddressOffsetOperator::Add,exec_ir::Immediate{0x31,exec_ir::DataType::s64,false,0x31}}};

  thread.set_pc(initial_pc);
  const auto b32 = engine_.execute(
      warp(), issue(initial_pc, {0}), *(mov(std::nullopt, exec_ir::DataType::b32, RegisterSlot{0}, address)),
      move_fallthrough);
  ASSERT_TRUE(b32);
  EXPECT_TRUE(b32->faults.empty());
  EXPECT_EQ(*registers.read(RegisterSlot{0}), RawValue::b32(0x89ab'ce20U));

  thread.set_pc(initial_pc);
  const auto b64 = engine_.execute(
      warp(), issue(initial_pc, {0}), *(mov(std::nullopt, exec_ir::DataType::b64, RegisterSlot{1}, address)),
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
      warp(), issue(initial_pc, {0}), *(mov_pack(exec_ir::DataType::b32, RegisterSlot{2},
               {RegisterSlot{0}, RegisterSlot{1}})),
      move_fallthrough);
  ASSERT_TRUE(packed);
  EXPECT_TRUE(packed->faults.empty());
  EXPECT_EQ(*registers.read(RegisterSlot{2}), RawValue::b32(0x3344'1122U));

  ASSERT_TRUE(registers.write(RegisterSlot{4},
                             RawValue::b64(std::uint64_t{0x5566'7788'1122'3344ULL})));
  thread.set_pc(initial_pc);
  const auto unpacked = engine_.execute(
      warp(), issue(initial_pc, {0}), *(mov_unpack(exec_ir::DataType::b64, {RegisterSlot{3}, std::nullopt},
                 RegisterSlot{4})),
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
      warp(), issue(initial_pc, {1}), *(mov_v4_u32(RegisterSlot{0}, exec_ir::SpecialRegisterId{exec_ir::SpecialRegisterKind::Tid,0})),
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
      warp(), issue(initial_pc, {0}), *(mov_pred(exec_ir::Predicate{reg(RegisterSlot{0},exec_ir::DataType::pred)},
               exec_ir::PredicateSource{exec_ir::Predicate{reg(RegisterSlot{0},exec_ir::DataType::pred)}})),
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
      warp(), issue(initial_pc, {0}), *(mov_pred(exec_ir::Predicate{reg(RegisterSlot{0},exec_ir::DataType::pred)},
               exec_ir::PredicateSource{exec_ir::Predicate{reg(RegisterSlot{0},exec_ir::DataType::pred), true}})),
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
  auto bad_special = mov(
      std::nullopt, exec_ir::DataType::f32, RegisterSlot{0},
      intrinsic(exec_ir::SpecialRegisterId{exec_ir::SpecialRegisterKind::Tid,0},0U));
  auto bad_grid = mov(
      std::nullopt, exec_ir::DataType::f64, RegisterSlot{0},
      intrinsic(exec_ir::SpecialRegisterId{exec_ir::SpecialRegisterKind::GridId,0},std::nullopt));
  const std::array malformed{
      mov_pack(exec_ir::DataType::u32, RegisterSlot{0},
               {RegisterSlot{1}, RegisterSlot{2}}),
      mov_pack(exec_ir::DataType::b16, RegisterSlot{0},
               {RegisterSlot{1}, RegisterSlot{2}, RegisterSlot{3}, RegisterSlot{4}}),
      mov_pack(exec_ir::DataType::b32, RegisterSlot{0},
               {RegisterSlot{1}, std::nullopt}),
      std::move(bad_special),
      std::move(bad_grid),
  };
  for (const auto& operation : malformed) {
    const auto result = engine_.execute(warp(), issue(initial_pc, {0}), *(operation),
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
      warp(), issue(initial_pc, {0}), *(mov(exec_ir::Predicate{reg(RegisterSlot{0},exec_ir::DataType::pred)}, exec_ir::DataType::b32,
          RegisterSlot{1}, exec_ir::SymbolRef{exec_ir::StorageId{9}})),
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
      warp(), issue(initial_pc, {0}), *(mov(std::nullopt, exec_ir::DataType::b32, RegisterSlot{0},
          exec_ir::SymbolRef{exec_ir::StorageId{9}})),
      move_fallthrough);

  ASSERT_TRUE(result);
  ASSERT_EQ(result->faults.size(), 1U);
  EXPECT_TRUE(std::holds_alternative<runtime::StorageError>(
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

  const auto result = engine_.execute(warp(), issue(location.pc, {0}), instruction->get(), successor->pc);

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

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}), *(move_instruction), std::nullopt);

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
  const auto form = std::make_unique<exec_ir::DivU32>(exec_ir::DivU32::Operands{reg(RegisterSlot{0},exec_ir::DataType::u32),scalar(RegisterSlot{1},exec_ir::DataType::u32),scalar(RegisterSlot{2},exec_ir::DataType::u32)});
  const auto& instruction = form;

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}), *(instruction), std::nullopt);

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
  const auto form = std::make_unique<exec_ir::MulWideU32>(exec_ir::MulWideU32::Operands{reg(RegisterSlot{0},exec_ir::DataType::u64),scalar(RegisterSlot{1},exec_ir::DataType::u32),scalar(RegisterSlot{2},exec_ir::DataType::u32)});
  const auto& instruction = form;

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}), *(instruction), ProgramCounter{56});

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

  const auto result = engine_.execute(warp(), issue(initial_pc, {0, 1}), *(move_instruction), move_fallthrough);

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
  const auto instruction = setp_lt_u32(exec_ir::Predicate{reg(RegisterSlot{0},exec_ir::DataType::pred)},
                                       RegisterSlot{1}, RawValue::b32(1U));

  const auto result = engine_.execute(warp(), issue(initial_pc, {0, 1}), *(instruction), move_fallthrough);

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
  const auto form = comparison<exec_ir::SetpSigned>(static_cast<exec_ir::ComparisonOperator>(65535), exec_ir::Predicate{reg(RegisterSlot{0},exec_ir::DataType::pred)}, RegisterSlot{1}, RawValue::b32(0U));
  const auto& instruction = form;

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}), *(instruction), move_fallthrough);

  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code, StepErrorCode::invalid_instruction);
  EXPECT_EQ(thread.pc(), initial_pc);
  EXPECT_EQ(thread.status(), ThreadStatus::Ready);
  EXPECT_EQ(*registers.read(RegisterSlot{0}), RawValue::pred(false));
  EXPECT_EQ(*registers.read(RegisterSlot{1}), RawValue::b32(7U));
}

TEST_F(InstExecuteEngineTest,
       SetpRejectsNegatedDestinationsAndPreservesLegalSourceInversion) {
  const auto frame =
      bind(LaneId{0}, {RawWidth::pred, RawWidth::pred, RawWidth::b32});
  auto registers = view(frame);
  ASSERT_TRUE(registers.write(RegisterSlot{0}, RawValue::pred(false)));
  ASSERT_TRUE(registers.write(RegisterSlot{1}, RawValue::pred(true)));
  ASSERT_TRUE(registers.write(RegisterSlot{2}, RawValue::b32(1U)));
  auto& thread = warp().thread(LaneId{0});
  thread.set_pc(initial_pc);
  const auto negated_destination = comparison<exec_ir::SetpUnsigned>(exec_ir::ComparisonOperator::lt, exec_ir::Predicate{reg(RegisterSlot{0},exec_ir::DataType::pred), true}, RegisterSlot{2}, RawValue::b32(2U));
  const auto& negated_instruction = negated_destination;
  const auto negated_result = engine_.execute(
      warp(), issue(initial_pc, {0}), *(negated_instruction), move_fallthrough);
  ASSERT_FALSE(negated_result);
  EXPECT_EQ(negated_result.error().code, StepErrorCode::invalid_instruction);

  const auto invalid_boolean = comparison<exec_ir::SetpUnsignedBoolean>(exec_ir::ComparisonOperator::lt, exec_ir::Predicate{reg(RegisterSlot{0},exec_ir::DataType::pred)}, RegisterSlot{2}, RawValue::b32(2U), exec_ir::Predicate{reg(RegisterSlot{1},exec_ir::DataType::pred)}, exec_ir::BooleanOperator::or_);
  const auto& boolean_instruction = invalid_boolean;
  const auto boolean_result = engine_.execute(
      warp(), issue(initial_pc, {0}), *(boolean_instruction), move_fallthrough);
  ASSERT_FALSE(boolean_result);
  EXPECT_EQ(boolean_result.error().code, StepErrorCode::unsupported_instruction);

  const auto negated_combine = comparison<exec_ir::SetpSignedBoolean>(exec_ir::ComparisonOperator::lt, exec_ir::PredicatePair{exec_ir::Predicate{reg(RegisterSlot{0},exec_ir::DataType::pred)},
                             exec_ir::Predicate{reg(RegisterSlot{1},exec_ir::DataType::pred)}}, RegisterSlot{2}, RawValue::b32(2U), exec_ir::Predicate{reg(RegisterSlot{1},exec_ir::DataType::pred), true}, exec_ir::BooleanOperator::and_);
  const auto& combine_instruction = negated_combine;
  const auto combine_result = engine_.execute(
      warp(), issue(initial_pc, {0}), *(combine_instruction), move_fallthrough);
  ASSERT_TRUE(combine_result);
  EXPECT_TRUE(combine_result->faults.empty());
  EXPECT_EQ(thread.pc(), move_fallthrough);
  EXPECT_EQ(*registers.read(RegisterSlot{0}), RawValue::pred(false));
  EXPECT_EQ(*registers.read(RegisterSlot{1}), RawValue::pred(false));
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
  const auto form = comparison<exec_ir::SetpUnsigned>(exec_ir::ComparisonOperator::eq, exec_ir::PredicatePair{exec_ir::Predicate{reg(RegisterSlot{0},exec_ir::DataType::pred)},
                             exec_ir::Predicate{reg(RegisterSlot{2},exec_ir::DataType::pred)}}, RegisterSlot{1}, RegisterSlot{2});
  const auto& instruction = form;

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}), *(instruction), move_fallthrough);

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
  const auto form = comparison<exec_ir::SetpUnsigned>(exec_ir::ComparisonOperator::lt, exec_ir::Predicate{reg(RegisterSlot{0},exec_ir::DataType::pred)}, RegisterSlot{2}, RawValue::b32(1U));
  form->execution_predicate=exec_ir::Predicate{reg(RegisterSlot{1},exec_ir::DataType::pred)};
  const auto& instruction = form;

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}), *(instruction), move_fallthrough);

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
  const auto form = comparison<exec_ir::SetpSignedBoolean>(exec_ir::ComparisonOperator::lt, exec_ir::PredicatePair{exec_ir::Predicate{reg(RegisterSlot{0},exec_ir::DataType::pred)},
                             exec_ir::Predicate{reg(RegisterSlot{0},exec_ir::DataType::pred)}}, RegisterSlot{1}, RawValue::b32(2U), exec_ir::Predicate{reg(RegisterSlot{0},exec_ir::DataType::pred)}, exec_ir::BooleanOperator::and_);
  const auto& instruction = form;

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}), *(instruction), move_fallthrough);

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
  const auto instruction =
      mov(std::nullopt, exec_ir::DataType::u32, RegisterSlot{0},
          intrinsic({exec_ir::SpecialRegisterKind::Tid,0},0U));

  const auto result = engine_.execute(warp(), issue(initial_pc, {0, 1}), *(instruction), move_fallthrough);

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
                                   exec_ir::SpecialRegisterId source) {
    thread.set_pc(initial_pc);
    return engine_.execute(
        warp(), issue(initial_pc, {1}), *(mov(std::nullopt, type, destination,
            intrinsic(source,std::nullopt))),
        move_fallthrough);
  };

  ASSERT_TRUE(execute_special(RegisterSlot{0}, exec_ir::DataType::u64,
                              exec_ir::SpecialRegisterId{exec_ir::SpecialRegisterKind::GridId,0}));
  ASSERT_TRUE(execute_special(RegisterSlot{1}, exec_ir::DataType::u16,
                              exec_ir::SpecialRegisterId{exec_ir::SpecialRegisterKind::GridId,0}));
  EXPECT_EQ(*registers.read(RegisterSlot{0}),
            RawValue::b64(std::uint64_t{7U}));
  EXPECT_EQ(*registers.read(RegisterSlot{1}),
            RawValue::b16(std::uint16_t{7U}));

  ASSERT_TRUE(execute_special(RegisterSlot{2}, exec_ir::DataType::u32,
                              exec_ir::SpecialRegisterId{exec_ir::SpecialRegisterKind::LaneMaskEq,0}));
  ASSERT_TRUE(execute_special(RegisterSlot{3}, exec_ir::DataType::u32,
                              exec_ir::SpecialRegisterId{exec_ir::SpecialRegisterKind::LaneMaskLt,0}));
  ASSERT_TRUE(execute_special(RegisterSlot{4}, exec_ir::DataType::u32,
                              exec_ir::SpecialRegisterId{exec_ir::SpecialRegisterKind::LaneMaskLe,0}));
  ASSERT_TRUE(execute_special(RegisterSlot{5}, exec_ir::DataType::u32,
                              exec_ir::SpecialRegisterId{exec_ir::SpecialRegisterKind::LaneMaskGe,0}));
  ASSERT_TRUE(execute_special(RegisterSlot{6}, exec_ir::DataType::u32,
                              exec_ir::SpecialRegisterId{exec_ir::SpecialRegisterKind::LaneMaskGt,0}));
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
      warp, WarpIssueGroup{.pc = initial_pc, .lanes = std::move(lanes)}, *(mov(std::nullopt, exec_ir::DataType::u32, RegisterSlot{0},
          intrinsic(exec_ir::SpecialRegisterId{exec_ir::SpecialRegisterKind::LaneMaskEq,0},std::nullopt))),
      move_fallthrough);

  ASSERT_TRUE(result);
  ASSERT_EQ(result->faults.size(), 1U);
  EXPECT_TRUE(std::holds_alternative<UnsupportedSpecialRegister>(
      result->faults.front().cause));
  EXPECT_EQ(warp.thread(LaneId{32}).status(), ThreadStatus::Trapped);
}

TEST_F(InstExecuteEngineTest, RejectsInvalidSpecialMoveIdentityBeforeLanePreparation) {
  const auto frame = bind(LaneId{0}, {RawWidth::b32});
  auto registers = view(frame);
  warp().thread(LaneId{0}).set_pc(initial_pc);
  const auto instruction = mov(std::nullopt, exec_ir::DataType::u32, RegisterSlot{0},
      exec_ir::SpecialRegisterRef{{static_cast<exec_ir::SpecialRegisterKind>(255),0},
          std::nullopt,exec_ir::DataType::u32,1});
  const auto result=engine_.execute(warp(),issue(initial_pc,{0}),*instruction,move_fallthrough);
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code,StepErrorCode::invalid_instruction);
  EXPECT_EQ(warp().thread(LaneId{0}).status(),ThreadStatus::Ready);
  EXPECT_EQ(warp().thread(LaneId{0}).pc(),initial_pc);
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

  const auto result = engine_.execute(warp(), issue(initial_pc, {0, 1}), *(move_instruction), move_fallthrough);

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

  const auto result = engine_.execute(warp(), issue(initial_pc, {0, 1}), *(move_instruction), move_fallthrough);

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

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}), *(move_instruction), move_fallthrough);

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

  const auto result = engine.execute(warp, issue(initial_pc, {0}), *(move_instruction), move_fallthrough);

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
  BoundStepAdapter thread_adapter(thread_engine, *move_instruction,
                                  move_fallthrough);
  BoundStepAdapter warp_adapter(warp_engine, *move_instruction,
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
  const auto instruction =
      mov(exec_ir::Predicate{.register_ref=reg(RegisterSlot{2},exec_ir::DataType::pred)}, exec_ir::DataType::b32,
          RegisterSlot{1}, RegisterSlot{0});

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}), *(instruction), ProgramCounter{43});

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
  const auto instruction =
      mov(exec_ir::Predicate{.register_ref=reg(RegisterSlot{2},exec_ir::DataType::pred)}, exec_ir::DataType::b32,
          RegisterSlot{1}, RegisterSlot{0});

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}), *(instruction), ProgramCounter{44});

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
  const auto instruction =
      mov(exec_ir::Predicate{.register_ref=reg(RegisterSlot{2},exec_ir::DataType::pred), .negated = true},
          exec_ir::DataType::b32, RegisterSlot{1}, RegisterSlot{0});

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}), *(instruction), ProgramCounter{45});

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
  const auto register_add =
      add(std::nullopt, exec_ir::DataType::u32, RegisterSlot{2},
          RegisterSlot{0}, RegisterSlot{1});

  const auto register_result = engine_.execute(
      warp(), issue(initial_pc, {0}), *(register_add), ProgramCounter{46});

  ASSERT_TRUE(register_result);
  EXPECT_TRUE(register_result->faults.empty());
  EXPECT_EQ(*registers.read(RegisterSlot{2}), RawValue::b32(42U));
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), ProgramCounter{46});

  warp().thread(LaneId{0}).set_pc(initial_pc);
  const auto immediate_add =
      add(std::nullopt, exec_ir::DataType::u32, RegisterSlot{2},
          RegisterSlot{0}, RawValue::b32(8U));

  const auto immediate_result = engine_.execute(
      warp(), issue(initial_pc, {0}), *(immediate_add), ProgramCounter{47});

  ASSERT_TRUE(immediate_result);
  EXPECT_TRUE(immediate_result->faults.empty());
  EXPECT_EQ(*registers.read(RegisterSlot{2}), RawValue::b32(20U));
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), ProgramCounter{47});
}

TEST_F(InstExecuteEngineTest, AddWrapsU32WithoutArchitecturalStatus) {
  const auto frame = bind(LaneId{0}, {RawWidth::b32});
  auto registers = view(frame);
  warp().thread(LaneId{0}).set_pc(initial_pc);
  const auto instruction =
      add(std::nullopt, exec_ir::DataType::u32, RegisterSlot{0},
          RawValue::b32(0xffff'ffffU), RawValue::b32(1U));

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}), *(instruction), ProgramCounter{48});

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

  EXPECT_EQ(move_b32->instruction_opcode(), exec_ir::Opcode::Mov);
  EXPECT_EQ(move_u32->instruction_opcode(), exec_ir::Opcode::Mov);
  EXPECT_EQ(add_u32->instruction_opcode(), exec_ir::Opcode::Add);
  EXPECT_EQ(add_b32->instruction_opcode(), exec_ir::Opcode::Add);
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

  const auto instruction =
      add(std::nullopt, exec_ir::DataType::b32, RegisterSlot{2},
          RegisterSlot{0}, RegisterSlot{1});
  const auto result = engine_.execute(warp(), issue(initial_pc, {0}), *(instruction), ProgramCounter{56});

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

  const auto instruction =
      sub(std::nullopt, exec_ir::DataType::b32, RegisterSlot{2},
          RegisterSlot{0}, RegisterSlot{1});
  const auto result = engine_.execute(warp(), issue(initial_pc, {0}), *(instruction), ProgramCounter{57});

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
  const auto instruction =
      sub(exec_ir::Predicate{.register_ref=reg(RegisterSlot{3},exec_ir::DataType::pred)}, exec_ir::DataType::u32,
          RegisterSlot{2}, RegisterSlot{0}, RegisterSlot{1});

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}), *(instruction), ProgramCounter{58});

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
      exec_ir::Predicate{reg(RegisterSlot{4},exec_ir::DataType::pred)}, RegisterSlot{0}, RegisterSlot{1},
      RegisterSlot{2}, RegisterSlot{3});

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}), *(instruction), ProgramCounter{59});

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
  const auto form = std::make_unique<exec_ir::FmaDirectedF32>(exec_ir::FmaDirectedF32::Operands{reg(RegisterSlot{0},exec_ir::DataType::f32),scalar(RegisterSlot{1},exec_ir::DataType::f32),scalar(RegisterSlot{2},exec_ir::DataType::f32),scalar(RegisterSlot{3},exec_ir::DataType::f32)},exec_ir::FmaDirectedF32::Controls{exec_ir::RoundingMode::rn, false, false});
  const auto& instruction = form;

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}), *(instruction), ProgramCounter{60});

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

  const auto result = engine_.execute(warp(), issue(initial_pc, {0, 1}), *(instruction), ProgramCounter{61});

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
  const auto instruction =
      sub(std::nullopt, exec_ir::DataType::u32, RegisterSlot{2},
          RegisterSlot{0}, RegisterSlot{1});

  const auto result = engine_.execute(warp(), issue(initial_pc, {0, 1}), *(instruction), ProgramCounter{59});

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

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}), *(move_instruction), move_fallthrough);

  ASSERT_TRUE(result);
  ASSERT_EQ(result->faults.size(), 1u);
  EXPECT_EQ(std::get<common::RawValueError>(result->faults.front().cause),
            (common::RawValueError{RawWidth::b32, RawWidth::b16}));
  EXPECT_EQ(*registers.read(RegisterSlot{1}), RawValue::b16(std::uint16_t{9}));
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), initial_pc);
  EXPECT_EQ(warp().thread(LaneId{0}).status(), ThreadStatus::Trapped);
}

TEST_F(InstExecuteEngineTest, UnpredicatedBranchDoesNotNeedSuccessor) {
  const auto instruction =
      bra(std::nullopt, ProgramCounter{80});
  warp().thread(LaneId{0}).set_pc(initial_pc);

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}), *(instruction), std::nullopt);

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
  const auto instruction =
      bra(exec_ir::Predicate{.register_ref=reg(RegisterSlot{0},exec_ir::DataType::pred)}, ProgramCounter{80});
  auto& thread = warp().thread(LaneId{0});
  thread.set_pc(initial_pc);

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}), *(instruction), std::nullopt);

  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code, StepErrorCode::missing_fallthrough);
  EXPECT_EQ(thread.pc(), initial_pc);
  EXPECT_EQ(thread.status(), ThreadStatus::Ready);
}

TEST_F(InstExecuteEngineTest, FalsePredicateMakesBranchFallThrough) {
  const auto frame = bind(LaneId{0}, {RawWidth::pred});
  auto registers = view(frame);
  ASSERT_TRUE(registers.write(RegisterSlot{0}, RawValue::pred(false)));
  const auto instruction =
      bra(exec_ir::Predicate{.register_ref=reg(RegisterSlot{0},exec_ir::DataType::pred)}, ProgramCounter{80});
  warp().thread(LaneId{0}).set_pc(initial_pc);

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}), *(instruction), ProgramCounter{50});

  ASSERT_TRUE(result);
  EXPECT_TRUE(result->faults.empty());
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), ProgramCounter{50});
  EXPECT_EQ(warp().thread(LaneId{0}).status(), ThreadStatus::Ready);
}

TEST(InstExecuteEngineBranchTest, DivergentBranchLeavesReadyLanesGroupedByPc) {
  runtime::LaunchRuntime runtime(grid_id, three_lane_shape());
  arith::context arithmetic;
  const auto instruction =
      bra(exec_ir::Predicate{.register_ref=reg(RegisterSlot{0},exec_ir::DataType::pred)}, ProgramCounter{81});
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

  const auto result = engine.execute(warp, issue(initial_pc, {0, 1}), *(instruction), ProgramCounter{51});

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
  const auto instruction =
      bra(exec_ir::Predicate{.register_ref=reg(RegisterSlot{0},exec_ir::DataType::pred)}, ProgramCounter{82});
  warp().thread(LaneId{0}).set_pc(initial_pc);

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}), *(instruction), ProgramCounter{52});

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
  const auto instruction =
      bra(exec_ir::Predicate{.register_ref=reg(RegisterSlot{0},exec_ir::DataType::pred)}, ProgramCounter{83});
  warp().thread(LaneId{0}).set_pc(initial_pc);

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}), *(instruction), ProgramCounter{53});

  ASSERT_TRUE(result);
  ASSERT_EQ(result->faults.size(), 1u);
  EXPECT_EQ(std::get<memory::RegisterError>(result->faults.front().cause).code,
            memory::RegisterErrorCode::uninitialized_read);
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), initial_pc);
  EXPECT_EQ(warp().thread(LaneId{0}).status(), ThreadStatus::Trapped);
}

TEST_F(InstExecuteEngineTest, UnpredicatedExitDoesNotNeedSuccessor) {
  const auto instruction = exit(std::nullopt);
  warp().thread(LaneId{0}).set_pc(initial_pc);

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}), *(instruction), std::nullopt);

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
  const auto instruction =
      exit(exec_ir::Predicate{.register_ref=reg(RegisterSlot{0},exec_ir::DataType::pred)});
  auto& thread = warp().thread(LaneId{0});
  thread.set_pc(initial_pc);

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}), *(instruction), std::nullopt);

  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code, StepErrorCode::missing_fallthrough);
  EXPECT_EQ(thread.pc(), initial_pc);
  EXPECT_EQ(thread.status(), ThreadStatus::Ready);
}

TEST_F(InstExecuteEngineTest, PredicatedOffExitFallsThroughAndStaysReady) {
  const auto frame = bind(LaneId{0}, {RawWidth::pred});
  auto registers = view(frame);
  ASSERT_TRUE(registers.write(RegisterSlot{0}, RawValue::pred(false)));
  const auto instruction =
      exit(exec_ir::Predicate{.register_ref=reg(RegisterSlot{0},exec_ir::DataType::pred)});
  warp().thread(LaneId{0}).set_pc(initial_pc);

  const auto result = engine_.execute(warp(), issue(initial_pc, {0}), *(instruction), ProgramCounter{55});

  ASSERT_TRUE(result);
  EXPECT_TRUE(result->faults.empty());
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), ProgramCounter{55});
  EXPECT_EQ(warp().thread(LaneId{0}).status(), ThreadStatus::Ready);
}

/** @brief Construct an ordinary vector transfer for focused boundary checks. */
auto vector_memory(bool load, exec_ir::DataType type,
                   exec_ir::VectorArity arity,
                   RegisterInputs registers, exec_ir::Address address,
                   exec_ir::AddressSpace space = exec_ir::AddressSpace::global)
    -> std::unique_ptr<exec_ir::Instruction> {
  auto lanes=register_vector(std::move(registers),type);
  if(load) return std::make_unique<exec_ir::LdExplicitVector>(
      exec_ir::LdExplicitVector::Operands{std::move(lanes),std::move(address)},
      exec_ir::LdExplicitVector::Controls{.state_space=space,.vector=arity,.type=type});
  return std::make_unique<exec_ir::StExplicitVector>(
      exec_ir::StExplicitVector::Operands{std::move(address),std::move(lanes)},
      exec_ir::StExplicitVector::Controls{.state_space=space,.vector=arity,.type=type});
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
                    make_address(RawValue::b64(std::uint64_t{0})));
  const auto result =
      engine_.execute(warp(), issue(initial_pc, {0}), *(load), move_fallthrough);
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
                    make_address(RawValue::b64(std::uint64_t{0})));
  const auto result =
      engine_.execute(warp(), issue(initial_pc, {0}), *(store), move_fallthrough);
  ASSERT_TRUE(result);
  ASSERT_EQ(result->faults.size(), 1U);
  EXPECT_EQ(*memory->snapshot(memory::Address{0}, 4),
            (std::vector<std::byte>{original.begin(), original.end()}));
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), initial_pc);
}

TEST_F(InstExecuteEngineTest,
       RejectsInvalidMemoryVectorsBeforeResolvingResources) {
  warp().thread(LaneId{0}).set_pc(initial_pc);
  const exec_ir::Address address{immediate(RawValue::b64(std::uint64_t{0}))};
  const RegisterInputs pair{RegisterSlot{0}, RegisterSlot{1}};
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
    const auto result = engine_.execute(warp(), issue(initial_pc, {0}), *(operation), move_fallthrough);
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
      RegisterSlot{0}, make_address(RawValue::b64(std::uint64_t{0})));
  const auto result =
      engine_.execute(warp(), issue(initial_pc, {0}), *(load), move_fallthrough);
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
        immediate(RawValue::b64(subtract ? std::uint64_t{0} : UINT64_MAX)),
        exec_ir::AddressOffset{subtract?exec_ir::AddressOffsetOperator::Subtract:exec_ir::AddressOffsetOperator::Add,exec_ir::Immediate{1,exec_ir::DataType::s64,false,1}}};
    const auto load =
        make_load(std::nullopt, exec_ir::DataType::u32,
                  exec_ir::AddressSpace::global, RegisterSlot{0}, address);
    const auto result =
        engine_.execute(warp(), issue(initial_pc, {0}), *(load), move_fallthrough);
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
  const auto load = make_load(
      std::nullopt, exec_ir::DataType::u32, exec_ir::AddressSpace::generic,
      RegisterSlot{0}, RegisterSlot{1});
  ASSERT_TRUE(engine_.execute(warp(), issue(initial_pc, {0}), *(load),
                              ProgramCounter{56}));
  EXPECT_EQ(*registers.read(RegisterSlot{0}), RawValue::b32(0x12345678U));

  warp().thread(LaneId{0}).set_pc(initial_pc);
  ASSERT_TRUE(registers.write(RegisterSlot{0}, RawValue::b32(0xaabbccddU)));
  const auto store = make_store(
      std::nullopt, exec_ir::DataType::u32, exec_ir::AddressSpace::global,
      RegisterSlot{1}, RegisterSlot{0});
  const auto result = engine_.execute(warp(), issue(initial_pc, {0}), *(store),
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
  const auto load = make_load(
      std::nullopt, exec_ir::DataType::u32, exec_ir::AddressSpace::param,
      RegisterSlot{0}, make_address(RawValue::b64(std::uint64_t{0})));

  const auto result =
      engine_.execute(warp(), issue(initial_pc, {0}), *(load), ProgramCounter{57});

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
  const auto predicated_store = make_store(
      exec_ir::Predicate{reg(RegisterSlot{2},exec_ir::DataType::pred)}, exec_ir::DataType::u32,
      exec_ir::AddressSpace::generic, RegisterSlot{1}, RegisterSlot{0});
  const auto predicated = engine_.execute(warp(), issue(initial_pc, {0}), *(predicated_store), ProgramCounter{59});
  ASSERT_TRUE(predicated);
  EXPECT_TRUE(predicated->faults.empty());
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), ProgramCounter{59});

  ASSERT_TRUE(registers.write(
      RegisterSlot{1},
      RawValue::b64(memory::GenericAddressLayout::constant_base)));
  ASSERT_TRUE(registers.write(RegisterSlot{0}, RawValue::b32(9U)));
  warp().thread(LaneId{0}).set_pc(initial_pc);
  const auto store = make_store(
      std::nullopt, exec_ir::DataType::u32, exec_ir::AddressSpace::generic,
      RegisterSlot{1}, RegisterSlot{0});
  const auto result = engine_.execute(warp(), issue(initial_pc, {0}), *(store),
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
  const auto load = make_load(
      std::nullopt, exec_ir::DataType::u32, exec_ir::AddressSpace::global,
      RegisterSlot{0}, RegisterSlot{1});
  const auto expect_storage_fault = [&](std::uint64_t address,
                                        memory::MemoryErrorCode code) {
    ASSERT_TRUE(registers.write(RegisterSlot{1}, RawValue::b64(address)));
    auto& thread = warp().thread(LaneId{0});
    thread.mark_ready();
    thread.set_pc(initial_pc);
    const auto result = engine_.execute(warp(), issue(initial_pc, {0}), *(load),
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
      engine_.execute(warp(), issue(initial_pc, {0}), *(load), ProgramCounter{60});
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
  const auto load = make_load(
      std::nullopt, exec_ir::DataType::u32, exec_ir::AddressSpace::global,
      RegisterSlot{0}, RegisterSlot{1});
  const auto result =
      engine_.execute(warp(), issue(initial_pc, {0}), *(load), ProgramCounter{61});
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
  const auto store = make_store(
      std::nullopt, exec_ir::DataType::u32, exec_ir::AddressSpace::global,
      RegisterSlot{1}, RegisterSlot{0});
  warp().thread(LaneId{0}).set_pc(initial_pc);
  warp().thread(LaneId{1}).set_pc(initial_pc);
  const auto isolated = engine_.execute(warp(), issue(initial_pc, {0, 1}), *(store), ProgramCounter{62});
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
  const auto partial = engine_.execute(warp(), issue(initial_pc, {0}), *(store),
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
  const auto ordered = engine_.execute(warp(), issue(initial_pc, {0, 1}), *(store),
                                       ProgramCounter{64});
  ASSERT_TRUE(ordered);
  EXPECT_TRUE(ordered->faults.empty());
  EXPECT_EQ(*memory->snapshot(memory::Address{0}, 4),
            (std::vector<std::byte>{std::byte{4}, std::byte{0}, std::byte{0},
                                    std::byte{0}}));
}

TEST_F(InstExecuteEngineTest, RejectsInvalidAddressSpaceBeforePreparation) {
  warp().thread(LaneId{0}).set_pc(initial_pc);
  const auto load = make_load(
      std::nullopt, exec_ir::DataType::u32,
      static_cast<exec_ir::AddressSpace>(99), RegisterSlot{0}, RegisterSlot{1});
  const auto result =
      engine_.execute(warp(), issue(initial_pc, {0}), *(load), ProgramCounter{65});
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code, StepErrorCode::invalid_instruction);
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), initial_pc);
  EXPECT_EQ(warp().thread(LaneId{0}).status(), ThreadStatus::Ready);
}

TEST_F(InstExecuteEngineTest,
       CompletesWarpSyncAcrossPartialArrivalsWithoutRegisterBindings) {
  const auto bar = make_bar(std::nullopt, RawValue::b32(3U));
  warp().thread(LaneId{0}).set_pc(initial_pc);
  warp().thread(LaneId{1}).set_pc(initial_pc);

  const auto first =
      engine_.execute(warp(), issue(initial_pc, {0}), *(bar), ProgramCounter{11});
  ASSERT_TRUE(first);
  EXPECT_TRUE(warp().execution_state().sync.active());
  EXPECT_TRUE(warp().thread(LaneId{0}).waiting());
  EXPECT_EQ(warp().thread(LaneId{0}).wait_reason(),
            execution_model::WaitReason::WarpSync);
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), initial_pc);

  const auto second =
      engine_.execute(warp(), issue(initial_pc, {1}), *(bar), ProgramCounter{11});
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
  const auto repeated = engine_.execute(warp(), issue(initial_pc, {0, 1}), *(bar),
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
    const auto bar = make_bar(std::nullopt, mask);
    const auto result = engine_.execute(warp(), issue(initial_pc, lanes), *(bar),
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
  const auto bar = make_bar(std::nullopt, RegisterSlot{1});
  const auto result = engine_.execute(warp(), issue(initial_pc, {0, 1}), *(bar),
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
  const auto bar = make_bar(std::nullopt, RegisterSlot{0});
  const auto result = engine_.execute(warp(), issue(initial_pc, {0, 1}), *(bar),
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
  const auto bar = make_bar(std::nullopt, RawValue::b32(3U));
  const auto result =
      engine_.execute(warp(), issue(initial_pc, {0}), *(bar), ProgramCounter{11});
  ASSERT_TRUE(result);
  EXPECT_FALSE(warp().execution_state().sync.active());
  EXPECT_TRUE(warp().thread(LaneId{0}).ready());
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), ProgramCounter{11});
}

TEST_F(InstExecuteEngineTest, PredicatedOffWarpSyncFallsThrough) {
  const auto frame = bind(LaneId{0}, {RawWidth::pred});
  ASSERT_TRUE(view(frame).write(RegisterSlot{0}, RawValue::pred(false)));
  warp().thread(LaneId{0}).set_pc(initial_pc);
  const auto bar =
      make_bar(exec_ir::Predicate{reg(RegisterSlot{0},exec_ir::DataType::pred), false}, RawValue::b32(1U));
  const auto result =
      engine_.execute(warp(), issue(initial_pc, {0}), *(bar), ProgramCounter{11});
  ASSERT_TRUE(result);
  EXPECT_FALSE(warp().execution_state().sync.active());
  EXPECT_TRUE(warp().thread(LaneId{0}).ready());
  EXPECT_EQ(warp().thread(LaneId{0}).pc(), ProgramCounter{11});
}

TEST_F(InstExecuteEngineTest,
       RejectsPendingWarpSyncMismatchAndDuplicateArrival) {
  const auto mask_three =
      make_bar(std::nullopt, RawValue::b32(3U));
  const auto mask_two =
      make_bar(std::nullopt, RawValue::b32(2U));
  bind(LaneId{0}, {RawWidth::b32});
  bind(LaneId{1}, {RawWidth::b32});
  warp().thread(LaneId{0}).set_pc(initial_pc);
  warp().thread(LaneId{1}).set_pc(initial_pc);
  ASSERT_TRUE(engine_.execute(warp(), issue(initial_pc, {0}), *(mask_three),
                              ProgramCounter{11}));

  const auto mismatch = engine_.execute(warp(), issue(initial_pc, {1}), *(mask_two), ProgramCounter{11});
  ASSERT_FALSE(mismatch);
  EXPECT_EQ(mismatch.error().code, StepErrorCode::collective_pending_mismatch);
  EXPECT_TRUE(warp().execution_state().sync.active());

  warp().thread(LaneId{0}).mark_ready();
  const auto duplicate = engine_.execute(warp(), issue(initial_pc, {0, 1}), *(mask_three), ProgramCounter{11});
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
  const auto bar =
      make_bar(std::nullopt, RawValue::b32(0xffffffffU));
  LaneMask lanes{32};
  for (const auto lane : {LaneId{0}, LaneId{1}, LaneId{2}}) {
    lanes.set(lane);
  }
  const auto result =
      engine.execute(warp, WarpIssueGroup{.pc = initial_pc, .lanes = lanes}, *(bar), ProgramCounter{11});
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

  const auto arrive = engine.execute(first_warp, all_lanes(initial_pc), *(make_cta_arrive(0, 4)), ProgramCounter{11});
  ASSERT_TRUE(arrive);
  for (const auto& thread : first_warp) {
    EXPECT_TRUE(thread.ready());
    EXPECT_EQ(thread.pc(), ProgramCounter{11});
  }
  const auto sync = engine.execute(second_warp, all_lanes(initial_pc), *(make_cta_sync(0, 4)), ProgramCounter{11});
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
  for (const auto& instruction : {make_cta_sync(16, 2), make_cta_sync(0, 0)}) {
    const auto rejected =
        engine.execute(warp, group, *(instruction), ProgramCounter{11});
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

  ASSERT_TRUE(engine.execute(partial_warp, group(initial_pc, {0}), *(make_cta_sync(0, 2)), ProgramCounter{11}));
  ASSERT_TRUE(engine.execute(completing_warp, group(initial_pc, {0, 1}), *(make_cta_sync(0, 2)), ProgramCounter{11}));
  EXPECT_TRUE(partial_warp.thread(LaneId{0}).waiting());
  EXPECT_EQ(partial_warp.thread(LaneId{0}).pc(), initial_pc);
  EXPECT_TRUE(cta.execution_state()
                  .barriers.barrier(execution_model::CtaBarrierId{0})
                  .active());

  partial_warp.thread(LaneId{1}).set_pc(initial_pc);
  ASSERT_TRUE(engine.execute(partial_warp, group(initial_pc, {1}), *(make_cta_sync(0, 2)), ProgramCounter{11}));
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
  ASSERT_TRUE(engine_.execute(warp(), issue(initial_pc, {0}), *(make_cta_sync(0)),
                              ProgramCounter{11}));
  const auto rejected = engine_.execute(warp(), issue(ProgramCounter{20}, {1}), *(make_cta_sync(1)), ProgramCounter{21});
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
  const auto first = engine_.execute(warp(), issue(initial_pc, {0}), *(make_cta_sync(0)), ProgramCounter{11});
  ASSERT_TRUE(first);
  EXPECT_TRUE(warp().thread(LaneId{0}).waiting());
  const auto exit_result = engine_.execute(
      warp(), issue(ProgramCounter{20}, {1}), *(exit()), std::nullopt);
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
  ASSERT_TRUE(engine_.execute(warp(), issue(initial_pc, {0}), *(make_cta_sync(0)),
                              ProgramCounter{11}));
  const auto rejected = engine_.execute(warp(), issue(initial_pc, {1}), *(make_cta_sync(0)), ProgramCounter{12});
  ASSERT_FALSE(rejected);
  EXPECT_EQ(rejected.error().code, StepErrorCode::collective_pending_mismatch);
  EXPECT_TRUE(warp().thread(LaneId{0}).waiting());
  EXPECT_TRUE(warp().thread(LaneId{1}).ready());
  EXPECT_EQ(warp().thread(LaneId{1}).pc(), initial_pc);
}

TEST_F(InstExecuteEngineTest, RejectsCtaBarrierWhileWarpSyncIsPending) {
  warp().thread(LaneId{0}).set_pc(initial_pc);
  warp().thread(LaneId{1}).set_pc(initial_pc);
  ASSERT_TRUE(engine_.execute(warp(), issue(initial_pc, {0}), *(make_bar(std::nullopt, RawValue::b32(3U))),
                              ProgramCounter{11}));
  const auto rejected = engine_.execute(warp(), issue(initial_pc, {1}), *(make_cta_sync(0)), ProgramCounter{12});
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
  const auto first = engine.execute(first_warp, group(initial_pc), *(make_cta_sync(0, 2)), ProgramCounter{11});
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
      second_warp, group(initial_pc), *(make_cta_red_popc(RegisterSlot{0}, 0,
                        exec_ir::Predicate{reg(RegisterSlot{1},exec_ir::DataType::pred), false})),
      ProgramCounter{11});
  ASSERT_FALSE(mixed);
  EXPECT_EQ(mixed.error().code, StepErrorCode::collective_pending_mismatch);
  EXPECT_TRUE(first_warp.thread(LaneId{0}).waiting());
  EXPECT_TRUE(second_warp.thread(LaneId{0}).ready());

  const auto mismatched = engine.execute(
      second_warp, group(initial_pc), *(make_cta_sync(0, 1)), ProgramCounter{11});
  ASSERT_FALSE(mismatched);
  EXPECT_EQ(mismatched.error().code,
            StepErrorCode::collective_pending_mismatch);
  first_warp.thread(LaneId{0}).mark_ready();
  const auto duplicate = engine.execute(
      first_warp, group(initial_pc), *(make_cta_sync(0, 2)), ProgramCounter{11});
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
      RegisterSlot{0}, 0, exec_ir::Predicate{reg(RegisterSlot{1},exec_ir::DataType::pred), false});
  ASSERT_TRUE(engine.execute(first_warp, group(initial_pc), *(reduction),
                             ProgramCounter{11}));
  ASSERT_TRUE(runtime.registers().destroy_frame(first_frame));
  const auto released = engine.execute(second_warp, group(initial_pc), *(reduction), ProgramCounter{11});
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
  ASSERT_TRUE(engine.execute(participating, group(initial_pc, {0}), *(make_cta_sync(0, 2)), ProgramCounter{11}));
  ASSERT_TRUE(engine.execute(participating, group(ProgramCounter{20}, {1}), *(exit()), std::nullopt));
  EXPECT_TRUE(unrelated.thread(LaneId{0}).ready());
  EXPECT_TRUE(participating.thread(LaneId{0}).ready());
  EXPECT_EQ(participating.thread(LaneId{0}).pc(), ProgramCounter{11});
  EXPECT_FALSE(cta.execution_state()
                   .barriers.barrier(execution_model::CtaBarrierId{0})
                   .active());
}

}  // namespace
}  // namespace ptxsim::inst_execute_engine::test
