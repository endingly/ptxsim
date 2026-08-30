#include <gtest/gtest.h>

#include <concepts>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <variant>

#include <ptxsim/exec_ir/operand.hpp>

namespace ptxsim::exec_ir::test {
namespace {

template <typename T>
concept CanMakeRegister =
    requires(T id) { RegisterOperand{id, common::RawWidth::b32}; };

template <typename T>
concept CanMakeSpecialRegister =
    requires(T id) { SpecialRegisterOperand{id, common::RawWidth::b32}; };

template <typename T>
concept CanMakeAddress =
    requires(T base) { AddressOperand{base, AddressWidth::bits32, 0}; };

template <typename T>
concept CanMakeAddressWithWidth =
    requires(T width) { AddressOperand{common::SymbolId{1}, width, 0}; };

TEST(Operand, TypedShapesAndDumps) {
  static_assert(std::equality_comparable<RegisterOperand>);
  static_assert(std::equality_comparable<ImmediateOperand>);
  static_assert(std::equality_comparable<SpecialRegisterOperand>);
  static_assert(std::equality_comparable<AddressOperand>);
  static_assert(std::equality_comparable<BranchTarget>);
  static_assert(std::equality_comparable<FunctionTarget>);
  static_assert(CanMakeRegister<common::RegisterSlot>);
  static_assert(!CanMakeRegister<common::SymbolId>);
  static_assert(!CanMakeRegister<std::uint32_t>);
  static_assert(CanMakeSpecialRegister<common::SpecialRegisterId>);
  static_assert(!CanMakeSpecialRegister<common::RegisterSlot>);
  static_assert(CanMakeAddress<common::RegisterSlot>);
  static_assert(CanMakeAddress<common::SymbolId>);
  static_assert(!CanMakeAddress<RegisterOperand>);
  static_assert(!CanMakeAddress<common::LabelId>);
  static_assert(!CanMakeAddressWithWidth<common::RawWidth>);
  static_assert(!std::is_convertible_v<std::uint32_t, AddressWidth>);

  const RegisterOperand register_operand{common::RegisterSlot{7},
                                         common::RawWidth::b32};
  const ImmediateOperand immediate_operand{
      common::RawValue::b16(std::uint16_t{0x12})};
  const SpecialRegisterOperand special_operand{common::SpecialRegisterId{3},
                                               common::RawWidth::b64};
  const BranchTarget branch{common::ProgramCounter{19}};
  const FunctionTarget function{common::FunctionId{5}};

  EXPECT_EQ(to_string(register_operand), "register:7:b32");
  EXPECT_EQ(to_string(immediate_operand), "immediate:b16:0x0012");
  EXPECT_EQ(to_string(special_operand), "special-register:3:b64");
  EXPECT_EQ(to_string(branch), "branch:pc:19");
  EXPECT_EQ(to_string(function), "function-target:function:5");
  EXPECT_EQ(register_operand,
            (RegisterOperand{common::RegisterSlot{7}, common::RawWidth::b32}));
  EXPECT_NE(register_operand,
            (RegisterOperand{common::RegisterSlot{7}, common::RawWidth::b64}));

  const ValueOperand register_value{register_operand};
  const ValueOperand immediate_value{immediate_operand};
  const ValueOperand special_value{special_operand};
  EXPECT_TRUE(std::holds_alternative<RegisterOperand>(register_value));
  EXPECT_TRUE(std::holds_alternative<ImmediateOperand>(immediate_value));
  EXPECT_TRUE(std::holds_alternative<SpecialRegisterOperand>(special_value));
  EXPECT_EQ(to_string(register_value), "register:7:b32");
}

TEST(Operand, AddressBasesWidthsAndOffsets) {
  const common::RegisterSlot register_slot{2};
  const AddressOperand register_address{register_slot, AddressWidth::bits32,
                                        std::int64_t{16}};
  const AddressOperand symbol_address{common::SymbolId{9}, AddressWidth::bits64,
                                      std::int64_t{-8}};

  EXPECT_TRUE(
      std::holds_alternative<common::RegisterSlot>(register_address.base));
  EXPECT_TRUE(std::holds_alternative<common::SymbolId>(symbol_address.base));
  EXPECT_EQ(to_string(register_address), "address:b32:register:2:+16");
  EXPECT_EQ(to_string(symbol_address), "address:b64:symbol:9:-8");
  EXPECT_EQ(to_string(AddressOperand{common::SymbolId{9}, AddressWidth::bits64,
                                     std::numeric_limits<std::int64_t>::min()}),
            "address:b64:symbol:9:-9223372036854775808");
  EXPECT_EQ(
      register_address,
      (AddressOperand{register_slot, AddressWidth::bits32, std::int64_t{16}}));
  EXPECT_NE(register_address, symbol_address);
}

}  // namespace
}  // namespace ptxsim::exec_ir::test
