#include <gtest/gtest.h>

#include <optional>

#include <ptxsim/lowering/context.hpp>

namespace ptxsim::lowering::test {
namespace {

using ptx_frontend::binding::SymbolId;

TEST(LoweringContext, BindsAndResolvesEachIdentityKind) {
  LoweringContext context{4};

  EXPECT_TRUE(context.bind_function(SymbolId{0}, common::FunctionId{7}));
  EXPECT_TRUE(context.bind_symbol(SymbolId{1}, common::SymbolId{8}));
  EXPECT_TRUE(context.bind_register(SymbolId{2}, common::RegisterSlot{9}));
  EXPECT_TRUE(context.bind_label(SymbolId{3}, common::ProgramCounter{10}));

  EXPECT_EQ(context.resolve_function(SymbolId{0}), common::FunctionId{7});
  EXPECT_EQ(context.resolve_symbol(SymbolId{1}), common::SymbolId{8});
  EXPECT_EQ(context.resolve_register(SymbolId{2}), common::RegisterSlot{9});
  EXPECT_EQ(context.resolve_label(SymbolId{3}), common::ProgramCounter{10});
}

TEST(LoweringContext, ReportsMissingMappingsForEachIdentityKind) {
  const LoweringContext context{1};
  const SymbolId symbol{0};

  EXPECT_EQ(
      context.resolve_function(symbol).error(),
      (LoweringContextError{LoweringContextErrorCode::missing_mapping,
                            LoweringContextIdentityKind::function, symbol}));
  EXPECT_EQ(
      context.resolve_symbol(symbol).error(),
      (LoweringContextError{LoweringContextErrorCode::missing_mapping,
                            LoweringContextIdentityKind::symbol, symbol}));
  EXPECT_EQ(context.resolve_register(symbol).error(),
            (LoweringContextError{LoweringContextErrorCode::missing_mapping,
                                  LoweringContextIdentityKind::register_symbol,
                                  symbol}));
  EXPECT_EQ(context.resolve_label(symbol).error(),
            (LoweringContextError{LoweringContextErrorCode::missing_mapping,
                                  LoweringContextIdentityKind::label, symbol}));
}

TEST(LoweringContext, RejectsDuplicateMappingEvenForTheSameValue) {
  LoweringContext context{1};
  const SymbolId symbol{0};

  ASSERT_TRUE(context.bind_function(symbol, common::FunctionId{3}));
  ASSERT_TRUE(context.bind_symbol(symbol, common::SymbolId{3}));
  ASSERT_TRUE(context.bind_register(symbol, common::RegisterSlot{3}));
  ASSERT_TRUE(context.bind_label(symbol, common::ProgramCounter{3}));
  EXPECT_EQ(
      context.bind_function(symbol, common::FunctionId{3}).error(),
      (LoweringContextError{LoweringContextErrorCode::duplicate_mapping,
                            LoweringContextIdentityKind::function, symbol}));
  EXPECT_EQ(
      context.bind_symbol(symbol, common::SymbolId{3}).error(),
      (LoweringContextError{LoweringContextErrorCode::duplicate_mapping,
                            LoweringContextIdentityKind::symbol, symbol}));
  EXPECT_EQ(context.bind_register(symbol, common::RegisterSlot{3}).error(),
            (LoweringContextError{LoweringContextErrorCode::duplicate_mapping,
                                  LoweringContextIdentityKind::register_symbol,
                                  symbol}));
  EXPECT_EQ(context.bind_label(symbol, common::ProgramCounter{3}).error(),
            (LoweringContextError{LoweringContextErrorCode::duplicate_mapping,
                                  LoweringContextIdentityKind::label, symbol}));
}

TEST(LoweringContext, ReportsOutOfRangeMappings) {
  LoweringContext context{0};
  const SymbolId symbol{0};

  EXPECT_EQ(
      context.bind_function(symbol, common::FunctionId{4}).error(),
      (LoweringContextError{LoweringContextErrorCode::out_of_range,
                            LoweringContextIdentityKind::function, symbol}));
  EXPECT_EQ(
      context.bind_symbol(symbol, common::SymbolId{4}).error(),
      (LoweringContextError{LoweringContextErrorCode::out_of_range,
                            LoweringContextIdentityKind::symbol, symbol}));
  EXPECT_EQ(context.bind_register(symbol, common::RegisterSlot{4}).error(),
            (LoweringContextError{LoweringContextErrorCode::out_of_range,
                                  LoweringContextIdentityKind::register_symbol,
                                  symbol}));
  EXPECT_EQ(context.bind_label(symbol, common::ProgramCounter{4}).error(),
            (LoweringContextError{LoweringContextErrorCode::out_of_range,
                                  LoweringContextIdentityKind::label, symbol}));
  EXPECT_EQ(
      context.resolve_function(symbol).error(),
      (LoweringContextError{LoweringContextErrorCode::out_of_range,
                            LoweringContextIdentityKind::function, symbol}));
  EXPECT_EQ(
      context.resolve_symbol(symbol).error(),
      (LoweringContextError{LoweringContextErrorCode::out_of_range,
                            LoweringContextIdentityKind::symbol, symbol}));
  EXPECT_EQ(context.resolve_register(symbol).error(),
            (LoweringContextError{LoweringContextErrorCode::out_of_range,
                                  LoweringContextIdentityKind::register_symbol,
                                  symbol}));
  EXPECT_EQ(context.resolve_label(symbol).error(),
            (LoweringContextError{LoweringContextErrorCode::out_of_range,
                                  LoweringContextIdentityKind::label, symbol}));
}

TEST(LoweringContext, KeepsIdentityKindsIndependentAndUsesSymbolIds) {
  LoweringContext context{1};
  const SymbolId symbol{0};

  ASSERT_TRUE(context.bind_function(symbol, common::FunctionId{1}));
  ASSERT_TRUE(context.bind_symbol(symbol, common::SymbolId{2}));
  ASSERT_TRUE(context.bind_register(symbol, common::RegisterSlot{3}));
  ASSERT_TRUE(context.bind_label(symbol, common::ProgramCounter{4}));

  EXPECT_EQ(context.resolve_function(symbol), common::FunctionId{1});
  EXPECT_EQ(context.resolve_symbol(symbol), common::SymbolId{2});
  EXPECT_EQ(context.resolve_register(symbol), common::RegisterSlot{3});
  EXPECT_EQ(context.resolve_label(symbol), common::ProgramCounter{4});
}

TEST(LoweringContext, ResolvedIdsOutliveTheTemporaryContext) {
  std::optional<common::FunctionId> function;
  std::optional<common::SymbolId> symbol;
  std::optional<common::RegisterSlot> register_slot;
  std::optional<common::ProgramCounter> label;
  {
    LoweringContext context{1};
    ASSERT_TRUE(context.bind_function(SymbolId{0}, common::FunctionId{1}));
    ASSERT_TRUE(context.bind_symbol(SymbolId{0}, common::SymbolId{2}));
    ASSERT_TRUE(context.bind_register(SymbolId{0}, common::RegisterSlot{3}));
    ASSERT_TRUE(context.bind_label(SymbolId{0}, common::ProgramCounter{4}));
    function = *context.resolve_function(SymbolId{0});
    symbol = *context.resolve_symbol(SymbolId{0});
    register_slot = *context.resolve_register(SymbolId{0});
    label = *context.resolve_label(SymbolId{0});
  }

  EXPECT_EQ(function, common::FunctionId{1});
  EXPECT_EQ(symbol, common::SymbolId{2});
  EXPECT_EQ(register_slot, common::RegisterSlot{3});
  EXPECT_EQ(label, common::ProgramCounter{4});
}

}  // namespace
}  // namespace ptxsim::lowering::test
