#include <gtest/gtest.h>

#include <algorithm>
#include <memory>

#include <ptxsim/exec_ir/exec_ir.hpp>
#include <ptxsim/exec_ir/model/control_flow/exit.gen.hpp>

namespace ptxsim::exec_ir::test {
namespace {
/** @brief Build a canonical entry with independent prototype/definition parameter scopes. */
auto entry_definition() -> ProgramDefinition {
  ProgramDefinition input;
  input.instructions.push_back(std::make_unique<ExitBare>(ExitBare::Operands{}));
  input.function_declarations={
      {.id=FunctionDeclarationId{0}, .function=common::FunctionId{0}, .name="entry",
       .is_entry=true, .is_prototype=true, .parameters={ParameterId{0}}},
      {.id=FunctionDeclarationId{1}, .function=common::FunctionId{0}, .name="entry",
       .is_entry=true, .parameters={ParameterId{1},ParameterId{2},ParameterId{3}}}};
  input.parameters={
      {.id=ParameterId{0}, .owner_declaration=FunctionDeclarationId{0}, .owner_function=common::FunctionId{0},
       .role=ParameterDeclarationRole::EntryInput, .scalar_type=DataType::u32, .alignment=4, .byte_extent=4},
      {.id=ParameterId{1}, .owner_declaration=FunctionDeclarationId{1}, .owner_function=common::FunctionId{0},
       .role=ParameterDeclarationRole::EntryInput, .scalar_type=DataType::u32, .alignment=4, .byte_extent=4},
      {.id=ParameterId{2}, .owner_declaration=FunctionDeclarationId{1}, .owner_function=common::FunctionId{0},
       .role=ParameterDeclarationRole::BodyLocal, .scalar_type=DataType::u32, .alignment=4, .byte_extent=4},
      {.id=ParameterId{3}, .owner_declaration=FunctionDeclarationId{1}, .owner_function=common::FunctionId{0},
       .role=ParameterDeclarationRole::LegacyEntryInput, .scalar_type=DataType::u64, .alignment=8, .byte_extent=8}};
  input.functions={{.id=common::FunctionId{0}, .name="entry", .is_entry=true,
      .declarations={FunctionDeclarationId{0},FunctionDeclarationId{1}}, .definition=FunctionDeclarationId{1},
      .body=FunctionLayout{.id=common::FunctionId{0}, .instruction_count=1,
          .entry_parameter_size=16, .entry_parameters={{0,4,4,ParameterId{1}},{8,8,8,ParameterId{3}}}}}};
  return input;
}

/** @brief Construct complete metadata for one known role without requiring a packing view. */
auto parameter_role_definition(ParameterDeclarationRole role) -> ProgramDefinition {
  const bool entry=role!=ParameterDeclarationRole::DeviceInput && role!=ParameterDeclarationRole::DeviceReturn;
  ProgramDefinition input;
  input.instructions.push_back(std::make_unique<ExitBare>(ExitBare::Operands{}));
  input.parameters={{.id=ParameterId{0},.owner_declaration=FunctionDeclarationId{0},
      .owner_function=common::FunctionId{0},.role=role,.scalar_type=DataType::u32,.alignment=4,.byte_extent=4}};
  input.function_declarations={{.id=FunctionDeclarationId{0},.function=common::FunctionId{0},
      .name="typed",.is_entry=entry,.parameters={ParameterId{0}}}};
  input.functions={{.id=common::FunctionId{0},.name="typed",.is_entry=entry,
      .declarations={FunctionDeclarationId{0}},.definition=FunctionDeclarationId{0},
      .body=FunctionLayout{.id=common::FunctionId{0},.instruction_count=1,.entry_packing_available=false}}};
  return input;
}
/** @brief Construct a resource with exact table ownership in either supported declaration role. */
auto resource_role_definition(ResourceDeclarationRole role,OpaqueResourceKind kind) -> ProgramDefinition {
  ProgramDefinition input;
  if(role==ResourceDeclarationRole::EntryInput) {
    input=parameter_role_definition(ParameterDeclarationRole::BodyLocal);
    input.function_declarations[0].opaque_entry_parameters={ResourceId{0}};
    input.resources={{.id=ResourceId{0},.name="opaque",.kind=kind,.role=role,
        .entry_declaration=FunctionDeclarationId{0},.entry_owner=common::FunctionId{0}}};
  } else {
    input.storage_declarations={{.id=StorageId{0},.name="opaque",.element_type=kind}};
    input.resources={{.id=ResourceId{0},.name="opaque",.kind=kind,.role=role,.storage=StorageId{0}}};
  }
  return input;
}
TEST(EntryMetadataJoin, PreservesAllKnownParameterResourceAndOpaqueDomains) {
  for(const auto role:{ParameterDeclarationRole::EntryInput,ParameterDeclarationRole::DeviceInput,
      ParameterDeclarationRole::DeviceReturn,ParameterDeclarationRole::BodyLocal,ParameterDeclarationRole::LegacyEntryInput}) {
    const auto program=ExecutableProgram::create(parameter_role_definition(role)); ASSERT_TRUE(program);
    ASSERT_EQ(program->parameters().size(),1); EXPECT_EQ(program->parameters()[0].role,role);
    EXPECT_FALSE(program->functions()[0].body->entry_packing_available);
  }
  for(const auto role:{ResourceDeclarationRole::Module,ResourceDeclarationRole::EntryInput})
    for(const auto kind:{OpaqueResourceKind::Texture,OpaqueResourceKind::Sampler,OpaqueResourceKind::Surface}) {
      const auto program=ExecutableProgram::create(resource_role_definition(role,kind)); ASSERT_TRUE(program);
      ASSERT_EQ(program->resources().size(),1); EXPECT_EQ(program->resources()[0].role,role);
      EXPECT_EQ(program->resources()[0].kind,kind);
    }
}
TEST(EntryMetadataJoin, RejectsUnknownParameterRoleBeforeSelectingAnyPackingView) {
  for(const bool available:{false,true}) {
    auto input=parameter_role_definition(ParameterDeclarationRole::EntryInput);
    input.parameters[0].role=static_cast<ParameterDeclarationRole>(255);
    input.functions[0].body->entry_packing_available=available;
    const auto program=ExecutableProgram::create(input); ASSERT_FALSE(program);
    EXPECT_EQ(program.error().code,ProgramErrorCode::invalid_parameter_declaration);
  }
}
TEST(EntryMetadataJoin, RejectsUnknownResourceRolesAndOpaqueKindsWithValidTableJoins) {
  for(const auto role:{ResourceDeclarationRole::Module,ResourceDeclarationRole::EntryInput})
    for(const bool invalid_kind:{false,true}) {
      auto input=resource_role_definition(role,OpaqueResourceKind::Texture);
      if(invalid_kind) input.resources[0].kind=static_cast<OpaqueResourceKind>(255);
      else input.resources[0].role=static_cast<ResourceDeclarationRole>(255);
      const auto program=ExecutableProgram::create(input); ASSERT_FALSE(program);
      EXPECT_EQ(program.error().code,ProgramErrorCode::invalid_resource_declaration);
    }
}

TEST(EntryMetadataJoin, AcceptsDefinitionEntryAndLegacyInputsWithBodyLocalMetadata) {
  const auto program=ExecutableProgram::create(entry_definition());
  ASSERT_TRUE(program);
  const auto& layout=*program->functions()[0].body;
  ASSERT_EQ(layout.entry_parameters.size(),2);
  EXPECT_EQ(layout.entry_parameters[0].parameter,ParameterId{1});
  EXPECT_EQ(layout.entry_parameters[1].parameter,ParameterId{3});
  EXPECT_EQ(program->parameters()[2].role,ParameterDeclarationRole::BodyLocal);
  EXPECT_EQ(program->function_declarations()[0].parameters.front(),ParameterId{0});
}
TEST(EntryMetadataJoin, RejectsForgedPrototypeRoleSizeAlignmentAndCoverage) {
  for(unsigned mutation=0;mutation<9;++mutation) {
    SCOPED_TRACE(mutation);
    auto input=entry_definition();
    auto& layout=*input.functions[0].body;
    if(mutation==0) layout.entry_parameters[0].parameter=ParameterId{0};
    if(mutation==1) layout.entry_parameters[0].parameter=ParameterId{2};
    if(mutation==2) layout.entry_parameters[0].size=8;
    if(mutation==3) layout.entry_parameters[0].alignment=8;
    if(mutation==4) {layout.entry_parameters.pop_back();layout.entry_parameter_size=4;}
    if(mutation==5) {layout.entry_parameters[1].parameter=ParameterId{1};}
    if(mutation==6) {
      std::swap(layout.entry_parameters[0],layout.entry_parameters[1]);
      layout.entry_parameters[0].offset=0; layout.entry_parameters[1].offset=8;
      layout.entry_parameter_size=12;
    }
    if(mutation==7) {layout.entry_parameters.clear();layout.entry_parameter_size=0;}
    if(mutation==8) layout.entry_parameters[0].parameter=ParameterId{999};
    const auto program=ExecutableProgram::create(input);
    ASSERT_FALSE(program);
    EXPECT_EQ(program.error().code,ProgramErrorCode::invalid_entry_parameter_layout);
  }
}
TEST(EntryMetadataJoin, RejectsMissingOrDuplicatedFullParameterMembership) {
  for(const bool duplicate:{false,true}) {
    auto input=entry_definition();
    if(duplicate) input.function_declarations[1].parameters.push_back(ParameterId{2});
    else input.function_declarations[1].parameters.erase(input.function_declarations[1].parameters.begin()+1);
    const auto program=ExecutableProgram::create(input);
    ASSERT_FALSE(program);
    EXPECT_EQ(program.error().code,ProgramErrorCode::invalid_parameter_declaration);
  }
}
TEST(EntryMetadataJoin, KeepsUnpackableOpaqueEntryMetadataWithoutFabricatedSlots) {
  auto input=entry_definition();
  auto& layout=*input.functions[0].body;
  layout.entry_packing_available=false; layout.entry_parameter_size=0; layout.entry_parameters.clear();
  input.function_declarations[1].opaque_entry_parameters={ResourceId{0}};
  input.resources={{.id=ResourceId{0}, .name="texture", .kind=OpaqueResourceKind::Texture,
      .role=ResourceDeclarationRole::EntryInput, .entry_declaration=FunctionDeclarationId{1},
      .entry_owner=common::FunctionId{0}}};
  const auto program=ExecutableProgram::create(input);
  ASSERT_TRUE(program);
  EXPECT_EQ(program->resources()[0].entry_declaration,FunctionDeclarationId{1});
  EXPECT_FALSE(program->functions()[0].body->entry_packing_available);
  input.functions[0].body->entry_packing_available=true;
  EXPECT_FALSE(ExecutableProgram::create(input));
}
}  // namespace
}  // namespace ptxsim::exec_ir::test
