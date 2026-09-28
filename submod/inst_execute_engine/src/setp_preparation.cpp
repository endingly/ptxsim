#include "setp_preparation.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <variant>

#include "semantics/setp_semantics.hpp"
#include "value_alu_helpers.hpp"

namespace ptxsim::inst_execute_engine::detail {
namespace {

/** @brief Read a bound predicate source, including constant and special forms. */
[[nodiscard]] auto read_setp_predicate(const memory::RegisterView& registers,
                                       const exec_ir::PredicateSource& source)
    -> std::expected<bool, LaneFaultCause> {
  if (const auto* predicate = std::get_if<exec_ir::Predicate>(&source))
    return read_predicate(registers, *predicate);
  if (const auto* constant = std::get_if<bool>(&source))
    return *constant;
  const auto& special = std::get<exec_ir::SpecialPredicateSource>(source);
  return std::unexpected(LaneFaultCause{UnsupportedSpecialRegister{
      special.register_ref.id, special.register_ref.component}});
}

/** @brief Stage one predicate result unless its destination is a sink. */
[[nodiscard]] auto stage_setp_result(
    PreparedEffect& effect, const memory::RegisterView& registers,
    const std::optional<exec_ir::Predicate>& destination, bool value,
    std::size_t index) -> std::expected<void, LaneFaultCause> {
  if (!destination)
    return {};
  if (destination->negated)
    return std::unexpected(LaneFaultCause{InvalidMovPredicate{}});
  const auto valid = resolve_destination<bool>(registers, destination->source);
  if (!valid)
    return std::unexpected(valid.error());
  effect.writes[index] = PreparedWrite{registers, destination->source,
                                       value_codec<bool>::encode(value)};
  return {};
}

}  // namespace

auto prepare_integer_setp(LaneResourceResolver& resolver,
                          const arith::context& arithmetic,
                          exec_ir::DataType type,
                          exec_ir::ComparisonOperator comparison,
                          std::optional<exec_ir::BooleanOperator> boolean,
                          const exec_ir::PredicateSource* combine,
                          const exec_ir::SetpDestination& destination,
                          const exec_ir::ScalarOperand& lhs,
                          const exec_ir::ScalarOperand& rhs,
                          std::optional<common::ProgramCounter> successor)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  const auto registers = resolver.resolve();
  if (!registers)
    return std::unexpected(registers.error());

  const auto prepare_typed = [&]<arith::arithmetic_integer T>()
      -> std::expected<PreparedEffect, LaneFaultCause> {
    const auto left = read_value<T>(registers->get(), lhs);
    if (!left)
      return std::unexpected(left.error());
    const auto right = read_value<T>(registers->get(), rhs);
    if (!right)
      return std::unexpected(right.error());
    const auto compared =
        semantics::compare(arithmetic, comparison, *left, *right);
    if (!compared)
      return std::unexpected(LaneFaultCause{compared.error()});
    std::optional<bool> predicate;
    if (combine) {
      const auto source = read_setp_predicate(registers->get(), *combine);
      if (!source)
        return std::unexpected(source.error());
      predicate = *source;
    }
    const auto result =
        [&](bool comparison_value) -> std::expected<bool, LaneFaultCause> {
      if (!boolean)
        return comparison_value;
      if (!predicate)
        return std::unexpected(LaneFaultCause{InvalidMovPredicate{}});
      const auto combined =
          semantics::combine(*boolean, comparison_value, *predicate);
      if (!combined)
        return std::unexpected(LaneFaultCause{combined.error()});
      return *combined;
    };
    const auto primary = result(*compared);
    if (!primary)
      return std::unexpected(primary.error());
    PreparedEffect effect{.control = *successor};
    const auto staged = std::visit(
        [&](const auto& destinations) -> std::expected<void, LaneFaultCause> {
          using Destination = std::remove_cvref_t<decltype(destinations)>;
          if constexpr (std::same_as<Destination, exec_ir::PredicateOrSink>) {
            return stage_setp_result(effect, registers->get(),
                                     destinations.predicate, *primary, 0);
          } else {
            const auto second = result(!*compared);
            if (!second)
              return std::unexpected(second.error());
            const auto first_write = stage_setp_result(
                effect, registers->get(), destinations.first, *primary, 0);
            if (!first_write)
              return std::unexpected(first_write.error());
            return stage_setp_result(effect, registers->get(),
                                     destinations.second, *second, 1);
          }
        },
        destination);
    if (!staged)
      return std::unexpected(staged.error());
    return effect;
  };
  switch (type) {
    case exec_ir::DataType::s16:
      return prepare_typed.template operator()<std::int16_t>();
    case exec_ir::DataType::s32:
      return prepare_typed.template operator()<std::int32_t>();
    case exec_ir::DataType::s64:
      return prepare_typed.template operator()<std::int64_t>();
    case exec_ir::DataType::u16:
      return prepare_typed.template operator()<std::uint16_t>();
    case exec_ir::DataType::u32:
      return prepare_typed.template operator()<std::uint32_t>();
    case exec_ir::DataType::u64:
      return prepare_typed.template operator()<std::uint64_t>();
    default:
      return std::unexpected(LaneFaultCause{InvalidMovPredicate{}});
  }
}

}  // namespace ptxsim::inst_execute_engine::detail
