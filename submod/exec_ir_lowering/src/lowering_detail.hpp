#pragma once

#include <concepts>
#include <cstdint>
#include <expected>
#include <map>
#include <optional>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptxsim/exec_ir_lowering/exec_ir_lowering.hpp>

namespace ptxsim::exec_ir_lowering::detail {

/** @brief Maps bound frontend register identities to function-local slots. */
struct RegisterLayout {
  /** @brief Width of each allocated slot, indexed by its slot value. */
  std::vector<common::RawWidth> widths;
  /** @brief Slot allocated for each symbol and optional array member identity. */
  std::map<std::pair<std::uint32_t, std::optional<std::uint32_t>>,
           common::RegisterSlot>
      slots;
};

/** @brief Maps resolved label symbols to function-local branch targets. */
using LabelTable = std::unordered_map<std::uint32_t, common::ProgramCounter>;

/**
 * @brief Immutable per-instruction bindings used by leaf lowering conversions.
 */
struct BindingContext {
  /** @brief Function-local register slots; owned by the enclosing lower call. */
  const RegisterLayout& registers;
  /** @brief Function-local labels; owned by the enclosing lower call. */
  const LabelTable& labels;
  /** @brief Number of executable instructions in the current function body. */
  std::uint32_t body_size;
  /** @brief Dense function index reported in lowering diagnostics. */
  std::uint32_t function_index;
  /** @brief Function-local instruction index reported in lowering diagnostics. */
  std::uint32_t instruction_index;
};

/**
 * @brief Translate a resolved scalar type to its executable raw register width.
 *
 * @return The width represented by @p type, or no value for an invalid type.
 */
[[nodiscard]] auto raw_width_for(ptx_frontend::base::ScalarType type)
    -> std::optional<common::RawWidth>;

/**
 * @brief Translate a frontend register type spelling to its executable width.
 *
 * @return The width represented by @p type, or no value for an unsupported spelling.
 */
[[nodiscard]] auto raw_width_for(std::string_view type)
    -> std::optional<common::RawWidth>;

/** @brief Bind one typed scalar register reference to a local register slot. */
[[nodiscard]] auto bind_register(
    const ptx_frontend::resolved_ir::ResolvedRegisterRef& reference,
    common::RawWidth expected, const BindingContext& context)
    -> std::expected<common::RegisterSlot, LoweringError>;

/** @brief Bind an optional resolved predicate without retaining frontend state. */
[[nodiscard]] auto bind_predicate(
    const std::optional<ptx_frontend::WithLocs<
        ptx_frontend::resolved_ir::ResolvedPredicate>>& predicate,
    const BindingContext& context)
    -> std::expected<std::optional<exec_ir::Predicate>, LoweringError>;

/** @brief Bind a resolved b32 register-or-immediate operand. */
[[nodiscard]] auto bind_b32_operand(
    const ptx_frontend::resolved_ir::RegOrImm& operand,
    const BindingContext& context)
    -> std::expected<exec_ir::B32Operand, LoweringError>;

/** @brief Bind an offset-free b64 register address. */
[[nodiscard]] auto bind_b64_address(
    const ptx_frontend::resolved_ir::ResolvedAddress& address,
    const BindingContext& context)
    -> std::expected<common::RegisterSlot, LoweringError>;

/** @brief Bind a direct resolved branch label to a function-local PC. */
[[nodiscard]] auto bind_label(
    const ptx_frontend::resolved_ir::ResolvedBranchTarget& target,
    const BindingContext& context)
    -> std::expected<common::ProgramCounter, LoweringError>;

/** @brief Construct an unsupported-form diagnostic at the current instruction. */
[[nodiscard]] auto unsupported_form(const BindingContext& context)
    -> std::unexpected<LoweringError>;

/** @brief Construct an unsupported-leaf diagnostic at the current instruction. */
[[nodiscard]] auto unsupported_operand(const BindingContext& context)
    -> std::unexpected<LoweringError>;

/**
 * @brief Bind the small common leaf subset required by executable scalar IR.
 *
 * Unmodeled frontend leaf categories deliberately return structured failure so
 * generated topology remains complete without retaining frontend ownership.
 */
template <typename Target, typename Source>
  requires std::movable<Target>
[[nodiscard]] auto bind_operand(const Source& source,
                                const BindingContext& context)
    -> std::expected<Target, LoweringError> {
  using SourceValue = std::remove_cvref_t<Source>;
  if constexpr (std::same_as<Target, common::RegisterSlot> &&
                std::same_as<SourceValue,
                             ptx_frontend::resolved_ir::ResolvedRegisterRef>) {
    if (!source.declared_type) {
      return unsupported_operand(context);
    }
    const auto width = raw_width_for(*source.declared_type);
    return width ? bind_register(source, *width, context)
                 : unsupported_operand(context);
  } else if constexpr (std::same_as<Target, exec_ir::ScalarOperand> &&
                       std::same_as<SourceValue,
                                    ptx_frontend::resolved_ir::RegOrImm>) {
    return bind_b32_operand(source, context);
  } else if constexpr (std::same_as<Target, exec_ir::MovSource> &&
                       std::same_as<
                           SourceValue,
                           ptx_frontend::resolved_ir::ResolvedMovSource>) {
    if (const auto* register_ref =
            std::get_if<ptx_frontend::resolved_ir::ResolvedRegisterRef>(
                &source)) {
      if (!register_ref->declared_type)
        return unsupported_operand(context);
      const auto width = raw_width_for(*register_ref->declared_type);
      if (!width)
        return unsupported_operand(context);
      const auto slot = bind_register(*register_ref, *width, context);
      if (!slot)
        return std::unexpected(slot.error());
      return exec_ir::MovSource{*slot};
    }
    if (const auto* special_register =
            std::get_if<ptx_frontend::resolved_ir::ResolvedSpecialRegisterRef>(
                &source);
        special_register != nullptr &&
        special_register->id.kind ==
            ptx_frontend::base::SpecialRegisterKind::Tid &&
        special_register->id.index == 0U && special_register->component &&
        *special_register->component ==
            ptx_frontend::base::VectorComponent::X) {
      return exec_ir::MovSource{exec_ir::SpecialRegisterRef{
          .id = exec_ir::kThreadIdSpecialRegister,
          .component = 0U,
      }};
    }
    return unsupported_operand(context);
  } else if constexpr (std::same_as<Target, common::ProgramCounter> &&
                       std::same_as<
                           SourceValue,
                           ptx_frontend::resolved_ir::ResolvedBranchTarget>) {
    return bind_label(source, context);
  } else if constexpr (std::same_as<Target, exec_ir::Address> &&
                       std::same_as<
                           SourceValue,
                           ptx_frontend::resolved_ir::ResolvedAddress>) {
    const auto base = bind_b64_address(source, context);
    if (!base)
      return std::unexpected(base.error());
    return exec_ir::Address{*base};
  } else {
    return unsupported_operand(context);
  }
}

}  // namespace ptxsim::exec_ir_lowering::detail
