#include "lowering_detail.hpp"

#include <bit>
#include <concepts>
#include <limits>
#include <string_view>
#include <type_traits>
#include <variant>

#include <magic_enum/magic_enum.hpp>
#include <ptx_frontend/base/ptx_special_register.hpp>
#include <ptxsim/exec_ir/exec_ir_types.hpp>

namespace ptxsim::exec_ir_lowering::detail {
namespace {
namespace source = ptx_frontend::resolved_ir;
namespace owned = exec_ir;

/** @brief Internal structured unwind, caught before a typed leaf binder returns. */
struct BindingFailure {
  /** @brief Exact cold-boundary location and malformed identity context. */
  LoweringError error;
};
/** @brief Stop an incomplete identity join without inventing a native identity. */
[[noreturn]] void fail(const BindingContext& context,
                       std::optional<std::uint32_t> symbol = std::nullopt) {
  throw BindingFailure{malformed(context, symbol).error()};
}
/** @brief Native dense identity domain recognized by its explicit numeric query. */
template <typename T>
concept OwnedIdentity = requires(T value) {
  { value.value() } -> std::same_as<std::uint32_t>;
};
/** @brief Resolve an existing typed identity without a zero/default fallback. */
template <OwnedIdentity Id>
[[nodiscard]] auto lookup(const std::map<std::uint32_t, Id>& table,
                          std::uint32_t symbol, const BindingContext& context) -> Id {
  const auto found = table.find(symbol);
  if (found == table.end()) fail(context, symbol);
  return found->second;
}
/** @brief Read a checked symbol record synchronously during binding only. */
[[nodiscard]] auto symbol(std::uint32_t id, const BindingContext& context)
    -> const ptx_frontend::binding::Symbol& {
  const auto& symbols = context.module.source.symbols.symbols();
  if (id >= symbols.size() || symbols[id].id.value != id) fail(context, id);
  return symbols[id];
}
/** @brief Require a real bound identity rather than guessing from source spelling. */
[[nodiscard]] auto id(const std::optional<ptx_frontend::binding::SymbolId>& value,
                      const BindingContext& context) -> std::uint32_t {
  if (!value) fail(context);
  return value->value;
}
/** @brief Convert a complete scalar domain and reject Invalid or forged values. */
[[nodiscard]] auto scalar(ptx_frontend::base::ScalarType value,
                          const BindingContext& context) -> owned::DataType {
  const auto result = convert_ScalarType(value);
  if (!result) fail(context);
  return *result;
}
/** @brief Check that a nested enum pair has exactly the same semantic vocabulary. */
template <typename Out, typename In>
  requires(std::is_enum_v<Out> && std::is_enum_v<In>)
[[nodiscard]] consteval auto same_vocabulary() -> bool {
  if (magic_enum::enum_count<Out>() != magic_enum::enum_count<In>()) return false;
  for (const auto value : magic_enum::enum_values<In>())
    if (!magic_enum::enum_cast<Out>(magic_enum::enum_name(value))) return false;
  return true;
}
/** @brief Only closed mirrored structural enum pairs may use name correspondence. */
template <typename Out, typename In>
concept MirroredSemanticEnums = std::is_enum_v<Out> && std::is_enum_v<In> && same_vocabulary<Out, In>();
/** @brief Convert a checked nested semantic enum; names are cold boundary data. */
template <typename Out, typename In>
  requires MirroredSemanticEnums<Out, In>
[[nodiscard]] auto semantic_enum(In value, const BindingContext& context) -> Out {
  const auto converted = magic_enum::enum_cast<Out>(magic_enum::enum_name(value));
  if (!converted) fail(context);
  return *converted;
}

/** @brief Bind the actual declaration and selected real component slot. */
[[nodiscard]] auto own(const source::ResolvedRegisterRef& value,
                       const BindingContext& context) -> owned::RegisterRef {
  const auto symbol_id = id(value.symbol_id, context);
  const auto found = context.function.registers.find({symbol_id, value.parameterized_index});
  if (found == context.function.registers.end() || !value.declared_type) fail(context, symbol_id);
  const auto& binding = found->second;
  if (*value.declared_type != binding.type) fail(context, symbol_id);
  owned::RegisterRef result{binding.first,
      semantic_enum<owned::RegisterClass>(value.register_class, context),
      scalar(*value.declared_type, context), value.vector_width, std::nullopt};
  if (value.component) {
    if (value.vector_width || value.component->declaration_width != binding.components ||
        value.component->lane >= binding.components || binding.components == 1)
      fail(context, symbol_id);
    result.component = owned::RegisterComponent{value.component->lane, value.component->declaration_width};
    result.slot = common::RegisterSlot{binding.first.value() + value.component->lane};
  } else if (value.vector_width != (binding.components == 1 ? std::nullopt : std::optional{binding.components})) {
    fail(context, symbol_id);
  }
  if (!owned::valid_operand(result)) fail(context, symbol_id);
  return result;
}
/** @brief Copy converted use bits and the original signed integer provenance. */
[[nodiscard]] auto own(const source::ResolvedImmediate& value,
                       const BindingContext& context) -> owned::Immediate {
  const owned::Immediate result{value.bits,scalar(value.type,context),value.is_negative,value.integer_source_bits};
  if (!owned::valid_operand(result)) fail(context);
  return result;
}
/** @brief Bind predicate storage before retaining logical inversion. */
[[nodiscard]] auto own(const source::ResolvedPredicate& value,
                       const BindingContext& context) -> owned::Predicate {
  return {own(value.register_ref, context), value.negated};
}
/** @brief Bind the full stable intrinsic identity, type and selected component. */
[[nodiscard]] auto own(const source::ResolvedSpecialRegisterRef& value,
                       const BindingContext& context) -> owned::SpecialRegisterRef {
  const auto kind = semantic_enum<owned::SpecialRegisterKind>(value.id.kind, context);
  switch (kind) {
    case owned::SpecialRegisterKind::Invalid: fail(context);
    case owned::SpecialRegisterKind::PerformanceMonitor:
    case owned::SpecialRegisterKind::PerformanceMonitor64:
      if (value.id.index >= 8) fail(context);
      break;
    case owned::SpecialRegisterKind::Environment:
      if (value.id.index >= 32) fail(context);
      break;
    case owned::SpecialRegisterKind::ReservedSmemOffset:
      if (value.id.index >= 2) fail(context);
      break;
    default:
      if (value.id.index != 0) fail(context);
      break;
  }
  const auto info = ptx_frontend::base::metadata(value.id);
  owned::SpecialRegisterRef result{{kind, value.id.index}, std::nullopt,
                                   scalar(info.element_type, context), info.vector_width};
  if (value.component) {
    switch (*value.component) {
      case ptx_frontend::base::VectorComponent::X: result.component = 0; break;
      case ptx_frontend::base::VectorComponent::Y: result.component = 1; break;
      case ptx_frontend::base::VectorComponent::Z: result.component = 2; break;
      default: fail(context);
    }
  }
  if (!owned::valid_operand(result)) fail(context);
  return result;
}
/** @brief Preserve an intrinsic predicate's independent complement. */
[[nodiscard]] auto own(const source::ResolvedPredicateSpecialRegister& value,
                       const BindingContext& context) -> owned::PredicateSpecialRegister {
  return {own(value.register_ref, context), value.negated};
}
/** @brief Copy the genuine literal predicate alternative. */
[[nodiscard]] auto own(const source::ResolvedPredicateConstant& value,
                       const BindingContext&) -> owned::PredicateConstant { return {value.value}; }

// Declarations keep every recursive sum alternative visible before sum binding.
/** @brief Bind a complete addressable storage or parameter symbol. */
[[nodiscard]] auto own(const source::ResolvedSymbolRef&, const BindingContext&) -> owned::SymbolRef;
/** @brief Bind every retained address/index field. */
[[nodiscard]] auto own(const source::ResolvedAddress&, const BindingContext&) -> owned::Address;
/** @brief Bind a real resource declaration and its module/entry ownership. */
[[nodiscard]] auto own(const source::ResolvedOpaqueSymbolRef&, const BindingContext&) -> owned::OpaqueSymbolRef;
/** @brief Bind a canonical callable, including external/prototype declarations. */
[[nodiscard]] auto own(const source::ResolvedFunctionRef&, const BindingContext&) -> owned::FunctionRef;
/** @brief Bind a prototype or callable target-set metadata identity. */
[[nodiscard]] auto own(const source::ResolvedIndirectMetadataRef&, const BindingContext&) -> owned::IndirectMetadataRef;
/** @brief Bind an actually consumed sparse callable storage table. */
[[nodiscard]] auto own(const source::ResolvedCallTableRef&, const BindingContext&) -> owned::CallTableRef;
/** @brief Bind a real call lvalue's parameter or register storage. */
[[nodiscard]] auto own(const source::ResolvedCallParameterRef&, const BindingContext&) -> owned::CallParameterRef;
/** @brief Copy independent call-literal source and use representations. */
[[nodiscard]] auto own(const source::ResolvedCallLiteral&, const BindingContext&) -> owned::CallLiteral;
/** @brief Bind every typed MOV lane without named-source provenance. */
[[nodiscard]] auto own(const source::ResolvedMovValueVector&, const BindingContext&) -> owned::MovValueVector;
/** @brief Bind an actual whole intrinsic vector. */
[[nodiscard]] auto own(const source::ResolvedVectorSpecialRegisterRef&, const BindingContext&) -> owned::VectorSpecialRegisterRef;
/** @brief Bind the cache-policy carrier alternative. */
[[nodiscard]] auto own(const source::ResolvedCpAsyncCachePolicy&, const BindingContext&) -> owned::CpAsyncCachePolicy;
/** @brief Bind an actual resource alternative and its expected use role. */
[[nodiscard]] auto own(const source::ResolvedOpaqueResourceRef&, const BindingContext&) -> owned::OpaqueResourceRef;
/** @brief Bind a complete independent address offset. */
[[nodiscard]] auto own(const source::ResolvedAddressOffset&, const BindingContext&) -> owned::AddressOffset;
/** @brief Bind a named-array index without collapsing it into an address addend. */
[[nodiscard]] auto own(const source::ResolvedNamedArrayIndex&, const BindingContext&) -> owned::NamedArrayIndex;
/** @brief Preserve a byte video selector. */
[[nodiscard]] auto own(const source::VideoByteSelector&, const BindingContext&) -> owned::VideoByteSelector;
/** @brief Preserve a halfword video selector. */
[[nodiscard]] auto own(const source::VideoHalfSelector&, const BindingContext&) -> owned::VideoHalfSelector;
/** @brief Preserve a two-source halfword swizzle. */
[[nodiscard]] auto own(const source::VideoHalfSwizzle&, const BindingContext&) -> owned::VideoHalfSwizzle;
/** @brief Preserve a two-source byte swizzle. */
[[nodiscard]] auto own(const source::VideoByteSwizzle&, const BindingContext&) -> owned::VideoByteSwizzle;
/** @brief Preserve active halfword mask entries and their count. */
[[nodiscard]] auto own(const source::VideoHalfMask&, const BindingContext&) -> owned::VideoHalfMask;
/** @brief Preserve active byte mask entries and their count. */
[[nodiscard]] auto own(const source::VideoByteMask&, const BindingContext&) -> owned::VideoByteMask;

/** @brief A source alternative with an explicit typed native binding operation. */
template <typename T>
concept BoundAlternative = requires(const T& value, const BindingContext& context) { own(value, context); };
/** @brief Map every alternative to a native sum with exactly the same ordering. */
template <BoundAlternative... T>
[[nodiscard]] auto own(const std::variant<T...>& value, const BindingContext& context)
    -> std::variant<decltype(own(std::declval<const T&>(), context))...> {
  using Result = std::variant<decltype(own(std::declval<const T&>(), context))...>;
  if (value.valueless_by_exception()) fail(context);
  return std::visit([&](const auto& alternative) -> Result { return own(alternative, context); }, value);
}
/** @brief Strip diagnostic wrappers only after binding their real value. */
template <BoundAlternative T>
[[nodiscard]] auto own(const ptx_frontend::WithLocs<T>& value, const BindingContext& context) {
  return own(value.value, context);
}
/** @brief Strip a single diagnostic location without retaining source lifetime. */
template <BoundAlternative T>
[[nodiscard]] auto own(const ptx_frontend::WithLoc<T>& value, const BindingContext& context) {
  return own(value.value, context);
}
/** @brief Preserve genuine absence and deeply bind every selected optional value. */
template <typename T>
  requires requires(const T& value, const BindingContext& context) { own(value, context); }
[[nodiscard]] auto own_optional(const std::optional<T>& value, const BindingContext& context)
    -> std::optional<decltype(own(std::declval<const T&>(), context))> {
  if (value) return own(*value, context);
  return std::nullopt;
}

/** @brief Bind every ordered vector lane and preserve genuine sink positions. */
[[nodiscard]] auto own(const source::ResolvedRegisterVector& value,
                       const BindingContext& context) -> owned::RegisterVector {
  owned::RegisterVector result;
  for (const auto& lane : value.elements) result.elements.push_back(own_optional(lane, context));
  return result;
}
/** @brief Bind all ordered scalar vector alternatives. */
[[nodiscard]] auto own(const source::ResolvedValueVector& value,
                       const BindingContext& context) -> owned::ValueVector {
  owned::ValueVector result;
  for (const auto& lane : value.elements) result.elements.push_back(own(lane, context));
  return result;
}
/** @brief Bind all ordered MOV register/immediate/intrinsic lane alternatives. */
[[nodiscard]] auto own(const source::ResolvedMovValueVector& value,
                       const BindingContext& context) -> owned::MovValueVector {
  owned::MovValueVector result;
  for (const auto& lane : value.elements) result.elements.push_back(own(lane, context));
  return result;
}
/** @brief Bind one whole ordinary vector without fabricating a scalar projection. */
[[nodiscard]] auto own(const source::ResolvedVectorRegisterRef& value,
                       const BindingContext& context) -> owned::VectorRegisterRef {
  return {own(value.register_ref, context)};
}
/** @brief Bind stable whole intrinsic vector metadata without a source component. */
[[nodiscard]] auto own(const source::ResolvedVectorSpecialRegisterRef& value,
                       const BindingContext& context) -> owned::VectorSpecialRegisterRef {
  source::ResolvedSpecialRegisterRef scalar_ref{value.spelling, value.id, std::nullopt};
  return {own(scalar_ref, context)};
}
/** @brief Preserve the real general-register destination or its genuine sink. */
[[nodiscard]] auto own(const source::ResolvedRegisterOrSink& value,
                       const BindingContext& context) -> owned::RegisterOrSink { return {own_optional(value.register_ref, context)}; }
/** @brief Preserve the real mbarrier token carrier or its admitted sink. */
[[nodiscard]] auto own(const source::ResolvedMbarrierStateToken& value,
                       const BindingContext& context) -> owned::MbarrierStateToken { return {own_optional(value.register_ref, context)}; }
/** @brief Preserve the real predicate destination or its genuine sink. */
[[nodiscard]] auto own(const source::ResolvedPredicateOrSink& value,
                       const BindingContext& context) -> owned::PredicateOrSink { return {own_optional(value.predicate, context)}; }
/** @brief Bind both ordered predicate destinations. */
[[nodiscard]] auto own(const source::ResolvedPredicatePair& value,
                       const BindingContext& context) -> owned::PredicatePair { return {own(value.first, context), own(value.second, context)}; }
/** @brief Preserve both independent predicate destinations and sinks. */
[[nodiscard]] auto own(const source::ResolvedPredicatePairOrSink& value,
                       const BindingContext& context) -> owned::PredicatePairOrSink { return {own_optional(value.first, context), own_optional(value.second, context)}; }
/** @brief Bind independently selected shuffle data and predicate results. */
[[nodiscard]] auto own(const source::ResolvedShflSyncDestination& value,
                       const BindingContext& context) -> owned::ShflDestination { return {own_optional(value.data, context), own_optional(value.predicate, context)}; }

/** @brief Preserve optional structural enum metadata without inventing defaults. */
template <typename Out, typename In>
  requires MirroredSemanticEnums<Out, In>
[[nodiscard]] auto optional_enum(const std::optional<In>& value,
                                 const BindingContext& context) -> std::optional<Out> {
  if (value) return semantic_enum<Out>(*value, context);
  return std::nullopt;
}
/** @brief Verify the selected compact-group member against the real declaration. */
void check_member(const ptx_frontend::binding::Symbol& declaration,
                   std::optional<std::uint32_t> member, const BindingContext& context) {
  if (declaration.parameterized_count ? !member || *member >= *declaration.parameterized_count : member.has_value())
    fail(context, declaration.id.value);
}
/** @brief Bind canonical callable identity and verify the source entry-role cache. */
[[nodiscard]] auto own(const source::ResolvedFunctionRef& value,
                       const BindingContext& context) -> owned::FunctionRef {
  const auto target = lookup(context.module.functions, id(value.symbol_id, context), context);
  const auto& function = context.module.output.functions[target.value()];
  if (value.is_entry != function.is_entry) fail(context, id(value.symbol_id, context));
  return {target, value.is_entry};
}
/** @brief Bind direct local branch labels, retaining local-PC identity. */
[[nodiscard]] auto own(const source::ResolvedBranchTarget& value,
                       const BindingContext& context) -> owned::BranchTarget {
  return {context.function.function, lookup(context.function.labels, id(value.symbol_id, context), context)};
}
/** @brief Bind the full ordered local target-set table identity. */
[[nodiscard]] auto own(const source::ResolvedBranchTargetSet& value,
                       const BindingContext& context) -> owned::BranchTargetSet {
  const auto target = lookup(context.module.branch_target_sets, id(value.symbol_id, context), context);
  if (context.module.output.branch_target_sets[target.value()].owner_function != context.function.function)
    fail(context, id(value.symbol_id, context));
  return {target};
}
/** @brief Preserve indirect metadata's prototype-versus-target-set category. */
[[nodiscard]] auto own(const source::ResolvedIndirectMetadataRef& value,
                       const BindingContext& context) -> owned::IndirectMetadataRef {
  const auto target = id(value.symbol_id, context);
  if (!value.declaration_kind || symbol(target, context).kind != *value.declaration_kind) fail(context, target);
  if (*value.declaration_kind == ptx_frontend::binding::SymbolKind::CallPrototype) {
    const auto native = lookup(context.module.prototypes, target, context);
    if (context.module.output.prototypes[native.value()].owner_declaration != context.function.declaration) fail(context, target);
    return {native};
  }
  if (*value.declaration_kind == ptx_frontend::binding::SymbolKind::CallTargetSet) {
    const auto native = lookup(context.module.call_target_sets, target, context);
    if (context.module.output.call_target_sets[native.value()].owner_declaration != context.function.declaration) fail(context, target);
    return {native};
  }
  fail(context, target);
}
/** @brief Resolve consumed call-table metadata without classifying ordinary storage. */
[[nodiscard]] auto own(const source::ResolvedCallTableRef& value,
                       const BindingContext& context) -> owned::CallTableRef {
  const auto target = lookup(context.module.call_tables, value.symbol_id.value, context);
  const auto owner = context.module.output.call_tables[target.value()].owner_function;
  if (owner && *owner != context.function.function) fail(context, value.symbol_id.value);
  return {target};
}
/** @brief Preserve storage/parameter address identity and every semantic source cache. */
[[nodiscard]] auto own(const source::ResolvedSymbolRef& value,
                       const BindingContext& context) -> owned::SymbolRef {
  const auto target = id(value.symbol_id, context);
  const auto& declaration = symbol(target, context);
  check_member(declaration, value.parameterized_index, context);
  if ((value.declaration_kind && *value.declaration_kind != declaration.kind) ||
      (value.declaration_state_space && value.declaration_state_space != declaration.state_space))
    fail(context, target);
  owned::SymbolRef result;
  if (const auto found = context.module.storage.find(target); found != context.module.storage.end()) {
    result.target = found->second;
    const auto& storage = context.module.output.storage_declarations[found->second.value()];
    if ((storage.owner_function && *storage.owner_function != context.function.function) ||
        (value.declared_type && (!std::holds_alternative<owned::DataType>(storage.element_type) ||
                                scalar(*value.declared_type, context) != std::get<owned::DataType>(storage.element_type))))
      fail(context, target);
  } else {
    const auto parameter = lookup(context.module.parameters, target, context);
    result.target = parameter;
    const auto& actual = context.module.output.parameters[parameter.value()];
    if (actual.owner_declaration != context.function.declaration ||
        (value.declared_type && scalar(*value.declared_type, context) != actual.scalar_type) ||
        (value.parameter_role && semantic_enum<owned::ParameterDeclarationRole>(*value.parameter_role, context) != actual.role))
      fail(context, target);
  }
  result.member = value.parameterized_index;
  result.declaration_space = optional_enum<owned::DeclarationSpace>(value.declaration_state_space, context);
  result.address_space = optional_enum<owned::DeclarationSpace>(value.address_state_space, context);
  if (value.declared_type) result.declared_type = scalar(*value.declared_type, context);
  result.alignment = value.address_alignment;
  result.is_unified = value.declaration_is_unified;
  result.enclosing_function_kind = semantic_enum<owned::EnclosingFunctionKind>(value.enclosing_function_kind, context);
  result.parameter_qualifier = semantic_enum<owned::ParameterAddressQualifier>(value.parameter_qualifier, context);
  result.parameter_role = optional_enum<owned::ParameterDeclarationRole>(value.parameter_role, context);
  return result;
}
/** @brief Bind exact resource kind, member and concrete entry declaration ownership. */
[[nodiscard]] auto own(const source::ResolvedOpaqueSymbolRef& value,
                       const BindingContext& context) -> owned::OpaqueSymbolRef {
  const auto& declaration = symbol(value.symbol_id.value, context);
  const auto resource_id = lookup(context.module.resources, value.symbol_id.value, context);
  const auto& resource = context.module.output.resources[resource_id.value()];
  check_member(declaration, value.parameterized_index, context);
  const auto kind = semantic_enum<owned::OpaqueResourceKind>(value.kind, context);
  if (declaration.scope != value.scope_id || kind != resource.kind ||
      value.entry_input != (resource.role == owned::ResourceDeclarationRole::EntryInput))
    fail(context, value.symbol_id.value);
  if (value.entry_input && (value.scope_id != context.source_function.declaration_scope ||
      resource.entry_declaration != context.function.declaration ||
      resource.entry_owner != context.function.function))
    fail(context, value.symbol_id.value);
  return {resource_id, value.parameterized_index, kind, resource.entry_owner};
}
/** @brief Preserve a typed direct resource or an actual indirect register carrier. */
[[nodiscard]] auto own(const source::ResolvedOpaqueResourceRef& value,
                       const BindingContext& context) -> owned::OpaqueResourceRef {
  return {own(value.value, context), semantic_enum<owned::OpaqueResourceKind>(value.expected_kind, context)};
}
/** @brief Bind actual call lvalues as parameter objects or complete register views. */
[[nodiscard]] auto own(const source::ResolvedCallParameterRef& value,
                       const BindingContext& context) -> owned::CallParameterRef {
  const auto target = id(value.symbol_id, context);
  const auto& declaration = symbol(target, context);
  check_member(declaration, value.parameterized_index, context);
  if (!value.state_space || !value.declared_type || declaration.state_space != value.state_space) fail(context, target);
  if (*value.state_space == ptx_frontend::base::DeclarationStateSpace::Register) {
    source::ResolvedRegisterRef register_ref;
    register_ref.spelling = value.spelling;
    register_ref.symbol_id = value.symbol_id;
    register_ref.parameterized_index = value.parameterized_index;
    register_ref.declared_type = value.declared_type;
    register_ref.vector_width = value.component ? std::nullopt : declaration.vector_width;
    register_ref.component = value.component;
    register_ref.register_class = *value.declared_type == ptx_frontend::base::ScalarType::Pred ?
        source::ResolvedRegisterClass::Predicate : source::ResolvedRegisterClass::General;
    return {own(register_ref, context)};
  }
  const auto parameter = lookup(context.module.parameters, target, context);
  const auto& actual = context.module.output.parameters[parameter.value()];
  if (*value.state_space != ptx_frontend::base::DeclarationStateSpace::Parameter ||
      value.component || actual.owner_declaration != context.function.declaration ||
      actual.scalar_type != scalar(*value.declared_type, context)) fail(context, target);
  return {owned::ParameterRef{parameter, value.parameterized_index}};
}
/** @brief Copy literal source bits and converted use bits without source spelling. */
[[nodiscard]] auto own(const source::ResolvedCallLiteral& value,
                       const BindingContext& context) -> owned::CallLiteral {
  owned::CallLiteral result;
  result.kind = semantic_enum<owned::LiteralCategory>(value.kind, context);
  if (value.source_value) {
    if (const auto* integer = std::get_if<ptx_frontend::declaration_semantics::IntegerConstantValue>(&*value.source_value))
      result.source_value = owned::IntegerConstant{integer->bits, integer->is_unsigned};
    else
      result.source_value = owned::FloatingConstant{std::bit_cast<std::uint64_t>(std::get<ptx_frontend::declaration_semantics::FloatingConstantValue>(*value.source_value).value)};
  }
  result.value = own_optional(value.value, context);
  return result;
}
/** @brief Bind every ordered parameter/register/literal call argument. */
[[nodiscard]] auto own(const source::ResolvedCallArguments& value,
                       const BindingContext& context) -> owned::CallArguments {
  owned::CallArguments result;
  for (const auto& argument : value.values) result.values.push_back(own(argument.value, context));
  return result;
}
/** @brief Preserve signed offset interpretation and typed immediate provenance. */
[[nodiscard]] auto own(const source::ResolvedAddressOffset& value,
                       const BindingContext& context) -> owned::AddressOffset {
  return {semantic_enum<owned::AddressOffsetOperator>(value.operation, context), own(value.value, context)};
}
/** @brief Preserve actual index carrier, source displacement, stride and combined bytes. */
[[nodiscard]] auto own(const source::ResolvedNamedArrayIndex& value,
                       const BindingContext& context) -> owned::NamedArrayIndex {
  owned::NamedArrayIndex result;
  if (const auto* integer = std::get_if<ptx_frontend::declaration_semantics::IntegerConstantValue>(&value.index))
    result.index = owned::IntegerConstant{integer->bits, integer->is_unsigned};
  else
    result.index = own(std::get<ptx_frontend::WithLocs<source::ResolvedRegisterRef>>(value.index).value, context);
  if (value.displacement) result.displacement = owned::IntegerConstant{value.displacement->bits, value.displacement->is_unsigned};
  result.operation = semantic_enum<owned::AddressOffsetOperator>(value.operation, context);
  result.scalar_stride = value.scalar_stride;
  result.byte_displacement = value.byte_displacement;
  return result;
}
/** @brief Preserve the full selected address alternative and parameter/unified semantics. */
[[nodiscard]] auto own(const source::ResolvedAddress& value,
                       const BindingContext& context) -> owned::Address {
  return {own(value.base, context), own_optional(value.offset, context),
          semantic_enum<owned::EnclosingFunctionKind>(value.enclosing_function_kind, context),
          semantic_enum<owned::ParameterAddressQualifier>(value.parameter_qualifier, context),
          value.unified, own_optional(value.named_index, context)};
}

/** @brief Bind cache-policy opaque bits to their actual real register carrier. */
[[nodiscard]] auto own(const source::ResolvedCpAsyncCachePolicy& value,
                       const BindingContext& context) -> owned::CpAsyncCachePolicy { return {own(value.register_ref, context)}; }
/** @brief Require the exact concrete function scope before canonicalizing ownership. */
void check_stack_scope(std::optional<ptx_frontend::binding::ScopeId> scope,
                        source::EnclosingFunctionKind kind, const BindingContext& context) {
  const auto expected = context.source_function.is_entry ? source::EnclosingFunctionKind::Entry : source::EnclosingFunctionKind::Device;
  if (!scope || *scope != context.source_function.declaration_scope || kind != expected) fail(context);
}
/** @brief Bind stack carrier and canonical function only after precise scope checking. */
[[nodiscard]] auto own(const source::ResolvedStackToken& value,
                       const BindingContext& context) -> owned::StackToken {
  check_stack_scope(value.function_scope, value.enclosing_function_kind, context);
  return {own(value.register_ref, context), semantic_enum<owned::EnclosingFunctionKind>(value.enclosing_function_kind, context), context.function.function};
}
/** @brief Bind local allocation result and its exact enclosing function contract. */
[[nodiscard]] auto own(const source::ResolvedLocalAllocationResult& value,
                       const BindingContext& context) -> owned::LocalAllocationResult {
  check_stack_scope(value.function_scope, value.enclosing_function_kind, context);
  return {own(value.register_ref, context), semantic_enum<owned::EnclosingFunctionKind>(value.enclosing_function_kind, context), context.function.function,
          semantic_enum<owned::DeclarationSpace>(value.address_state_space, context)};
}
/** @brief Bind shared descriptor storage without interpreting runtime descriptor bits. */
[[nodiscard]] auto own(const source::ResolvedSharedMatrixDescriptor& value,
                       const BindingContext& context) -> owned::SharedMatrixDescriptor { return {own(value.register_ref, context)}; }
/** @brief Preserve the selected video byte index exactly. */
[[nodiscard]] auto own(const source::VideoByteSelector& value, const BindingContext&) -> owned::VideoByteSelector { return {value.index}; }
/** @brief Preserve the selected video halfword index exactly. */
[[nodiscard]] auto own(const source::VideoHalfSelector& value, const BindingContext&) -> owned::VideoHalfSelector { return {value.index}; }
/** @brief Preserve the cross-source halfword swizzle exactly. */
[[nodiscard]] auto own(const source::VideoHalfSwizzle& value, const BindingContext&) -> owned::VideoHalfSwizzle { return {value.indices}; }
/** @brief Preserve the cross-source byte swizzle exactly. */
[[nodiscard]] auto own(const source::VideoByteSwizzle& value, const BindingContext&) -> owned::VideoByteSwizzle { return {value.indices}; }
/** @brief Preserve selected halfword mask count and fixed backing array. */
[[nodiscard]] auto own(const source::VideoHalfMask& value, const BindingContext&) -> owned::VideoHalfMask { return {value.indices, value.count}; }
/** @brief Preserve selected byte mask count and fixed backing array. */
[[nodiscard]] auto own(const source::VideoByteMask& value, const BindingContext&) -> owned::VideoByteMask { return {value.indices, value.count}; }
/** @brief Bind video value and actual selector while retaining arithmetic negation. */
[[nodiscard]] auto own(const source::ResolvedVideoOperand& value,
                       const BindingContext& context) -> owned::VideoOperand {
  owned::VideoOperand result{own(value.value.value, context), std::nullopt, value.negated};
  if (value.selector) result.selector = own(value.selector->value, context);
  return result;
}
/** @brief Preserve the actual Tensor Memory address carrier and selected bracket form. */
[[nodiscard]] auto own(const source::TensorMemoryAddress& value,
                       const BindingContext& context) -> owned::TensorMemoryAddress { return {own(value.value, context), value.bracketed}; }
/** @brief Retain original split-offset bits with signed/unsigned source interpretation. */
[[nodiscard]] auto own(const source::TcgenHalfSplitOffset& value,
                       const BindingContext& context) -> owned::TcgenHalfSplitOffset {
  return {value.source_bits, semantic_enum<owned::TcgenIntegerSourceKind>(value.source_kind, context)};
}
/** @brief Bind both independent matrix scale selectors in logical role order. */
[[nodiscard]] auto own(const source::ResolvedMatrixScaleSelector& value,
                       const BindingContext& context) -> owned::MatrixScaleSelector { return {own(value.byte_id, context), own(value.thread_id, context)}; }
/** @brief Preserve every ordered logical tensor coordinate. */
[[nodiscard]] auto own(const source::ResolvedTensorCoordinate& value,
                       const BindingContext& context) -> owned::TensorCoordinate {
  owned::TensorCoordinate result;
  for (const auto& element : value.elements) result.elements.push_back(own(element, context));
  return result;
}
/** @brief Preserve every typed im2col auxiliary value in exact logical order. */
[[nodiscard]] auto own(const source::ResolvedTensorIm2colInfo& value,
                       const BindingContext& context) -> owned::TensorIm2colInfo {
  owned::TensorIm2colInfo result;
  for (const auto& element : value.elements) result.elements.push_back(own(element, context));
  return result;
}
/** @brief Bind complete tensor-map address and exact rank/access topology. */
[[nodiscard]] auto own(const source::ResolvedTensorOperand& value,
                       const BindingContext& context) -> owned::TensorOperand {
  return {{own(value.tensor_map.address, context)}, own(value.coordinates, context),
          semantic_enum<owned::TensorRank>(value.rank, context), semantic_enum<owned::TensorAccessMode>(value.mode, context)};
}
/** @brief Bind fabric endpoint/data/counter carriers without inventing protocol state. */
[[nodiscard]] auto own(const source::ResolvedFabricHandle& value,
                       const BindingContext& context) -> owned::FabricHandle {
  return {own(value.endpoint.value, context), own(value.data_offset.value, context), own_optional(value.counter_offset, context)};
}
/** @brief Bind complete surface resource and every coordinate's execution role. */
[[nodiscard]] auto own(const source::ResolvedSurfaceAccess& value,
                       const BindingContext& context) -> owned::SurfaceAccess {
  owned::SurfaceAccess result{own(value.surface, context), {}, value.coordinates_packed, value.bracketed};
  for (const auto& coordinate : value.coordinates)
    result.coordinates.push_back({own(coordinate.value, context), semantic_enum<owned::SurfaceLaneRole>(coordinate.role, context)});
  return result;
}
/** @brief Bind actual direct/indirect surface query selection. */
[[nodiscard]] auto own(const source::ResolvedSurfaceQueryResource& value,
                       const BindingContext& context) -> owned::SurfaceQueryResource { return {own(value.resource, context), value.bracketed}; }
/** @brief Bind texture/sampler alternatives, all coordinate roles and selected type. */
[[nodiscard]] auto own(const source::ResolvedTextureAccess& value,
                       const BindingContext& context) -> owned::TextureAccess {
  owned::TextureAccess result{own(value.texture, context), own_optional(value.sampler, context), {},
                              scalar(value.coordinate_type, context), value.bracketed, value.coordinates_packed};
  for (const auto& coordinate : value.coordinates)
    result.coordinates.push_back({own(coordinate.value, context), semantic_enum<owned::TextureLaneRole>(coordinate.role, context)});
  return result;
}
/** @brief Preserve direct/indirect texture query resource and bracket topology. */
[[nodiscard]] auto own(const source::ResolvedTextureQueryResource& value,
                       const BindingContext& context) -> owned::TextureQueryResource { return {own(value.resource, context), value.bracketed}; }
/** @brief Bind all texture data destinations and the independent residency predicate. */
[[nodiscard]] auto own(const source::ResolvedTextureResult& value,
                       const BindingContext& context) -> owned::TextureResult {
  return {own(value.data, context), scalar(value.result_type, context), own_optional(value.residency, context)};
}

/** @brief Bound root payloads have a real structural validator and no frontend borrow. */
template <typename T>
concept CheckedOwnedOperand = requires(const T& value) {
  { owned::valid_operand(value) } -> std::same_as<bool>;
};
/** @brief Translate a typed leaf failure into its stable public diagnostic. */
template <CheckedOwnedOperand Out, typename F>
  requires std::invocable<F> && std::same_as<std::invoke_result_t<F>, Out>
[[nodiscard]] auto capture(const BindingContext& context, F&& operation) -> std::expected<Out, LoweringError> {
  try {
    auto result = std::forward<F>(operation)();
    if (!owned::valid_operand(result)) fail(context);
    return result;
  } catch (const BindingFailure& failure) {
    return std::unexpected(failure.error);
  }
}
}  // namespace

/** @brief Define an exact root binder backed by complete typed recursive ownership. */
#define PTXSIM_BIND_LEAF(Function, Source, Target) \
  auto Function(const ptx_frontend::resolved_ir::Source& value, const BindingContext& context) \
      -> std::expected<exec_ir::Target, LoweringError> { \
    return capture<exec_ir::Target>(context, [&]() -> exec_ir::Target { return own(value, context); }); \
  }

PTXSIM_BIND_LEAF(bind_register, ResolvedRegisterRef, RegisterRef)
PTXSIM_BIND_LEAF(bind_predicate, ResolvedPredicate, Predicate)
PTXSIM_BIND_LEAF(bind_predicate_or_sink, ResolvedPredicateOrSink, PredicateOrSink)
PTXSIM_BIND_LEAF(bind_predicate_source, ResolvedPredicateSource, PredicateSource)
PTXSIM_BIND_LEAF(bind_immediate, ResolvedImmediate, Immediate)
PTXSIM_BIND_LEAF(bind_reg_or_imm, RegOrImm, ScalarOperand)
PTXSIM_BIND_LEAF(bind_register_or_sink, ResolvedRegisterOrSink, RegisterOrSink)
PTXSIM_BIND_LEAF(bind_cvt_source, ResolvedCvtSource, CvtSource)
PTXSIM_BIND_LEAF(bind_mov_source, ResolvedMovSource, MovSource)
PTXSIM_BIND_LEAF(bind_mov_vector_source, ResolvedMovVectorSource, MovVectorSource)
PTXSIM_BIND_LEAF(bind_cp_async_source_control, ResolvedCpAsyncSourceControl, CpAsyncSourceControl)
PTXSIM_BIND_LEAF(bind_vector_register, ResolvedVectorRegisterRef, VectorRegisterRef)
PTXSIM_BIND_LEAF(bind_vector_special_register, ResolvedVectorSpecialRegisterRef, VectorSpecialRegisterRef)
PTXSIM_BIND_LEAF(bind_branch_target, ResolvedBranchTarget, BranchTarget)
PTXSIM_BIND_LEAF(bind_special_register, ResolvedSpecialRegisterRef, SpecialRegisterRef)
PTXSIM_BIND_LEAF(bind_symbol, ResolvedSymbolRef, SymbolRef)
PTXSIM_BIND_LEAF(bind_address, ResolvedAddress, Address)
PTXSIM_BIND_LEAF(bind_register_vector, ResolvedRegisterVector, RegisterVector)
PTXSIM_BIND_LEAF(bind_value_vector, ResolvedValueVector, ValueVector)
PTXSIM_BIND_LEAF(bind_tensor_coordinate, ResolvedTensorCoordinate, TensorCoordinate)
PTXSIM_BIND_LEAF(bind_tensor_im2col_info, ResolvedTensorIm2colInfo, TensorIm2colInfo)
PTXSIM_BIND_LEAF(bind_tensor_operand, ResolvedTensorOperand, TensorOperand)
PTXSIM_BIND_LEAF(bind_fabric_handle, ResolvedFabricHandle, FabricHandle)
PTXSIM_BIND_LEAF(bind_surface_access, ResolvedSurfaceAccess, SurfaceAccess)
PTXSIM_BIND_LEAF(bind_surface_query_resource, ResolvedSurfaceQueryResource, SurfaceQueryResource)
PTXSIM_BIND_LEAF(bind_stack_token, ResolvedStackToken, StackToken)
PTXSIM_BIND_LEAF(bind_local_allocation_result, ResolvedLocalAllocationResult, LocalAllocationResult)
PTXSIM_BIND_LEAF(bind_video_operand, ResolvedVideoOperand, VideoOperand)
PTXSIM_BIND_LEAF(bind_texture_access, ResolvedTextureAccess, TextureAccess)
PTXSIM_BIND_LEAF(bind_texture_query_resource, ResolvedTextureQueryResource, TextureQueryResource)
PTXSIM_BIND_LEAF(bind_texture_result, ResolvedTextureResult, TextureResult)
PTXSIM_BIND_LEAF(bind_tensor_memory_address, TensorMemoryAddress, TensorMemoryAddress)
PTXSIM_BIND_LEAF(bind_tcgen_bracketed_address, TensorMemoryAddress, TensorMemoryAddress)
PTXSIM_BIND_LEAF(bind_tcgen_half_split_offset, TcgenHalfSplitOffset, TcgenHalfSplitOffset)
PTXSIM_BIND_LEAF(bind_matrix_scale_selector, ResolvedMatrixScaleSelector, MatrixScaleSelector)
PTXSIM_BIND_LEAF(bind_shared_matrix_descriptor, ResolvedSharedMatrixDescriptor, SharedMatrixDescriptor)
PTXSIM_BIND_LEAF(bind_wgmma_scale_d, ResolvedPredicateSource, PredicateSource)
PTXSIM_BIND_LEAF(bind_direct_call_target, ResolvedFunctionRef, FunctionRef)
PTXSIM_BIND_LEAF(bind_indirect_callee, ResolvedIndirectCallee, IndirectCallee)
PTXSIM_BIND_LEAF(bind_branch_target_set, ResolvedBranchTargetSet, BranchTargetSet)
PTXSIM_BIND_LEAF(bind_call_return_parameter, ResolvedCallParameterRef, CallParameterRef)
PTXSIM_BIND_LEAF(bind_call_arguments, ResolvedCallArguments, CallArguments)
PTXSIM_BIND_LEAF(bind_shfl_destination, ResolvedShflSyncDestination, ShflDestination)
PTXSIM_BIND_LEAF(bind_predicate_pair, ResolvedPredicatePair, PredicatePair)
PTXSIM_BIND_LEAF(bind_predicate_pair_or_sink, ResolvedPredicatePairOrSink, PredicatePairOrSink)
PTXSIM_BIND_LEAF(bind_mbarrier_state_token, ResolvedMbarrierStateToken, MbarrierStateToken)
#undef PTXSIM_BIND_LEAF
}  // namespace ptxsim::exec_ir_lowering::detail
