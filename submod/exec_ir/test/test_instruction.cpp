#include <gtest/gtest.h>

#include <cstdint>
#include <expected>
#include <variant>

#include <ptxsim/exec_ir/instruction.hpp>

namespace ptxsim::exec_ir::test {
namespace {

const RegisterOperand kR32{common::RegisterSlot{1}, common::RawWidth::b32};
const RegisterOperand kR64{common::RegisterSlot{2}, common::RawWidth::b64};
const ValueOperand kImm32{
    ImmediateOperand{common::RawValue::b32(std::uint32_t{3})}};
const ValueOperand kImm64{
    ImmediateOperand{common::RawValue::b64(std::uint64_t{3})}};
const AddressOperand kAddress{common::RegisterSlot{7}, AddressWidth::bits64,
                              std::int64_t{16}};
const PredicateGuard kNegatedGuard{common::RegisterSlot{8}, true};

TEST(Instruction, InitialRecordsValidateAndDump) {
  const MovInst mov{kR32, kImm32, kNegatedGuard};
  const IntegerBinaryInst add{
      IntegerBinaryOp::add, IntegerSignedness::unsigned_, kR32, kR32, kImm32,
      std::nullopt};
  const IntegerBinaryInst sub{
      IntegerBinaryOp::sub, IntegerSignedness::signed_, kR64, kR64, kImm64,
      kNegatedGuard};
  const IntegerMulInst low{
      ProductPart::low, IntegerSignedness::unsigned_, kR32, kR32, kImm32,
      std::nullopt};
  const IntegerMulInst high{
      ProductPart::high, IntegerSignedness::unsigned_, kR32, kR32, kImm32,
      std::nullopt};
  const IntegerMulInst wide_unsigned{
      ProductPart::wide, IntegerSignedness::unsigned_, kR64, kR32, kImm32,
      std::nullopt};
  const IntegerMulInst wide_signed{
      ProductPart::wide, IntegerSignedness::signed_, kR64, kR32, kImm32,
      std::nullopt};
  const BitInst bit{BitOp::xor_, kR32, kR32, kImm32, std::nullopt};
  const BranchInst branch{BranchTarget{common::ProgramCounter{17}},
                          kNegatedGuard};
  const LoadInst load{MemorySpace::global, kR32, kAddress, std::nullopt};
  const StoreInst store{MemorySpace::global, kAddress, kR32, kNegatedGuard};

  EXPECT_TRUE(validate(mov));
  EXPECT_TRUE(validate(add));
  EXPECT_TRUE(validate(sub));
  EXPECT_TRUE(validate(low));
  EXPECT_TRUE(validate(high));
  EXPECT_TRUE(validate(wide_unsigned));
  EXPECT_TRUE(validate(wide_signed));
  EXPECT_TRUE(validate(bit));
  EXPECT_TRUE(validate(branch));
  EXPECT_TRUE(validate(load));
  EXPECT_TRUE(validate(store));
  const RegisterOperand pred{common::RegisterSlot{4}, common::RawWidth::pred};
  const ValueOperand pred_value{ImmediateOperand{common::RawValue::pred(true)}};
  const RegisterOperand r16{common::RegisterSlot{5}, common::RawWidth::b16};
  const ValueOperand imm16{
      ImmediateOperand{common::RawValue::b16(std::uint16_t{3})}};
  EXPECT_TRUE(validate(MovInst{pred, pred_value, std::nullopt}));
  EXPECT_TRUE(validate(MovInst{r16, imm16, std::nullopt}));
  EXPECT_TRUE(validate(MovInst{kR64, kImm64, std::nullopt}));
  EXPECT_EQ(width(kImm32), common::RawWidth::b32);
  EXPECT_EQ(width(ValueOperand{kR32}), common::RawWidth::b32);
  EXPECT_EQ(to_string(mov),
            "mov:b32 @!register:8 register:1:b32,immediate:b32:0x00000003");
  EXPECT_EQ(to_string(add),
            "add:u:b32 register:1:b32,register:1:b32,immediate:b32:0x00000003");
  EXPECT_EQ(to_string(sub),
            "sub:s:b64 @!register:8 "
            "register:2:b64,register:2:b64,immediate:b64:0x0000000000000003");
  EXPECT_EQ(
      to_string(low),
      "mul:lo:u:b32 register:1:b32,register:1:b32,immediate:b32:0x00000003");
  EXPECT_EQ(
      to_string(high),
      "mul:hi:u:b32 register:1:b32,register:1:b32,immediate:b32:0x00000003");
  EXPECT_EQ(
      to_string(wide_unsigned),
      "mul:wide:u:b64 register:2:b64,register:1:b32,immediate:b32:0x00000003");
  EXPECT_EQ(
      to_string(wide_signed),
      "mul:wide:s:b64 register:2:b64,register:1:b32,immediate:b32:0x00000003");
  EXPECT_EQ(to_string(bit),
            "xor:b32 register:1:b32,register:1:b32,immediate:b32:0x00000003");
  EXPECT_EQ(to_string(branch), "bra @!register:8 branch:pc:17");
  EXPECT_EQ(to_string(load),
            "ld:global:b32 register:1:b32,address:b64:register:7:+16");
  EXPECT_EQ(
      to_string(store),
      "st:global:b32 @!register:8 address:b64:register:7:+16,register:1:b32");

  const Instruction instruction{store};
  EXPECT_TRUE(std::holds_alternative<StoreInst>(instruction));
  EXPECT_TRUE(validate(instruction));
  EXPECT_EQ(to_string(instruction), to_string(store));
}

TEST(Instruction, StructuralValidationRejectsUnsupportedShapes) {
  const auto mov_mismatch = validate(MovInst{kR32, kImm64, std::nullopt});
  ASSERT_FALSE(mov_mismatch);
  EXPECT_EQ(mov_mismatch.error().code, InstructionErrorCode::width_mismatch);

  const RegisterOperand r8{common::RegisterSlot{3}, common::RawWidth::b8};
  const ValueOperand imm8{
      ImmediateOperand{common::RawValue::b8(std::uint8_t{3})}};
  const auto mov_unsupported = validate(MovInst{r8, imm8, std::nullopt});
  ASSERT_FALSE(mov_unsupported);
  EXPECT_EQ(mov_unsupported.error().code,
            InstructionErrorCode::unsupported_width);

  const auto binary_mismatch = validate(
      IntegerBinaryInst{IntegerBinaryOp::add, IntegerSignedness::unsigned_,
                        kR32, kR32, kImm64, std::nullopt});
  ASSERT_FALSE(binary_mismatch);
  EXPECT_EQ(binary_mismatch.error().code, InstructionErrorCode::width_mismatch);

  const auto invalid_binary_op = validate(IntegerBinaryInst{
      static_cast<IntegerBinaryOp>(99), IntegerSignedness::unsigned_, kR32,
      kR32, kImm32, std::nullopt});
  ASSERT_FALSE(invalid_binary_op);
  EXPECT_EQ(invalid_binary_op.error().code,
            InstructionErrorCode::invalid_control);

  const auto invalid_signedness = validate(IntegerBinaryInst{
      IntegerBinaryOp::add, static_cast<IntegerSignedness>(99), kR32, kR32,
      kImm32, std::nullopt});
  ASSERT_FALSE(invalid_signedness);
  EXPECT_EQ(invalid_signedness.error().code,
            InstructionErrorCode::invalid_control);

  const auto binary_unsupported = validate(
      IntegerBinaryInst{IntegerBinaryOp::sub, IntegerSignedness::signed_, r8,
                        r8, imm8, std::nullopt});
  ASSERT_FALSE(binary_unsupported);
  EXPECT_EQ(binary_unsupported.error().code,
            InstructionErrorCode::unsupported_width);

  const auto mul_source_unsupported =
      validate(IntegerMulInst{ProductPart::wide, IntegerSignedness::signed_,
                              kR64, kR64, kImm64, std::nullopt});
  ASSERT_FALSE(mul_source_unsupported);
  EXPECT_EQ(mul_source_unsupported.error().code,
            InstructionErrorCode::unsupported_width);

  const auto mul_result_invalid =
      validate(IntegerMulInst{ProductPart::low, IntegerSignedness::unsigned_,
                              kR64, kR32, kImm32, std::nullopt});
  ASSERT_FALSE(mul_result_invalid);
  EXPECT_EQ(mul_result_invalid.error().code,
            InstructionErrorCode::invalid_mul_result_relation);

  const auto mul_signed_high =
      validate(IntegerMulInst{ProductPart::high, IntegerSignedness::signed_,
                              kR32, kR32, kImm32, std::nullopt});
  ASSERT_FALSE(mul_signed_high);
  EXPECT_EQ(mul_signed_high.error().code,
            InstructionErrorCode::invalid_mul_result_relation);

  const auto invalid_product_part = validate(
      IntegerMulInst{static_cast<ProductPart>(99), IntegerSignedness::unsigned_,
                     kR32, kR32, kImm32, std::nullopt});
  ASSERT_FALSE(invalid_product_part);
  EXPECT_EQ(invalid_product_part.error().code,
            InstructionErrorCode::invalid_control);

  const auto bit_unsupported =
      validate(BitInst{BitOp::and_, kR64, kR64, kImm64, std::nullopt});
  ASSERT_FALSE(bit_unsupported);
  EXPECT_EQ(bit_unsupported.error().code,
            InstructionErrorCode::unsupported_width);

  const auto invalid_bit_op = validate(
      BitInst{static_cast<BitOp>(99), kR32, kR32, kImm32, std::nullopt});
  ASSERT_FALSE(invalid_bit_op);
  EXPECT_EQ(invalid_bit_op.error().code, InstructionErrorCode::invalid_control);

  const auto load_unsupported =
      validate(LoadInst{MemorySpace::global, kR64, kAddress, std::nullopt});
  ASSERT_FALSE(load_unsupported);
  EXPECT_EQ(load_unsupported.error().code,
            InstructionErrorCode::unsupported_width);

  const AddressOperand address32{common::RegisterSlot{7}, AddressWidth::bits32,
                                 std::int64_t{16}};
  const auto load_address_unsupported =
      validate(LoadInst{MemorySpace::global, kR32, address32, std::nullopt});
  ASSERT_FALSE(load_address_unsupported);
  EXPECT_EQ(load_address_unsupported.error().code,
            InstructionErrorCode::unsupported_width);

  const auto constant_store =
      validate(StoreInst{MemorySpace::constant, kAddress, kR32, std::nullopt});
  ASSERT_FALSE(constant_store);
  EXPECT_EQ(constant_store.error().code, InstructionErrorCode::read_only_store);

  const auto invalid_space = validate(
      LoadInst{static_cast<MemorySpace>(99), kR32, kAddress, std::nullopt});
  ASSERT_FALSE(invalid_space);
  EXPECT_EQ(invalid_space.error().code, InstructionErrorCode::invalid_control);
}

}  // namespace
}  // namespace ptxsim::exec_ir::test
