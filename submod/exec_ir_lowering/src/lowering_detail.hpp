#pragma once

#include <concepts>
#include <cstddef>
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

/** @brief Contiguous scalar slots allocated for one declared register member. */
struct RegisterBinding {
  /** @brief First component slot, owned by the function's register frame. */
  common::RegisterSlot first;
  /** @brief Number of component slots: one for a scalar, two or four for a vector. */
  std::uint8_t components;
};

/** @brief Maps bound frontend register identities to function-local slots. */
struct RegisterLayout {
  /** @brief Width of each allocated slot, indexed by its slot value. */
  std::vector<common::RawWidth> widths;
  /** @brief Slot allocated for each symbol and optional array member identity. */
  std::map<std::pair<std::uint32_t, std::optional<std::uint32_t>>,
           RegisterBinding>
      slots;
};

/** @brief Maps resolved label symbols to function-local branch targets. */
using LabelTable = std::unordered_map<std::uint32_t, common::ProgramCounter>;

/** @brief ABI record used to bind one entry-parameter symbol to its slot. */
struct EntryParameterBinding {
  /** @brief Immutable byte layout for this parameter in its entry argument blob. */
  exec_ir::EntryParameterLayout layout;
  /** @brief Exact scalar declaration type required of resolved address references. */
  ptx_frontend::base::ScalarType type;
};

/** @brief Maps each entry-parameter symbol identity to its ABI binding record. */
using EntryParameterTable =
    std::unordered_map<std::uint32_t, EntryParameterBinding>;

/**
 * @brief Immutable per-instruction bindings used by leaf lowering conversions.
 */
