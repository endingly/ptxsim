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

auto scalar_type_for(std::string_view type)
    -> std::optional<ptx_frontend::base::ScalarType> {
  if (type == ".pred")
    return ScalarType::Pred;
  if (type == ".u8")
    return ScalarType::U8;
  if (type == ".u8x4")
    return ScalarType::U8x4;
  if (type == ".u16")
    return ScalarType::U16;
  if (type == ".u16x2")
    return ScalarType::U16x2;
  if (type == ".u32")
    return ScalarType::U32;
  if (type == ".u64")
    return ScalarType::U64;
  if (type == ".s8")
    return ScalarType::S8;
  if (type == ".s8x4")
    return ScalarType::S8x4;
  if (type == ".s16")
    return ScalarType::S16;
  if (type == ".s16x2")
    return ScalarType::S16x2;
  if (type == ".s32")
    return ScalarType::S32;
  if (type == ".s64")
    return ScalarType::S64;
  if (type == ".b8")
    return ScalarType::B8;
  if (type == ".b16")
    return ScalarType::B16;
  if (type == ".b32")
    return ScalarType::B32;
  if (type == ".b64")
    return ScalarType::B64;
  if (type == ".b128")
    return ScalarType::B128;
  if (type == ".f16")
    return ScalarType::F16;
  if (type == ".f16x2")
    return ScalarType::F16x2;
  if (type == ".f32")
    return ScalarType::F32;
  if (type == ".f32x2")
    return ScalarType::F32x2;
  if (type == ".f64")
    return ScalarType::F64;
  if (type == ".bf16")
    return ScalarType::BF16;
  if (type == ".bf16x2")
    return ScalarType::BF16x2;
  if (type == ".e4m3")
    return ScalarType::E4m3;
  if (type == ".e4m3x2")
    return ScalarType::E4m3x2;
  if (type == ".e5m2")
    return ScalarType::E5m2;
  if (type == ".e5m2x2")
    return ScalarType::E5m2x2;
  if (type == ".tf32")
    return ScalarType::TF32;
  return std::nullopt;
}

auto raw_width_for(std::string_view type) -> std::optional<common::RawWidth> {
  const auto scalar_type = scalar_type_for(type);
  return scalar_type ? raw_width_for(*scalar_type) : std::nullopt;
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
      found->second.components != 1U ||
      context.registers.widths[found->second.first.value()] != expected) {
    return binding_error(LoweringErrorCode::malformed_resolved_ir, context,
                         reference.symbol_id->value);
  }
  return found->second.first;
}

auto bind_vector_register(
    const ptx_frontend::resolved_ir::ResolvedVectorRegisterRef& reference,
    const BindingContext& context)
    -> std::expected<exec_ir::VectorRegisterRef, LoweringError> {
  const auto& base = reference.register_ref;
  if (!base.symbol_id || base.vector_width != 4U || !base.declared_type ||
      raw_width_for(*base.declared_type) != common::RawWidth::b32 ||
      base.register_class != ResolvedRegisterClass::General) {
    return binding_error(LoweringErrorCode::malformed_resolved_ir, context);
  }
  const auto found = context.registers.slots.find(
      std::pair{base.symbol_id->value, base.parameterized_index});
  if (found == context.registers.slots.end() || found->second.components != 4U)
    return binding_error(LoweringErrorCode::malformed_resolved_ir, context,
                         base.symbol_id->value);
  const auto first = found->second.first.value();
  if (first > context.registers.widths.size() ||
      context.registers.widths.size() - first < 4U)
    return binding_error(LoweringErrorCode::malformed_resolved_ir, context);
  for (std::uint32_t component = 0; component < 4U; ++component) {
    if (context.registers.widths[first + component] != common::RawWidth::b32)
      return binding_error(LoweringErrorCode::malformed_resolved_ir, context);
  }
  return exec_ir::VectorRegisterRef{found->second.first};
}

