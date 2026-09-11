#include <gtest/gtest.h>

#include <ptxsim/exec_ir/exec_ir.hpp>

namespace ptxsim::exec_ir::test {
namespace {

/** @brief Build a declaration-only program definition for layout validation. */
auto definition(StorageDeclaration declaration) -> ProgramDefinition {
  return {.storage_declarations = {std::move(declaration)}};
}

}  // namespace

TEST(StorageLayout, RejectsOutOfDomainStorageEnums) {
  StorageDeclaration declaration{.symbol = common::SymbolId{1},
                                 .name = "data",
                                 .space = static_cast<StorageSpace>(255),
                                 .extent = 4,
                                 .alignment = 4,
                                 .initialization = StorageInitialization::zero};
  const auto program =
      ExecutableProgram::create(definition(std::move(declaration)));
  ASSERT_FALSE(program);
  EXPECT_EQ(program.error().code,
            ProgramErrorCode::invalid_storage_declaration);
}

TEST(StorageLayout, RejectsSizedExternalSharedStorage) {
  StorageDeclaration declaration{
      .symbol = common::SymbolId{1},
      .name = "shared",
      .space = StorageSpace::shared,
      .extent = 4,
      .alignment = 4,
      .initialization = StorageInitialization::external};
  const auto program =
      ExecutableProgram::create(definition(std::move(declaration)));
  ASSERT_FALSE(program);
  EXPECT_EQ(program.error().code,
            ProgramErrorCode::invalid_storage_declaration);
}

TEST(StorageLayout, RejectsInvalidInitializationAndAlignment) {
  for (unsigned mutation = 0; mutation != 3; ++mutation) {
    StorageDeclaration declaration{
        .symbol = common::SymbolId{1},
        .name = "data",
        .space = StorageSpace::global,
        .extent = 4,
        .alignment = 4,
        .initialization = StorageInitialization::zero};
    if (mutation == 0)
      declaration.initialization = static_cast<StorageInitialization>(255);
    if (mutation == 1)
      declaration.alignment = 3;
    if (mutation == 2)
      declaration.extent = 0;
    const auto program =
        ExecutableProgram::create(definition(std::move(declaration)));
    ASSERT_FALSE(program);
    EXPECT_EQ(program.error().code,
              ProgramErrorCode::invalid_storage_declaration);
  }
}

TEST(StorageLayout, RejectsAmbiguousExternalNames) {
  StorageDeclaration first{.symbol = common::SymbolId{1},
                           .name = "external",
                           .space = StorageSpace::global,
                           .extent = 4,
                           .alignment = 4,
                           .initialization = StorageInitialization::external};
  auto second = first;
  second.symbol = common::SymbolId{2};
  const auto program =
      ExecutableProgram::create({.storage_declarations = {first, second}});
  ASSERT_FALSE(program);
  EXPECT_EQ(program.error().code,
            ProgramErrorCode::invalid_storage_declaration);
  EXPECT_EQ(program.error().symbol, second.symbol);
}

TEST(StorageLayout, RejectsZeroUnsizedExternalStride) {
  StorageDeclaration declaration{
      .symbol = common::SymbolId{1},
      .name = "external",
      .space = StorageSpace::global,
      .extent = 0,
      .alignment = 4,
      .initialization = StorageInitialization::external,
      .external_extent_multiple = 0};
  const auto program =
      ExecutableProgram::create(definition(std::move(declaration)));
  ASSERT_FALSE(program);
  EXPECT_EQ(program.error().code,
            ProgramErrorCode::invalid_storage_declaration);
}

}  // namespace ptxsim::exec_ir::test