struct BindingContext {
  /** @brief Function-local register slots; owned by the enclosing lower call. */
  const RegisterLayout& registers;
  /** @brief Function-local labels; owned by the enclosing lower call. */
  const LabelTable& labels;
  /** @brief ABI bindings for entry parameters, owned by the enclosing lower call. */
  const EntryParameterTable& entry_parameters;
  /** @brief Whether this function owns an entry ABI rather than device parameters. */
  bool function_is_entry;
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

/**
 * @brief Translate a normalized PTX type spelling to its exact scalar type.
 *
 * @return The scalar type represented by @p type, or no value for unsupported
 * declaration spellings.
 */
[[nodiscard]] auto scalar_type_for(std::string_view type)
    -> std::optional<ptx_frontend::base::ScalarType>;

/** @brief Bind one typed scalar register reference to a local register slot. */
[[nodiscard]] auto bind_register(
    const ptx_frontend::resolved_ir::ResolvedRegisterRef& reference,
    common::RawWidth expected, const BindingContext& context)
    -> std::expected<common::RegisterSlot, LoweringError>;

/** @brief Bind a declared four-component register to contiguous executable slots. */
[[nodiscard]] auto bind_vector_register(
    const ptx_frontend::resolved_ir::ResolvedVectorRegisterRef& reference,
    const BindingContext& context)
    -> std::expected<exec_ir::VectorRegisterRef, LoweringError>;

/** @brief Translate a supported topology identity without retaining frontend IDs. */
[[nodiscard]] auto bind_special_register(
    const ptx_frontend::resolved_ir::ResolvedSpecialRegisterRef& reference,
    const BindingContext& context)
    -> std::expected<exec_ir::SpecialRegisterRef, LoweringError>;

/** @brief Bind one vector topology register, retaining its executable identity. */
[[nodiscard]] auto bind_vector_special_register(
    const ptx_frontend::resolved_ir::ResolvedVectorSpecialRegisterRef& reference,
    const BindingContext& context)
    -> std::expected<exec_ir::VectorSpecialRegisterRef, LoweringError>;

/** @brief Bind an optional resolved predicate without retaining frontend state. */
[[nodiscard]] auto bind_predicate(
    const std::optional<ptx_frontend::WithLocs<
        ptx_frontend::resolved_ir::ResolvedPredicate>>& predicate,
    const BindingContext& context)
    -> std::expected<std::optional<exec_ir::Predicate>, LoweringError>;

/** @brief Preserve the declared width and resolved bits of a scalar operand. */
[[nodiscard]] auto bind_scalar_operand(
    const ptx_frontend::resolved_ir::RegOrImm& operand,
    const BindingContext& context)
    -> std::expected<exec_ir::ScalarOperand, LoweringError>;

/** @brief Bind a numeric or entry-parameter address, retaining its byte offset. */
[[nodiscard]] auto bind_address(
    const ptx_frontend::resolved_ir::ResolvedAddress& address,
    const BindingContext& context)
    -> std::expected<exec_ir::Address, LoweringError>;

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
    return bind_scalar_operand(source, context);
  } else if constexpr (std::same_as<Target, common::RawValue> &&
                       std::same_as<
                           SourceValue,
                           ptx_frontend::resolved_ir::ResolvedImmediate>) {
    const auto value = bind_scalar_operand(
        ptx_frontend::resolved_ir::RegOrImm{source}, context);
    if (!value)
      return std::unexpected(value.error());
    return std::get<common::RawValue>(*value);
  } else if constexpr (std::same_as<Target, exec_ir::Predicate> &&
                       std::same_as<
                           SourceValue,
                           ptx_frontend::resolved_ir::ResolvedPredicate>) {
    const auto slot =
        bind_register(source.register_ref, common::RawWidth::pred, context);
    if (!slot)
      return std::unexpected(slot.error());
    return exec_ir::Predicate{*slot, source.negated};
  } else if constexpr (std::same_as<Target, exec_ir::PredicatePair> &&
                       std::same_as<
                           SourceValue,
                           ptx_frontend::resolved_ir::ResolvedPredicatePair>) {
    const auto first = bind_operand<exec_ir::Predicate>(source.first, context);
    if (!first)
      return std::unexpected(first.error());
    const auto second =
        bind_operand<exec_ir::Predicate>(source.second, context);
    if (!second)
      return std::unexpected(second.error());
    return exec_ir::PredicatePair{*first, *second};
  } else if constexpr (std::same_as<Target, exec_ir::RegisterVector> &&
                       std::same_as<
                           SourceValue,
                           ptx_frontend::resolved_ir::ResolvedRegisterVector>) {
    exec_ir::RegisterVector result;
    result.elements.reserve(source.elements.size());
    for (const auto& element : source.elements) {
      if (!element) {
        result.elements.push_back(std::nullopt);
        continue;
      }
      const auto slot = bind_operand<common::RegisterSlot>(*element, context);
      if (!slot)
        return std::unexpected(slot.error());
      result.elements.push_back(*slot);
    }
    return result;
  } else if constexpr (std::same_as<Target, exec_ir::VectorRegisterRef> &&
                       std::same_as<SourceValue,
                           ptx_frontend::resolved_ir::ResolvedVectorRegisterRef>) {
    return bind_vector_register(source, context);
  } else if constexpr (std::same_as<Target, exec_ir::VectorSpecialRegisterRef> &&
                       std::same_as<SourceValue,
                           ptx_frontend::resolved_ir::ResolvedVectorSpecialRegisterRef>) {
    return bind_vector_special_register(source, context);
  } else if constexpr (std::same_as<Target, exec_ir::PredicateSource> &&
                       std::same_as<SourceValue,
                           ptx_frontend::resolved_ir::ResolvedPredicateSource>) {
    if (const auto* predicate =
            std::get_if<ptx_frontend::resolved_ir::ResolvedPredicate>(&source)) {
      const auto value = bind_operand<exec_ir::Predicate>(*predicate, context);
      if (!value)
        return std::unexpected(value.error());
      return exec_ir::PredicateSource{*value};
    }
    const auto value = bind_special_register(
        std::get<ptx_frontend::resolved_ir::ResolvedSpecialRegisterRef>(source),
        context);
    if (!value)
      return std::unexpected(value.error());
    return exec_ir::PredicateSource{*value};
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
                &source)) {
      const auto value = bind_special_register(*special_register, context);
      if (!value)
        return std::unexpected(value.error());
      return exec_ir::MovSource{*value};
    }
    if (const auto* immediate =
            std::get_if<ptx_frontend::resolved_ir::ResolvedImmediate>(&source)) {
      const auto value = bind_operand<common::RawValue>(*immediate, context);
      if (!value)
        return std::unexpected(value.error());
      return exec_ir::MovSource{*value};
    }
    if (const auto* address =
            std::get_if<ptx_frontend::resolved_ir::ResolvedAddress>(&source)) {
      const auto value = bind_address(*address, context);
      if (!value)
        return std::unexpected(value.error());
      return exec_ir::MovSource{*value};
    }
    if (const auto* symbol =
            std::get_if<ptx_frontend::resolved_ir::ResolvedSymbolRef>(&source)) {
      const auto value = bind_address(
          ptx_frontend::resolved_ir::ResolvedAddress{.base = *symbol}, context);
      if (!value)
        return std::unexpected(value.error());
      return exec_ir::MovSource{*value};
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
    return bind_address(source, context);
  } else {
    return unsupported_operand(context);
  }
}

}  // namespace ptxsim::exec_ir_lowering::detail
