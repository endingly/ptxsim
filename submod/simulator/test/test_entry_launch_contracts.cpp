#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>
#include <ptxsim/exec_ir_lowering/exec_ir_lowering.hpp>

#include <ptxsim/exec_ir/model/control_flow/exit.gen.hpp>
#include <ptxsim/simulator/simulator.hpp>

namespace ptxsim::simulator::test {
namespace {
/** @brief Independent full metadata states that exercise the cold launch boundary. */
enum class LaunchCase { Device, Prototype, OpaqueEntry, ScalarEntry };
/** @brief Construct complete owned metadata while leaving launch capability explicit. */
auto launch_definition(LaunchCase kind) -> exec_ir::ProgramDefinition {
  exec_ir::ProgramDefinition input;
  const bool entry=kind!=LaunchCase::Device;
  const bool prototype=kind==LaunchCase::Prototype;
  input.function_declarations={{.id=exec_ir::FunctionDeclarationId{0},.function=common::FunctionId{0},
      .name="selected",.is_entry=entry,.is_prototype=prototype}};
  exec_ir::FunctionRecord function{.id=common::FunctionId{0},.name="selected",.is_entry=entry,
      .declarations={exec_ir::FunctionDeclarationId{0}}};
  if(!prototype) {
    function.definition=exec_ir::FunctionDeclarationId{0};
    function.body=exec_ir::FunctionLayout{.id=common::FunctionId{0},.instruction_count=1,
        .entry_packing_available=kind==LaunchCase::ScalarEntry};
    input.instructions.push_back(std::make_unique<exec_ir::ExitBare>(exec_ir::ExitBare::Operands{}));
  }
  if(kind==LaunchCase::OpaqueEntry) {
    input.function_declarations[0].opaque_entry_parameters={exec_ir::ResourceId{0}};
    input.resources={{.id=exec_ir::ResourceId{0},.name="texture",.kind=exec_ir::OpaqueResourceKind::Texture,
        .role=exec_ir::ResourceDeclarationRole::EntryInput,.entry_declaration=exec_ir::FunctionDeclarationId{0},
        .entry_owner=common::FunctionId{0}}};
  }
  if(kind==LaunchCase::ScalarEntry) {
    input.parameters={{.id=exec_ir::ParameterId{0},.owner_declaration=exec_ir::FunctionDeclarationId{0},
        .owner_function=common::FunctionId{0},.role=exec_ir::ParameterDeclarationRole::EntryInput,
        .scalar_type=exec_ir::DataType::u32,.alignment=4,.byte_extent=4}};
    input.function_declarations[0].parameters={exec_ir::ParameterId{0}};
    function.body->entry_parameter_size=4;
    function.body->entry_parameters={{0,4,4,exec_ir::ParameterId{0}}};
  }
  input.functions.push_back(std::move(function));
  return input;
}
/** @brief Assert rejected launch preparation does not allocate resources or alter a thread. */
void expect_launch_rejection(const exec_ir::ExecutableProgram& program,RunErrorCode code) {
  runtime::LaunchRuntime runtime{execution_model::GridId{404},
      {.cta_dim={1,1,1},.thread_dim={1,1,1},.warp_size=1}};
  auto& thread=runtime.grid().cta(execution_model::CtaId{execution_model::GridId{404},0}).warp(0).thread(execution_model::LaneId{0});
  thread.set_pc(common::ProgramCounter{7});
  const auto before=thread.status();
  const arith::context arithmetic;
  Simulator simulator{program,runtime,common::FunctionId{0},arithmetic};
  const auto result=simulator.step();
  ASSERT_FALSE(result); EXPECT_EQ(result.error().code,code);
  EXPECT_EQ(thread.pc(),common::ProgramCounter{7}); EXPECT_EQ(thread.status(),before);
  EXPECT_FALSE(runtime.global()); EXPECT_FALSE(runtime.constant()); EXPECT_FALSE(runtime.entry_parameter());
  EXPECT_FALSE(runtime.register_frame(thread.id(),common::FunctionId{0}));
  const auto repeated=simulator.step();
  ASSERT_FALSE(repeated); EXPECT_EQ(repeated.error().code,code);
}
TEST(EntryLaunchContract, RealDeviceBodyIsRepresentableButCannotLaunch) {
  const auto program=exec_ir::ExecutableProgram::create(launch_definition(LaunchCase::Device));
  ASSERT_TRUE(program); EXPECT_TRUE(program->functions()[0].body);
  const auto packed=pack_entry_arguments(*program,common::FunctionId{0},{});
  ASSERT_FALSE(packed); EXPECT_EQ(packed.error().code,EntryArgumentErrorCode::non_entry_function);
  expect_launch_rejection(*program,RunErrorCode::non_entry_function);
}
TEST(EntryLaunchContract, BodylessEntryKeepsIdentityAndReportsNonexecutableFunction) {
  const auto program=exec_ir::ExecutableProgram::create(launch_definition(LaunchCase::Prototype));
  ASSERT_TRUE(program); EXPECT_FALSE(program->functions()[0].body);
  const auto packed=pack_entry_arguments(*program,common::FunctionId{0},{});
  ASSERT_FALSE(packed); EXPECT_EQ(packed.error().code,EntryArgumentErrorCode::program_error);
  ASSERT_TRUE(packed.error().program_error);
  EXPECT_EQ(packed.error().program_error->code,exec_ir::ProgramErrorCode::nonexecutable_function);
  expect_launch_rejection(*program,RunErrorCode::program_error);
}
TEST(EntryLaunchContract, FullOpaqueAbiDoesNotPretendToHaveAnEmptyScalarSignature) {
  const auto program=exec_ir::ExecutableProgram::create(launch_definition(LaunchCase::OpaqueEntry));
  ASSERT_TRUE(program); ASSERT_EQ(program->resources().size(),1);
  EXPECT_FALSE(program->functions()[0].body->entry_packing_available);
  const auto packed=pack_entry_arguments(*program,common::FunctionId{0},{});
  ASSERT_FALSE(packed); EXPECT_EQ(packed.error().code,EntryArgumentErrorCode::unsupported_entry_signature);
  expect_launch_rejection(*program,RunErrorCode::unsupported_entry_signature);
}
TEST(EntryLaunchContract, RealResolvedVectorEntryRetainsItsValidatedBytePackingView) {
  ptx_frontend::PtxSyntaxParser parser(".entry vector_entry(.param .v2 .u32 value) { exit; }");
  const auto ast=parser.parseModule(); ASSERT_TRUE(ast);
  const auto source=ptx_frontend::resolved_ir::resolveModule(*ast); ASSERT_TRUE(source);
  const auto program=exec_ir_lowering::lower(*source); ASSERT_TRUE(program);
  ASSERT_EQ(program->parameters().size(),1);
  EXPECT_EQ(program->parameters()[0].vector_width,2);
  EXPECT_EQ(program->parameters()[0].byte_extent,8);
  EXPECT_TRUE(program->resources().empty());
  EXPECT_TRUE(program->functions()[0].body->entry_packing_available);
  const std::array bytes{std::byte{1},std::byte{2},std::byte{3},std::byte{4},
      std::byte{5},std::byte{6},std::byte{7},std::byte{8}};
  const std::array<std::span<const std::byte>,1> arguments{std::span<const std::byte>{bytes}};
  const auto packed=pack_entry_arguments(*program,common::FunctionId{0},arguments);
  ASSERT_TRUE(packed); EXPECT_EQ(*packed,(std::vector<std::byte>{bytes.begin(),bytes.end()}));
  runtime::LaunchRuntime runtime{execution_model::GridId{406},
      {.cta_dim={1,1,1},.thread_dim={1,1,1},.warp_size=1}};
  const arith::context arithmetic;
  Simulator simulator{*program,runtime,common::FunctionId{0},arithmetic,*packed};
  ASSERT_TRUE(simulator.run(1)); EXPECT_TRUE(runtime.grid().completed());
}
TEST(EntryLaunchContract, ExplicitlyUnavailableNonopaquePackingViewRejectsWithoutAllocation) {
  auto input=launch_definition(LaunchCase::ScalarEntry);
  auto& body=*input.functions[0].body;
  body.entry_packing_available=false;
  body.entry_parameters.clear();
  body.entry_parameter_size=0;
  const auto program=exec_ir::ExecutableProgram::create(input); ASSERT_TRUE(program);
  ASSERT_EQ(program->parameters().size(),1);
  EXPECT_EQ(program->parameters()[0].scalar_type,exec_ir::DataType::u32);
  EXPECT_TRUE(program->resources().empty());
  EXPECT_FALSE(program->functions()[0].body->entry_packing_available);
  const auto packed=pack_entry_arguments(*program,common::FunctionId{0},{});
  ASSERT_FALSE(packed); EXPECT_EQ(packed.error().code,EntryArgumentErrorCode::unsupported_entry_signature);
  expect_launch_rejection(*program,RunErrorCode::unsupported_entry_signature);
}

TEST(EntryLaunchContract, ScalarEntryStillPacksAndLaunchesWithOwnedParameterIdentity) {
  const auto program=exec_ir::ExecutableProgram::create(launch_definition(LaunchCase::ScalarEntry));
  ASSERT_TRUE(program);
  const std::array bytes{std::byte{1},std::byte{2},std::byte{3},std::byte{4}};
  const std::array<std::span<const std::byte>,1> arguments{std::span<const std::byte>{bytes}};
  const auto packed=pack_entry_arguments(*program,common::FunctionId{0},arguments);
  ASSERT_TRUE(packed); EXPECT_EQ(*packed,(std::vector<std::byte>{bytes.begin(),bytes.end()}));
  runtime::LaunchRuntime runtime{execution_model::GridId{405},
      {.cta_dim={1,1,1},.thread_dim={1,1,1},.warp_size=1}};
  const arith::context arithmetic;
  Simulator simulator{*program,runtime,common::FunctionId{0},arithmetic,*packed};
  const auto result=simulator.run(1);
  ASSERT_TRUE(result);
  EXPECT_TRUE(runtime.grid().completed());
  EXPECT_TRUE(runtime.entry_parameter());
}
}  // namespace
}  // namespace ptxsim::simulator::test
