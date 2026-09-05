#include "lowering_detail.hpp"

#include <limits>
#include <utility>

namespace ptxsim::exec_ir_lowering::detail {
namespace {

using ptx_frontend::base::ScalarType;
using ptx_frontend::resolved_ir::ResolvedImmediate;
using ptx_frontend::resolved_ir::ResolvedRegisterClass;
using ptx_frontend::resolved_ir::ResolvedRegisterRef;

/** @brief Construct a lowering error at the current binding location. */
[[nodiscard]] auto binding_error(
    LoweringErrorCode code, const BindingContext& context,
    std::optional<std::uint32_t> symbol = std::nullopt)
    -> std::unexpected<LoweringError> {
  return std::unexpected(LoweringError{code, context.function_index,
                                       context.instruction_index, symbol,
                                       std::nullopt});
}

}  // namespace

auto raw_width_for(ptx_frontend::base::ScalarType type)
    -> std::optional<common::RawWidth> {
  switch (type) {
    case ScalarType::Pred:
      return common::RawWidth::pred;
    case ScalarType::U8:
    case ScalarType::S8:
    case ScalarType::B8:
    case ScalarType::E4m3:
    case ScalarType::E5m2:
      return common::RawWidth::b8;
    case ScalarType::U16:
    case ScalarType::S16:
    case ScalarType::B16:
    case ScalarType::F16:
    case ScalarType::BF16:
      return common::RawWidth::b16;
    case ScalarType::U8x4:
    case ScalarType::U16x2:
    case ScalarType::U32:
    case ScalarType::S8x4:
    case ScalarType::S16x2:
    case ScalarType::S32:
    case ScalarType::B32:
    case ScalarType::F16x2:
    case ScalarType::F32:
    case ScalarType::BF16x2:
    case ScalarType::E4m3x2:
    case ScalarType::E5m2x2:
    case ScalarType::TF32:
      return common::RawWidth::b32;
    case ScalarType::U64:
    case ScalarType::S64:
    case ScalarType::B64:
    case ScalarType::F32x2:
    case ScalarType::F64:
      return common::RawWidth::b64;
    case ScalarType::B128:
      return common::RawWidth::b128;
    case ScalarType::Invalid:
      return std::nullopt;
  }
  return std::nullopt;
}

auto raw_width_for(std::string_view type) -> std::optional<common::RawWidth> {
  if (type == ".pred")
    return common::RawWidth::pred;
  if (type == ".u8" || type == ".s8" || type == ".b8" || type == ".e4m3" ||
      type == ".e5m2")
    return common::RawWidth::b8;
  if (type == ".u16" || type == ".s16" || type == ".b16" || type == ".f16" ||
      type == ".bf16")
    return common::RawWidth::b16;
  if (type == ".u8x4" || type == ".u16x2" || type == ".u32" ||
      type == ".s8x4" || type == ".s16x2" || type == ".s32" || type == ".b32" ||
      type == ".f16x2" || type == ".f32" || type == ".bf16x2" ||
      type == ".e4m3x2" || type == ".e5m2x2" || type == ".tf32")
    return common::RawWidth::b32;
  if (type == ".u64" || type == ".s64" || type == ".b64" || type == ".f32x2" ||
      type == ".f64")
    return common::RawWidth::b64;
  if (type == ".b128")
    return common::RawWidth::b128;
  return std::nullopt;
}

auto bind_register(const ResolvedRegisterRef& reference,
                   common::RawWidth expected, const BindingContext& context)
    -> std::expected<common::RegisterSlot, LoweringError> {
  if (!reference.symbol_id || !reference.declared_type ||
      (reference.vector_width && *reference.vector_width != 1U) ||
      (expected == common::RawWidth::pred &&
       reference.register_class != ResolvedRegisterClass::Predicate) ||
      (expected != common::RawWidth::pred &&
       reference.register_class != ResolvedRegisterClass::General)) {
    return binding_error(
        LoweringErrorCode::malformed_resolved_ir, context,
        reference.symbol_id
            ? std::optional<std::uint32_t>{reference.symbol_id->value}
            : std::nullopt);
  }
  const auto width = raw_width_for(*reference.declared_type);
  if (!width || *width != expected) {
    return binding_error(LoweringErrorCode::malformed_resolved_ir, context,
                         reference.symbol_id->value);
  }
  const auto found = context.registers.slots.find(
      std::pair{reference.symbol_id->value, reference.parameterized_index});
  if (found == context.registers.slots.end() ||
      context.registers.widths[found->second.value()] != expected) {
    return binding_error(LoweringErrorCode::malformed_resolved_ir, context,
                         reference.symbol_id->value);
  }
  return found->second;
}

auto bind_predicate(
    const std::optional<ptx_frontend::WithLocs<
        ptx_frontend::resolved_ir::ResolvedPredicate>>& predicate,
    const BindingContext& context)
    -> std::expected<std::optional<exec_ir::Predicate>, LoweringError> {
  if (!predicate)
    return std::nullopt;
  const auto slot = bind_register(predicate->value.register_ref,
                                  common::RawWidth::pred, context);
  if (!slot)
    return std::unexpected(slot.error());
  return exec_ir::Predicate{*slot, predicate->value.negated};
}

auto bind_b32_operand(const ptx_frontend::resolved_ir::RegOrImm& operand,
                      const BindingContext& context)
    -> std::expected<exec_ir::B32Operand, LoweringError> {
  if (const auto* reference = std::get_if<ResolvedRegisterRef>(&operand)) {
    const auto slot = bind_register(*reference, common::RawWidth::b32, context);
    if (!slot)
      return std::unexpected(slot.error());
    return *slot;
  }
  const auto& immediate = std::get<ResolvedImmediate>(operand);
  if (immediate.type != ScalarType::U32 ||
      immediate.bits > std::numeric_limits<std::uint32_t>::max()) {
    return binding_error(LoweringErrorCode::unsupported_operand, context);
  }
  return common::RawValue::b32(static_cast<std::uint32_t>(immediate.bits));
}

auto bind_b64_address(const ptx_frontend::resolved_ir::ResolvedAddress& address,
                      const BindingContext& context)
    -> std::expected<exec_ir::Address, LoweringError> {
  if (address.offset) {
    return binding_error(LoweringErrorCode::unsupported_operand, context);
  }
  if (const auto* base = std::get_if<ResolvedRegisterRef>(&address.base)) {
    const auto slot = bind_register(*base, common::RawWidth::b64, context);
    if (!slot) {
      return std::unexpected(slot.error());
    }
    return exec_ir::Address{*slot};
  }
  const auto* parameter =
      std::get_if<ptx_frontend::resolved_ir::ResolvedSymbolRef>(&address.base);
  if (parameter == nullptr || !parameter->symbol_id ||
      !context.entry_parameter_symbol ||
      parameter->symbol_id->value != *context.entry_parameter_symbol ||
      parameter->parameterized_index ||
      parameter->declaration_kind !=
          ptx_frontend::binding::SymbolKind::InputParameter ||
      parameter->declaration_state_space !=
          ptx_frontend::syntax_ast::AstStateSpace::Parameter ||
      parameter->address_state_space !=
          ptx_frontend::syntax_ast::AstStateSpace::Parameter ||
      parameter->declared_type != ScalarType::U32) {
    return binding_error(LoweringErrorCode::unsupported_operand, context);
  }
  return exec_ir::Address{common::RawValue::b64(std::uint64_t{0})};
}

auto bind_label(const ptx_frontend::resolved_ir::ResolvedBranchTarget& target,
                const BindingContext& context)
    -> std::expected<common::ProgramCounter, LoweringError> {
  if (!target.symbol_id) {
    return binding_error(LoweringErrorCode::malformed_resolved_ir, context);
  }
  const auto found = context.labels.find(target.symbol_id->value);
  if (found == context.labels.end()) {
    return binding_error(LoweringErrorCode::malformed_resolved_ir, context,
                         target.symbol_id->value);
  }
  if (found->second.value() >= context.body_size) {
    return binding_error(LoweringErrorCode::invalid_branch_target, context,
                         target.symbol_id->value);
  }
  return found->second;
}

auto unsupported_form(const BindingContext& context)
    -> std::unexpected<LoweringError> {
  return binding_error(LoweringErrorCode::unsupported_form, context);
}

auto unsupported_operand(const BindingContext& context)
    -> std::unexpected<LoweringError> {
  return binding_error(LoweringErrorCode::unsupported_operand, context);
}

}  // namespace ptxsim::exec_ir_lowering::detail