auto bind_special_register(
    const ptx_frontend::resolved_ir::ResolvedSpecialRegisterRef& reference,
    const BindingContext& context)
    -> std::expected<exec_ir::SpecialRegisterRef, LoweringError> {
  using Kind = ptx_frontend::base::SpecialRegisterKind;
  std::optional<common::SpecialRegisterId> id;
  switch (reference.id.kind) {
    case Kind::Tid: id = exec_ir::kThreadIdSpecialRegister; break;
    case Kind::NTid: id = exec_ir::kThreadCountSpecialRegister; break;
    case Kind::CtaId: id = exec_ir::kCtaIdSpecialRegister; break;
    case Kind::NCtaId: id = exec_ir::kCtaCountSpecialRegister; break;
    case Kind::LaneId: id = exec_ir::kLaneIdSpecialRegister; break;
    case Kind::WarpId: id = exec_ir::kWarpIdSpecialRegister; break;
    case Kind::GridId: id = exec_ir::kGridIdSpecialRegister; break;
    case Kind::LaneMaskEq: id = exec_ir::kLaneMaskEqSpecialRegister; break;
    case Kind::LaneMaskLe: id = exec_ir::kLaneMaskLeSpecialRegister; break;
    case Kind::LaneMaskLt: id = exec_ir::kLaneMaskLtSpecialRegister; break;
    case Kind::LaneMaskGe: id = exec_ir::kLaneMaskGeSpecialRegister; break;
    case Kind::LaneMaskGt: id = exec_ir::kLaneMaskGtSpecialRegister; break;
    default: return unsupported_operand(context);
  }
  const bool vector = reference.id.kind == Kind::Tid ||
                      reference.id.kind == Kind::NTid ||
                      reference.id.kind == Kind::CtaId ||
                      reference.id.kind == Kind::NCtaId;
  if (!vector) {
    if (reference.component || reference.id.index != 0U)
      return binding_error(LoweringErrorCode::malformed_resolved_ir, context);
    return exec_ir::SpecialRegisterRef{*id, std::nullopt};
  }
  if (reference.id.index != 0U || !reference.component ||
      static_cast<std::uint8_t>(*reference.component) > 2U)
    return binding_error(LoweringErrorCode::malformed_resolved_ir, context);
  return exec_ir::SpecialRegisterRef{
      *id, static_cast<std::uint8_t>(*reference.component)};
}

