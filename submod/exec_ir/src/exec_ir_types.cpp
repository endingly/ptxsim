#include <ptxsim/exec_ir/exec_ir_types.hpp>

#include <string>
#include <string_view>
#include <type_traits>

#include <fmt/format.h>
#include <magic_enum/magic_enum.hpp>

#include "exec_ir_diagnostic.hpp"

namespace ptxsim::exec_ir {
namespace {

/** @brief Return an enum's declared spelling or the diagnostic invalid marker. */
template <typename T>
  requires std::is_enum_v<T>
[[nodiscard]] auto enum_text(T value) noexcept -> std::string_view {
  const auto name = magic_enum::enum_name(value);
  return name.empty() ? "<invalid>" : name;
}

/** @brief Remove the C++ keyword-avoidance suffix from a declared enum spelling. */
[[nodiscard]] auto without_trailing_underscore(std::string_view value) noexcept
    -> std::string_view {
  if (!value.empty() && value.back() == '_')
    value.remove_suffix(1);
  return value;
}

}  // namespace

auto to_string(DataType value) noexcept -> std::string_view {
  return enum_text(value);
}
auto to_string(RoundingMode value) noexcept -> std::string_view {
  return enum_text(value);
}
auto to_string(ComparisonOperator value) noexcept -> std::string_view {
  return enum_text(value);
}
auto to_string(BooleanOperator value) noexcept -> std::string_view {
  return without_trailing_underscore(enum_text(value));
}
auto to_string(CacheOperator value) noexcept -> std::string_view {
  return enum_text(value);
}
auto to_string(EvictionPriority value) noexcept -> std::string_view {
  return enum_text(value);
}
auto to_string(MemoryConsistency value) noexcept -> std::string_view {
  return without_trailing_underscore(enum_text(value));
}
auto to_string(MemoryScope value) noexcept -> std::string_view {
  return enum_text(value);
}
auto to_string(VectorArity value) noexcept -> std::string_view {
  return enum_text(value);
}
auto to_string(AddressSpace value) noexcept -> std::string_view {
  switch (value) {
    case AddressSpace::const_:
      return "const";
    case AddressSpace::param_entry:
      return "param::entry";
    case AddressSpace::param_func:
      return "param::func";
    default:
      return enum_text(value);
  }
}
auto to_string(MbarrierPhaseType value) noexcept -> std::string_view {
  switch (value) {
    case MbarrierPhaseType::conditional:
      return "phase_type::conditional";
    case MbarrierPhaseType::primary:
      return "phase_type::primary";
  }
  return "<invalid>";
}
auto to_string(MbarrierLayout value) noexcept -> std::string_view {
  switch (value) {
    case MbarrierLayout::v0:
      return "layout::v0";
    case MbarrierLayout::v1:
      return "layout::v1";
  }
  return "<invalid>";
}
auto to_string(AsyncProxyKind value) noexcept -> std::string_view {
  switch (value) {
    case AsyncProxyKind::async:
      return "async";
    case AsyncProxyKind::async_global:
      return "async.global";
    case AsyncProxyKind::async_shared_cluster:
      return "async.shared::cluster";
    case AsyncProxyKind::async_shared_cta:
      return "async.shared::cta";
  }
  return "<invalid>";
}
auto to_string(ProxyKindPair value) noexcept -> std::string_view {
  switch (value) {
    case ProxyKindPair::async_generic:
      return "async::generic";
    case ProxyKindPair::tensormap_generic:
      return "tensormap::generic";
  }
  return "<invalid>";
}

auto to_string(const Predicate& value) -> std::string {
  return fmt::format("{}predicate:{}", value.negated ? "!" : "",
                     value.source.value());
}
auto to_string(const RegisterOrSink& value) -> std::string {
  return value.register_slot ? common::to_string(*value.register_slot) : "_";
}
auto to_string(const SpecialRegisterRef& value) -> std::string {
  if (!value.component)
    return common::to_string(value.id);
  return fmt::format("{}[{}]", common::to_string(value.id), *value.component);
}
auto to_string(const VectorRegisterRef& value) -> std::string {
  return common::to_string(value.register_slot);
}
auto to_string(const VectorSpecialRegisterRef& value) -> std::string {
  return common::to_string(value.id);
}
auto to_string(const RegisterVector& value) -> std::string {
  std::string output{"{"};
  for (const auto& element : value.elements) {
    if (output.back() != '{')
      output += ", ";
    output += element ? common::to_string(*element) : "_";
  }
  output += '}';
  return output;
}
auto to_string(const PredicatePair& value) -> std::string {
  return fmt::format("{{{}, {}}}", to_string(value.first),
                     to_string(value.second));
}
auto to_string(const ShflDestination& value) -> std::string {
  return fmt::format("{{{}, {}}}",
                     value.data ? common::to_string(*value.data) : "_",
                     value.predicate ? to_string(*value.predicate) : "_");
}
auto to_string(const SymbolRef& value) -> std::string {
  return common::to_string(value.id);
}
auto to_string(const FunctionRef& value) -> std::string {
  return common::to_string(value.id);
}
auto to_string(const AddressOffset& value) -> std::string {
  return fmt::format("{}{}", value.subtract ? "-" : "+",
                     common::to_string(value.value));
}
auto to_string(const Address& value) -> std::string {
  const auto base = std::visit(
      [](const auto& item) {
        using Item = std::remove_cvref_t<decltype(item)>;
        if constexpr (std::same_as<Item, SymbolRef>)
          return to_string(item);
        else
          return common::to_string(item);
      },
      value.base);
  return fmt::format("[{}{}]", base,
                     value.offset ? to_string(*value.offset) : "");
}
auto to_string(const BranchTargetSet& value) -> std::string {
  return common::to_string(value.id);
}
auto to_string(const CallParameterRef& value) -> std::string {
  return value.index ? fmt::format("{}[{}]", common::to_string(value.id),
                                   *value.index)
                     : common::to_string(value.id);
}
auto to_string(const CallArguments& value) -> std::string {
  std::string output{"("};
  for (const auto& argument : value.values) {
    if (output.back() != '(')
      output += ", ";
    output += detail::diagnostic_value(argument);
  }
  output += ')';
  return output;
}
auto to_string(const MbarrierStateToken& value) -> std::string {
  return value.register_slot ? common::to_string(*value.register_slot) : "_";
}

}  // namespace ptxsim::exec_ir
