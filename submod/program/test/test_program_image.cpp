#include <gtest/gtest.h>

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <ptxsim/program/program_image.hpp>

namespace ptxsim::program::test {
namespace {

auto valid_data() -> ProgramImageData {
  using common::FunctionId;
  using common::ProgramCounter;
  using common::RawValue;
  using common::RawWidth;
  using common::RegisterSlot;
  using common::SourceLocationId;
  using common::SymbolId;
  using exec_ir::BranchInst;
  using exec_ir::BranchTarget;
  using exec_ir::ImmediateOperand;
  using exec_ir::MovInst;
  using exec_ir::RegisterOperand;
  using exec_ir::ValueOperand;

  const RegisterOperand r0{RegisterSlot{0}, RawWidth::b32};
  const auto immediate = ValueOperand{ImmediateOperand{RawValue::b32(7U)}};
  return {
      .instructions =
          {BranchInst{BranchTarget{ProgramCounter{2}}, std::nullopt},
           MovInst{r0, immediate, std::nullopt},
           BranchInst{BranchTarget{ProgramCounter{0}}, std::nullopt},
           MovInst{r0, immediate, std::nullopt},
           BranchInst{BranchTarget{ProgramCounter{3}}, std::nullopt}},
      .functions = {{FunctionId{0},
                     "main\n\"kernel\"",
                     ProgramCounter{0},
                     ProgramCounter{3},
                     {{RegisterSlot{0}, RawWidth::b32},
                      {RegisterSlot{1}, RawWidth::pred},
                      {RegisterSlot{2}, RawWidth::b64}}},
                    {FunctionId{1},
                     "helper",
                     ProgramCounter{3},
                     ProgramCounter{5},
                     {{RegisterSlot{0}, RawWidth::b32},
                      {RegisterSlot{1}, RawWidth::pred},
                      {RegisterSlot{2}, RawWidth::b64}}}},
      .symbols = {{SymbolId{0}, "global"}},
      .entry_points = {FunctionId{0}},
      .source_locations = {{SourceLocationId{0}, "source.ptx", 7, 3}},
      .source_locations_by_pc = {SourceLocationId{0}, std::nullopt,
                                 SourceLocationId{0}, std::nullopt,
                                 SourceLocationId{0}},
  };
}

void expect_error(ProgramImageData data, ProgramErrorCode code,
                  std::optional<common::FunctionId> function = std::nullopt,
                  std::optional<common::ProgramCounter> pc = std::nullopt,
                  std::optional<exec_ir::InstructionErrorCode>
                      instruction_error = std::nullopt) {
  const auto image = ProgramImage::create(std::move(data));
  ASSERT_FALSE(image);
  EXPECT_EQ(image.error().code, code);
  if (function)
    EXPECT_EQ(image.error().function, function);
  if (pc)
    EXPECT_EQ(image.error().pc, pc);
  if (instruction_error)
    EXPECT_EQ(image.error().instruction_error, instruction_error);
}

TEST(ProgramImage, OwnsDataVerifiesAndDumpsDeterministically) {
  auto first = ProgramImage::create(valid_data());
  auto second = ProgramImage::create(valid_data());
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  EXPECT_TRUE(verify(*first));
  EXPECT_EQ(first->instructions().size(), 5U);
  EXPECT_EQ(first->functions().size(), 2U);
  EXPECT_EQ(first->functions()[0].name, "main\n\"kernel\"");
  EXPECT_EQ(first->symbols()[0].name, "global");
  EXPECT_EQ(first->entry_points()[0], common::FunctionId{0});
  ASSERT_TRUE(first->source_locations_by_pc()[0]);
  EXPECT_EQ(*first->source_locations_by_pc()[0], common::SourceLocationId{0});
  EXPECT_EQ(dump(*first), dump(*second));
  EXPECT_NE(dump(*first).find("main\\n\\\"kernel\\\""), std::string::npos);
  EXPECT_NE(dump(*first).find("pc:0 bra branch:pc:2"), std::string::npos);
  EXPECT_NE(dump(*first).find("pc:2 bra branch:pc:0"), std::string::npos);
}

TEST(ProgramImage, EmptyImageIsValidOnlyWithoutFunctionsOrEntries) {
  EXPECT_TRUE(ProgramImage::create(ProgramImageData{}));
  auto with_function = ProgramImageData{};
  with_function.functions.push_back({common::FunctionId{0},
                                     "empty",
                                     common::ProgramCounter{0},
                                     common::ProgramCounter{0},
                                     {}});
  expect_error(std::move(with_function),
               ProgramErrorCode::empty_program_has_functions);
  auto with_entry = ProgramImageData{};
  with_entry.entry_points.push_back(common::FunctionId{0});
  expect_error(std::move(with_entry), ProgramErrorCode::invalid_entry_function);
}

TEST(ProgramImage, RejectsMetadataAndRecordInvariants) {
  auto data = valid_data();
  data.functions[0].id = common::FunctionId{1};
  expect_error(std::move(data), ProgramErrorCode::function_id_not_canonical);

  data = valid_data();
  data.functions[0].end_pc = common::ProgramCounter{6};
  expect_error(std::move(data), ProgramErrorCode::invalid_function_range,
               common::FunctionId{0});

  data = valid_data();
  data.functions[1].begin_pc = common::ProgramCounter{1};
  expect_error(std::move(data), ProgramErrorCode::function_ranges_not_partition,
               common::FunctionId{1});

  data = valid_data();
  data.functions[1].registers[0].slot = common::RegisterSlot{1};
  expect_error(std::move(data), ProgramErrorCode::register_slot_not_canonical,
               common::FunctionId{1});

  data = valid_data();
  data.functions[1].registers[0].width = static_cast<common::RawWidth>(99);
  expect_error(std::move(data), ProgramErrorCode::invalid_register_width,
               common::FunctionId{1});

  data = valid_data();
  data.symbols[0].id = common::SymbolId{1};
  expect_error(std::move(data), ProgramErrorCode::symbol_id_not_canonical);

  data = valid_data();
  data.source_locations[0].id = common::SourceLocationId{1};
  expect_error(std::move(data),
               ProgramErrorCode::source_location_id_not_canonical);

  data = valid_data();
  data.entry_points[0] = common::FunctionId{2};
  expect_error(std::move(data), ProgramErrorCode::invalid_entry_function);

  data = valid_data();
  data.source_locations_by_pc.pop_back();
  expect_error(std::move(data), ProgramErrorCode::source_map_size_mismatch);

  data = valid_data();
  data.source_locations_by_pc[0] = common::SourceLocationId{1};
  expect_error(std::move(data), ProgramErrorCode::invalid_source_location);

  data = valid_data();
  data.instructions[0] = exec_ir::MovInst{
      {common::RegisterSlot{0}, common::RawWidth::b32},
      exec_ir::ValueOperand{
          exec_ir::ImmediateOperand{common::RawValue::b64(std::uint64_t{7})}},
      std::nullopt};
  expect_error(std::move(data), ProgramErrorCode::invalid_instruction,
               common::FunctionId{0}, common::ProgramCounter{0},
               exec_ir::InstructionErrorCode::width_mismatch);
}

TEST(ProgramImage, VisitsEveryInstructionRecord) {
  const auto r0 =
      exec_ir::RegisterOperand{common::RegisterSlot{0}, common::RawWidth::b32};
  const auto r2 =
      exec_ir::RegisterOperand{common::RegisterSlot{2}, common::RawWidth::b64};
  const auto immediate = exec_ir::ValueOperand{
      exec_ir::ImmediateOperand{common::RawValue::b32(std::uint32_t{7})}};
  const auto guard =
      std::optional{exec_ir::PredicateGuard{common::RegisterSlot{1}, false}};
  const auto address = exec_ir::AddressOperand{
      common::RegisterSlot{2}, exec_ir::AddressWidth::bits64, 0};
  const std::vector<exec_ir::Instruction> replacements = {
      exec_ir::MovInst{r0, immediate, guard},
      exec_ir::IntegerBinaryInst{exec_ir::IntegerBinaryOp::add,
                                 exec_ir::IntegerSignedness::unsigned_, r0, r0,
                                 immediate, guard},
      exec_ir::IntegerMulInst{exec_ir::ProductPart::wide,
                              exec_ir::IntegerSignedness::unsigned_, r2, r0,
                              immediate, guard},
      exec_ir::BitInst{exec_ir::BitOp::xor_, r0, r0, immediate, guard},
      exec_ir::BranchInst{exec_ir::BranchTarget{common::ProgramCounter{2}},
                          guard},
      exec_ir::LoadInst{exec_ir::MemorySpace::global, r0, address, guard},
      exec_ir::StoreInst{exec_ir::MemorySpace::global, address, r0, guard},
  };
  for (const auto& replacement : replacements) {
    auto data = valid_data();
    data.instructions[0] = replacement;
    EXPECT_TRUE(ProgramImage::create(std::move(data)));
  }
}

TEST(ProgramImage, RejectsInvalidOperandsGuardsAndBranchTargets) {
  auto data = valid_data();
  data.instructions[0] = exec_ir::IntegerBinaryInst{
      exec_ir::IntegerBinaryOp::add,
      exec_ir::IntegerSignedness::unsigned_,
      {common::RegisterSlot{0}, common::RawWidth::b32},
      exec_ir::ValueOperand{exec_ir::RegisterOperand{common::RegisterSlot{9},
                                                     common::RawWidth::b32}},
      exec_ir::ValueOperand{
          exec_ir::ImmediateOperand{common::RawValue::b32(std::uint32_t{7})}},
      std::nullopt};
  expect_error(std::move(data), ProgramErrorCode::register_slot_not_found,
               common::FunctionId{0}, common::ProgramCounter{0});

  data = valid_data();
  data.instructions[3] = exec_ir::MovInst{
      {common::RegisterSlot{9}, common::RawWidth::b32},
      exec_ir::ValueOperand{
          exec_ir::ImmediateOperand{common::RawValue::b32(std::uint32_t{7})}},
      std::nullopt};
  expect_error(std::move(data), ProgramErrorCode::register_slot_not_found,
               common::FunctionId{1}, common::ProgramCounter{3});

  data = valid_data();
  data.instructions[3] = exec_ir::MovInst{
      {common::RegisterSlot{0}, common::RawWidth::b64},
      exec_ir::ValueOperand{
          exec_ir::ImmediateOperand{common::RawValue::b64(std::uint64_t{7})}},
      std::nullopt};
  expect_error(std::move(data), ProgramErrorCode::register_width_mismatch,
               common::FunctionId{1}, common::ProgramCounter{3});

  data = valid_data();
  data.instructions[3] = exec_ir::StoreInst{
      exec_ir::MemorySpace::global,
      {common::RegisterSlot{2}, exec_ir::AddressWidth::bits64, 0},
      {common::RegisterSlot{0}, common::RawWidth::b32},
      exec_ir::PredicateGuard{common::RegisterSlot{1}, false}};
  data.functions[1].registers[1].width = common::RawWidth::b32;
  expect_error(std::move(data), ProgramErrorCode::register_width_mismatch,
               common::FunctionId{1}, common::ProgramCounter{3});

  data = valid_data();
  data.instructions[3] = exec_ir::LoadInst{
      exec_ir::MemorySpace::global,
      {common::RegisterSlot{0}, common::RawWidth::b32},
      {common::RegisterSlot{9}, exec_ir::AddressWidth::bits64, 0},
      std::nullopt};
  expect_error(std::move(data), ProgramErrorCode::register_slot_not_found,
               common::FunctionId{1}, common::ProgramCounter{3});

  data = valid_data();
  data.instructions[0] =
      exec_ir::LoadInst{exec_ir::MemorySpace::global,
                        {common::RegisterSlot{0}, common::RawWidth::b32},
                        {common::SymbolId{1}, exec_ir::AddressWidth::bits64, 0},
                        std::nullopt};
  expect_error(std::move(data), ProgramErrorCode::invalid_symbol);

  data = valid_data();
  data.instructions[1] = exec_ir::BranchInst{
      exec_ir::BranchTarget{common::ProgramCounter{5}}, std::nullopt};
  expect_error(std::move(data), ProgramErrorCode::branch_target_out_of_range);

  data = valid_data();
  data.instructions[1] = exec_ir::BranchInst{
      exec_ir::BranchTarget{common::ProgramCounter{3}}, std::nullopt};
  expect_error(std::move(data), ProgramErrorCode::branch_crosses_function);
}

}  // namespace
}  // namespace ptxsim::program::test
