#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <string>
#include <string_view>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>
#include <ptxsim/exec_ir/model/data_movement/mov.gen.hpp>
#include <ptxsim/exec_ir/model/data_movement/ld.gen.hpp>
#include <ptxsim/exec_ir/model/data_movement/st.gen.hpp>
#include <ptxsim/exec_ir/model/stack_manipulation/stacksave.gen.hpp>
#include <ptxsim/exec_ir_lowering/exec_ir_lowering.hpp>

namespace ptxsim::exec_ir_lowering::test {
namespace {
namespace source = ptx_frontend::resolved_ir;
namespace owned = exec_ir;

/** @brief Resolve a real source fixture while retaining useful parser diagnostics. */
[[nodiscard]] auto resolve(std::string_view text) -> source::ResolvedModule {
  ptx_frontend::PtxSyntaxParser parser(text);
  const auto ast = parser.parseModule();
  if (!ast) {
    ADD_FAILURE() << "PTX fixture did not parse";
    return {};
  }
  auto result = source::resolveModule(*ast);
  if (!result) {
    ADD_FAILURE() << (result.error().empty() ? "PTX fixture did not resolve" : result.error().front().message);
    return {};
  }
  return std::move(*result);
}
/** @brief Find a native canonical boundary name without relying on frontend IDs. */
[[nodiscard]] auto function_id(const owned::ExecutableProgram& program, std::string_view name)
    -> common::FunctionId {
  const auto found = std::find_if(program.functions().begin(), program.functions().end(),
                                  [&](const auto& value) { return value.name == name; });
  EXPECT_NE(found, program.functions().end());
  return found == program.functions().end() ? common::FunctionId{0} : found->id;
}
/** @brief Assert that malformed cold input cannot publish a partial program. */
void expect_malformed(const source::ResolvedModule& module) {
  const auto result = lower(module);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, LoweringErrorCode::malformed_resolved_ir);
}

TEST(InheritedBinding, HeaderlessVectorComponentsRetainRealSlotsAfterSourceDeath) {
  const auto program = lower(resolve(R"ptx(
.entry kernel() {
  .reg .v2 .u32 %T<2>;
  mov.u32 %T1.x, %T1.r;
  exit;
}
)ptx"));
  ASSERT_TRUE(program.has_value());
  const auto instruction = program->fetch({function_id(*program, "kernel"), common::ProgramCounter{0}});
  ASSERT_TRUE(instruction.has_value());
  const auto* mov = instruction->get().get_if<owned::MovScalar>();
  ASSERT_NE(mov, nullptr);
  const auto& operands = std::get<owned::MovScalar::ScalarOperands>(mov->operands);
  const auto& src = std::get<owned::RegisterRef>(operands.src);
  EXPECT_EQ(operands.dst.slot, src.slot);
  ASSERT_TRUE(src.component.has_value());
  EXPECT_EQ(src.component->declaration_width, 2);
  EXPECT_EQ(src.component->lane, 0);
  EXPECT_EQ(src.slot.value(), 2);
  EXPECT_EQ(src.declared_type, owned::DataType::u32);
}

TEST(InheritedBinding, CanonicalFunctionsKeepBodylessDeclarationsAndAllAliasNames) {
  const auto program = lower(resolve(R"ptx(
.version 9.3
.target sm_80
.func f(.reg .u32 x);
.func alias_fn(.reg .u32 x);
.func f(.reg .u32 x) { ret; }
.alias alias_fn,f;
.extern .func external();
.global .u64 T[4] = {alias_fn,f,alias_fn};
.entry kernel() { .reg .u64 %fp; call %fp,(1+2),T; }
)ptx"));
  ASSERT_TRUE(program.has_value());
  const auto f = function_id(*program, "f");
  ASSERT_EQ(program->aliases().size(), 1);
  EXPECT_EQ(program->aliases()[0].name, "alias_fn");
  EXPECT_EQ(program->aliases()[0].function, f);
  EXPECT_GE(program->function_declarations().size(), 5);
  ASSERT_EQ(program->call_tables().size(), 1);
  const auto& table = program->call_tables().front();
  ASSERT_EQ(table.slots.size(), 3);
  EXPECT_EQ(table.extent, 4);
  EXPECT_EQ(table.slots[0].index, 0);
  EXPECT_EQ(table.slots[2].index, 2);
  EXPECT_EQ(table.slots[2].byte_offset, 16);
  for (const auto& slot : table.slots) EXPECT_EQ(slot.function, f);
  const auto external = program->fetch({function_id(*program, "external"), common::ProgramCounter{0}});
  ASSERT_FALSE(external.has_value());
  EXPECT_EQ(external.error().code, owned::ProgramErrorCode::nonexecutable_function);
}

TEST(InheritedBinding, UnusedFunctionAddressStorageRetainsRelocationsWithoutCallTable) {
  const auto program = lower(resolve(R"ptx(
.version 9.3
.target sm_80
.func f(.reg .u32 x);
.func g(.reg .f32 y);
.global .u64 T[3] = {f,g,0};
)ptx"));
  ASSERT_TRUE(program.has_value());
  EXPECT_TRUE(program->call_tables().empty());
  ASSERT_EQ(program->storage_declarations().size(), 1);
  const auto& elements = program->storage_declarations()[0].initializer;
  ASSERT_EQ(elements.size(), 3);
  EXPECT_EQ(std::get<common::FunctionId>(std::get<owned::StorageRelocation>(elements[0].value).target), function_id(*program, "f"));
  EXPECT_EQ(std::get<common::FunctionId>(std::get<owned::StorageRelocation>(elements[1].value).target), function_id(*program, "g"));
}

TEST(InheritedBinding, FullAbiRetainsOpaqueUnknownAxesAndGenericPointerAbsence) {
  const auto program = lower(resolve(R"ptx(
.version 9.3
.target sm_80
.entry kernel(.param .texref texture, .param .u64 .ptr address) { ret; }
.func device(.param .u32 count, .param .align 8 .b8 bytes[]);
)ptx"));
  ASSERT_TRUE(program.has_value());
  ASSERT_EQ(program->function_declarations().size(), 2);
  const auto& entry = program->function_declarations()[0];
  const auto& abi = entry.contract.signature.parameters;
  ASSERT_EQ(abi.size(), 2);
  EXPECT_EQ(std::get<owned::OpaqueResourceKind>(abi[0].element_type), owned::OpaqueResourceKind::Texture);
  ASSERT_TRUE(abi[1].pointer.has_value());
  EXPECT_FALSE(abi[1].pointer->pointed_space.has_value());
  EXPECT_EQ(abi[1].pointer->pointed_alignment, 4);
  const auto& device = program->function_declarations()[1].contract.signature.parameters[1];
  ASSERT_EQ(device.array_extents.size(), 1);
  EXPECT_FALSE(device.array_extents[0].has_value());
  EXPECT_FALSE(program->functions()[1].body.has_value());
}

TEST(InheritedBinding, OpaqueStorageStaticObjectsAndEntryDeclarationsRemainDistinct) {
  const auto program = lower(resolve(R"ptx(
.version 9.3
.target sm_80
.global .texref tex0;
.global .samplerref samp0 = {filter_mode=nearest};
.entry kernel(.param .align 8 .texref tex0[2][3]) { ret; }
.entry other(.param .texref tex0) { ret; }
)ptx"));
  ASSERT_TRUE(program.has_value());
  ASSERT_EQ(program->resources().size(), 4);
  EXPECT_EQ(program->resources()[0].role, owned::ResourceDeclarationRole::Module);
  EXPECT_EQ(program->resources()[2].role, owned::ResourceDeclarationRole::EntryInput);
  EXPECT_NE(program->resources()[2].entry_declaration, program->resources()[3].entry_declaration);
  EXPECT_NE(program->resources()[2].entry_owner, program->resources()[3].entry_owner);
  EXPECT_EQ(program->resources()[2].array_extents, (std::vector<std::optional<std::uint64_t>>{2,3}));
  const auto& sampler = program->storage_declarations()[1];
  ASSERT_EQ(sampler.opaque_static_objects.size(), 1);
  ASSERT_EQ(sampler.opaque_static_objects[0].members.size(), 1);
  EXPECT_EQ(std::get<owned::OpaqueStaticEnum>(sampler.opaque_static_objects[0].members[0].value), owned::OpaqueStaticEnum::Nearest);
}

TEST(InheritedBinding, SparseConstantsRelocationAddendsMasksAndUuidSurvive) {
  const auto program = lower(resolve(R"ptx(
.version 9.3
.target sm_90
.global .u32 base[4];
.global .u64 addresses[3] = {base,generic(base)+8,0xff(base+1)};
.global .b128 values[][3] = {{1,-1},{-1U}};
.func .attribute(.unified(0,18446744073709551615U)) helper() { ret; }
)ptx"));
  ASSERT_TRUE(program.has_value());
  ASSERT_EQ(program->storage_declarations().size(), 3);
  const auto& addresses = program->storage_declarations()[1].initializer;
  ASSERT_EQ(addresses.size(), 3);
  const auto& generic = std::get<owned::StorageRelocation>(addresses[1].value);
  EXPECT_EQ(generic.address_kind, owned::StorageAddressKind::Generic);
  EXPECT_EQ(generic.addend_bits, 8);
  const auto& masked = std::get<owned::StorageRelocation>(addresses[2].value);
  EXPECT_EQ(masked.addend_bits, 1);
  EXPECT_EQ(masked.byte_mask, 0xff);
  const auto& sparse = program->storage_declarations()[2];
  ASSERT_EQ(sparse.initializer.size(), 3);
  EXPECT_EQ(sparse.initializer[2].byte_offset, 48);
  EXPECT_EQ(std::get<owned::Constant128>(sparse.initializer[1].value).high_bits, std::numeric_limits<std::uint64_t>::max());
  ASSERT_EQ(program->function_declarations().size(), 1);
  ASSERT_EQ(program->function_declarations()[0].contract.attributes.size(), 1);
  const auto& uuid = program->function_declarations()[0].contract.attributes[0].unified_id;
  ASSERT_TRUE(uuid.has_value());
  EXPECT_EQ(uuid->lower, std::numeric_limits<std::uint64_t>::max());
}

/** @brief Legal user subclass of the open frontend base with an untrusted reported Kind. */
class Impostor final : public source::Instruction {
 public:
  /** @brief Select the malicious known or unknown identity reported by this object. */
  explicit Impostor(source::InstructionKind reported) : reported_(reported) {}
  /** @brief Return a lying identity without reading any real form fields. */
  auto instruction_kind() const noexcept -> source::InstructionKind override { return reported_; }
  /** @brief Clone this legal user subclass as the frontend interface permits. */
  auto clone() const -> std::unique_ptr<source::Instruction> override { return std::make_unique<Impostor>(*this); }
  /** @brief Deliberately claim success to prove check success cannot prove final type. */
  auto check(const source::checker::Context&) const -> source::checker::CheckResult override { return {}; }
  /** @brief Deliberately expose no references; the binder must still reject the type. */
  void visit_references(source::detail::IReferenceObserver&) const override {}
 private:
  /** @brief Reported identity; this object is never an actual generated final form. */
  source::InstructionKind reported_;
};

TEST(InheritedBinding, ColdRttiRejectsLyingKnownKindUnknownKindAndNullBeforeVisitors) {
  for (const auto kind : {source::InstructionKind::AddFloatF32,
                         static_cast<source::InstructionKind>(std::numeric_limits<std::uint32_t>::max())}) {
    auto module = resolve(".entry kernel() { exit; }");
    ASSERT_EQ(module.functions.size(), 1);
    module.functions[0].body[0] = std::make_unique<Impostor>(kind);
    expect_malformed(module);
  }
  auto module = resolve(".entry kernel() { exit; }");
  ASSERT_EQ(module.functions.size(), 1);
  module.functions[0].body[0].reset();
  expect_malformed(module);
}

TEST(InheritedBinding, HeaderlessUnusedPrototypeAbiStillRejectsInvalidStructuralKeys) {
  for (const auto category : {0, 1, 2}) {
    auto module = resolve(".func f(.param .u64 pointer, .param .align 8 .b8 bytes[]);");
    ASSERT_EQ(module.functions.size(), 1);
    ASSERT_EQ(module.functions[0].contract.signature.parameters.size(), 2);
    auto& formal = module.functions[0].contract.signature.parameters[category == 1 ? 1 : 0];
    const ptx_frontend::declaration_semantics::InvalidStructuralKey invalid{"invalid"};
    if (category == 0) formal.alignment = invalid;
    if (category == 1) {
      ASSERT_EQ(formal.array_extents.size(), 1);
      formal.array_extents[0] = invalid;
    }
    if (category == 2) {
      formal.is_pointer = true;
      formal.pointer_alignment = invalid;
    }
    expect_malformed(module);
  }
}

TEST(InheritedBinding, StackScopeCannotUseAnotherDeclarationOfSameCanonicalFunction) {
  auto module = resolve(R"ptx(
.func f();
.func f() { .reg .u64 %token; stacksave.u64 %token; ret; }
)ptx");
  ASSERT_EQ(module.functions.size(), 2);
  ASSERT_FALSE(module.functions[1].body.empty());
  auto* instruction = dynamic_cast<source::StacksaveU64*>(module.functions[1].body[0].get());
  ASSERT_NE(instruction, nullptr);
  ASSERT_TRUE(lower(module).has_value());
  instruction->dst.value.function_scope = module.functions[0].declaration_scope;
  expect_malformed(module);
  instruction->dst.value.function_scope.reset();
  expect_malformed(module);
}

TEST(InheritedBinding, HeaderlessUnusedTargetSetAbiAndStaleCallTableAreStillChecked) {
  auto targets = resolve(R"ptx(
.func f(.reg .u32 x);
.entry kernel() { list: .calltargets f; exit; }
)ptx");
  ASSERT_FALSE(targets.functions.empty());
  ASSERT_EQ(targets.functions.back().call_target_sets.size(), 1);
  ASSERT_EQ(targets.functions.back().call_target_sets[0].signature.parameters.size(), 1);
  ASSERT_TRUE(lower(targets).has_value());
  targets.functions.back().call_target_sets[0].signature.parameters[0].scalar_type = ptx_frontend::base::ScalarType::Invalid;
  expect_malformed(targets);

  auto table = resolve(R"ptx(
.version 9.3
.target sm_80
.func f(.reg .u32 x);
.global .u64 T[2] = {f};
.entry kernel() { .reg .u64 %fp; call %fp,(1),T; }
)ptx");
  ASSERT_FALSE(table.functions.empty());
  ASSERT_EQ(table.call_tables.size(), 1);
  table.functions.back().body.clear();
  table.functions.back().instruction_ranges.clear();
  table.functions.back().instruction_opcodes.clear();
  expect_malformed(table);
}

TEST(InheritedBinding, HeaderlessIntrinsicIndicesHaveClosedFamilyBounds) {
  for (const auto text : {
      ".entry k() { .reg .b32 %r; mov.b32 %r,%pm7; exit; }",
      ".entry k() { .reg .b64 %r; mov.b64 %r,%pm7_64; exit; }",
      ".entry k() { .reg .b32 %r; mov.b32 %r,%envreg31; exit; }",
      ".entry k() { .reg .b32 %r; mov.b32 %r,%reserved_smem_offset_1; exit; }",
      ".entry k() { .reg .u32 %r; mov.u32 %r,%laneid; exit; }"}) {
    SCOPED_TRACE(text);
    auto module = resolve(text);
    ASSERT_EQ(module.functions.size(), 1);
    ASSERT_FALSE(module.functions[0].body.empty());
    auto* mov = dynamic_cast<source::MovScalar*>(module.functions[0].body[0].get());
    ASSERT_NE(mov, nullptr);
    ASSERT_TRUE(mov->src_mov_source.has_value());
    auto* intrinsic = std::get_if<source::ResolvedSpecialRegisterRef>(&mov->src_mov_source->value);
    ASSERT_NE(intrinsic, nullptr);
    ASSERT_TRUE(lower(module).has_value());
    ++intrinsic->id.index;
    expect_malformed(module);
  }
}

TEST(InheritedBinding, WholeIntrinsicVectorUsesTheSameIdentityGuard) {
  auto module = resolve(".entry k() { .reg .v4 .u32 V; mov.v4.u32 V,%tid; exit; }");
  ASSERT_EQ(module.functions.size(), 1);
  ASSERT_FALSE(module.functions[0].body.empty());
  auto* mov = dynamic_cast<source::MovV4U32*>(module.functions[0].body[0].get());
  ASSERT_NE(mov, nullptr);
  auto* intrinsic = std::get_if<source::ResolvedVectorSpecialRegisterRef>(&mov->src.value);
  ASSERT_NE(intrinsic, nullptr);
  ASSERT_TRUE(lower(module).has_value());
  intrinsic->id.index = 1;
  expect_malformed(module);
}

TEST(InheritedBinding, CompatibleExternalAlignmentOccurrencesCanonicalizeInEitherOrder) {
  const std::array cases{
      std::pair{".u32 scalar", std::uint64_t{4}},
      std::pair{".u64 word", std::uint64_t{8}},
      std::pair{".u8 bytes[32]", std::uint64_t{1}},
      std::pair{".v2 .u32 pair[4]", std::uint64_t{8}},
      std::pair{".v4 .u32 quad[3]", std::uint64_t{16}},
  };
  for (const auto& [declaration, alignment] : cases) {
    for (const bool explicit_first : {false, true}) {
      const auto natural = ".extern .global " + std::string(declaration) + ";\n";
      const auto explicit_ = ".extern .global .align " + std::to_string(alignment) + " " + declaration + ";\n";
      auto module = resolve(explicit_first ? explicit_ + natural : natural + explicit_);
      ASSERT_EQ(module.storage_declarations.size(), 2);
      const auto program = lower(module);
      ASSERT_TRUE(program.has_value());
      ASSERT_EQ(program->storage_declarations().size(), 1);
      EXPECT_EQ(program->storage_declarations()[0].alignment, alignment);
      EXPECT_EQ(program->storage_declarations()[0].explicit_alignment, alignment);
    }
  }
}

TEST(InheritedBinding, StorageOccurrenceMergeDoesNotHideExecutionFactConflicts) {
  for (const auto conflict : {0, 1, 2, 3, 4, 5, 6}) {
    auto module = resolve(".extern .global .u32 value[2]; .extern .global .align 4 .u32 value[2];");
    ASSERT_EQ(module.storage_declarations.size(), 2);
    auto& second = module.storage_declarations[1];
    if (conflict == 0) second.alignment = 8;
    if (conflict == 1) second.array_extents[0] = 3;
    if (conflict == 2) second.linkage = ptx_frontend::binding::SymbolLinkage::Visible;
    if (conflict == 3) second.declaration_kind = source::StorageDeclarationKind::Definition;
    if (conflict == 4) second.unified_id = source::ResolvedUnifiedId{1, 2};
    if (conflict == 5) second.explicit_alignment = 8;
    if (conflict == 6) {
      second.initialization = source::StorageInitializationKind::Explicit;
      second.initializer.push_back({0, source::StorageConstant{9, 0}});
    }
    expect_malformed(module);
  }
  auto definition = resolve(".global .u32 value = 7;");
  ASSERT_EQ(definition.storage_declarations.size(), 1);
  definition.storage_declarations.push_back(definition.storage_declarations[0]);
  expect_malformed(definition);
}
}  // namespace
TEST(InheritedBinding, FormalSymbolsCannotAcquireCompactGroupsEvenWhenUnused) {
  const auto original=resolve(R"ptx(
.version 9.3
.target sm_90
.func (.param .u32 ret) device(.reg .u32 reg_input, .param .u32 scalar_input);
.entry kernel(.param .u32 scalar, .param .texref texture) { exit; }
)ptx");
  ASSERT_EQ(original.functions.size(),2);
  const auto positive=lower(original); ASSERT_TRUE(positive);
  EXPECT_EQ(positive->function_declarations()[0].contract.signature.parameters.size(),2);
  EXPECT_EQ(positive->function_declarations()[0].contract.signature.return_parameters.size(),1);
  ASSERT_EQ(positive->resources().size(),1);
  std::size_t formals=0;
  for(const auto& symbol:original.symbols.symbols()) {
    if(symbol.kind!=ptx_frontend::binding::SymbolKind::InputParameter &&
        symbol.kind!=ptx_frontend::binding::SymbolKind::ReturnParameter) continue;
    ++formals;
    for(const auto count:{0U,1U,2U}) {
      SCOPED_TRACE(symbol.name);
      SCOPED_TRACE(count);
      auto mutated=original;
      const_cast<ptx_frontend::binding::Symbol&>(mutated.symbols.symbol(symbol.id)).parameterized_count=count;
      const auto program=lower(mutated); ASSERT_FALSE(program);
      EXPECT_EQ(program.error().code,LoweringErrorCode::malformed_resolved_ir);
    }
  }
  EXPECT_EQ(formals,5);
}
TEST(InheritedBinding, BodyLocalAndLegacyCompactMembersStayDistinctFromHeaderFormals) {
  const auto body=lower(resolve(R"ptx(
.version 9.3
.target sm_90
.address_size 64
.global .u32 objects<2>;
.func take(.param .u32 input);
.func (.param .u32 result) exchange(.param .u32 input);
.entry kernel() { .reg .u32 %r; .param .u32 value<2>;
  st.param.u32 [value0], %r; call (value1), exchange, (value0);
  ld.param.u32 %r, [value1]; exit; }
)ptx"));
  ASSERT_TRUE(body);
  const auto function=function_id(*body,"kernel");
  const auto first=body->fetch({function,common::ProgramCounter{0}}); ASSERT_TRUE(first);
  const auto third=body->fetch({function,common::ProgramCounter{2}}); ASSERT_TRUE(third);
  const auto* store=first->get().get_if<owned::StExplicitScalar>(); ASSERT_NE(store,nullptr);
  const auto* load=third->get().get_if<owned::LdExplicitScalar>(); ASSERT_NE(load,nullptr);
  const auto& stored=std::get<owned::SymbolRef>(store->operands.address.base);
  const auto& loaded=std::get<owned::SymbolRef>(load->operands.address.base);
  EXPECT_EQ(stored.target,loaded.target); EXPECT_EQ(stored.member,0); EXPECT_EQ(loaded.member,1);
  const auto parameter=std::get<owned::ParameterId>(stored.target);
  EXPECT_EQ(body->parameters()[parameter.value()].role,owned::ParameterDeclarationRole::BodyLocal);
  EXPECT_EQ(body->parameters()[parameter.value()].parameterized_count,2);
  EXPECT_EQ(body->parameters()[parameter.value()].byte_extent,4);
  ASSERT_EQ(body->storage_declarations().size(),1);
  EXPECT_EQ(body->storage_declarations()[0].parameterized_count,2);
  const auto& declaration=body->function_declarations()[body->functions()[function.value()].definition->value()];
  EXPECT_TRUE(declaration.contract.signature.parameters.empty());
  ASSERT_EQ(declaration.parameters.size(),1); EXPECT_EQ(declaration.parameters[0],parameter);

  const auto legacy=lower(resolve(R"ptx(
.version 1.3
.target sm_10
.entry legacy() { .reg .u32 %r; .param .align 8 .u32 x<3>;
  ld.param.u32 %r, [x2]; exit; }
)ptx"));
  ASSERT_TRUE(legacy); ASSERT_EQ(legacy->parameters().size(),1);
  EXPECT_EQ(legacy->parameters()[0].role,owned::ParameterDeclarationRole::LegacyEntryInput);
  EXPECT_EQ(legacy->parameters()[0].parameterized_count,3);
  EXPECT_EQ(legacy->parameters()[0].byte_extent,4);
  const auto instruction=legacy->fetch({common::FunctionId{0},common::ProgramCounter{0}}); ASSERT_TRUE(instruction);
  const auto* form=instruction->get().get_if<owned::LdExplicitScalar>(); ASSERT_NE(form,nullptr);
  const auto& member=std::get<owned::SymbolRef>(form->operands.address.base);
  EXPECT_EQ(std::get<owned::ParameterId>(member.target),owned::ParameterId{0}); EXPECT_EQ(member.member,2);
  EXPECT_TRUE(legacy->function_declarations()[0].contract.signature.parameters.empty());
}
}  // namespace ptxsim::exec_ir_lowering::test
