#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>
#include <ptxsim/exec_ir_lowering/exec_ir_lowering.hpp>

namespace ptxsim::exec_ir_lowering::test {
namespace {

/** @brief Resolve normalized declaration metadata for independent mutation tests. */
auto storage_module(std::string_view source)
    -> std::optional<ptx_frontend::resolved_ir::ResolvedModule> {
  ptx_frontend::PtxSyntaxParser parser(source);
  const auto ast = parser.parseModule();
  if (!ast || !ast.diagnostics.empty()) {
    ADD_FAILURE() << "Invalid PTX fixture";
    return std::nullopt;
  }
  auto module = ptx_frontend::resolved_ir::resolveModule(*ast);
  if (!module) {
    for (const auto& diagnostic : module.error())
      ADD_FAILURE() << diagnostic.message;
    return std::nullopt;
  }
  return std::move(*module);
}

/** @brief A valid sparse array with two initializer cells and one zero-filled cell. */
constexpr std::string_view kSparseStorage = R"ptx(
.global .align 16 .u32 data[3] = {0x11223344, 0x55667788};
.entry kernel() { exit; }
)ptx";

}  // namespace

TEST(StorageLowering, OwnsSparseInitializerCellsAfterFrontendDestruction) {
  auto program = [&]() {
    const auto module = storage_module(kSparseStorage);
    EXPECT_TRUE(module);
    return lower(*module);
  }();
  ASSERT_TRUE(program);
  const auto& storage = program->storage_declarations();
  ASSERT_EQ(storage.size(), 1U);
  EXPECT_EQ(storage[0].byte_extent, 12U);
  EXPECT_EQ(storage[0].alignment, 16U);
  ASSERT_EQ(storage[0].initializer.size(),2);
  EXPECT_EQ(storage[0].initializer[0].byte_offset,0);
  EXPECT_EQ(storage[0].initializer[1].byte_offset,4);
  EXPECT_EQ(std::get<exec_ir::Constant128>(storage[0].initializer[0].value),
            (exec_ir::Constant128{0x11223344,0}));
  EXPECT_EQ(std::get<exec_ir::Constant128>(storage[0].initializer[1].value),
            (exec_ir::Constant128{0x55667788,0}));
  EXPECT_EQ(storage[0].initialization,exec_ir::StorageInitialization::explicit_values);
}

TEST(StorageLowering, RejectsInconsistentNormalizedMetadata) {
  using namespace ptx_frontend::resolved_ir;
  const auto original = storage_module(kSparseStorage);
  ASSERT_TRUE(original);
  for (unsigned mutation = 0; mutation != 16; ++mutation) {
    SCOPED_TRACE(mutation);
    auto module = *original;
    auto& row = module.storage_declarations[0];
    switch (mutation) {
      case 0:
        row.symbol_id.value = std::numeric_limits<std::uint32_t>::max();
        break;
      case 1:
        row.scope_id.value = std::numeric_limits<std::uint32_t>::max();
        break;
      case 2:
        row.byte_extent = 8;
        break;
      case 3:
        row.array_extents[0] = 4;
        break;
      case 4:
        row.vector_width = 2;
        break;
      case 5:
        row.space = static_cast<StorageSpace>(255);
        break;
      case 6:
        row.initialization = static_cast<StorageInitializationKind>(255);
        break;
      case 7:
        row.initializer[1].byte_offset = 0;
        break;
      case 8:
        row.initializer[1].byte_offset = 1;
        break;
      case 9:
        row.initializer[1].byte_offset = 12;
        break;
      case 10:
        row.initialization = StorageInitializationKind::Zero;
        break;
      case 11:
        row.owner_function = module.functions[0].symbol_id;
        break;
      case 12:
        row.element_type = ptx_frontend::base::ScalarType::Pred;
        break;
      case 13:
        row.alignment = 3;
        break;
      case 14:
        std::get<StorageConstant>(row.initializer[0].value).high_bits = 1;
        break;
      case 15:
        std::get<StorageConstant>(row.initializer[0].value).bits =
            std::uint64_t{1} << 40;
        break;
    }
    EXPECT_FALSE(lower(module));
  }
}

TEST(StorageLowering, RetainsHugeSparseInitializerWithoutMaterializingBytes) {
  const auto module=storage_module(R"ptx(
.global .align 16 .u32 data[1073741824] = {0x11223344, 0x55667788};
.entry kernel() { exit; }
)ptx");
  ASSERT_TRUE(module);
  const auto program=lower(*module);
  ASSERT_TRUE(program);
  ASSERT_EQ(program->storage_declarations().size(),1);
  const auto& row=program->storage_declarations()[0];
  EXPECT_EQ(row.byte_extent,std::uint64_t{1}<<32);
  EXPECT_EQ(row.array_extents,(std::vector<std::optional<std::uint64_t>>{1073741824}));
  ASSERT_EQ(row.initializer.size(),2);
  EXPECT_EQ(std::get<exec_ir::Constant128>(row.initializer[0].value).bits,0x11223344);
  EXPECT_EQ(std::get<exec_ir::Constant128>(row.initializer[1].value).bits,0x55667788);
}

TEST(StorageLowering, CoalescesOnlyCompatibleExternalRedeclarations) {
  auto module = storage_module(R"ptx(
.extern .global .align 8 .u32 external_data[2];
.extern .global .align 8 .u32 external_data[2];
.entry kernel() { exit; }
)ptx");
  ASSERT_TRUE(module);
  ASSERT_EQ(module->storage_declarations.size(), 2U);
  const auto program = lower(*module);
  ASSERT_TRUE(program);
  EXPECT_EQ(program->storage_declarations().size(), 1U);
  module->storage_declarations[1].byte_extent = 16;
  EXPECT_FALSE(lower(*module));
}

TEST(StorageLowering, RetainsUnsizedExternalExtentForLaunchBinding) {
  const auto module = storage_module(R"ptx(
.extern .global .align 8 .u32 external_data[][2];
.entry kernel() { exit; }
)ptx");
  ASSERT_TRUE(module);
  const auto program = lower(*module);
  ASSERT_TRUE(program);
  ASSERT_EQ(program->storage_declarations().size(), 1U);
  EXPECT_EQ(program->storage_declarations()[0].byte_extent, std::nullopt);
  EXPECT_EQ(program->storage_declarations()[0].initialization,
            exec_ir::StorageInitialization::external);
  EXPECT_EQ(program->storage_declarations()[0].array_extents,
            (std::vector<std::optional<std::uint64_t>>{std::nullopt,2}));
}

TEST(StorageLowering, OwnsUnevaluatedRelocationsWithoutInventingAddresses) {
  const auto module = storage_module(R"ptx(
.global .u32 data = 4;
.global .u64 pointer = data;
.entry kernel() { exit; }
)ptx");
  ASSERT_TRUE(module);
  const auto result = lower(*module);
  ASSERT_TRUE(result);
  ASSERT_EQ(result->storage_declarations().size(),2);
  const auto& pointer=result->storage_declarations()[1];
  ASSERT_EQ(pointer.initializer.size(),1);
  const auto* relocation=std::get_if<exec_ir::StorageRelocation>(&pointer.initializer[0].value);
  ASSERT_NE(relocation,nullptr);
  EXPECT_EQ(relocation->target,(decltype(relocation->target){exec_ir::StorageId{0}}));
  EXPECT_EQ(relocation->address_kind,exec_ir::StorageAddressKind::StateSpace);
  EXPECT_EQ(relocation->addend_bits,0);
}

}  // namespace ptxsim::exec_ir_lowering::test
