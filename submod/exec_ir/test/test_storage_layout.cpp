#include <gtest/gtest.h>

#include <ptxsim/exec_ir/exec_ir.hpp>

namespace ptxsim::exec_ir::test {
namespace {
/** @brief Build a storage-only owned graph without imposing allocation capability. */
auto definition(StorageDeclaration declaration) -> ProgramDefinition {
  return {.storage_declarations = {std::move(declaration)}};
}
/** @brief Construct one concrete byte-addressable object for structural mutations. */
auto declaration() -> StorageDeclaration {
  return {.id=StorageId{0}, .name="data", .space=StorageSpace::global,
          .element_type=DataType::b8, .byte_extent=4, .alignment=4,
          .initialization=StorageInitialization::zero};
}
}  // namespace

TEST(StorageLayout, RejectsOutOfDomainStorageEnums) {
  for (unsigned field=0; field<3; ++field) {
    auto value=declaration();
    if (field==0) value.space=static_cast<StorageSpace>(255);
    if (field==1) value.declaration_kind=static_cast<StorageDeclarationKind>(255);
    if (field==2) value.initialization=static_cast<StorageInitialization>(255);
    const auto program=ExecutableProgram::create(definition(value));
    ASSERT_FALSE(program);
    EXPECT_EQ(program.error().code,ProgramErrorCode::invalid_storage_declaration);
  }
}
TEST(StorageLayout, RetainsExternalShapeForSeparateRuntimeAdmission) {
  auto value=declaration();
  value.space=StorageSpace::shared;
  value.declaration_kind=StorageDeclarationKind::External;
  value.initialization=StorageInitialization::external;
  const auto program=ExecutableProgram::create(definition(value));
  ASSERT_TRUE(program);
  EXPECT_EQ(program->storage_declarations().front(),value);
}
TEST(StorageLayout, RejectsInvalidAlignmentAndRetainsExplicitEmptyAggregate) {
  auto value=declaration(); value.alignment=3;
  const auto invalid=ExecutableProgram::create(definition(value));
  ASSERT_FALSE(invalid);
  EXPECT_EQ(invalid.error().code,ProgramErrorCode::invalid_storage_declaration);
  value=declaration(); value.byte_extent=0;
  value.initialization=StorageInitialization::explicit_values;
  const auto empty=ExecutableProgram::create(definition(value));
  ASSERT_TRUE(empty);
  EXPECT_EQ(empty->storage_declarations().front().byte_extent,0);
  EXPECT_TRUE(empty->storage_declarations().front().initializer.empty());
}
TEST(StorageLayout, KeepsDistinctOwnedIdentitiesWithEqualBoundaryNames) {
  auto first=declaration(); first.name="external";
  first.declaration_kind=StorageDeclarationKind::External;
  first.initialization=StorageInitialization::external;
  auto second=first; second.id=StorageId{1};
  const auto program=ExecutableProgram::create({.storage_declarations={first,second}});
  ASSERT_TRUE(program);
  ASSERT_EQ(program->storage_declarations().size(),2);
  EXPECT_NE(program->storage_declarations()[0].id,program->storage_declarations()[1].id);
  EXPECT_EQ(program->storage_declarations()[0].name,program->storage_declarations()[1].name);
}
TEST(StorageLayout, RetainsUnknownExternalAxesWithoutInventingExtent) {
  auto value=declaration(); value.byte_extent.reset();
  value.array_extents={std::nullopt,8};
  value.declaration_kind=StorageDeclarationKind::External;
  value.initialization=StorageInitialization::external;
  const auto program=ExecutableProgram::create(definition(value));
  ASSERT_TRUE(program);
  EXPECT_EQ(program->storage_declarations()[0].array_extents,value.array_extents);
  EXPECT_FALSE(program->storage_declarations()[0].byte_extent);
}
}  // namespace ptxsim::exec_ir::test