auto bind_vector_special_register(
    const ptx_frontend::resolved_ir::ResolvedVectorSpecialRegisterRef& reference,
    const BindingContext& context)
    -> std::expected<exec_ir::VectorSpecialRegisterRef, LoweringError> {
  const auto scalar = bind_special_register(
      {.spelling = reference.spelling,
       .id = reference.id,
       .component = ptx_frontend::base::VectorComponent::X}, context);
  if (!scalar)
    return std::unexpected(scalar.error());
  return exec_ir::VectorSpecialRegisterRef{scalar->id};
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

auto bind_scalar_operand(const ptx_frontend::resolved_ir::RegOrImm& operand,
                         const BindingContext& context)
    -> std::expected<exec_ir::ScalarOperand, LoweringError> {
  if (const auto* reference = std::get_if<ResolvedRegisterRef>(&operand)) {
    if (!reference->declared_type)
      return binding_error(LoweringErrorCode::malformed_resolved_ir, context);
    const auto width = raw_width_for(*reference->declared_type);
    if (!width || *width == common::RawWidth::pred)
      return unsupported_operand(context);
    const auto slot = bind_register(*reference, *width, context);
    if (!slot)
      return std::unexpected(slot.error());
    return *slot;
  }
  const auto& immediate = std::get<ResolvedImmediate>(operand);
  const auto width = raw_width_for(immediate.type);
  if (!width)
    return unsupported_operand(context);
  // Frontend resolution already encodes signed literals as two's-complement
  // bits and floating literals as their binary representation. Do not negate
  // again using the source-spelling flag or convert through host floating point.
  switch (*width) {
    case common::RawWidth::b8:
      if (immediate.bits <= std::numeric_limits<std::uint8_t>::max())
        return common::RawValue::b8(static_cast<std::uint8_t>(immediate.bits));
      break;
    case common::RawWidth::b16:
      if (immediate.bits <= std::numeric_limits<std::uint16_t>::max())
        return common::RawValue::b16(
            static_cast<std::uint16_t>(immediate.bits));
      break;
    case common::RawWidth::b32:
      if (immediate.bits <= std::numeric_limits<std::uint32_t>::max())
        return common::RawValue::b32(
            static_cast<std::uint32_t>(immediate.bits));
      break;
    case common::RawWidth::b64:
      return common::RawValue::b64(immediate.bits);
    default:
      return unsupported_operand(context);
  }
  return binding_error(LoweringErrorCode::malformed_resolved_ir, context);
}

auto bind_address(const ptx_frontend::resolved_ir::ResolvedAddress& address,
                  const BindingContext& context)
    -> std::expected<exec_ir::Address, LoweringError> {
  std::optional<exec_ir::AddressOffset> offset;
  if (address.offset) {
    using Operator = ptx_frontend::resolved_ir::ResolvedAddressOffsetOperator;
    if ((address.offset->operation != Operator::Add &&
         address.offset->operation != Operator::Subtract) ||
        address.offset->value.type != ScalarType::S64 ||
        address.offset->value.is_negative)
      return unsupported_operand(context);
    offset = exec_ir::AddressOffset{
        address.offset->operation == Operator::Subtract,
        common::RawValue::b64(address.offset->value.bits)};
  }
  if (const auto* base = std::get_if<ResolvedRegisterRef>(&address.base)) {
    const auto width = base->declared_type ? raw_width_for(*base->declared_type)
                                           : std::nullopt;
    if (width != common::RawWidth::b32 && width != common::RawWidth::b64)
      return unsupported_operand(context);
    const auto slot = bind_register(*base, *width, context);
    if (!slot) {
      return std::unexpected(slot.error());
    }
    return exec_ir::Address{*slot, offset};
  }
  if (const auto* base = std::get_if<ResolvedImmediate>(&address.base)) {
    const auto width = raw_width_for(base->type);
    if ((width != common::RawWidth::b32 && width != common::RawWidth::b64) ||
        (width == common::RawWidth::b32 &&
         base->bits > std::numeric_limits<std::uint32_t>::max()))
      return unsupported_operand(context);
    return exec_ir::Address{common::RawValue::b64(base->bits), offset};
  }
  const auto* parameter =
      std::get_if<ptx_frontend::resolved_ir::ResolvedSymbolRef>(&address.base);
  if (parameter == nullptr) {
    return unsupported_operand(context);
  }
  if (!parameter->symbol_id) {
    return binding_error(LoweringErrorCode::malformed_resolved_ir, context);
  }
  if (parameter->declaration_kind ==
          ptx_frontend::binding::SymbolKind::Variable &&
      !parameter->parameterized_index) {
    const auto found =
        context.storage_symbols.find(parameter->symbol_id->value);
    if (found == context.storage_symbols.end() ||
        parameter->declared_type != found->second.type ||
        parameter->declaration_state_space != found->second.space ||
        parameter->address_state_space != found->second.space ||
        !parameter->address_alignment ||
        *parameter->address_alignment != found->second.alignment ||
        (found->second.owner_function &&
         *found->second.owner_function !=
             common::FunctionId{context.function_index})) {
      return binding_error(LoweringErrorCode::malformed_resolved_ir, context,
                           parameter->symbol_id->value);
    }
    return exec_ir::Address{
        exec_ir::SymbolRef{common::SymbolId{parameter->symbol_id->value}},
        offset};
  }
  if (parameter->parameterized_index ||
      parameter->declaration_kind !=
          ptx_frontend::binding::SymbolKind::InputParameter ||
      parameter->declaration_state_space !=
          ptx_frontend::syntax_ast::AstStateSpace::Parameter ||
      parameter->address_state_space !=
          ptx_frontend::syntax_ast::AstStateSpace::Parameter) {
    return unsupported_operand(context);
  }
  const auto found = context.entry_parameters.find(parameter->symbol_id->value);
  if (found == context.entry_parameters.end()) {
    return context.function_is_entry
               ? binding_error(LoweringErrorCode::malformed_resolved_ir,
                               context, parameter->symbol_id->value)
               : unsupported_operand(context);
  }
  if (parameter->declared_type != found->second.type ||
      !parameter->address_alignment ||
      *parameter->address_alignment != found->second.layout.alignment ||
      found->second.layout.offset > std::numeric_limits<std::uint64_t>::max()) {
    return binding_error(LoweringErrorCode::malformed_resolved_ir, context,
                         parameter->symbol_id->value);
  }
  return exec_ir::Address{common::RawValue::b64(static_cast<std::uint64_t>(
                              found->second.layout.offset)),
                          offset};
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
