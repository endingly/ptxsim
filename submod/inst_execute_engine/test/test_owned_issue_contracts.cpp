#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

#include <ptxsim/exec_ir/exec_ir.gen.hpp>
#include <ptxsim/inst_execute_engine/inst_execute_engine.hpp>
#include "../src/resource_resolution.hpp"
#include "../src/mov_preparation.hpp"

namespace ptxsim::inst_execute_engine::test {
namespace {
/** @brief Construct a typed register view without claiming that a frame slot exists. */
auto reg(std::uint32_t slot,exec_ir::DataType type) -> exec_ir::RegisterRef {
  return {.slot=common::RegisterSlot{slot},
          .register_class=type==exec_ir::DataType::pred?exec_ir::RegisterClass::Predicate:exec_ir::RegisterClass::General,
          .declared_type=type};
}
/** @brief Preserve a literal's original sign independently from its written operator. */
auto offset(exec_ir::AddressOffsetOperator operation,bool negative,std::uint64_t magnitude=4)
    -> exec_ir::AddressOffset {
  const auto bits=negative?std::uint64_t{0}-magnitude:magnitude;
  return {operation,{bits,exec_ir::DataType::s64,negative,bits}};
}
/** @brief Run exact typed inputs against one real ready lane and optional runtime bindings. */
class OwnedIssueContract : public ::testing::Test {
 protected:
  /** @brief Initialize an independent launch and bind an engine to canonical function zero. */
  OwnedIssueContract():runtime_(execution_model::GridId{303},
      {.cta_dim={1,1,1},.thread_dim={1,1,1},.warp_size=1}),engine_(runtime_,common::FunctionId{0},arithmetic_) {
    warp().thread(execution_model::LaneId{0}).set_pc(common::ProgramCounter{0});
  }
  /** @brief Borrow the only real warp for one test instance. */
  auto warp() -> execution_model::Warp& {
    return runtime_.grid().cta(execution_model::CtaId{execution_model::GridId{303},0}).warp(0);
  }
  /** @brief Issue exactly lane zero at its known local instruction position. */
  auto issue() -> execution_model::WarpIssueGroup {
    execution_model::LaneMask lanes{1}; lanes.set(execution_model::LaneId{0});
    return {.pc=common::ProgramCounter{0},.lanes=std::move(lanes)};
  }
  /** @brief Bind scalar address/value slots and two predicates, returning their live view. */
  auto bind() -> memory::RegisterView {
    const auto frame=runtime_.registers().create_frame({.slot_widths={common::RawWidth::b64,
        common::RawWidth::pred,common::RawWidth::b32,common::RawWidth::pred}});
    if(!frame) throw std::runtime_error("frame creation failed");
    const auto bound=runtime_.bind_register_frame(warp().thread(execution_model::LaneId{0}).id(),common::FunctionId{0},*frame);
    if(!bound) throw std::runtime_error("frame binding failed");
    const auto view=runtime_.registers().view(*frame);
    if(!view) throw std::runtime_error("frame lookup failed");
    return *view;
  }
  /** @brief Assert a static rejection leaves the issued thread unchanged. */
  void rejected(const exec_ir::Instruction& instruction,StepErrorCode code,
                std::optional<common::ProgramCounter> successor=std::nullopt) {
    auto& thread=warp().thread(execution_model::LaneId{0});
    thread.set_pc(common::ProgramCounter{0});
    const auto status=thread.status();
    const auto result=engine_.execute(warp(),issue(),instruction,successor);
    ASSERT_FALSE(result); EXPECT_EQ(result.error().code,code);
    EXPECT_EQ(thread.pc(),common::ProgramCounter{0}); EXPECT_EQ(thread.status(),status);
  }
  /** @brief Launch resources owned for this fixture's entire engine lifetime. */
  runtime::LaunchRuntime runtime_;
  /** @brief Existing pure arithmetic policy, independent of ordinary operand reads. */
  arith::context arithmetic_;
  /** @brief Engine borrows the fixture's runtime and arithmetic context. */
  InstExecuteEngine engine_;
};

TEST_F(OwnedIssueContract, UnimplementedIntrinsicPrecedesGuardResourcesAndSuccessor) {
  for(const auto kind:{exec_ir::SpecialRegisterKind::Clock,exec_ir::SpecialRegisterKind::SmId,
                       exec_ir::SpecialRegisterKind::PerformanceMonitor}) {
    const exec_ir::SpecialRegisterRef source{{kind,static_cast<std::uint8_t>(kind==exec_ir::SpecialRegisterKind::PerformanceMonitor?1:0)},
        std::nullopt,exec_ir::DataType::u32,1};
    const exec_ir::MovScalar instruction{exec_ir::MovScalar::ScalarOperands{reg(999,exec_ir::DataType::u32),source},
        exec_ir::MovScalar::Controls{exec_ir::DataType::u32},exec_ir::Predicate{reg(999,exec_ir::DataType::pred)}};
    rejected(instruction,StepErrorCode::unsupported_instruction);
    rejected(instruction,StepErrorCode::unsupported_instruction,common::ProgramCounter{999});
  }
}
TEST_F(OwnedIssueContract, FalsePredicateCannotSuppressUnimplementedIntrinsicOrSourceCategories) {
  auto registers=bind();
  ASSERT_TRUE(registers.write(common::RegisterSlot{0},common::RawValue::b64(std::uint64_t{55})));
  ASSERT_TRUE(registers.write(common::RegisterSlot{1},common::RawValue::pred(false)));
  const std::array sources{
      std::pair{exec_ir::MovSource{exec_ir::SpecialRegisterRef{{exec_ir::SpecialRegisterKind::PerformanceMonitor,1},std::nullopt,exec_ir::DataType::u32,1}},exec_ir::DataType::u32},
      std::pair{exec_ir::MovSource{exec_ir::FunctionRef{common::FunctionId{999},false}},exec_ir::DataType::u64},
      std::pair{exec_ir::MovSource{exec_ir::OpaqueSymbolRef{.id=exec_ir::ResourceId{999}}},exec_ir::DataType::u64}};
  for(const auto& [source,type]:sources) {
    const exec_ir::MovScalar instruction{exec_ir::MovScalar::ScalarOperands{reg(999,type),source},
        exec_ir::MovScalar::Controls{type},exec_ir::Predicate{reg(1,exec_ir::DataType::pred)}};
    rejected(instruction,StepErrorCode::unsupported_instruction);
    rejected(instruction,StepErrorCode::unsupported_instruction,common::ProgramCounter{999});
    EXPECT_EQ(*registers.read(common::RegisterSlot{0}),common::RawValue::b64(std::uint64_t{55}));
  }
}
TEST_F(OwnedIssueContract, IntrinsicIllegalIdentitiesAreStructuralErrors) {
  const std::array sources{
      exec_ir::SpecialRegisterRef{{static_cast<exec_ir::SpecialRegisterKind>(255),0},std::nullopt,exec_ir::DataType::u32,1},
      exec_ir::SpecialRegisterRef{{exec_ir::SpecialRegisterKind::PerformanceMonitor,8},std::nullopt,exec_ir::DataType::u32,1},
      exec_ir::SpecialRegisterRef{{exec_ir::SpecialRegisterKind::Environment,32},std::nullopt,exec_ir::DataType::u32,1},
      exec_ir::SpecialRegisterRef{{exec_ir::SpecialRegisterKind::ReservedSmemOffset,2},std::nullopt,exec_ir::DataType::u32,1},
      exec_ir::SpecialRegisterRef{{exec_ir::SpecialRegisterKind::LaneId,1},std::nullopt,exec_ir::DataType::u32,1},
      exec_ir::SpecialRegisterRef{{exec_ir::SpecialRegisterKind::Tid,0},3,exec_ir::DataType::u32,4}};
  for(const auto& source:sources) {
    const exec_ir::MovScalar instruction{exec_ir::MovScalar::ScalarOperands{reg(0,exec_ir::DataType::u32),source},
        exec_ir::MovScalar::Controls{exec_ir::DataType::u32}};
    rejected(instruction,StepErrorCode::invalid_instruction);
  }
}
TEST_F(OwnedIssueContract, NegatedPredicateDestinationsPrecedeMissingFrameAndFallthrough) {
  const exec_ir::Predicate bad{reg(999,exec_ir::DataType::pred),true};
  const exec_ir::Predicate guard{reg(777,exec_ir::DataType::pred)};
  const exec_ir::MovPred mov{exec_ir::MovPred::Operands{bad,exec_ir::PredicateSource{guard}},guard};
  rejected(mov,StepErrorCode::invalid_instruction);
  rejected(mov,StepErrorCode::invalid_instruction,common::ProgramCounter{999});
  const exec_ir::SetpUnsigned single{exec_ir::SetpUnsigned::SingleOperands{
      exec_ir::PredicateOrSink{bad},exec_ir::ScalarOperand{reg(999,exec_ir::DataType::u32)},
      exec_ir::ScalarOperand{exec_ir::Immediate{1,exec_ir::DataType::u32}}},
      exec_ir::SetpUnsigned::Controls{.comparison=exec_ir::ComparisonOperator::lt,.type=exec_ir::DataType::u32},guard};
  rejected(single,StepErrorCode::invalid_instruction);
  const exec_ir::SetpUnsigned pair{exec_ir::SetpUnsigned::PairOperands{
      exec_ir::PredicatePairOrSink{std::nullopt,bad},exec_ir::ScalarOperand{reg(999,exec_ir::DataType::u32)},
      exec_ir::ScalarOperand{exec_ir::Immediate{1,exec_ir::DataType::u32}}},
      exec_ir::SetpUnsigned::Controls{.comparison=exec_ir::ComparisonOperator::lt,.type=exec_ir::DataType::u32},guard};
  rejected(pair,StepErrorCode::invalid_instruction,common::ProgramCounter{999});
  auto registers=bind();
  ASSERT_TRUE(registers.write(common::RegisterSlot{1},common::RawValue::pred(false)));
  const exec_ir::Predicate false_guard{reg(1,exec_ir::DataType::pred)};
  auto false_mov=mov; false_mov.execution_predicate=false_guard;
  auto false_single=single; false_single.execution_predicate=false_guard;
  auto false_pair=pair; false_pair.execution_predicate=false_guard;
  rejected(false_mov,StepErrorCode::invalid_instruction);
  rejected(false_single,StepErrorCode::invalid_instruction,common::ProgramCounter{999});
  rejected(false_pair,StepErrorCode::invalid_instruction);
}
TEST_F(OwnedIssueContract, GuardSourceAndSignedCombineNegationRemainExecutable) {
  auto registers=bind();
  ASSERT_TRUE(registers.write(common::RegisterSlot{1},common::RawValue::pred(false)));
  ASSERT_TRUE(registers.write(common::RegisterSlot{2},common::RawValue::b32(std::uint32_t{1})));
  const exec_ir::Predicate source{reg(1,exec_ir::DataType::pred),true};
  const exec_ir::MovPred mov{exec_ir::MovPred::Operands{exec_ir::Predicate{reg(3,exec_ir::DataType::pred)},
      exec_ir::PredicateSource{source}},source};
  auto result=engine_.execute(warp(),issue(),mov,common::ProgramCounter{1});
  ASSERT_TRUE(result); EXPECT_TRUE(result->faults.empty());
  EXPECT_EQ(*registers.read(common::RegisterSlot{3}),common::RawValue::pred(true));
  warp().thread(execution_model::LaneId{0}).set_pc(common::ProgramCounter{0});
  ASSERT_TRUE(registers.write(common::RegisterSlot{3},common::RawValue::pred(false)));
  const exec_ir::SetpSignedBoolean compare{exec_ir::SetpSignedBoolean::SingleOperands{
      exec_ir::PredicateOrSink{exec_ir::Predicate{reg(3,exec_ir::DataType::pred)}},
      exec_ir::ScalarOperand{reg(2,exec_ir::DataType::s32)},
      exec_ir::ScalarOperand{exec_ir::Immediate{2,exec_ir::DataType::s32,false,2}},exec_ir::PredicateSource{source}},
      exec_ir::SetpSignedBoolean::Controls{.comparison=exec_ir::ComparisonOperator::lt,
          .boolean=exec_ir::BooleanOperator::and_,.type=exec_ir::DataType::s32},source};
  result=engine_.execute(warp(),issue(),compare,common::ProgramCounter{1});
  ASSERT_TRUE(result); EXPECT_TRUE(result->faults.empty());
  EXPECT_EQ(*registers.read(common::RegisterSlot{3}),common::RawValue::pred(true));
}
TEST(OwnedAddressOffset, CombinesBothOperatorsWithSourceSignAndChecksBounds) {
  using exec_ir::AddressOffsetOperator;
  const auto max=std::numeric_limits<std::uint64_t>::max();
  EXPECT_EQ(*detail::apply_address_offset(100,offset(AddressOffsetOperator::Add,false)),104);
  EXPECT_EQ(*detail::apply_address_offset(100,offset(AddressOffsetOperator::Add,true)),96);
  EXPECT_EQ(*detail::apply_address_offset(100,offset(AddressOffsetOperator::Subtract,false)),96);
  EXPECT_EQ(*detail::apply_address_offset(100,offset(AddressOffsetOperator::Subtract,true)),104);
  EXPECT_EQ(*detail::apply_address_offset(0,offset(AddressOffsetOperator::Add,false,0)),0);
  EXPECT_EQ(*detail::apply_address_offset(std::uint64_t{1}<<63,offset(AddressOffsetOperator::Add,true,std::uint64_t{1}<<63)),0);
  EXPECT_EQ(*detail::apply_address_offset(0,offset(AddressOffsetOperator::Subtract,true,std::uint64_t{1}<<63)),std::uint64_t{1}<<63);
  EXPECT_EQ(*detail::apply_address_offset(0,offset(AddressOffsetOperator::Add,false,max)),max);
  EXPECT_FALSE(detail::apply_address_offset(max,offset(AddressOffsetOperator::Add,false,1)));
  EXPECT_FALSE(detail::apply_address_offset(0,offset(AddressOffsetOperator::Subtract,false,1)));
  EXPECT_FALSE(detail::apply_address_offset(0,offset(AddressOffsetOperator::Add,true,1)));
  EXPECT_FALSE(detail::apply_address_offset(max,offset(AddressOffsetOperator::Subtract,true,1)));
}
TEST_F(OwnedIssueContract, MemoryAndSymbolMoveShareSignedOffsetSemantics) {
  auto registers=bind();
  const auto global=runtime_.address_spaces().create_global({.capacity=100});
  ASSERT_TRUE(runtime_.address_spaces().grow(global,100)); ASSERT_TRUE(runtime_.bind_global(global));
  const auto program=exec_ir::ExecutableProgram::create({.storage_declarations={{
      .id=exec_ir::StorageId{0},.name="data",.element_type=exec_ir::DataType::b8,
      .byte_extent=8,.alignment=1,.initialization=exec_ir::StorageInitialization::zero}}});
  ASSERT_TRUE(program); ASSERT_TRUE(runtime_.prepare_storage(*program));
  auto space=runtime_.address_spaces().view(global); ASSERT_TRUE(space);
  const std::array reset{std::byte{0xa5},std::byte{0xa5},std::byte{0xa5},std::byte{0xa5},
      std::byte{0xa5},std::byte{0xa5},std::byte{0xa5},std::byte{0xa5},
      std::byte{0xa5},std::byte{0xa5},std::byte{0xa5},std::byte{0xa5}};
  const std::array stored{std::byte{0x78},std::byte{0x56},std::byte{0x34},std::byte{0x12}};
  const std::array loaded{std::byte{0xef},std::byte{0xbe},std::byte{0xad},std::byte{0xde}};
  const std::array base_bytes{std::byte{0x11},std::byte{0x22},std::byte{0x33},std::byte{0x44}};
  for(const auto operation:{exec_ir::AddressOffsetOperator::Add,exec_ir::AddressOffsetOperator::Subtract})
    for(const bool negative:{false,true}) {
      const auto delta=offset(operation,negative);
      const auto expected=(operation==exec_ir::AddressOffsetOperator::Subtract)!=negative?96U:104U;
      ASSERT_TRUE(space->initialize(memory::Address{96},reset));
      ASSERT_TRUE(registers.write(common::RegisterSlot{0},common::RawValue::b64(std::uint64_t{100})));
      ASSERT_TRUE(registers.write(common::RegisterSlot{2},common::RawValue::b32(std::uint32_t{0x12345678})));
      const exec_ir::Address address{reg(0,exec_ir::DataType::u64),delta};
      const exec_ir::StExplicitScalar store{exec_ir::StExplicitScalar::Operands{address,reg(2,exec_ir::DataType::u32)},
          exec_ir::StExplicitScalar::Controls{.state_space=exec_ir::AddressSpace::global,.type=exec_ir::DataType::u32}};
      warp().thread(execution_model::LaneId{0}).set_pc(common::ProgramCounter{0});
      auto result=engine_.execute(warp(),issue(),store,common::ProgramCounter{1});
      ASSERT_TRUE(result); ASSERT_TRUE(result->faults.empty());
      const auto actual_store=space->snapshot(memory::Address{expected},4);
      ASSERT_TRUE(actual_store); EXPECT_EQ(*actual_store,(std::vector<std::byte>{stored.begin(),stored.end()}));
      ASSERT_TRUE(space->initialize(memory::Address{expected},loaded));
      ASSERT_TRUE(space->initialize(memory::Address{100},base_bytes));
      ASSERT_TRUE(registers.write(common::RegisterSlot{2},common::RawValue::b32(std::uint32_t{0})));
      const exec_ir::LdExplicitScalar load{exec_ir::LdExplicitScalar::Operands{reg(2,exec_ir::DataType::u32),address},
          exec_ir::LdExplicitScalar::Controls{.state_space=exec_ir::AddressSpace::global,.type=exec_ir::DataType::u32}};
      warp().thread(execution_model::LaneId{0}).set_pc(common::ProgramCounter{0});
      result=engine_.execute(warp(),issue(),load,common::ProgramCounter{1});
      ASSERT_TRUE(result); ASSERT_TRUE(result->faults.empty());
      EXPECT_EQ(*registers.read(common::RegisterSlot{2}),common::RawValue::b32(std::uint32_t{0xdeadbeef}));
      const exec_ir::MovScalar move{exec_ir::MovScalar::ScalarOperands{reg(0,exec_ir::DataType::u64),
          exec_ir::Address{exec_ir::SymbolRef{exec_ir::StorageId{0}},delta}},exec_ir::MovScalar::Controls{exec_ir::DataType::u64}};
      warp().thread(execution_model::LaneId{0}).set_pc(common::ProgramCounter{0});
      result=engine_.execute(warp(),issue(),move,common::ProgramCounter{1});
      ASSERT_TRUE(result); ASSERT_TRUE(result->faults.empty());
      EXPECT_EQ(*registers.read(common::RegisterSlot{0}),common::RawValue::b64(std::uint64_t{expected}));
    }
}
TEST_F(OwnedIssueContract, FalsePredicateSuppressesMissingSymbolsAndAddressUnderflow) {
  auto registers=bind();
  ASSERT_TRUE(registers.write(common::RegisterSlot{1},common::RawValue::pred(false)));
  ASSERT_TRUE(registers.write(common::RegisterSlot{0},common::RawValue::b64(std::uint64_t{55})));
  const exec_ir::Address address{exec_ir::SymbolRef{exec_ir::StorageId{999}},offset(exec_ir::AddressOffsetOperator::Add,true)};
  const exec_ir::Predicate guard{reg(1,exec_ir::DataType::pred)};
  const exec_ir::MovScalar move{exec_ir::MovScalar::ScalarOperands{reg(0,exec_ir::DataType::u64),address},
      exec_ir::MovScalar::Controls{exec_ir::DataType::u64},guard};
  auto result=engine_.execute(warp(),issue(),move,common::ProgramCounter{1});
  ASSERT_TRUE(result); EXPECT_TRUE(result->faults.empty());
  EXPECT_EQ(*registers.read(common::RegisterSlot{0}),common::RawValue::b64(std::uint64_t{55}));
  warp().thread(execution_model::LaneId{0}).set_pc(common::ProgramCounter{0});
  const exec_ir::LdExplicitScalar load{exec_ir::LdExplicitScalar::Operands{reg(2,exec_ir::DataType::u32),address},
      exec_ir::LdExplicitScalar::Controls{.state_space=exec_ir::AddressSpace::global,.type=exec_ir::DataType::u32},guard};
  result=engine_.execute(warp(),issue(),load,common::ProgramCounter{1});
  ASSERT_TRUE(result); EXPECT_TRUE(result->faults.empty());
  warp().thread(execution_model::LaneId{0}).set_pc(common::ProgramCounter{0});
  const exec_ir::LdGenericScalar underflow{exec_ir::LdGenericScalar::Operands{reg(2,exec_ir::DataType::u32),
      exec_ir::Address{exec_ir::Immediate{0,exec_ir::DataType::u64,false,0},offset(exec_ir::AddressOffsetOperator::Add,true)}},
      exec_ir::LdGenericScalar::Controls{.type=exec_ir::DataType::u32},guard};
  result=engine_.execute(warp(),issue(),underflow,common::ProgramCounter{1});
  ASSERT_TRUE(result); EXPECT_TRUE(result->faults.empty());
  warp().thread(execution_model::LaneId{0}).set_pc(common::ProgramCounter{0});
  ASSERT_TRUE(registers.write(common::RegisterSlot{1},common::RawValue::pred(true)));
  result=engine_.execute(warp(),issue(),load,common::ProgramCounter{1});
  ASSERT_TRUE(result); ASSERT_EQ(result->faults.size(),1);
  EXPECT_TRUE(std::holds_alternative<runtime::StorageError>(result->faults[0].cause));
}
}  // namespace
TEST_F(OwnedIssueContract, MalformedMoveShapesPrecedeFalseGuardsResourcesAndSuccessors) {
  const exec_ir::RegisterVector pair{{reg(1,exec_ir::DataType::b16),reg(2,exec_ir::DataType::b16)}};
  const exec_ir::RegisterVector four{{reg(1,exec_ir::DataType::b8),reg(2,exec_ir::DataType::b8),
      reg(3,exec_ir::DataType::b8),reg(4,exec_ir::DataType::b8)}};
  const exec_ir::RegisterVector source_sink{{reg(1,exec_ir::DataType::b16),std::nullopt}};
  const exec_ir::SpecialRegisterRef tid{{exec_ir::SpecialRegisterKind::Tid,0},0,exec_ir::DataType::u32,4};
  const exec_ir::SpecialRegisterRef grid{{exec_ir::SpecialRegisterKind::GridId,0},std::nullopt,exec_ir::DataType::u64,1};
  const exec_ir::RegisterVector all_sinks{{std::nullopt,std::nullopt}};
  const std::array<std::unique_ptr<exec_ir::Instruction>,6> malformed{
      std::make_unique<exec_ir::MovScalar>(exec_ir::MovScalar::PackOperands{reg(999,exec_ir::DataType::u32),pair},exec_ir::MovScalar::Controls{exec_ir::DataType::u32}),
      std::make_unique<exec_ir::MovScalar>(exec_ir::MovScalar::PackOperands{reg(999,exec_ir::DataType::b16),four},exec_ir::MovScalar::Controls{exec_ir::DataType::b16}),
      std::make_unique<exec_ir::MovScalar>(exec_ir::MovScalar::PackOperands{reg(999,exec_ir::DataType::b32),source_sink},exec_ir::MovScalar::Controls{exec_ir::DataType::b32}),
      std::make_unique<exec_ir::MovScalar>(exec_ir::MovScalar::ScalarOperands{reg(999,exec_ir::DataType::f32),tid},exec_ir::MovScalar::Controls{exec_ir::DataType::f32}),
      std::make_unique<exec_ir::MovScalar>(exec_ir::MovScalar::ScalarOperands{reg(999,exec_ir::DataType::f64),grid},exec_ir::MovScalar::Controls{exec_ir::DataType::f64}),
      std::make_unique<exec_ir::MovScalar>(exec_ir::MovScalar::UnpackOperands{all_sinks,reg(999,exec_ir::DataType::b64)},exec_ir::MovScalar::Controls{exec_ir::DataType::b64})};
  for(const auto& instruction:malformed) {
    instruction->execution_predicate=exec_ir::Predicate{reg(999,exec_ir::DataType::pred)};
    rejected(*instruction,StepErrorCode::invalid_instruction);
    rejected(*instruction,StepErrorCode::invalid_instruction,common::ProgramCounter{999});
  }
  auto registers=bind(); ASSERT_TRUE(registers.write(common::RegisterSlot{1},common::RawValue::pred(false)));
  for(const auto& instruction:malformed) {
    instruction->execution_predicate=exec_ir::Predicate{reg(1,exec_ir::DataType::pred)};
    rejected(*instruction,StepErrorCode::invalid_instruction);
    rejected(*instruction,StepErrorCode::invalid_instruction,common::ProgramCounter{999});
  }
}
TEST(OwnedMoveShapeContract, UsesScopedIntrinsicOverridesAndPreservesLegalDestinationSinks) {
  const exec_ir::SpecialRegisterRef tid{{exec_ir::SpecialRegisterKind::Tid,0},0,exec_ir::DataType::u32,4};
  const exec_ir::SpecialRegisterRef grid{{exec_ir::SpecialRegisterKind::GridId,0},std::nullopt,exec_ir::DataType::u64,1};
  const auto& contract=exec_ir::MovScalar::static_contract();
  EXPECT_TRUE(detail::valid_scalar_move_shape(tid,exec_ir::DataType::b16,contract,0));
  EXPECT_TRUE(detail::valid_scalar_move_shape(tid,exec_ir::DataType::s16,contract,0));
  EXPECT_TRUE(detail::valid_scalar_move_shape(grid,exec_ir::DataType::b32,contract,0));
  EXPECT_TRUE(detail::valid_scalar_move_shape(grid,exec_ir::DataType::u64,contract,0));
  EXPECT_TRUE(detail::valid_scalar_move_shape(tid,exec_ir::DataType::b16,contract,2));
  /** @brief Restrict an owned fact to one layout to detect accidental cross-layout matching. */
  const std::array<std::uint16_t,1> scalar_layout{0};
  auto restricted_contract=contract;
  std::vector<exec_ir::contract::OperandTypeCompatibility> restricted_facts(
      contract.facts.operand_type_compatibility.begin(),contract.facts.operand_type_compatibility.end());
  for(auto& fact:restricted_facts) fact.target_field_id.layouts=scalar_layout;
  restricted_contract.facts.operand_type_compatibility=restricted_facts;
  EXPECT_TRUE(detail::valid_scalar_move_shape(tid,exec_ir::DataType::b16,restricted_contract,0));
  EXPECT_FALSE(detail::valid_scalar_move_shape(tid,exec_ir::DataType::b16,restricted_contract,2));
  EXPECT_FALSE(detail::valid_scalar_move_shape(tid,exec_ir::DataType::f32,contract,0));
  const exec_ir::SpecialRegisterRef clock{{exec_ir::SpecialRegisterKind::Clock,0},std::nullopt,exec_ir::DataType::u32,1};
  EXPECT_TRUE(detail::valid_scalar_move_shape(clock,exec_ir::DataType::u32,contract,0));
  EXPECT_FALSE(detail::valid_scalar_move_source(clock,exec_ir::DataType::u32));
  const exec_ir::RegisterVector pair{{reg(0,exec_ir::DataType::b8),reg(1,exec_ir::DataType::b8)}};
  const exec_ir::RegisterVector sinks{{reg(0,exec_ir::DataType::b16),std::nullopt}};
  EXPECT_TRUE(detail::valid_aggregate_move_shape(pair,exec_ir::DataType::b16,true));
  EXPECT_TRUE(detail::valid_aggregate_move_shape(sinks,exec_ir::DataType::b32,false));
  EXPECT_FALSE(detail::valid_aggregate_move_shape(sinks,exec_ir::DataType::b32,true));
  const exec_ir::RegisterVector all_sinks{{std::nullopt,std::nullopt}};
  EXPECT_FALSE(detail::valid_aggregate_move_shape(all_sinks,exec_ir::DataType::b64,false));
}
TEST_F(OwnedIssueContract, Unsigned32NumericBasesKeepSignedOffsetsAndPredicateLazyFaults) {
  auto registers=bind();
  for(const auto operation:{exec_ir::AddressOffsetOperator::Add,exec_ir::AddressOffsetOperator::Subtract})
    for(const bool negative:{false,true}) {
      const auto expected=(operation==exec_ir::AddressOffsetOperator::Subtract)!=negative?96U:104U;
      const exec_ir::Address address{exec_ir::Immediate{100,exec_ir::DataType::u32,false,100},offset(operation,negative)};
      const exec_ir::MovScalar instruction{exec_ir::MovScalar::ScalarOperands{reg(0,exec_ir::DataType::u64),address},
          exec_ir::MovScalar::Controls{exec_ir::DataType::u64}};
      warp().thread(execution_model::LaneId{0}).set_pc(common::ProgramCounter{0});
      const auto result=engine_.execute(warp(),issue(),instruction,common::ProgramCounter{1});
      ASSERT_TRUE(result); ASSERT_TRUE(result->faults.empty());
      EXPECT_EQ(*registers.read(common::RegisterSlot{0}),common::RawValue::b64(std::uint64_t{expected}));
    }
  ASSERT_TRUE(registers.write(common::RegisterSlot{1},common::RawValue::pred(false)));
  const exec_ir::Address underflow{exec_ir::Immediate{0,exec_ir::DataType::u32,false,0},
      offset(exec_ir::AddressOffsetOperator::Add,true)};
  const exec_ir::MovScalar guarded{exec_ir::MovScalar::ScalarOperands{reg(999,exec_ir::DataType::u64),underflow},
      exec_ir::MovScalar::Controls{exec_ir::DataType::u64},exec_ir::Predicate{reg(1,exec_ir::DataType::pred)}};
  warp().thread(execution_model::LaneId{0}).set_pc(common::ProgramCounter{0});
  auto result=engine_.execute(warp(),issue(),guarded,common::ProgramCounter{1});
  ASSERT_TRUE(result); EXPECT_TRUE(result->faults.empty());
  ASSERT_TRUE(registers.write(common::RegisterSlot{1},common::RawValue::pred(true)));
  warp().thread(execution_model::LaneId{0}).set_pc(common::ProgramCounter{0});
  result=engine_.execute(warp(),issue(),guarded,common::ProgramCounter{1});
  ASSERT_TRUE(result); ASSERT_EQ(result->faults.size(),1);
  EXPECT_TRUE(std::holds_alternative<memory::AddressResolutionError>(result->faults[0].cause));
  EXPECT_EQ(warp().thread(execution_model::LaneId{0}).pc(),common::ProgramCounter{0});
}
}  // namespace ptxsim::inst_execute_engine::test
