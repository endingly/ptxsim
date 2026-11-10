#include "lowering_detail.hpp"

#include <bit>
#include <concepts>
#include <limits>
#include <set>
#include <string_view>
#include <type_traits>

#include <magic_enum/magic_enum.hpp>
#include <ptx_frontend/semantic/ptx_function_contract.hpp>

namespace ptxsim::exec_ir_lowering::detail {
namespace {
namespace source = ptx_frontend::resolved_ir;
namespace owned = exec_ir;
namespace abi = ptx_frontend::call_argument_compatibility;

/** @brief Structured metadata conversion unwind; no partially built graph is published. */
struct MetadataFailure {
  /** @brief Stable source-boundary error and optional identity context. */
  LoweringError error;
};
/** @brief Stop a malformed metadata join without synthesizing a replacement. */
[[noreturn]] void reject(std::optional<std::uint32_t> symbol = std::nullopt) {
  throw MetadataFailure{LoweringError{LoweringErrorCode::malformed_resolved_ir,
                                      std::nullopt, std::nullopt, symbol, std::nullopt}};
}
/** @brief Supported named metadata enum domains with fixed source semantic names. */
template <typename T>
concept MetadataEnum = std::same_as<T, owned::Linkage> || std::same_as<T, owned::OpaqueResourceKind> ||
    std::same_as<T, owned::DeclarationSpace> || std::same_as<T, owned::AbiVectorShape> ||
    std::same_as<T, owned::ParameterDeclarationRole> || std::same_as<T, owned::FunctionAttributeKind> ||
    std::same_as<T, owned::KernelResourceKind> || std::same_as<T, owned::OpaqueStaticField> ||
    std::same_as<T, owned::StorageDeclarationKind> || std::same_as<T, owned::StorageAddressKind> ||
    std::same_as<T, owned::TextureMode>;
/** @brief Map a cold named semantic enum, rejecting invalid/unknown source values. */
template <MetadataEnum Out, typename In>
  requires std::is_enum_v<In>
[[nodiscard]] auto named(In value) -> Out {
  const auto result = magic_enum::enum_cast<Out>(magic_enum::enum_name(value));
  if (!result) reject();
  return *result;
}
/** @brief Check dense-count conversion before introducing any native identity. */
[[nodiscard]] auto index(std::size_t value) -> std::uint32_t {
  if (value > std::numeric_limits<std::uint32_t>::max()) reject();
  return static_cast<std::uint32_t>(value);
}
/** @brief Resolve a previously registered native identity or report malformed source. */
template <typename Id>
  requires requires(Id value) { { value.value() } -> std::same_as<std::uint32_t>; }
[[nodiscard]] auto identity(const std::map<std::uint32_t, Id>& table, std::uint32_t source_id) -> Id {
  const auto found = table.find(source_id);
  if (found == table.end()) reject(source_id);
  return found->second;
}
/** @brief Check one current source symbol identity before using any cached field. */
[[nodiscard]] auto symbol(const ModuleBindings& state, std::uint32_t id)
    -> const ptx_frontend::binding::Symbol& {
  const auto& symbols = state.source.symbols.symbols();
  if (id >= symbols.size() || symbols[id].id.value != id) reject(id);
  return symbols[id];
}
/** @brief Resolve an exact lexical scope's owning concrete function declaration. */
[[nodiscard]] auto scope_owner(const ModuleBindings& state, ptx_frontend::binding::ScopeId scope)
    -> std::optional<owned::FunctionDeclarationId> {
  const auto& scopes = state.source.symbols.scopes();
  for (std::size_t depth = 0; depth <= scopes.size(); ++depth) {
    if (const auto found = state.declaration_scopes.find(scope.value); found != state.declaration_scopes.end())
      return found->second;
    if (scope.value >= scopes.size() || scopes[scope.value].id != scope) reject();
    if (!scopes[scope.value].parent) return std::nullopt;
    scope = *scopes[scope.value].parent;
  }
  reject();
}
/** @brief Require a symbol's exact lexical owner to be the current declaration. */
void check_scope(const ModuleBindings& state, std::uint32_t symbol_id,
                   ptx_frontend::binding::ScopeId scope, owned::FunctionDeclarationId owner) {
  if (symbol(state, symbol_id).scope != scope || scope_owner(state, scope) != owner) reject(symbol_id);
}
/** @brief Convert every modeled valid scalar type without an execution filter. */
[[nodiscard]] auto scalar(ptx_frontend::base::ScalarType type) -> owned::DataType {
  const auto result = convert_ScalarType(type);
  if (!result) reject();
  return *result;
}
/** @brief Copy genuine optional numeric ABI values while rejecting invalid keys. */
[[nodiscard]] auto numeric(const std::optional<ptx_frontend::declaration_semantics::NormalizedNumericValue>& value)
    -> std::optional<std::uint64_t> {
  if (!value) return std::nullopt;
  const auto* number = std::get_if<std::uint64_t>(&*value);
  if (!number) reject();
  return *number;
}
/** @brief Validate byte alignment without fabricating an effective default. */
[[nodiscard]] auto alignment(std::uint64_t value) -> bool { return std::has_single_bit(value); }
/** @brief Copy legal generic/concrete pointer target space and effective alignment. */
[[nodiscard]] auto pointer(const abi::PointerProperties& value) -> owned::PointerProperties {
  owned::PointerProperties result;
  if (value.pointed_state_space) result.pointed_space = named<owned::DeclarationSpace>(*value.pointed_state_space);
  if (!alignment(value.pointed_alignment)) reject();
  result.pointed_alignment = value.pointed_alignment;
  return result;
}
/** @brief Copy one full valid ABI formal, including opaque and unsized alternatives. */
[[nodiscard]] auto formal(const ptx_frontend::declaration_semantics::FunctionParameterContract& value)
    -> owned::FunctionParameterContract {
  owned::FunctionParameterContract result;
  result.state_space = named<owned::DeclarationSpace>(value.state_space);
  result.alignment = numeric(value.alignment);
  if (result.alignment && !alignment(*result.alignment)) reject();
  if (value.opaque_kind) {
    if (value.scalar_type != ptx_frontend::base::ScalarType::Invalid) reject();
    result.element_type = named<owned::OpaqueResourceKind>(*value.opaque_kind);
  } else {
    result.element_type = scalar(value.scalar_type);
  }
  result.vector_shape = named<owned::AbiVectorShape>(value.vector_shape);
  if (value.is_pointer) {
    const auto effective = numeric(value.pointer_alignment);
    if (!effective || !alignment(*effective)) reject();
    owned::PointerProperties properties;
    if (value.pointed_state_space) properties.pointed_space = named<owned::DeclarationSpace>(*value.pointed_state_space);
    properties.pointed_alignment = *effective;
    result.pointer = properties;
  } else if (value.pointed_state_space || value.pointer_alignment) {
    reject();
  }
  std::uint64_t product = 1;
  for (std::size_t position = 0; position < value.array_extents.size(); ++position) {
    const auto extent = numeric(value.array_extents[position]);
    if (!extent) {
      if (position != 0) reject();
    } else if (*extent == 0 || product > std::numeric_limits<std::uint64_t>::max() / *extent) {
      reject();
    } else {
      product *= *extent;
    }
    result.array_extents.push_back(extent);
  }
  return result;
}
/** @brief Deep-copy ordered returned/input formals and callable-kind flags. */
[[nodiscard]] auto signature(const ptx_frontend::declaration_semantics::FunctionSignature& value)
    -> owned::FunctionSignature {
  owned::FunctionSignature result{value.is_entry, value.is_noreturn, {}, {}};
  for (const auto& parameter : value.return_parameters) result.return_parameters.push_back(formal(parameter));
  for (const auto& parameter : value.parameters) result.parameters.push_back(formal(parameter));
  return result;
}
/** @brief Copy an optional ABI preservation obligation without source locations. */
[[nodiscard]] auto preserve(const std::optional<source::ResolvedAbiPreservationContract>& value)
    -> std::optional<owned::AbiPreservationContract> {
  if (value) return owned::AbiPreservationContract{value->count, value->control_registers};
  return std::nullopt;
}
/** @brief Preserve UUID halves in source operand significance, never host byte order. */
[[nodiscard]] auto uuid(const std::optional<source::ResolvedUnifiedId>& value) -> std::optional<owned::UnifiedId> {
  if (value) return owned::UnifiedId{value->upper, value->lower};
  return std::nullopt;
}
/** @brief Deep-copy full callable attributes, resource dimensions and ABI contracts. */
[[nodiscard]] auto contract(const source::ResolvedFunctionContract& value) -> owned::FunctionContract {
  owned::FunctionContract result;
  result.signature = signature(value.signature);
  result.linkage = named<owned::Linkage>(value.linkage);
  for (const auto& attribute : value.attributes)
    result.attributes.push_back({named<owned::FunctionAttributeKind>(attribute.kind), uuid(attribute.unified_id)});
  result.is_noreturn = value.is_noreturn;
  result.abi_preserve = preserve(value.abi_preserve);
  result.abi_preserve_control = preserve(value.abi_preserve_control);
  for (const auto& resource : value.resources)
    result.resources.push_back({named<owned::KernelResourceKind>(resource.kind), resource.values, resource.explicit_value_count});
  result.blocks_are_clusters = value.blocks_are_clusters;
  result.language_values = value.language_values;
  return result;
}
/** @brief Convert full declaration storage space without imposing allocation support. */
[[nodiscard]] auto storage_space(source::StorageSpace value) -> owned::StorageSpace {
  switch (value) {
    case source::StorageSpace::Global: return owned::StorageSpace::global;
    case source::StorageSpace::Constant: return owned::StorageSpace::constant;
    case source::StorageSpace::Shared: return owned::StorageSpace::shared;
    case source::StorageSpace::Local: return owned::StorageSpace::local;
  }
  reject();
}
/** @brief Convert every complete initialization category without flattening opaque data. */
[[nodiscard]] auto initialization(source::StorageInitializationKind value) -> owned::StorageInitialization {
  switch (value) {
    case source::StorageInitializationKind::Uninitialized: return owned::StorageInitialization::uninitialized;
    case source::StorageInitializationKind::Zero: return owned::StorageInitialization::zero;
    case source::StorageInitializationKind::Explicit: return owned::StorageInitialization::explicit_values;
    case source::StorageInitializationKind::External: return owned::StorageInitialization::external;
    case source::StorageInitializationKind::OpaqueStatic: return owned::StorageInitialization::opaque_static;
  }
  reject();
}
/** @brief Register all callable identities and distinct concrete declaration scopes. */
void functions(ModuleBindings& state) {
  for (const auto& function : state.source.functions) {
    const auto& declaration_symbol = symbol(state, function.symbol_id.value);
    const auto& canonical = symbol(state, function.contract.canonical_function.value);
    const auto& scopes = state.source.symbols.scopes();
    if (declaration_symbol.kind != ptx_frontend::binding::SymbolKind::Function ||
        canonical.kind != ptx_frontend::binding::SymbolKind::Function ||
        function.is_entry != declaration_symbol.function_is_entry ||
        function.declaration_scope.value >= scopes.size() ||
        scopes[function.declaration_scope.value].kind != ptx_frontend::binding::ScopeKind::Function ||
        scopes[function.declaration_scope.value].owner != function.symbol_id ||
        (function.is_prototype && !function.body.empty()) ||
        (!function.is_prototype && declaration_symbol.owned_scope != function.declaration_scope))
      reject(function.symbol_id.value);
    for(const auto& formal:state.source.symbols.symbols())
      if(formal.scope==function.declaration_scope &&
          (formal.kind==ptx_frontend::binding::SymbolKind::InputParameter ||
           formal.kind==ptx_frontend::binding::SymbolKind::ReturnParameter) && formal.parameterized_count)
        reject(formal.id.value);
    auto found = state.functions.find(canonical.id.value);
    if (found == state.functions.end()) {
      const common::FunctionId next{index(state.output.functions.size())};
      state.output.functions.push_back({.id = next, .name = canonical.name, .is_entry = function.is_entry});
      found = state.functions.emplace(canonical.id.value, next).first;
    }
    const auto canonical_id = found->second;
    if (state.output.functions[canonical_id.value()].is_entry != function.is_entry) reject(function.symbol_id.value);
    const auto [written, added] = state.functions.emplace(function.symbol_id.value, canonical_id);
    if (!added && written->second != canonical_id) reject(function.symbol_id.value);
    const owned::FunctionDeclarationId declaration{index(state.output.function_declarations.size())};
    if (!state.declaration_scopes.emplace(function.declaration_scope.value, declaration).second) reject(function.symbol_id.value);
    owned::FunctionDeclaration record{.id = declaration, .function = canonical_id,
        .name = function.name, .is_entry = function.is_entry, .is_prototype = function.is_prototype,
        .contract = contract(function.contract)};
    record.source_profile = function.source_region;
    record.source_target = function.source_target;
    if (function.source_version) record.source_version = owned::PtxVersion{function.source_version->major, function.source_version->minor};
    state.output.function_declarations.push_back(std::move(record));
    state.output.functions[canonical_id.value()].declarations.push_back(declaration);
    if (!function.is_prototype) {
      auto& callable = state.output.functions[canonical_id.value()];
      if (callable.definition) reject(function.symbol_id.value);
      callable.definition = declaration;
      callable.body = owned::FunctionLayout{.id = canonical_id, .instruction_count = index(function.body.size())};
    }
    state.declarations.push_back({.function = canonical_id, .declaration = declaration});
  }
  for (const auto& alias : state.source.function_aliases) {
    const auto canonical = identity(state.functions, alias.canonical_function.value);
    const auto& declaration = symbol(state, alias.symbol_id.value);
    if (declaration.kind != ptx_frontend::binding::SymbolKind::Function || !declaration.canonical_function ||
        *declaration.canonical_function != alias.canonical_function) reject(alias.symbol_id.value);
    const auto [found, added] = state.functions.emplace(alias.symbol_id.value, canonical);
    if (!added && found->second != canonical) reject(alias.symbol_id.value);
    state.output.aliases.push_back({alias.name, canonical});
  }
  for (const auto& region : state.source.header.regions) {
    owned::SourceProfile profile;
    if (region.version) profile.version = owned::PtxVersion{region.version->major, region.version->minor};
    profile.target_options = region.target_options;
    profile.texture_mode = named<owned::TextureMode>(region.texture_mode);
    profile.map_f64_to_f32 = region.map_f64_to_f32;
    profile.address_size_bits = region.address_size_bits;
    state.output.source_profiles.push_back(std::move(profile));
  }
}

/** @brief Convert every typed opaque static enum with its exact external codepoint. */
[[nodiscard]] auto opaque_enum(source::OpaqueStaticEnum value) -> owned::OpaqueStaticEnum {
  switch (value) {
#define PTXSIM_OPAQUE_VALUE(Name) case source::OpaqueStaticEnum::Name: return owned::OpaqueStaticEnum::Name;
    PTXSIM_OPAQUE_VALUE(Nearest)
    PTXSIM_OPAQUE_VALUE(Linear)
    PTXSIM_OPAQUE_VALUE(Wrap)
    PTXSIM_OPAQUE_VALUE(Mirror)
    PTXSIM_OPAQUE_VALUE(ClampOgl)
    PTXSIM_OPAQUE_VALUE(ClampToEdge)
    PTXSIM_OPAQUE_VALUE(ClampToBorder)
    PTXSIM_OPAQUE_VALUE(ClSnormInt8)
    PTXSIM_OPAQUE_VALUE(ClSnormInt16)
    PTXSIM_OPAQUE_VALUE(ClUnormInt8)
    PTXSIM_OPAQUE_VALUE(ClUnormInt16)
    PTXSIM_OPAQUE_VALUE(ClUnormShort565)
    PTXSIM_OPAQUE_VALUE(ClUnormShort555)
    PTXSIM_OPAQUE_VALUE(ClUnormInt101010)
    PTXSIM_OPAQUE_VALUE(ClSignedInt8)
    PTXSIM_OPAQUE_VALUE(ClSignedInt16)
    PTXSIM_OPAQUE_VALUE(ClSignedInt32)
    PTXSIM_OPAQUE_VALUE(ClUnsignedInt8)
    PTXSIM_OPAQUE_VALUE(ClUnsignedInt16)
    PTXSIM_OPAQUE_VALUE(ClUnsignedInt32)
    PTXSIM_OPAQUE_VALUE(ClHalfFloat)
    PTXSIM_OPAQUE_VALUE(ClFloat)
    PTXSIM_OPAQUE_VALUE(ClR)
    PTXSIM_OPAQUE_VALUE(ClA)
    PTXSIM_OPAQUE_VALUE(ClRg)
    PTXSIM_OPAQUE_VALUE(ClRa)
    PTXSIM_OPAQUE_VALUE(ClRgb)
    PTXSIM_OPAQUE_VALUE(ClRgba)
    PTXSIM_OPAQUE_VALUE(ClBgra)
    PTXSIM_OPAQUE_VALUE(ClArgb)
    PTXSIM_OPAQUE_VALUE(ClIntensity)
    PTXSIM_OPAQUE_VALUE(ClLuminance)
#undef PTXSIM_OPAQUE_VALUE
  }
  reject();
}
/** @brief Copy all static opaque objects without flattening arrays or inventing defaults. */
[[nodiscard]] auto opaque_objects(const std::vector<source::OpaqueStaticObject>& values)
    -> std::vector<owned::OpaqueStaticObject> {
  std::vector<owned::OpaqueStaticObject> result;
  for (const auto& value : values) {
    owned::OpaqueStaticObject object{value.indices, {}};
    for (const auto& member : value.members) {
      owned::OpaqueStaticMember field;
      field.field = named<owned::OpaqueStaticField>(member.field);
      if (const auto* number = std::get_if<std::uint64_t>(&member.value)) field.value = *number;
      else field.value = opaque_enum(std::get<source::OpaqueStaticEnum>(member.value));
      object.members.push_back(std::move(field));
    }
    result.push_back(std::move(object));
  }
  return result;
}
/** @brief Retain an unevaluated relocation with a fully owned typed target. */
[[nodiscard]] auto relocation(const source::StorageRelocation& value, const ModuleBindings& state)
    -> owned::StorageRelocation {
  owned::StorageRelocation result;
  const auto& target = symbol(state, value.symbol_id.value);
  if (value.parameterized_index ? !target.parameterized_count || *value.parameterized_index >= *target.parameterized_count : target.parameterized_count.has_value())
    reject(value.symbol_id.value);
  if (target.kind == ptx_frontend::binding::SymbolKind::Function) {
    result.target = identity(state.functions, value.symbol_id.value);
  } else if (const auto found = state.resources.find(value.symbol_id.value); found != state.resources.end()) {
    result.target = found->second;
  } else {
    result.target = identity(state.storage, value.symbol_id.value);
  }
  result.member = value.parameterized_index;
  result.address_kind = named<owned::StorageAddressKind>(value.address_kind);
  result.addend_bits = value.addend_bits;
  result.byte_mask = value.byte_mask;
  return result;
}
/** @brief Copy all allocation-independent declaration facts for one real source object. */
[[nodiscard]] auto storage_record(const source::ResolvedStorageDeclaration& value,
                                   const ModuleBindings& state) -> owned::StorageDeclaration {
  const auto& declaration = symbol(state, value.symbol_id.value);
  if (declaration.kind != ptx_frontend::binding::SymbolKind::Variable || declaration.scope != value.scope_id ||
      declaration.parameterized_count != value.parameterized_count || declaration.vector_width.value_or(1) != value.vector_width ||
      declaration.address_alignment != value.alignment || declaration.linkage != value.linkage)
    reject(value.symbol_id.value);
  owned::StorageDeclaration result;
  result.id = identity(state.storage, value.symbol_id.value);
  result.name = declaration.name;
  const auto owner = scope_owner(state, value.scope_id);
  if (owner) result.owner_function = state.output.function_declarations[owner->value()].function;
  if (value.owner_function ? result.owner_function != identity(state.functions, value.owner_function->value) : result.owner_function.has_value())
    reject(value.symbol_id.value);
  result.space = storage_space(value.space);
  if (const auto* type = std::get_if<ptx_frontend::base::ScalarType>(&value.element_type)) result.element_type = scalar(*type);
  else result.element_type = named<owned::OpaqueResourceKind>(std::get<ptx_frontend::base::OpaqueResourceKind>(value.element_type));
  result.legacy_texture = value.legacy_texture;
  result.vector_width = value.vector_width;
  result.array_extents = value.array_extents;
  result.byte_extent = value.byte_extent;
  result.alignment = value.alignment;
  result.explicit_alignment = value.explicit_alignment;
  result.linkage = named<owned::Linkage>(value.linkage);
  result.declaration_kind = named<owned::StorageDeclarationKind>(value.declaration_kind);
  result.dynamic_shared = value.is_dynamic_shared;
  result.parameterized_count = value.parameterized_count;
  result.managed = value.is_managed;
  result.unified_id = uuid(value.unified_id);
  result.initialization = initialization(value.initialization);
  result.opaque_static_objects = opaque_objects(value.opaque_static_objects);
  /** @brief Seen scalar-cell byte positions; preserve source order without overlapping cells. */
  std::set<std::uint64_t> initializer_offsets;
  const auto* initializer_type=std::get_if<ptx_frontend::base::ScalarType>(&value.element_type);
  /** @brief Width of one scalar initializer cell in bytes, independent of vector lane count. */
  const auto initializer_width=initializer_type?ptx_frontend::base::scalar_size_of(*initializer_type):0;
  if (!value.initializer.empty() && value.initialization != source::StorageInitializationKind::Explicit)
    reject(value.symbol_id.value);
  for (const auto& element : value.initializer) {
    if (initializer_width == 0 || !value.byte_extent ||
        element.byte_offset % initializer_width != 0 ||
        element.byte_offset > *value.byte_extent ||
        initializer_width > *value.byte_extent - element.byte_offset ||
        !initializer_offsets.emplace(element.byte_offset).second)
      reject(value.symbol_id.value);
    owned::StorageInitializerElement target;
    target.byte_offset = element.byte_offset;
    if (const auto* constant = std::get_if<source::StorageConstant>(&element.value)) {
      if (initializer_width <= sizeof(std::uint64_t) &&
          (constant->high_bits != 0 ||
           (initializer_width < sizeof(std::uint64_t) &&
            (constant->bits >> (initializer_width * 8)) != 0)))
        reject(value.symbol_id.value);
      target.value = owned::Constant128{constant->bits, constant->high_bits};
    } else
      target.value = relocation(std::get<source::StorageRelocation>(element.value), state);
    result.initializer.push_back(std::move(target));
  }
  return result;
}
/** @brief Register every storage/resource identity before resolving any initializer target. */
void storage(ModuleBindings& state) {
  for (const auto& value : state.source.storage_declarations) {
    if (state.storage.contains(value.symbol_id.value)) continue;
    const owned::StorageId storage_id{index(state.output.storage_declarations.size())};
    state.storage.emplace(value.symbol_id.value, storage_id);
    state.output.storage_declarations.emplace_back();
    if (const auto* kind = std::get_if<ptx_frontend::base::OpaqueResourceKind>(&value.element_type)) {
      const owned::ResourceId resource_id{index(state.output.resources.size())};
      if (!state.resources.emplace(value.symbol_id.value, resource_id).second) reject(value.symbol_id.value);
      state.output.resources.push_back({.id = resource_id, .name = symbol(state, value.symbol_id.value).name,
          .kind = named<owned::OpaqueResourceKind>(*kind), .storage = storage_id,
          .parameterized_count = value.parameterized_count, .explicit_alignment = value.explicit_alignment,
          .array_extents = value.array_extents});
    }
  }
  std::set<std::uint32_t> copied;
  for (const auto& value : state.source.storage_declarations) {
    auto record = storage_record(value, state);
    const auto target = record.id.value();
    if (copied.insert(target).second) state.output.storage_declarations[target] = std::move(record);
    else {
      auto& canonical = state.output.storage_declarations[target];
      if (canonical.declaration_kind != owned::StorageDeclarationKind::External ||
          record.declaration_kind != owned::StorageDeclarationKind::External)
        reject(value.symbol_id.value);
      if (std::holds_alternative<owned::DataType>(canonical.element_type) &&
          std::holds_alternative<owned::DataType>(record.element_type)) {
        if (canonical.alignment != record.alignment || !record.alignment ||
            (canonical.explicit_alignment && canonical.explicit_alignment != canonical.alignment) ||
            (record.explicit_alignment && record.explicit_alignment != record.alignment))
          reject(value.symbol_id.value);
        auto left = canonical;
        auto right = record;
        left.explicit_alignment.reset();
        right.explicit_alignment.reset();
        if (left != right) reject(value.symbol_id.value);
        if (record.explicit_alignment) canonical.explicit_alignment = record.explicit_alignment;
      } else if (canonical != record) {
        reject(value.symbol_id.value);
      }
    }
  }
  for (auto& resource : state.output.resources) {
    if (!resource.storage) continue;
    const auto& canonical = state.output.storage_declarations[resource.storage->value()];
    resource.explicit_alignment = canonical.explicit_alignment;
    resource.parameterized_count = canonical.parameterized_count;
    resource.array_extents = canonical.array_extents;
  }
}
/** @brief Retain full scalar parameters and entry resources for every concrete declaration. */
void parameters(ModuleBindings& state) {
  for (std::size_t position = 0; position < state.source.functions.size(); ++position) {
    const auto& source_function = state.source.functions[position];
    const auto& bindings = state.declarations[position];
    auto& declaration = state.output.function_declarations[bindings.declaration.value()];
    for (const auto& value : source_function.parameter_declarations) {
      const auto& source_symbol = symbol(state, value.symbol_id.value);
      check_scope(state, value.symbol_id.value, value.scope_id, bindings.declaration);
      const owned::ParameterId id{index(state.output.parameters.size())};
      if (!state.parameters.emplace(value.symbol_id.value, id).second) reject(value.symbol_id.value);
      owned::ParameterDeclaration record{.id = id, .owner_declaration = bindings.declaration,
          .owner_function = bindings.function, .role = named<owned::ParameterDeclarationRole>(value.role),
          .scalar_type = scalar(value.scalar_type), .alignment = value.alignment,
          .explicit_alignment = value.explicit_alignment, .vector_width = value.vector_width,
          .array_extents = value.array_extents, .byte_extent = value.byte_extent,
          .parameterized_count = source_symbol.parameterized_count};
      if (!alignment(value.alignment)) reject(value.symbol_id.value);
      if (value.pointer) record.pointer = pointer(*value.pointer);
      state.output.parameters.push_back(std::move(record));
      declaration.parameters.push_back(id);
    }
    for (const auto& value : source_function.opaque_entry_parameters) {
      check_scope(state, value.symbol_id.value, value.scope_id, bindings.declaration);
      if (!source_function.is_entry || value.scope_id != source_function.declaration_scope) reject(value.symbol_id.value);
      const owned::ResourceId id{index(state.output.resources.size())};
      if (!state.resources.emplace(value.symbol_id.value, id).second) reject(value.symbol_id.value);
      state.output.resources.push_back({.id = id, .name = symbol(state, value.symbol_id.value).name,
          .kind = named<owned::OpaqueResourceKind>(value.kind), .role = owned::ResourceDeclarationRole::EntryInput,
          .entry_declaration = bindings.declaration, .entry_owner = bindings.function,
          .explicit_alignment = value.explicit_alignment, .array_extents = value.array_extents});
      declaration.opaque_entry_parameters.push_back(id);
    }
  }
}
/** @brief Map a fundamental scalar declaration to its real physical register width. */
[[nodiscard]] auto raw_width(ptx_frontend::base::ScalarType type) -> common::RawWidth {
  if (type == ptx_frontend::base::ScalarType::Pred) return common::RawWidth::pred;
  switch (ptx_frontend::base::scalar_size_of(type)) {
    case 1: return common::RawWidth::b8;
    case 2: return common::RawWidth::b16;
    case 4: return common::RawWidth::b32;
    case 8: return common::RawWidth::b64;
    case 16: return common::RawWidth::b128;
  }
  reject();
}
/** @brief Build real component slots and labels independently for every declaration scope. */
void frames(ModuleBindings& state) {
  for (std::size_t position = 0; position < state.source.functions.size(); ++position) {
    const auto& function = state.source.functions[position];
    auto& frame = state.declarations[position];
    for (const auto& declaration : state.source.symbols.symbols()) {
      if (declaration.state_space != ptx_frontend::base::DeclarationStateSpace::Register ||
          scope_owner(state, declaration.scope) != frame.declaration) continue;
      if (!declaration.type) reject(declaration.id.value);
      const auto* type = ptx_frontend::base::find_scalar_type_metadata(*declaration.type);
      const auto components = declaration.vector_width.value_or(1);
      const auto count = declaration.parameterized_count.value_or(1);
      if (!type || (components != 1 && components != 2 && components != 4) || count == 0 ||
          (type->type == ptx_frontend::base::ScalarType::Pred && components != 1)) reject(declaration.id.value);
      const auto required = std::uint64_t{count} * components;
      if (required + frame.register_widths.size() > std::uint64_t{std::numeric_limits<std::uint32_t>::max()} + 1)
        reject(declaration.id.value);
      for (std::uint32_t member = 0; member < count; ++member) {
        const auto selected = declaration.parameterized_count ? std::optional{member} : std::nullopt;
        const auto first = common::RegisterSlot{index(frame.register_widths.size())};
        if (!frame.registers.emplace(std::pair{declaration.id.value, selected}, RegisterBinding{first, components, type->type}).second)
          reject(declaration.id.value);
        frame.register_widths.insert(frame.register_widths.end(), components, raw_width(type->type));
      }
    }
    for (const auto& label : function.label_positions) {
      if (symbol(state, label.symbol_id.value).kind != ptx_frontend::binding::SymbolKind::Label ||
          scope_owner(state, symbol(state, label.symbol_id.value).scope) != frame.declaration ||
          label.instruction_offset > function.body.size() ||
          !frame.labels.emplace(label.symbol_id.value, common::ProgramCounter{index(label.instruction_offset)}).second)
        reject(label.symbol_id.value);
    }
    if (!function.is_prototype) state.output.functions[frame.function.value()].body->register_widths = frame.register_widths;
  }
}

/** @brief Bind every ordered branch/call metadata declaration, including unused prototypes. */
void control_tables(ModuleBindings& state) {
  for (std::size_t position = 0; position < state.source.functions.size(); ++position) {
    const auto& function = state.source.functions[position];
    const auto& frame = state.declarations[position];
    for (const auto& source_set : function.branch_target_sets) {
      check_scope(state, source_set.symbol_id.value, source_set.scope_id, frame.declaration);
      if (symbol(state, source_set.symbol_id.value).kind != ptx_frontend::binding::SymbolKind::BranchTargetSet)
        reject(source_set.symbol_id.value);
      const owned::BranchTargetSetId id{index(state.output.branch_target_sets.size())};
      if (!state.branch_target_sets.emplace(source_set.symbol_id.value, id).second) reject(source_set.symbol_id.value);
      owned::BranchTargetSetRecord target{id, frame.function, {}};
      for (const auto& branch : source_set.targets) target.targets.push_back(identity(frame.labels, branch.symbol_id.value));
      state.output.branch_target_sets.push_back(std::move(target));
    }
    for (const auto& source_set : function.call_target_sets) {
      check_scope(state, source_set.symbol_id.value, source_set.scope_id, frame.declaration);
      if (symbol(state, source_set.symbol_id.value).kind != ptx_frontend::binding::SymbolKind::CallTargetSet)
        reject(source_set.symbol_id.value);
      const owned::CallTargetSetId id{index(state.output.call_target_sets.size())};
      if (!state.call_target_sets.emplace(source_set.symbol_id.value, id).second) reject(source_set.symbol_id.value);
      owned::CallTargetSetRecord target{id, frame.declaration, {}, signature(source_set.signature)};
      for (const auto& call : source_set.targets) {
        const auto written = identity(state.functions, call.symbol_id.value);
        const auto canonical = identity(state.functions, call.canonical_function.value);
        if (written != canonical) reject(call.symbol_id.value);
        target.targets.push_back(canonical);
      }
      state.output.call_target_sets.push_back(std::move(target));
    }
    for (const auto& prototype : function.call_prototypes) {
      check_scope(state, prototype.symbol_id.value, prototype.scope_id, frame.declaration);
      if (symbol(state, prototype.symbol_id.value).kind != ptx_frontend::binding::SymbolKind::CallPrototype)
        reject(prototype.symbol_id.value);
      const owned::PrototypeId id{index(state.output.prototypes.size())};
      if (!state.prototypes.emplace(prototype.symbol_id.value, id).second) reject(prototype.symbol_id.value);
      state.output.prototypes.push_back({id, frame.declaration, signature(prototype.signature),
          prototype.is_noreturn, preserve(prototype.abi_preserve), preserve(prototype.abi_preserve_control)});
    }
  }
  for (const auto& table : state.source.call_tables) {
    const auto& declaration = symbol(state, table.symbol_id.value);
    if (declaration.kind != ptx_frontend::binding::SymbolKind::Variable || declaration.scope != table.scope_id)
      reject(table.symbol_id.value);
    const owned::CallTableId id{index(state.output.call_tables.size())};
    if (!state.call_tables.emplace(table.symbol_id.value, id).second) reject(table.symbol_id.value);
    const auto storage_id = identity(state.storage, table.symbol_id.value);
    const auto& actual = state.output.storage_declarations[storage_id.value()];
    owned::CallTableRecord record{.id = id, .storage = storage_id,
        .owner_function = actual.owner_function, .space = storage_space(table.space),
        .element_type = scalar(table.element_type), .extent = table.extent,
        .byte_extent = table.byte_extent, .signature = signature(table.signature)};
    if (record.space != actual.space || actual.byte_extent != table.byte_extent ||
        (table.owner_function ? actual.owner_function != identity(state.functions, table.owner_function->value) : actual.owner_function.has_value()) ||
        !std::holds_alternative<owned::DataType>(actual.element_type) || std::get<owned::DataType>(actual.element_type) != record.element_type)
      reject(table.symbol_id.value);
    for (const auto& slot : table.slots) {
      const auto written = identity(state.functions, slot.symbol_id.value);
      const auto canonical = identity(state.functions, slot.canonical_function.value);
      if (written != canonical) reject(slot.symbol_id.value);
      record.slots.push_back({slot.index, slot.byte_offset, canonical});
    }
    state.output.call_tables.push_back(std::move(record));
  }
}
/** @brief Derive a packed entry-byte view only when all full declarations admit one. */
void entry_layouts(ModuleBindings& state) {
  for (std::size_t position = 0; position < state.source.functions.size(); ++position) {
    const auto& source_function = state.source.functions[position];
    if (source_function.is_prototype) continue;
    const auto& frame = state.declarations[position];
    auto& layout = *state.output.functions[frame.function.value()].body;
    const auto& declaration = state.output.function_declarations[frame.declaration.value()];
    layout.entry_packing_available = source_function.is_entry && declaration.opaque_entry_parameters.empty();
    if (!layout.entry_packing_available) continue;
    std::size_t end = 0;
    for (const auto id : declaration.parameters) {
      const auto& parameter = state.output.parameters[id.value()];
      if (parameter.role != owned::ParameterDeclarationRole::EntryInput &&
          parameter.role != owned::ParameterDeclarationRole::LegacyEntryInput) continue;
      if (!parameter.byte_extent || *parameter.byte_extent == 0 || parameter.parameterized_count ||
          *parameter.byte_extent > std::numeric_limits<std::size_t>::max() ||
          parameter.alignment > std::numeric_limits<std::size_t>::max()) {
        layout.entry_packing_available = false;
        break;
      }
      const auto alignment = static_cast<std::size_t>(parameter.alignment);
      const auto size = static_cast<std::size_t>(*parameter.byte_extent);
      const auto remainder = end % alignment;
      const auto padding = remainder == 0 ? 0 : alignment - remainder;
      if (padding > std::numeric_limits<std::size_t>::max() - end ||
          size > std::numeric_limits<std::size_t>::max() - end - padding) {
        layout.entry_packing_available = false;
        break;
      }
      layout.entry_parameters.push_back({end + padding, size, alignment, id});
      end += padding + size;
    }
    if (layout.entry_packing_available) layout.entry_parameter_size = end;
    else {
      layout.entry_parameters.clear();
      layout.entry_parameter_size = 0;
    }
  }
}

}  // namespace

auto bind_module_metadata(ModuleBindings& state) -> std::expected<void, LoweringError> {
  try {
    functions(state);
    storage(state);
    parameters(state);
    frames(state);
    control_tables(state);
    entry_layouts(state);
    return {};
  } catch (const MetadataFailure& failure) {
    return std::unexpected(failure.error);
  }
}
}  // namespace ptxsim::exec_ir_lowering::detail
