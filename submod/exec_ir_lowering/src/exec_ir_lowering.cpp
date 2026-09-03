#include <ptxsim/exec_ir_lowering/exec_ir_lowering.hpp>

#include <concepts>
#include <limits>
#include <map>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>

namespace ptxsim::exec_ir_lowering {
namespace {

using ptx_frontend::base::ScalarType;
using ptx_frontend::binding::ScopeId;
using ptx_frontend::binding::Symbol;
using ptx_frontend::binding::SymbolKind;
using ptx_frontend::binding::SymbolTable;
using ptx_frontend::resolved_ir::Add;
using ptx_frontend::resolved_ir::Bar;
using ptx_frontend::resolved_ir::Bra;
using ptx_frontend::resolved_ir::Exit;
using ptx_frontend::resolved_ir::Ld;
using ptx_frontend::resolved_ir::MemoryStateSpace;
using ptx_frontend::resolved_ir::Mov;
using ptx_frontend::resolved_ir::RegOrImm;
using ptx_frontend::resolved_ir::ResolvedBranchTarget;
using ptx_frontend::resolved_ir::ResolvedImmediate;
using ptx_frontend::resolved_ir::ResolvedInstruction;
using ptx_frontend::resolved_ir::ResolvedPredicate;
using ptx_frontend::resolved_ir::ResolvedRegisterClass;
using ptx_frontend::resolved_ir::ResolvedRegisterRef;
using ptx_frontend::resolved_ir::St;

struct RegisterLayout {
  std::vector<common::RawWidth> widths;
  std::map<std::pair<std::uint32_t, std::optional<std::uint32_t>>,
           common::RegisterSlot>
      slots;
};

[[nodiscard]] auto error(
    LoweringErrorCode code,
    std::optional<std::uint32_t> function = std::nullopt,
    std::optional<std::uint32_t> instruction = std::nullopt,
    std::optional<std::uint32_t> symbol = std::nullopt,
    std::optional<exec_ir::ProgramError> program_error = std::nullopt)
    -> std::unexpected<LoweringError> {
  return std::unexpected(
      LoweringError{code, function, instruction, symbol, program_error});
}

[[nodiscard]] constexpr auto raw_width(ScalarType type)
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

[[nodiscard]] auto raw_width(std::string_view type)
    -> std::optional<common::RawWidth> {
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

[[nodiscard]] auto descendant_of(const SymbolTable& symbols, ScopeId scope,
                                 ScopeId function_scope) -> bool {
  const auto& scopes = symbols.scopes();
  for (std::size_t depth = 0; depth <= scopes.size(); ++depth) {
    if (scope.value == function_scope.value)
      return true;
    if (scope.value >= scopes.size())
      return false;
    const auto parent = scopes[scope.value].parent;
    if (!parent)
      return false;
    scope = *parent;
  }
  return false;
}

[[nodiscard]] auto register_layout(const SymbolTable& symbols,
                                   const Symbol& function_symbol,
                                   std::uint32_t function, bool empty_body)
    -> std::expected<RegisterLayout, LoweringError> {
  if (!function_symbol.owned_scope) {
    if (empty_body)
      return RegisterLayout{};
    return error(LoweringErrorCode::malformed_resolved_ir, function,
                 std::nullopt, function_symbol.id.value);
  }
  if (function_symbol.owned_scope->value >= symbols.scopes().size()) {
    return error(LoweringErrorCode::malformed_resolved_ir, function,
                 std::nullopt, function_symbol.id.value);
  }

  RegisterLayout result;
  for (const Symbol& symbol : symbols.symbols()) {
    if (!descendant_of(symbols, symbol.scope, *function_symbol.owned_scope) ||
        symbol.kind != SymbolKind::Variable || !symbol.state_space ||
        *symbol.state_space !=
            ptx_frontend::syntax_ast::AstStateSpace::Register) {
      continue;
    }
    if (symbol.vector_width && *symbol.vector_width != 1U) {
      return error(LoweringErrorCode::unsupported_type, function, std::nullopt,
                   symbol.id.value);
    }
    if (!symbol.type) {
      return error(LoweringErrorCode::malformed_resolved_ir, function,
                   std::nullopt, symbol.id.value);
    }
    const auto width = raw_width(*symbol.type);
    const auto count = symbol.parameterized_count.value_or(1U);
    constexpr std::uint64_t max_slots =
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) +
        1U;
    const auto slot_count = static_cast<std::uint64_t>(result.widths.size());
    if (!width || count == 0U || slot_count > max_slots ||
        static_cast<std::uint64_t>(count) > max_slots - slot_count) {
      return error(LoweringErrorCode::malformed_resolved_ir, function,
                   std::nullopt, symbol.id.value);
    }
    for (std::uint32_t member = 0; member < count; ++member) {
      const auto index = symbol.parameterized_count
                             ? std::optional<std::uint32_t>{member}
                             : std::nullopt;
      const auto slot = common::RegisterSlot{
          static_cast<std::uint32_t>(result.widths.size())};
      if (!result.slots.emplace(std::pair{symbol.id.value, index}, slot)
               .second) {
        return error(LoweringErrorCode::malformed_resolved_ir, function,
                     std::nullopt, symbol.id.value);
      }
      result.widths.push_back(*width);
    }
  }
  return result;
}

[[nodiscard]] auto slot_for(const RegisterLayout& layout,
                            const ResolvedRegisterRef& reference,
                            common::RawWidth expected, std::uint32_t function,
                            std::uint32_t instruction)
    -> std::expected<common::RegisterSlot, LoweringError> {
  if (!reference.symbol_id || !reference.declared_type ||
      (reference.vector_width && *reference.vector_width != 1U) ||
      (expected == common::RawWidth::pred &&
       reference.register_class != ResolvedRegisterClass::Predicate) ||
      (expected != common::RawWidth::pred &&
       reference.register_class != ResolvedRegisterClass::General)) {
    return error(LoweringErrorCode::malformed_resolved_ir, function,
                 instruction,
                 reference.symbol_id
                     ? std::optional<std::uint32_t>{reference.symbol_id->value}
                     : std::nullopt);
  }
  const auto width = raw_width(*reference.declared_type);
  if (!width || *width != expected) {
    return error(LoweringErrorCode::malformed_resolved_ir, function,
                 instruction, reference.symbol_id->value);
  }
  const auto found = layout.slots.find(
      std::pair{reference.symbol_id->value, reference.parameterized_index});
  if (found == layout.slots.end() ||
      layout.widths[found->second.value()] != expected) {
    return error(LoweringErrorCode::malformed_resolved_ir, function,
                 instruction, reference.symbol_id->value);
  }
  return found->second;
}

[[nodiscard]] auto predicate_for(
    const std::optional<ptx_frontend::WithLocs<ResolvedPredicate>>& predicate,
    const RegisterLayout& layout, std::uint32_t function,
    std::uint32_t instruction)
    -> std::expected<std::optional<exec_ir::Predicate>, LoweringError> {
  if (!predicate)
    return std::nullopt;
  const auto slot = slot_for(layout, predicate->value.register_ref,
                             common::RawWidth::pred, function, instruction);
  if (!slot)
    return std::unexpected(slot.error());
  return exec_ir::Predicate{*slot, predicate->value.negated};
}

[[nodiscard]] auto operand_for(const RegOrImm& operand,
                               const RegisterLayout& layout,
                               std::uint32_t function,
                               std::uint32_t instruction)
    -> std::expected<exec_ir::B32Operand, LoweringError> {
  if (const auto* reference = std::get_if<ResolvedRegisterRef>(&operand)) {
    const auto slot = slot_for(layout, *reference, common::RawWidth::b32,
                               function, instruction);
    if (!slot)
      return std::unexpected(slot.error());
    return *slot;
  }
  const auto& immediate = std::get<ResolvedImmediate>(operand);
  if (immediate.type != ScalarType::U32 ||
      immediate.bits > std::numeric_limits<std::uint32_t>::max()) {
    return error(LoweringErrorCode::unsupported_operand, function, instruction);
  }
  return common::RawValue::b32(static_cast<std::uint32_t>(immediate.bits));
}

/**
 * @brief Bind an offset-free b64 register address without retaining a frontend
 *        address expression.
 */
[[nodiscard]] auto address_for(
    const ptx_frontend::resolved_ir::ResolvedAddress& address,
    const RegisterLayout& layout, std::uint32_t function,
    std::uint32_t instruction)
    -> std::expected<common::RegisterSlot, LoweringError> {
  if (address.offset) {
    return error(LoweringErrorCode::unsupported_operand, function, instruction);
  }
  const auto* base = std::get_if<ResolvedRegisterRef>(&address.base);
  if (base == nullptr) {
    return error(LoweringErrorCode::unsupported_operand, function, instruction);
  }
  return slot_for(layout, *base, common::RawWidth::b64, function, instruction);
}

/** @brief Constrains resolved scalar memory forms carrying modeled controls. */
template <typename Form>
concept DefaultMemoryControls = requires(const Form& form) {
  {
    form.semantics.value == ptx_frontend::base::MemoryConsistency::Omitted
  } -> std::convertible_to<bool>;
  {
    form.scope.value == ptx_frontend::base::MemoryScope::None
  } -> std::convertible_to<bool>;
  { form.mmio.value } -> std::convertible_to<bool>;
  {
    form.cache.value == ptx_frontend::base::CacheOperator::Unspecified
  } -> std::convertible_to<bool>;
};

/**
 * @brief Execution-domain memory controls copied from a supported source form.
 */
struct MemoryControls {
  /** @brief Memory consistency retained for diagnostics and future execution. */
  exec_ir::MemoryConsistency semantics;
  /** @brief Memory visibility scope retained from the source form. */
  exec_ir::MemoryScope scope;
  /** @brief Whether the source requested MMIO behavior. */
  bool mmio;
  /** @brief Cache hint retained from the source form. */
  exec_ir::CacheOperator cache;
};

/** @brief Copy currently supported memory controls into execution-domain types. */
template <DefaultMemoryControls Form>
[[nodiscard]] constexpr auto memory_controls(const Form& form)
    -> std::optional<MemoryControls> {
  if (form.scope.value != ptx_frontend::base::MemoryScope::None ||
      form.mmio.value ||
      form.cache.value != ptx_frontend::base::CacheOperator::Unspecified) {
    return std::nullopt;
  }
  if (form.semantics.value == ptx_frontend::base::MemoryConsistency::Omitted) {
    return MemoryControls{exec_ir::MemoryConsistency::omitted,
                          exec_ir::MemoryScope::none, false,
                          exec_ir::CacheOperator::unspecified};
  }
  if (form.semantics.value == ptx_frontend::base::MemoryConsistency::Weak) {
    return MemoryControls{exec_ir::MemoryConsistency::weak,
                          exec_ir::MemoryScope::none, false,
                          exec_ir::CacheOperator::unspecified};
  }
  return std::nullopt;
}

[[nodiscard]] auto labels_for(
    const ptx_frontend::resolved_ir::ResolvedFunction& function,
    const SymbolTable& symbols, std::uint32_t function_index)
    -> std::expected<std::unordered_map<std::uint32_t, common::ProgramCounter>,
                     LoweringError> {
  std::unordered_map<std::uint32_t, common::ProgramCounter> labels;
  if (function.body.size() > std::numeric_limits<std::uint32_t>::max()) {
    return error(LoweringErrorCode::malformed_resolved_ir, function_index);
  }
  if (function.body.empty() && function.label_positions.empty()) {
    return labels;
  }
  const auto& function_symbol = symbols.symbols()[function.symbol_id.value];
  if (!function_symbol.owned_scope ||
      function_symbol.owned_scope->value >= symbols.scopes().size()) {
    return error(LoweringErrorCode::malformed_resolved_ir, function_index,
                 std::nullopt, function.symbol_id.value);
  }
  for (const auto& label : function.label_positions) {
    if (label.symbol_id.value >= symbols.symbols().size() ||
        symbols.symbols()[label.symbol_id.value].kind != SymbolKind::Label ||
        !descendant_of(symbols, symbols.symbols()[label.symbol_id.value].scope,
                       *function_symbol.owned_scope) ||
        label.instruction_offset > function.body.size() ||
        label.instruction_offset > std::numeric_limits<std::uint32_t>::max() ||
        !labels
             .emplace(label.symbol_id.value,
                      common::ProgramCounter{
                          static_cast<std::uint32_t>(label.instruction_offset)})
             .second) {
      return error(LoweringErrorCode::malformed_resolved_ir, function_index,
                   std::nullopt, label.symbol_id.value);
    }
  }
  return labels;
}

[[nodiscard]] auto branch_for(
    const ResolvedBranchTarget& target, bool uni,
    const std::unordered_map<std::uint32_t, common::ProgramCounter>& labels,
    std::uint32_t body_size, std::uint32_t function, std::uint32_t instruction)
    -> std::expected<exec_ir::Bra::Direct, LoweringError> {
  if (!target.symbol_id) {
    return error(LoweringErrorCode::malformed_resolved_ir, function,
                 instruction);
  }
  const auto found = labels.find(target.symbol_id->value);
  if (found == labels.end()) {
    return error(LoweringErrorCode::malformed_resolved_ir, function,
                 instruction, target.symbol_id->value);
  }
  if (found->second.value() >= body_size) {
    return error(LoweringErrorCode::invalid_branch_target, function,
                 instruction, target.symbol_id->value);
  }
  return exec_ir::Bra::Direct{uni, found->second};
}

[[nodiscard]] auto lower_instruction(
    const ResolvedInstruction& instruction, const RegisterLayout& registers,
    const std::unordered_map<std::uint32_t, common::ProgramCounter>& labels,
    std::uint32_t body_size, std::uint32_t function_index,
    std::uint32_t instruction_index)
    -> std::expected<exec_ir::Instruction, LoweringError> {
  if (const auto* mov = std::get_if<Mov>(&instruction)) {
    const auto* form = std::get_if<Mov::Scalar>(&mov->variant);
    if (form == nullptr) {
      return error(LoweringErrorCode::unsupported_form, function_index,
                   instruction_index);
    }
    if (form->type.value != ScalarType::B32) {
      return error(LoweringErrorCode::unsupported_type, function_index,
                   instruction_index);
    }
    const auto* operands =
        std::get_if<Mov::Scalar::ScalarOperands>(&form->operands);
    if (operands == nullptr) {
      return error(LoweringErrorCode::unsupported_form, function_index,
                   instruction_index);
    }
    const auto* source = std::get_if<ResolvedRegisterRef>(&operands->src.value);
    if (source == nullptr) {
      return error(LoweringErrorCode::unsupported_operand, function_index,
                   instruction_index);
    }
    const auto predicate = predicate_for(mov->execution_predicate, registers,
                                         function_index, instruction_index);
    const auto destination =
        slot_for(registers, operands->dst.value, common::RawWidth::b32,
                 function_index, instruction_index);
    const auto source_slot = slot_for(registers, *source, common::RawWidth::b32,
                                      function_index, instruction_index);
    if (!predicate)
      return std::unexpected(predicate.error());
    if (!destination)
      return std::unexpected(destination.error());
    if (!source_slot)
      return std::unexpected(source_slot.error());
    return exec_ir::Mov{
        *predicate,
        exec_ir::Mov::Variant{exec_ir::Mov::Scalar{
            exec_ir::DataType::b32,
            exec_ir::Mov::Scalar::Operands{exec_ir::Mov::Scalar::ScalarOperands{
                *destination, *source_slot}}}}};
  }

  if (const auto* add = std::get_if<Add>(&instruction)) {
    const auto* form = std::get_if<Add::IntegerNoSat>(&add->variant);
    if (form == nullptr) {
      return error(LoweringErrorCode::unsupported_form, function_index,
                   instruction_index);
    }
    if (form->type.value != ScalarType::U32) {
      return error(LoweringErrorCode::unsupported_type, function_index,
                   instruction_index);
    }
    const auto predicate = predicate_for(add->execution_predicate, registers,
                                         function_index, instruction_index);
    const auto destination =
        slot_for(registers, form->dst.value, common::RawWidth::b32,
                 function_index, instruction_index);
    const auto lhs = operand_for(form->src1.value, registers, function_index,
                                 instruction_index);
    const auto rhs = operand_for(form->src2.value, registers, function_index,
                                 instruction_index);
    if (!predicate)
      return std::unexpected(predicate.error());
    if (!destination)
      return std::unexpected(destination.error());
    if (!lhs)
      return std::unexpected(lhs.error());
    if (!rhs)
      return std::unexpected(rhs.error());
    return exec_ir::Add{*predicate,
                        exec_ir::Add::Variant{exec_ir::Add::IntegerNoSat{
                            exec_ir::DataType::u32, *destination, *lhs, *rhs}}};
  }

  if (const auto* bra = std::get_if<Bra>(&instruction)) {
    const auto* form = std::get_if<Bra::Direct>(&bra->variant);
    if (form == nullptr) {
      return error(LoweringErrorCode::unsupported_form, function_index,
                   instruction_index);
    }
    const auto predicate = predicate_for(bra->execution_predicate, registers,
                                         function_index, instruction_index);
    const auto branch =
        branch_for(form->target.value, form->uni.value, labels, body_size,
                   function_index, instruction_index);
    if (!predicate)
      return std::unexpected(predicate.error());
    if (!branch)
      return std::unexpected(branch.error());
    return exec_ir::Bra{*predicate, exec_ir::Bra::Variant{*branch}};
  }

  if (const auto* ld = std::get_if<Ld>(&instruction)) {
    const auto predicate = predicate_for(ld->execution_predicate, registers,
                                         function_index, instruction_index);
    if (!predicate)
      return std::unexpected(predicate.error());
    const auto lower = [&](const auto& form, exec_ir::AddressSpace space)
        -> std::expected<exec_ir::Instruction, LoweringError> {
      const auto controls = memory_controls(form);
      if (!controls) {
        return error(LoweringErrorCode::unsupported_form, function_index,
                     instruction_index);
      }
      if (space == exec_ir::AddressSpace::generic &&
          controls->semantics != exec_ir::MemoryConsistency::omitted) {
        return error(LoweringErrorCode::unsupported_form, function_index,
                     instruction_index);
      }
      if (form.type.value != ScalarType::U32) {
        return error(LoweringErrorCode::unsupported_type, function_index,
                     instruction_index);
      }
      const auto destination =
          slot_for(registers, form.dst.value, common::RawWidth::b32,
                   function_index, instruction_index);
      const auto address = address_for(form.address.value, registers,
                                       function_index, instruction_index);
      if (!destination)
        return std::unexpected(destination.error());
      if (!address)
        return std::unexpected(address.error());
      if (space == exec_ir::AddressSpace::generic) {
        return exec_ir::Ld{
            *predicate, exec_ir::Ld::Variant{exec_ir::Ld::GenericScalar{
                            controls->semantics, controls->scope,
                            controls->mmio, controls->cache,
                            exec_ir::DataType::u32, *destination, *address}}};
      }
      return exec_ir::Ld{
          *predicate,
          exec_ir::Ld::Variant{exec_ir::Ld::ExplicitScalar{
              space, controls->cache, controls->semantics, controls->scope,
              controls->mmio, exec_ir::DataType::u32, *destination, *address}}};
    };
    if (const auto* form = std::get_if<Ld::GenericScalar>(&ld->variant)) {
      return lower(*form, exec_ir::AddressSpace::generic);
    }
    if (const auto* form = std::get_if<Ld::ExplicitScalar>(&ld->variant)) {
      if (form->state_space.value != MemoryStateSpace::Global) {
        return error(LoweringErrorCode::unsupported_form, function_index,
                     instruction_index);
      }
      return lower(*form, exec_ir::AddressSpace::global);
    }
    return error(LoweringErrorCode::unsupported_form, function_index,
                 instruction_index);
  }

  if (const auto* st = std::get_if<St>(&instruction)) {
    const auto predicate = predicate_for(st->execution_predicate, registers,
                                         function_index, instruction_index);
    if (!predicate)
      return std::unexpected(predicate.error());
    const auto lower = [&](const auto& form, exec_ir::AddressSpace space)
        -> std::expected<exec_ir::Instruction, LoweringError> {
      const auto controls = memory_controls(form);
      if (!controls) {
        return error(LoweringErrorCode::unsupported_form, function_index,
                     instruction_index);
      }
      if (space == exec_ir::AddressSpace::generic &&
          controls->semantics != exec_ir::MemoryConsistency::omitted) {
        return error(LoweringErrorCode::unsupported_form, function_index,
                     instruction_index);
      }
      if (form.type.value != ScalarType::U32) {
        return error(LoweringErrorCode::unsupported_type, function_index,
                     instruction_index);
      }
      const auto address = address_for(form.address.value, registers,
                                       function_index, instruction_index);
      const auto source =
          slot_for(registers, form.src.value, common::RawWidth::b32,
                   function_index, instruction_index);
      if (!address)
        return std::unexpected(address.error());
      if (!source)
        return std::unexpected(source.error());
      if (space == exec_ir::AddressSpace::generic) {
        return exec_ir::St{
            *predicate,
            exec_ir::St::Variant{exec_ir::St::GenericScalar{
                controls->semantics, controls->scope, controls->mmio,
                controls->cache, exec_ir::DataType::u32, *address, *source}}};
      }
      return exec_ir::St{
          *predicate,
          exec_ir::St::Variant{exec_ir::St::ExplicitScalar{
              space, controls->cache, controls->semantics, controls->scope,
              controls->mmio, exec_ir::DataType::u32, *address, *source}}};
    };
    if (const auto* form = std::get_if<St::GenericScalar>(&st->variant)) {
      return lower(*form, exec_ir::AddressSpace::generic);
    }
    if (const auto* form = std::get_if<St::ExplicitScalar>(&st->variant)) {
      if (form->state_space.value != MemoryStateSpace::Global) {
        return error(LoweringErrorCode::unsupported_form, function_index,
                     instruction_index);
      }
      return lower(*form, exec_ir::AddressSpace::global);
    }
    return error(LoweringErrorCode::unsupported_form, function_index,
                 instruction_index);
  }

  if (const auto* bar = std::get_if<Bar>(&instruction)) {
    if (bar->execution_predicate) {
      return error(LoweringErrorCode::unsupported_form, function_index,
                   instruction_index);
    }
    const auto* form = std::get_if<Bar::WarpSync>(&bar->variant);
    if (form == nullptr) {
      return error(LoweringErrorCode::unsupported_form, function_index,
                   instruction_index);
    }
    const auto membermask = operand_for(form->membermask.value, registers,
                                        function_index, instruction_index);
    if (!membermask) {
      return std::unexpected(membermask.error());
    }
    return exec_ir::Bar{std::nullopt, exec_ir::Bar::Variant{
                                          exec_ir::Bar::WarpSync{*membermask}}};
  }

  if (const auto* exit = std::get_if<Exit>(&instruction)) {
    if (!std::holds_alternative<Exit::Bare>(exit->variant)) {
      return error(LoweringErrorCode::unsupported_form, function_index,
                   instruction_index);
    }
    const auto predicate = predicate_for(exit->execution_predicate, registers,
                                         function_index, instruction_index);
    if (!predicate)
      return std::unexpected(predicate.error());
    return exec_ir::Exit{*predicate,
                         exec_ir::Exit::Variant{exec_ir::Exit::Bare{}}};
  }

  return error(LoweringErrorCode::unsupported_instruction, function_index,
               instruction_index);
}

}  // namespace

auto lower(const ptx_frontend::resolved_ir::ResolvedModule& module)
    -> std::expected<exec_ir::ExecutableProgram, LoweringError> {
  if (module.functions.size() > std::numeric_limits<std::uint32_t>::max()) {
    return error(LoweringErrorCode::malformed_resolved_ir);
  }

  exec_ir::ProgramDefinition definition;
  definition.functions.reserve(module.functions.size());
  for (std::size_t index = 0; index < module.functions.size(); ++index) {
    const auto function_index = static_cast<std::uint32_t>(index);
    const auto& function = module.functions[index];
    if (function.symbol_id.value >= module.symbols.symbols().size()) {
      return error(LoweringErrorCode::malformed_resolved_ir, function_index,
                   std::nullopt, function.symbol_id.value);
    }
    const auto& function_symbol =
        module.symbols.symbols()[function.symbol_id.value];
    if (function_symbol.kind != SymbolKind::Function) {
      return error(LoweringErrorCode::malformed_resolved_ir, function_index,
                   std::nullopt, function.symbol_id.value);
    }
    auto registers = register_layout(module.symbols, function_symbol,
                                     function_index, function.body.empty());
    const auto labels = labels_for(function, module.symbols, function_index);
    if (!registers)
      return std::unexpected(registers.error());
    if (!labels)
      return std::unexpected(labels.error());
    if (function.body.size() > std::numeric_limits<std::uint32_t>::max()) {
      return error(LoweringErrorCode::malformed_resolved_ir, function_index);
    }

    const auto begin = definition.instructions.size();
    for (std::size_t instruction_index = 0;
         instruction_index < function.body.size(); ++instruction_index) {
      auto lowered = lower_instruction(
          function.body[instruction_index], *registers, *labels,
          static_cast<std::uint32_t>(function.body.size()), function_index,
          static_cast<std::uint32_t>(instruction_index));
      if (!lowered)
        return std::unexpected(lowered.error());
      definition.instructions.push_back(std::move(*lowered));
    }
    definition.functions.push_back(
        {common::FunctionId{function_index}, begin,
         static_cast<std::uint32_t>(function.body.size()),
         std::move(registers->widths)});
  }

  auto program = exec_ir::ExecutableProgram::create(std::move(definition));
  if (!program) {
    const auto function = program.error().function
                              ? std::optional{program.error().function->value()}
                              : std::nullopt;
    const auto instruction = program.error().pc
                                 ? std::optional{program.error().pc->value()}
                                 : std::nullopt;
    return error(LoweringErrorCode::program_validation_failed, function,
                 instruction, std::nullopt, program.error());
  }
  return std::move(*program);
}

}  // namespace ptxsim::exec_ir_lowering
