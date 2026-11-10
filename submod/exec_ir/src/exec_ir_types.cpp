#include <ptxsim/exec_ir/exec_ir_types.hpp>

#include <concepts>
#include <initializer_list>
#include <limits>
#include <string_view>
#include <type_traits>
#include <utility>

#include <fmt/format.h>
#include <magic_enum/magic_enum.hpp>

namespace ptxsim::exec_ir {
auto valid_predicate_destination(const Predicate& value) noexcept -> bool { return !value.negated; }
auto valid_predicate_destination(const PredicateOrSink& value) noexcept -> bool {
  return !value.predicate || valid_predicate_destination(*value.predicate);
}
auto valid_predicate_destination(const PredicatePair& value) noexcept -> bool {
  return valid_predicate_destination(value.first) && valid_predicate_destination(value.second);
}
auto valid_predicate_destination(const PredicatePairOrSink& value) noexcept -> bool {
  return (!value.first || valid_predicate_destination(*value.first)) &&
         (!value.second || valid_predicate_destination(*value.second));
}
auto valid_predicate_destination(const ShflDestination& value) noexcept -> bool {
  return !value.predicate || valid_predicate_destination(*value.predicate);
}
auto valid_predicate_destination(const TextureResult& value) noexcept -> bool {
  return !value.residency || valid_predicate_destination(*value.residency);
}
auto data_type_width(DataType type) noexcept -> std::optional<common::RawWidth> {
  switch (type) {
    case DataType::pred: return common::RawWidth::pred;
    case DataType::u2: case DataType::u4: case DataType::u8:
    case DataType::s2: case DataType::s4: case DataType::s8:
    case DataType::b1: case DataType::b8:
    case DataType::e4m3: case DataType::e5m2: case DataType::e2m1x2:
    case DataType::e2m1: case DataType::e2m3: case DataType::e3m2:
    case DataType::ue8m0: case DataType::ue4m3: return common::RawWidth::b8;
    case DataType::u16: case DataType::s16: case DataType::b16:
    case DataType::f16: case DataType::bf16:
    case DataType::e4m3x2: case DataType::e5m2x2: case DataType::e2m3x2:
    case DataType::e3m2x2: case DataType::e2m1x4: case DataType::ue8m0x2:
    case DataType::s2f6x2: return common::RawWidth::b16;
    case DataType::u8x4: case DataType::u16x2: case DataType::u32:
    case DataType::s8x4: case DataType::s16x2: case DataType::s32:
    case DataType::b32: case DataType::f16x2: case DataType::f32:
    case DataType::bf16x2: case DataType::e4m3x4: case DataType::e5m2x4:
    case DataType::e2m3x4: case DataType::e3m2x4: case DataType::tf32:
      return common::RawWidth::b32;
    case DataType::u64: case DataType::s64: case DataType::b64:
    case DataType::f32x2: case DataType::f64: return common::RawWidth::b64;
    case DataType::b128: return common::RawWidth::b128;
  }
  return std::nullopt;
}
auto immediate_value(const Immediate& value) noexcept -> std::optional<common::RawValue> {
  const auto width = data_type_width(value.type);
  if (!width) return std::nullopt;
  switch (*width) {
    case common::RawWidth::pred: return common::RawValue::pred(value.bits != 0);
    case common::RawWidth::b8: return common::RawValue::b8(static_cast<std::uint8_t>(value.bits));
    case common::RawWidth::b16: return common::RawValue::b16(static_cast<std::uint16_t>(value.bits));
    case common::RawWidth::b32: return common::RawValue::b32(static_cast<std::uint32_t>(value.bits));
    case common::RawWidth::b64: return common::RawValue::b64(value.bits);
    case common::RawWidth::b128: return common::RawValue::b128(common::Bits128{value.bits,
        value.is_negative ? std::numeric_limits<std::uint64_t>::max() : 0});
  }
  return std::nullopt;
}
namespace {

/** @brief Format a real nested opaque symbol without a frontend spelling. */
[[nodiscard]] auto leaf_text(const OpaqueSymbolRef&) -> std::string;
/** @brief Format a complete nested direct/indirect opaque resource. */
[[nodiscard]] auto leaf_text(const OpaqueResourceRef&) -> std::string;
/** @brief Format a nested parameter identity and compact member. */
[[nodiscard]] auto leaf_text(const ParameterRef&) -> std::string;
/** @brief Format a complete source integer value. */
[[nodiscard]] auto leaf_text(const IntegerConstant&) -> std::string;
/** @brief Format exact binary64 source bits. */
[[nodiscard]] auto leaf_text(const FloatingConstant&) -> std::string;
/** @brief Format a complementable intrinsic predicate. */
[[nodiscard]] auto leaf_text(const PredicateSpecialRegister&) -> std::string;
/** @brief Format a literal logical predicate. */
[[nodiscard]] auto leaf_text(const PredicateConstant&) -> std::string;
/** @brief Format typed indirect-call metadata. */
[[nodiscard]] auto leaf_text(const IndirectMetadataRef&) -> std::string;
/** @brief Format an owned sparse callable table reference. */
[[nodiscard]] auto leaf_text(const CallTableRef&) -> std::string;
/** @brief Format source and converted values of a call literal. */
[[nodiscard]] auto leaf_text(const CallLiteral&) -> std::string;
/** @brief Format one call argument without losing its selected alternative. */
[[nodiscard]] auto leaf_text(const CallArgument&) -> std::string;
/** @brief Format a named-array index independently from the address byte offset. */
[[nodiscard]] auto leaf_text(const NamedArrayIndex&) -> std::string;
/** @brief Format an owned CP-async opaque policy carrier. */
[[nodiscard]] auto leaf_text(const CpAsyncCachePolicy&) -> std::string;
/** @brief Format a complete owned MOV value vector. */
[[nodiscard]] auto leaf_text(const MovValueVector&) -> std::string;
/** @brief Format the selected scalar/swizzle/mask video alternative. */
[[nodiscard]] auto leaf_text(const VideoSelector&) -> std::string;
/** @brief Format a surface coordinate's selected role and value. */
[[nodiscard]] auto leaf_text(const SurfaceLane&) -> std::string;
/** @brief Format a texture coordinate's selected role and value. */
[[nodiscard]] auto leaf_text(const TextureLane&) -> std::string;

/** @brief Format booleans independently from integer source values. */
[[nodiscard]] auto leaf_text(bool value) -> std::string {
  return value ? "true" : "false";
}
/** @brief Format integer metadata without changing its signed interpretation. */
template <std::integral T>
  requires(!std::same_as<T, bool>)
[[nodiscard]] auto leaf_text(T value) -> std::string {
  return std::to_string(value);
}
/** @brief Format complete generated domains and public owned operand values. */
template <typename T>
  requires requires(const T& value) { ::ptxsim::exec_ir::to_string(value); }
[[nodiscard]] auto leaf_text(const T& value) -> std::string {
  return std::string{::ptxsim::exec_ir::to_string(value)};
}
/** @brief Format the closed small native role domains by their declared names. */
template <typename T>
  requires std::is_enum_v<T> &&
           (!requires(const T& value) { ::ptxsim::exec_ir::to_string(value); })
[[nodiscard]] auto leaf_text(T value) -> std::string {
  const auto name = magic_enum::enum_name(value);
  return name.empty() ? "<invalid>" : std::string{name};
}
/** @brief Format one existing strong execution identity. */
template <common::detail::HasPrefixMember Tag>
[[nodiscard]] auto leaf_text(common::detail::Id<Tag> value) -> std::string {
  return common::to_string(value);
}
/** @brief Format actual optional values without inventing operand alternatives. */
template <typename T>
  requires requires(const T& value) { { leaf_text(value) } -> std::same_as<std::string>; }
[[nodiscard]] auto leaf_text(const std::optional<T>& value) -> std::string {
  return value ? leaf_text(*value) : "none";
}
/** @brief Format complete ordered value groups. */
template <typename T>
  requires requires(const T& value) { { leaf_text(value) } -> std::same_as<std::string>; }
[[nodiscard]] auto leaf_text(const std::vector<T>& values) -> std::string {
  std::string output{"["};
  for (const auto& value : values) {
    if (output.size() > 1)
      output += ", ";
    output += leaf_text(value);
  }
  return output + ']';
}
/** @brief Format actual typed sum alternatives, safely reporting valueless input. */
template <typename... T>
  requires(requires(const T& value) { { leaf_text(value) } -> std::same_as<std::string>; } && ...)
[[nodiscard]] auto sum_text(const std::variant<T...>& value) -> std::string {
  if (value.valueless_by_exception())
    return "<valueless>";
  return std::visit([](const auto& item) { return leaf_text(item); }, value);
}
/** @brief Compose named owned fields into unambiguous diagnostic records. */
[[nodiscard]] auto record(
    std::string_view name,
    std::initializer_list<std::pair<std::string_view, std::string>> fields)
    -> std::string {
  std::string output{name};
  output += '{';
  bool first = true;
  for (const auto& [field, value] : fields) {
    if (!first)
      output += ", ";
    first = false;
    output += field;
    output += '=';
    output += value;
  }
  return output + '}';
}

/** @brief Validate every actual MOV value-vector lane. */
[[nodiscard]] auto valid_value(const MovValueVector&) noexcept -> bool;
/** @brief Validate an opaque resource identity's local typed shape. */
[[nodiscard]] auto valid_value(const OpaqueSymbolRef& value) noexcept -> bool {
  return static_cast<unsigned>(value.kind) <= static_cast<unsigned>(OpaqueResourceKind::Surface);
}
/** @brief Validate a complete direct/indirect resource reference. */
[[nodiscard]] auto valid_value(const OpaqueResourceRef& value) noexcept -> bool {
  if (static_cast<unsigned>(value.expected_kind) > static_cast<unsigned>(OpaqueResourceKind::Surface))
    return false;
  if (const auto* direct = std::get_if<OpaqueSymbolRef>(&value.value))
    return valid_value(*direct) && direct->kind == value.expected_kind;
  return valid_operand(std::get<RegisterRef>(value.value));
}
/** @brief Validate a nested literal's typed use when one exists. */
[[nodiscard]] auto valid_value(const CallLiteral& value) noexcept -> bool {
  return static_cast<unsigned>(value.kind) <= static_cast<unsigned>(LiteralCategory::BinaryInteger) &&
         (!value.value || valid_operand(*value.value));
}
/** @brief Validate a nested parameter reference's local shape. */
[[nodiscard]] auto valid_value(const ParameterRef&) noexcept -> bool { return true; }
/** @brief Validate a nested typed indirect metadata reference. */
[[nodiscard]] auto valid_value(const IndirectMetadataRef&) noexcept -> bool { return true; }
/** @brief Validate a nested callable table reference's local shape. */
[[nodiscard]] auto valid_value(const CallTableRef&) noexcept -> bool { return true; }
/** @brief Validate an intrinsic predicate view. */
[[nodiscard]] auto valid_value(const PredicateSpecialRegister& value) noexcept -> bool {
  return valid_operand(value.register_ref);
}
/** @brief Every boolean literal is a structurally valid predicate constant. */
[[nodiscard]] auto valid_value(const PredicateConstant&) noexcept -> bool { return true; }
/** @brief Validate opaque CP-async policy carrier metadata. */
[[nodiscard]] auto valid_value(const CpAsyncCachePolicy& value) noexcept -> bool {
  return valid_operand(value.register_ref);
}
/** @brief Defer a public owned leaf to its complete structural overload. */
template <typename T>
  requires requires(const T& value) { { valid_operand(value) } -> std::same_as<bool>; }
[[nodiscard]] auto valid_value(const T& value) noexcept -> bool {
  return valid_operand(value);
}
/** @brief Validate all selected alternatives of an actual fixed typed sum. */
template <typename... T>
  requires(requires(const T& value) { { valid_value(value) } -> std::same_as<bool>; } && ...)
[[nodiscard]] auto valid_sum(const std::variant<T...>& value) noexcept -> bool {
  return !value.valueless_by_exception() &&
         std::visit([](const auto& item) { return valid_value(item); }, value);
}

/** @brief Format a selected opaque resource declaration and semantic owner. */
auto leaf_text(const OpaqueSymbolRef& value) -> std::string {
  return record("opaque", {{"id", leaf_text(value.id)}, {"member", leaf_text(value.member)},
                           {"kind", leaf_text(value.kind)}, {"entry", leaf_text(value.entry_owner)}});
}
/** @brief Format a complete opaque carrier and its required use category. */
auto leaf_text(const OpaqueResourceRef& value) -> std::string {
  return record("resource", {{"value", sum_text(value.value)}, {"expected", leaf_text(value.expected_kind)}});
}
/** @brief Format bound parameter identity and compact member. */
auto leaf_text(const ParameterRef& value) -> std::string {
  return record("param", {{"id", leaf_text(value.id)}, {"member", leaf_text(value.member)}});
}
/** @brief Format original integer bits and unsigned interpretation. */
auto leaf_text(const IntegerConstant& value) -> std::string {
  return record("integer", {{"bits", fmt::format("{:#x}", value.bits)}, {"unsigned", leaf_text(value.is_unsigned)}});
}
/** @brief Format original binary64 bits without host floating formatting. */
auto leaf_text(const FloatingConstant& value) -> std::string {
  return record("floating", {{"bits", fmt::format("{:#x}", value.bits)}});
}
/** @brief Format complete intrinsic predicate interpretation. */
auto leaf_text(const PredicateSpecialRegister& value) -> std::string {
  return record("intrinsic-predicate", {{"register", leaf_text(value.register_ref)}, {"negated", leaf_text(value.negated)}});
}
/** @brief Format an actual literal predicate alternative. */
auto leaf_text(const PredicateConstant& value) -> std::string {
  return "predicate-constant{" + leaf_text(value.value) + '}';
}
/** @brief Format the exact owned prototype versus target-set metadata identity. */
auto leaf_text(const IndirectMetadataRef& value) -> std::string { return sum_text(value.target); }
/** @brief Format a sparse callable table's distinct metadata identity. */
auto leaf_text(const CallTableRef& value) -> std::string { return leaf_text(value.id); }
/** @brief Format source category/value and optional use conversion of a literal. */
auto leaf_text(const CallLiteral& value) -> std::string {
  return record("call-literal", {{"kind", leaf_text(value.kind)},
      {"source", value.source_value ? sum_text(*value.source_value) : "none"}, {"use", leaf_text(value.value)}});
}
/** @brief Format a call lvalue versus literal without losing its alternative. */
auto leaf_text(const CallArgument& value) -> std::string { return sum_text(value); }
/** @brief Format all independent named-index source and byte-displacement facts. */
auto leaf_text(const NamedArrayIndex& value) -> std::string {
  return record("named-index", {{"index", sum_text(value.index)}, {"displacement", leaf_text(value.displacement)},
      {"operator", leaf_text(value.operation)}, {"stride", leaf_text(value.scalar_stride)},
      {"byte-displacement", leaf_text(value.byte_displacement)}});
}
/** @brief Format a CP-async policy carrier without evaluating register state. */
auto leaf_text(const CpAsyncCachePolicy& value) -> std::string {
  return record("cache-policy", {{"register", leaf_text(value.register_ref)}});
}
/** @brief Format every MOV vector source in actual order. */
auto leaf_text(const MovValueVector& value) -> std::string {
  std::string output{"mov-vector["};
  for (const auto& lane : value.elements) {
    if (output.size() > 11)
      output += ", ";
    output += sum_text(lane);
  }
  return output + ']';
}
/** @brief Format every active scalar/swizzle/mask selector position. */
auto leaf_text(const VideoSelector& value) -> std::string {
  if (value.valueless_by_exception())
    return "<valueless>";
  return std::visit([](const auto& selector) {
    using T = std::decay_t<decltype(selector)>;
    if constexpr (std::same_as<T, VideoByteSelector>)
      return record("byte", {{"index", leaf_text(selector.index)}});
    else if constexpr (std::same_as<T, VideoHalfSelector>)
      return record("half", {{"index", leaf_text(selector.index)}});
    else {
      std::string positions{"["};
      for (const auto index : selector.indices) {
        if (positions.size() > 1)
          positions += ',';
        positions += leaf_text(index);
      }
      positions += ']';
      if constexpr (std::same_as<T, VideoHalfMask> || std::same_as<T, VideoByteMask>)
        return record("mask", {{"indices", positions}, {"count", leaf_text(selector.count)}});
      else
        return record("swizzle", {{"indices", positions}});
    }
  }, value);
}
/** @brief Format one surface coordinate with its execution role. */
auto leaf_text(const SurfaceLane& value) -> std::string {
  return record("lane", {{"value", leaf_text(value.value)}, {"role", leaf_text(value.role)}});
}
/** @brief Format one texture coordinate with its execution role. */
auto leaf_text(const TextureLane& value) -> std::string {
  return record("lane", {{"value", leaf_text(value.value)}, {"role", leaf_text(value.role)}});
}
}  // namespace

// Public overloads validate owned representation only; register state/bindings
// remain execution-time lane-local concerns.

auto valid_operand(const RegisterRef& value) noexcept -> bool {
  if (!is_valid(value.declared_type) ||
      (value.register_class != RegisterClass::General && value.register_class != RegisterClass::Predicate) ||
      ((value.register_class == RegisterClass::Predicate) != (value.declared_type == DataType::pred)))
    return false;
  if (value.vector_width && *value.vector_width != 2 && *value.vector_width != 4)
    return false;
  if (value.component && (value.vector_width ||
      (value.component->declaration_width != 2 && value.component->declaration_width != 4) ||
      value.component->lane >= value.component->declaration_width))
    return false;
  return true;
}
auto to_string(const RegisterRef& value) -> std::string {
  return record("register", {{"slot", leaf_text(value.slot)}, {"class", leaf_text(value.register_class)},
      {"type", leaf_text(value.declared_type)}, {"width", leaf_text(value.vector_width)},
      {"component", value.component ? record("component", {{"lane", leaf_text(value.component->lane)},
        {"declaration-width", leaf_text(value.component->declaration_width)}}) : "none"}});
}
auto valid_operand(const Immediate& value) noexcept -> bool {
  const auto width=data_type_width(value.type);
  if (!width || (value.is_negative && (!value.integer_source_bits ||
      (*value.integer_source_bits & (std::uint64_t{1} << 63)) == 0))) return false;
  // Frontend conversion retains use bits in the physical byte container, including subbyte types.
  std::uint64_t mask=std::numeric_limits<std::uint64_t>::max();
  switch(*width) {
    case common::RawWidth::pred: mask=1; break;
    case common::RawWidth::b8: mask=0xff; break;
    case common::RawWidth::b16: mask=0xffff; break;
    case common::RawWidth::b32: mask=0xffffffff; break;
    case common::RawWidth::b64: case common::RawWidth::b128: break;
  }
  return (value.bits & ~mask) == 0 &&
      (!value.integer_source_bits || (*value.integer_source_bits & mask) == value.bits);
}
auto to_string(const Immediate& value) -> std::string {
  return record("immediate", {{"bits", fmt::format("{:#x}", value.bits)}, {"type", leaf_text(value.type)},
      {"negative", leaf_text(value.is_negative)}, {"integer-source", value.integer_source_bits
          ? fmt::format("{:#x}", *value.integer_source_bits) : "none"}});
}
auto valid_operand(const Predicate& value) noexcept -> bool {
  return valid_operand(value.register_ref) && value.register_ref.register_class == RegisterClass::Predicate;
}
auto to_string(const Predicate& value) -> std::string {
  return record("predicate", {{"register", leaf_text(value.register_ref)}, {"negated", leaf_text(value.negated)}});
}
auto valid_operand(const SpecialRegisterRef& value) noexcept -> bool {
  if (value.id.kind == SpecialRegisterKind::Invalid ||
      static_cast<unsigned>(value.id.kind) > static_cast<unsigned>(SpecialRegisterKind::ReservedSmemOffset) ||
      !is_valid(value.element_type)) return false;
  switch (value.id.kind) {
    case SpecialRegisterKind::PerformanceMonitor:
    case SpecialRegisterKind::PerformanceMonitor64:
      if (value.id.index >= 8) return false;
      break;
    case SpecialRegisterKind::Environment:
      if (value.id.index >= 32) return false;
      break;
    case SpecialRegisterKind::ReservedSmemOffset:
      if (value.id.index >= 2) return false;
      break;
    default:
      if (value.id.index != 0) return false;
      break;
  }
  const bool vector = value.id.kind == SpecialRegisterKind::Tid || value.id.kind == SpecialRegisterKind::NTid ||
      value.id.kind == SpecialRegisterKind::CtaId || value.id.kind == SpecialRegisterKind::NCtaId ||
      value.id.kind == SpecialRegisterKind::ClusterId || value.id.kind == SpecialRegisterKind::NClusterId ||
      value.id.kind == SpecialRegisterKind::ClusterCtaId || value.id.kind == SpecialRegisterKind::ClusterNCtaId;
  return value.vector_width == (vector ? 4 : 1) &&
      (!value.component || (vector && *value.component < 3));
}
auto to_string(const SpecialRegisterRef& value) -> std::string {
  return record("intrinsic", {{"kind", leaf_text(value.id.kind)}, {"member", leaf_text(value.id.index)},
      {"component", leaf_text(value.component)}, {"type", leaf_text(value.element_type)},
      {"width", leaf_text(value.vector_width)}});
}
auto valid_operand(const RegisterOrSink& value) noexcept -> bool {
  return !value.register_ref || valid_operand(*value.register_ref);
}
auto to_string(const RegisterOrSink& value) -> std::string { return leaf_text(value.register_ref); }
auto valid_operand(const MbarrierStateToken& value) noexcept -> bool {
  return !value.register_ref || valid_operand(*value.register_ref);
}
auto to_string(const MbarrierStateToken& value) -> std::string { return leaf_text(value.register_ref); }
auto valid_operand(const VectorRegisterRef& value) noexcept -> bool {
  return valid_operand(value.register_ref) && value.register_ref.vector_width.has_value();
}
auto to_string(const VectorRegisterRef& value) -> std::string { return leaf_text(value.register_ref); }
auto valid_operand(const VectorSpecialRegisterRef& value) noexcept -> bool {
  return valid_operand(value.register_ref) && !value.register_ref.component;
}
auto to_string(const VectorSpecialRegisterRef& value) -> std::string { return leaf_text(value.register_ref); }
auto valid_operand(const RegisterVector& value) noexcept -> bool {
  for (const auto& lane : value.elements)
    if (lane && !valid_operand(*lane))
      return false;
  return true;
}
auto to_string(const RegisterVector& value) -> std::string { return leaf_text(value.elements); }
auto valid_operand(const ValueVector& value) noexcept -> bool {
  for (const auto& lane : value.elements)
    if (!valid_operand(lane))
      return false;
  return true;
}
auto to_string(const ValueVector& value) -> std::string { return leaf_text(value.elements); }
auto valid_operand(const PredicateOrSink& value) noexcept -> bool {
  return !value.predicate || valid_operand(*value.predicate);
}
auto to_string(const PredicateOrSink& value) -> std::string { return leaf_text(value.predicate); }
auto valid_operand(const PredicatePair& value) noexcept -> bool {
  return valid_operand(value.first) && valid_operand(value.second);
}
auto to_string(const PredicatePair& value) -> std::string {
  return record("predicate-pair", {{"first", leaf_text(value.first)}, {"second", leaf_text(value.second)}});
}
auto valid_operand(const PredicatePairOrSink& value) noexcept -> bool {
  return (!value.first || valid_operand(*value.first)) && (!value.second || valid_operand(*value.second));
}
auto to_string(const PredicatePairOrSink& value) -> std::string {
  return record("predicate-pair-or-sink", {{"first", leaf_text(value.first)}, {"second", leaf_text(value.second)}});
}
auto valid_operand(const ShflDestination& value) noexcept -> bool {
  return (!value.data || valid_operand(*value.data)) && (!value.predicate || valid_operand(*value.predicate));
}
auto to_string(const ShflDestination& value) -> std::string {
  return record("shuffle-result", {{"data", leaf_text(value.data)}, {"predicate", leaf_text(value.predicate)}});
}
auto valid_operand(const BranchTarget&) noexcept -> bool { return true; }
auto to_string(const BranchTarget& value) -> std::string {
  return record("branch-target", {{"function", leaf_text(value.function)}, {"pc", leaf_text(value.pc)}});
}
auto valid_operand(const BranchTargetSet&) noexcept -> bool { return true; }
auto to_string(const BranchTargetSet& value) -> std::string { return leaf_text(value.id); }
auto valid_operand(const FunctionRef&) noexcept -> bool { return true; }
auto to_string(const FunctionRef& value) -> std::string {
  return record("function-ref", {{"function", leaf_text(value.id)}, {"entry", leaf_text(value.is_entry)}});
}
auto valid_operand(const CallParameterRef& value) noexcept -> bool { return valid_sum(value.value); }
auto to_string(const CallParameterRef& value) -> std::string { return sum_text(value.value); }
auto valid_operand(const CallArguments& value) noexcept -> bool {
  for (const auto& argument : value.values)
    if (!valid_sum(argument))
      return false;
  return true;
}
auto to_string(const CallArguments& value) -> std::string { return leaf_text(value.values); }
auto valid_operand(const SymbolRef& value) noexcept -> bool {
  return (!value.declared_type || is_valid(*value.declared_type)) &&
      static_cast<unsigned>(value.enclosing_function_kind) <= static_cast<unsigned>(EnclosingFunctionKind::Device) &&
      static_cast<unsigned>(value.parameter_qualifier) <= static_cast<unsigned>(ParameterAddressQualifier::Function) &&
      (!value.parameter_role || static_cast<unsigned>(*value.parameter_role) <= static_cast<unsigned>(ParameterDeclarationRole::LegacyEntryInput));
}
auto to_string(const SymbolRef& value) -> std::string {
  return record("symbol", {{"target", sum_text(value.target)}, {"member", leaf_text(value.member)},
      {"declaration-space", leaf_text(value.declaration_space)}, {"address-space", leaf_text(value.address_space)},
      {"type", leaf_text(value.declared_type)}, {"alignment", leaf_text(value.alignment)},
      {"unified", leaf_text(value.is_unified)}, {"enclosing", leaf_text(value.enclosing_function_kind)},
      {"parameter", leaf_text(value.parameter_qualifier)}, {"role", leaf_text(value.parameter_role)}});
}
auto valid_operand(const Address& value) noexcept -> bool {
  if (!valid_sum(value.base) ||
      static_cast<unsigned>(value.enclosing_function_kind) > static_cast<unsigned>(EnclosingFunctionKind::Device) ||
      static_cast<unsigned>(value.parameter_qualifier) > static_cast<unsigned>(ParameterAddressQualifier::Function))
    return false;
  if (value.offset && (!valid_operand(value.offset->value) ||
      value.offset->value.type != DataType::s64 ||
      (value.offset->value.integer_source_bits &&
       *value.offset->value.integer_source_bits != value.offset->value.bits) ||
      static_cast<unsigned>(value.offset->operation) > static_cast<unsigned>(AddressOffsetOperator::Subtract)))
    return false;
  if (value.named_index) {
    if (const auto* index = std::get_if<RegisterRef>(&value.named_index->index))
      if (!valid_operand(*index))
        return false;
    if (static_cast<unsigned>(value.named_index->operation) > static_cast<unsigned>(AddressOffsetOperator::Subtract))
      return false;
  }
  return true;
}
auto to_string(const Address& value) -> std::string {
  return record("address", {{"base", sum_text(value.base)},
      {"offset", value.offset ? record("offset", {{"operator", leaf_text(value.offset->operation)},
        {"value", leaf_text(value.offset->value)}}) : "none"},
      {"enclosing", leaf_text(value.enclosing_function_kind)}, {"parameter", leaf_text(value.parameter_qualifier)},
      {"unified", leaf_text(value.unified)}, {"named-index", leaf_text(value.named_index)}});
}
auto valid_operand(const StackToken& value) noexcept -> bool {
  return valid_operand(value.register_ref) && value.enclosing_function_kind != EnclosingFunctionKind::Unknown &&
      static_cast<unsigned>(value.enclosing_function_kind) <= static_cast<unsigned>(EnclosingFunctionKind::Device);
}
auto to_string(const StackToken& value) -> std::string {
  return record("stack-token", {{"register", leaf_text(value.register_ref)},
      {"enclosing", leaf_text(value.enclosing_function_kind)}, {"function", leaf_text(value.function)}});
}
auto valid_operand(const LocalAllocationResult& value) noexcept -> bool {
  return valid_operand(value.register_ref) && value.enclosing_function_kind != EnclosingFunctionKind::Unknown &&
      static_cast<unsigned>(value.enclosing_function_kind) <= static_cast<unsigned>(EnclosingFunctionKind::Device) &&
      value.address_space == DeclarationSpace::Local;
}
auto to_string(const LocalAllocationResult& value) -> std::string {
  return record("local-allocation", {{"register", leaf_text(value.register_ref)},
      {"enclosing", leaf_text(value.enclosing_function_kind)}, {"function", leaf_text(value.function)},
      {"space", leaf_text(value.address_space)}});
}
auto valid_operand(const SharedMatrixDescriptor& value) noexcept -> bool { return valid_operand(value.register_ref); }
auto to_string(const SharedMatrixDescriptor& value) -> std::string { return leaf_text(value.register_ref); }

namespace {
/** @brief Validate each real MOV vector lane without source provenance. */
[[nodiscard]] auto valid_value(const MovValueVector& value) noexcept -> bool {
  for (const auto& lane : value.elements)
    if (!valid_operand(lane))
      return false;
  return true;
}
/** @brief Validate local fixed-size selector storage, without PTX cross-rules. */
[[nodiscard]] auto valid_video_selector(const VideoSelector& value) noexcept -> bool {
  if (value.valueless_by_exception())
    return false;
  return std::visit([](const auto& selector) {
    using T = std::decay_t<decltype(selector)>;
    if constexpr (std::same_as<T, VideoByteSelector>)
      return selector.index < 4;
    else if constexpr (std::same_as<T, VideoHalfSelector>)
      return selector.index < 2;
    else if constexpr (std::same_as<T, VideoHalfSwizzle> || std::same_as<T, VideoByteSwizzle>) {
      constexpr auto limit = std::same_as<T, VideoHalfSwizzle> ? 4U : 8U;
      for (const auto index : selector.indices)
        if (index >= limit)
          return false;
      return true;
    } else {
      if (selector.count == 0 || selector.count > selector.indices.size())
        return false;
      constexpr auto limit = std::same_as<T, VideoHalfMask> ? 2U : 4U;
      for (std::size_t index = 0; index < selector.count; ++index)
        if (selector.indices[index] >= limit ||
            (index != 0 && selector.indices[index - 1] <= selector.indices[index]))
          return false;
      return true;
    }
  }, value);
}
}  // namespace

auto valid_operand(const VideoOperand& value) noexcept -> bool {
  return valid_operand(value.value) && (!value.selector || valid_video_selector(*value.selector));
}
auto to_string(const VideoOperand& value) -> std::string {
  return record("video", {{"value", leaf_text(value.value)}, {"selector", leaf_text(value.selector)},
      {"negated", leaf_text(value.negated)}});
}
auto valid_operand(const TensorMemoryAddress& value) noexcept -> bool { return valid_operand(value.value); }
auto to_string(const TensorMemoryAddress& value) -> std::string {
  return record("tensor-memory-address", {{"value", leaf_text(value.value)}, {"bracketed", leaf_text(value.bracketed)}});
}
auto valid_operand(const TcgenHalfSplitOffset& value) noexcept -> bool {
  return value.source_kind == TcgenIntegerSourceKind::Signed || value.source_kind == TcgenIntegerSourceKind::Unsigned;
}
auto to_string(const TcgenHalfSplitOffset& value) -> std::string {
  return record("split-offset", {{"source-bits", fmt::format("{:#x}", value.source_bits)},
      {"source-kind", leaf_text(value.source_kind)}});
}
auto valid_operand(const MatrixScaleSelector& value) noexcept -> bool {
  return valid_operand(value.byte_id) && valid_operand(value.thread_id);
}
auto to_string(const MatrixScaleSelector& value) -> std::string {
  return record("matrix-scale", {{"byte", leaf_text(value.byte_id)}, {"thread", leaf_text(value.thread_id)}});
}
auto valid_operand(const TensorCoordinate& value) noexcept -> bool {
  for (const auto& element : value.elements)
    if (!valid_operand(element))
      return false;
  return true;
}
auto to_string(const TensorCoordinate& value) -> std::string { return leaf_text(value.elements); }
auto valid_operand(const TensorIm2colInfo& value) noexcept -> bool {
  for (const auto& element : value.elements)
    if (!valid_operand(element))
      return false;
  return true;
}
auto to_string(const TensorIm2colInfo& value) -> std::string { return leaf_text(value.elements); }
auto valid_operand(const TensorOperand& value) noexcept -> bool {
  return valid_operand(value.tensor_map.address) && valid_operand(value.coordinates) &&
      static_cast<unsigned>(value.rank) >= 1 && static_cast<unsigned>(value.rank) <= 5 &&
      static_cast<unsigned>(value.mode) <= static_cast<unsigned>(TensorAccessMode::TileScatter4);
}
auto to_string(const TensorOperand& value) -> std::string {
  return record("tensor", {{"map", leaf_text(value.tensor_map.address)}, {"coordinates", leaf_text(value.coordinates)},
      {"rank", leaf_text(value.rank)}, {"mode", leaf_text(value.mode)}});
}
auto valid_operand(const FabricHandle& value) noexcept -> bool {
  return valid_operand(value.endpoint) && valid_operand(value.data_offset) &&
      (!value.counter_offset || valid_operand(*value.counter_offset));
}
auto to_string(const FabricHandle& value) -> std::string {
  return record("fabric", {{"endpoint", leaf_text(value.endpoint)}, {"data-offset", leaf_text(value.data_offset)},
      {"counter-offset", leaf_text(value.counter_offset)}});
}
auto valid_operand(const SurfaceAccess& value) noexcept -> bool {
  if (!valid_value(value.surface) || value.surface.expected_kind != OpaqueResourceKind::Surface)
    return false;
  for (const auto& lane : value.coordinates)
    if (!valid_operand(lane.value) || static_cast<unsigned>(lane.role) > static_cast<unsigned>(SurfaceLaneRole::Ignored))
      return false;
  return true;
}
auto to_string(const SurfaceAccess& value) -> std::string {
  return record("surface", {{"resource", leaf_text(value.surface)}, {"coordinates", leaf_text(value.coordinates)},
      {"packed", leaf_text(value.coordinates_packed)}, {"bracketed", leaf_text(value.bracketed)}});
}
auto valid_operand(const SurfaceQueryResource& value) noexcept -> bool { return valid_value(value.resource); }
auto to_string(const SurfaceQueryResource& value) -> std::string {
  return record("surface-query", {{"resource", leaf_text(value.resource)}, {"bracketed", leaf_text(value.bracketed)}});
}
auto valid_operand(const TextureAccess& value) noexcept -> bool {
  if (!valid_value(value.texture) || value.texture.expected_kind != OpaqueResourceKind::Texture ||
      !is_valid(value.coordinate_type) || (value.sampler && (!valid_value(*value.sampler) ||
          value.sampler->expected_kind != OpaqueResourceKind::Sampler)))
    return false;
  for (const auto& lane : value.coordinates)
    if (!valid_operand(lane.value) || static_cast<unsigned>(lane.role) > static_cast<unsigned>(TextureLaneRole::Ignored))
      return false;
  return true;
}
auto to_string(const TextureAccess& value) -> std::string {
  return record("texture", {{"resource", leaf_text(value.texture)}, {"sampler", leaf_text(value.sampler)},
      {"coordinates", leaf_text(value.coordinates)}, {"type", leaf_text(value.coordinate_type)},
      {"packed", leaf_text(value.coordinates_packed)}, {"bracketed", leaf_text(value.bracketed)}});
}
auto valid_operand(const TextureQueryResource& value) noexcept -> bool { return valid_value(value.resource); }
auto to_string(const TextureQueryResource& value) -> std::string {
  return record("texture-query", {{"resource", leaf_text(value.resource)}, {"bracketed", leaf_text(value.bracketed)}});
}
auto valid_operand(const TextureResult& value) noexcept -> bool {
  return valid_operand(value.data) && is_valid(value.result_type) &&
      (!value.residency || valid_operand(*value.residency));
}
auto to_string(const TextureResult& value) -> std::string {
  return record("texture-result", {{"data", leaf_text(value.data)}, {"type", leaf_text(value.result_type)},
      {"residency", leaf_text(value.residency)}});
}

auto valid_operand(const ScalarOperand& value) noexcept -> bool { return valid_sum(value); }
auto to_string(const ScalarOperand& value) -> std::string { return sum_text(value); }

auto valid_operand(const PredicateSource& value) noexcept -> bool { return valid_sum(value); }
auto to_string(const PredicateSource& value) -> std::string { return sum_text(value); }

auto valid_operand(const CvtSource& value) noexcept -> bool { return valid_sum(value); }
auto to_string(const CvtSource& value) -> std::string { return sum_text(value); }

auto valid_operand(const MovSource& value) noexcept -> bool { return valid_sum(value); }
auto to_string(const MovSource& value) -> std::string { return sum_text(value); }

auto valid_operand(const MovVectorSource& value) noexcept -> bool { return valid_sum(value); }
auto to_string(const MovVectorSource& value) -> std::string { return sum_text(value); }

auto valid_operand(const IndirectCallee& value) noexcept -> bool { return valid_sum(value); }
auto to_string(const IndirectCallee& value) -> std::string { return sum_text(value); }

auto valid_operand(const CpAsyncSourceControl& value) noexcept -> bool { return valid_sum(value); }
auto to_string(const CpAsyncSourceControl& value) -> std::string { return sum_text(value); }

auto atomic_space_matches(AddressSpace space, AtomicAddressQualifier qualifier) noexcept -> bool {
  switch (qualifier) {
    case AtomicAddressQualifier::generic: return space == AddressSpace::generic;
    case AtomicAddressQualifier::global: return space == AddressSpace::global;
    case AtomicAddressQualifier::shared: return space == AddressSpace::shared;
    case AtomicAddressQualifier::shared_cta:
      return space == AddressSpace::shared || space == AddressSpace::shared_cta;
    case AtomicAddressQualifier::shared_cluster:
      return space == AddressSpace::shared || space == AddressSpace::shared_cluster;
  }
  return false;
}
auto parameter_space_matches(AddressSpace space, ParameterAddressQualifier qualifier) noexcept -> bool {
  if (space == AddressSpace::param)
    return static_cast<unsigned>(qualifier) <= static_cast<unsigned>(ParameterAddressQualifier::Function);
  if (space == AddressSpace::param_entry)
    return qualifier == ParameterAddressQualifier::Entry;
  if (space == AddressSpace::param_func)
    return qualifier == ParameterAddressQualifier::Function;
  return qualifier == ParameterAddressQualifier::Default;
}

auto vector_arity_size(VectorArity value) noexcept -> std::size_t {
  switch (value) {
    case VectorArity::v2: return 2;
    case VectorArity::v4: return 4;
    case VectorArity::v8: return 8;
  }
  return 0;
}
auto operand_cardinality(const RegisterVector& value) noexcept -> std::optional<std::size_t> { return value.elements.size(); }
auto operand_cardinality(const ValueVector& value) noexcept -> std::optional<std::size_t> { return value.elements.size(); }
auto operand_cardinality(const TensorCoordinate& value) noexcept -> std::optional<std::size_t> { return value.elements.size(); }
auto operand_cardinality(const TensorIm2colInfo& value) noexcept -> std::optional<std::size_t> { return value.elements.size(); }
auto operand_cardinality(const CallArguments& value) noexcept -> std::optional<std::size_t> { return value.values.size(); }
auto operand_cardinality(const TensorOperand& value) noexcept -> std::optional<std::size_t> { return value.coordinates.elements.size(); }
auto operand_cardinality(const TextureResult& value) noexcept -> std::optional<std::size_t> { return value.data.elements.size(); }
auto operand_cardinality(const MatrixScaleSelector&) noexcept -> std::optional<std::size_t> { return 2; }
auto operand_cardinality(const MovVectorSource& value) noexcept -> std::optional<std::size_t> {
  if (const auto* vector = std::get_if<MovValueVector>(&value))
    return vector->elements.size();
  return std::nullopt;
}
}  // namespace ptxsim::exec_ir
