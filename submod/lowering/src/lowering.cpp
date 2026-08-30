#include <ptxsim/lowering/lowering.hpp>

#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <ptxsim/exec_ir/instruction.hpp>
#include <ptxsim/lowering/context.hpp>

namespace ptxsim::lowering {
namespace {

namespace frontend = ptx_frontend;
namespace binding = frontend::binding;
namespace resolved_ir = frontend::resolved_ir;
namespace syntax = frontend::syntax_ast;

using common::RawWidth;

struct RegisterBinding {
  common::RegisterSlot base;
  RawWidth width;
  std::uint32_t count;
};

struct FunctionState {
  const syntax::AstFunction* ast{};
  const resolved_ir::ResolvedFunction* resolved{};
  binding::ScopeId scope{};
  common::FunctionId id{0};
  common::ProgramCounter begin{0};
  common::ProgramCounter end{0};
  std::size_t resolved_index{};
  std::vector<program::RegisterLayout> registers;
};

[[nodiscard]] auto diagnostic(LoweringDiagnosticCode code,
                              std::optional<LoweringSourceLocation> location,
                              std::string instruction, std::string feature = {},
                              std::string detail = {}) -> LoweringDiagnostic {
  return {
      .code = code,
      .source_location = std::move(location),
      .instruction_context = std::move(instruction),
      .unsupported_feature =
          feature.empty() ? std::nullopt
                          : std::optional<std::string>{std::move(feature)},
      .operand_or_control_detail =
          detail.empty() ? std::nullopt
                         : std::optional<std::string>{std::move(detail)},
  };
}

[[nodiscard]] auto raw_width(resolved_ir::ScalarType type)
    -> std::optional<RawWidth> {
  using enum resolved_ir::ScalarType;
  switch (type) {
    case Pred:
      return RawWidth::pred;
    case B16:
    case U16:
    case S16:
      return RawWidth::b16;
    case B32:
    case U32:
    case S32:
      return RawWidth::b32;
    case B64:
    case U64:
    case S64:
      return RawWidth::b64;
    default:
      return std::nullopt;
  }
}

[[nodiscard]] auto raw_width(std::string_view type) -> std::optional<RawWidth> {
  if (type.starts_with('.'))
    type.remove_prefix(1);
  if (type == "pred")
    return RawWidth::pred;
  if (type == "b16" || type == "u16" || type == "s16")
    return RawWidth::b16;
  if (type == "b32" || type == "u32" || type == "s32")
    return RawWidth::b32;
  if (type == "b64" || type == "u64" || type == "s64")
    return RawWidth::b64;
  return std::nullopt;
}

[[nodiscard]] auto is_integer_type(resolved_ir::ScalarType type) -> bool {
  using enum resolved_ir::ScalarType;
  return type == U32 || type == S32 || type == U64 || type == S64;
}

[[nodiscard]] auto signedness(resolved_ir::ScalarType type)
    -> std::optional<exec_ir::IntegerSignedness> {
  using enum resolved_ir::ScalarType;
  if (type == U32 || type == U64)
    return exec_ir::IntegerSignedness::unsigned_;
  if (type == S32 || type == S64)
    return exec_ir::IntegerSignedness::signed_;
  return std::nullopt;
}

class ModuleLowerer {
 public:
  ModuleLowerer(const syntax::AstModule& ast,
                const resolved_ir::ResolvedModule& resolved,
                std::string source_file)
      : ast_(ast),
        resolved_(resolved),
        source_file_(std::move(source_file)),
        context_(resolved.symbols.symbols().size()),
        register_bindings_(resolved.symbols.symbols().size()),
        scope_functions_(resolved.symbols.scopes().size()) {}

  [[nodiscard]] auto run()
      -> std::expected<program::ProgramImage, LoweringDiagnostic> {
    if (auto result = collect_functions(); !result)
      return std::unexpected(result.error());
    if (auto result = bind_declarations(); !result)
      return std::unexpected(result.error());
    if (auto result = place_labels(); !result)
      return std::unexpected(result.error());
    if (auto result = lower_instructions(); !result)
      return std::unexpected(result.error());

    auto image = program::ProgramImage::create(std::move(data_));
    if (!image) {
      auto error = diagnostic(
          LoweringDiagnosticCode::lowering_invariant_violation, std::nullopt,
          "program-image", "ProgramImage::create", "verification failed");
      if (image.error().function) {
        for (const auto& function : functions_) {
          if (function.id == *image.error().function) {
            error.function_context = function.resolved->name;
            break;
          }
        }
      }
      return std::unexpected(std::move(error));
    }
    return std::move(*image);
  }

 private:
  [[nodiscard]] auto location(const frontend::SourceRange& range) const
      -> std::optional<LoweringSourceLocation> {
    if (range.start.line < 0 || range.start.column < 0)
      return std::nullopt;
    return LoweringSourceLocation{
        source_file_, static_cast<std::uint32_t>(range.start.line),
        static_cast<std::uint32_t>(range.start.column)};
  }

  [[nodiscard]] auto malformed(const frontend::SourceRange& range,
                               std::string detail) const -> LoweringDiagnostic {
    return diagnostic(LoweringDiagnosticCode::malformed_resolved_ir,
                      location(range), "module", "AST/resolved alignment",
                      std::move(detail));
  }

  [[nodiscard]] auto with_function(LoweringDiagnostic error,
                                   const FunctionState& function) const
      -> LoweringDiagnostic {
    if (!error.function_context)
      error.function_context = function.resolved->name;
    return error;
  }

  [[nodiscard]] auto declaration_error(
      LoweringDiagnostic error, std::optional<std::size_t> function) const
      -> LoweringDiagnostic {
    if (function)
      return with_function(std::move(error), functions_[*function]);
    return error;
  }

  [[nodiscard]] auto checked_pc(std::size_t value,
                                const frontend::SourceRange& range) const
      -> std::expected<common::ProgramCounter, LoweringDiagnostic> {
    if (value > std::numeric_limits<std::uint32_t>::max())
      return std::unexpected(malformed(range, "program counter overflow"));
    return common::ProgramCounter{static_cast<std::uint32_t>(value)};
  }

  [[nodiscard]] auto collect_functions()
      -> std::expected<void, LoweringDiagnostic> {
    std::vector<const syntax::AstFunction*> ast_functions;
    for (const auto& item : ast_.items) {
      if (const auto* function = std::get_if<syntax::AstFunction>(&item))
        ast_functions.push_back(function);
    }
    if (ast_functions.size() != resolved_.functions.size()) {
      return std::unexpected(malformed(ast_.range, "function count mismatch"));
    }

    std::size_t function_id = 0;
    for (std::size_t index = 0; index != ast_functions.size(); ++index) {
      const auto& ast_function = *ast_functions[index];
      const auto& resolved_function = resolved_.functions[index];
      if (ast_function.range != resolved_function.range ||
          ast_function.is_entry != resolved_function.is_entry ||
          ast_function.is_prototype != resolved_function.is_prototype ||
          ast_function.name.syntax.text != resolved_function.name) {
        return std::unexpected(
            malformed(ast_function.range, "function ordering mismatch"));
      }
      if (resolved_function.is_prototype) {
        if (!ast_function.body.empty() || !resolved_function.body.empty()) {
          return std::unexpected(
              malformed(ast_function.range, "prototype has a body"));
        }
        continue;
      }
      if (function_id > std::numeric_limits<std::uint32_t>::max()) {
        return std::unexpected(
            malformed(ast_function.range, "function identifier overflow"));
      }
      const auto& symbol =
          resolved_.symbols.symbol(resolved_function.symbol_id);
      if (symbol.kind != binding::SymbolKind::Function || !symbol.owned_scope) {
        return std::unexpected(malformed(
            ast_function.range, "function symbol has no function scope"));
      }
      FunctionState state{
          .ast = &ast_function,
          .resolved = &resolved_function,
          .scope = *symbol.owned_scope,
          .id = common::FunctionId{static_cast<std::uint32_t>(function_id++)},
      };
      const auto scope_index = static_cast<std::size_t>(state.scope.value);
      if (scope_index >= scope_functions_.size() ||
          scope_functions_[scope_index]) {
        return std::unexpected(
            malformed(ast_function.range, "duplicate function scope"));
      }
      scope_functions_[scope_index] = functions_.size();
      if (auto bound =
              context_.bind_function(resolved_function.symbol_id, state.id);
          !bound) {
        return std::unexpected(
            malformed(ast_function.range, "duplicate function symbol mapping"));
      }
      functions_.push_back(std::move(state));
    }
    return {};
  }

  [[nodiscard]] auto function_for_scope(binding::ScopeId scope)
      -> std::optional<std::size_t> {
    for (;;) {
      const auto index = static_cast<std::size_t>(scope.value);
      if (index >= scope_functions_.size())
        return std::nullopt;
      if (scope_functions_[index])
        return *scope_functions_[index];
      const auto& current = resolved_.symbols.scope(scope);
      if (!current.parent)
        return std::nullopt;
      scope = *current.parent;
    }
  }

  [[nodiscard]] auto bind_declarations()
      -> std::expected<void, LoweringDiagnostic> {
    std::vector<std::uint32_t> next_slots(functions_.size());
    for (const auto& symbol : resolved_.symbols.symbols()) {
      if (symbol.kind != binding::SymbolKind::Variable || !symbol.state_space)
        continue;
      const auto symbol_index = static_cast<std::size_t>(symbol.id.value);
      if (*symbol.state_space == syntax::AstStateSpace::Register) {
        const auto function = function_for_scope(symbol.scope);
        if (!function)
          return std::unexpected(malformed(symbol.declaration_range,
                                           "register outside a function"));
        if (symbol.vector_width) {
          return std::unexpected(declaration_error(
              diagnostic(LoweringDiagnosticCode::unsupported_ptx_feature,
                         location(symbol.declaration_range),
                         "register declaration", "vector register", "vN"),
              function));
        }
        if (!symbol.type) {
          return std::unexpected(declaration_error(
              malformed(symbol.declaration_range, "register lacks a type"),
              function));
        }
        const auto width = raw_width(*symbol.type);
        if (!width) {
          return std::unexpected(declaration_error(
              diagnostic(LoweringDiagnosticCode::unsupported_type_combination,
                         location(symbol.declaration_range),
                         "register declaration", "register type", *symbol.type),
              function));
        }
        const auto count = symbol.parameterized_count.value_or(1);
        if (count == 0 ||
            next_slots[*function] >
                std::numeric_limits<std::uint32_t>::max() - count) {
          return std::unexpected(declaration_error(
              malformed(symbol.declaration_range, "register slot overflow"),
              function));
        }
        const auto base = common::RegisterSlot{next_slots[*function]};
        if (auto bound = context_.bind_register(symbol.id, base); !bound) {
          return std::unexpected(declaration_error(
              malformed(symbol.declaration_range, "duplicate register mapping"),
              function));
        }
        register_bindings_[symbol_index] = RegisterBinding{base, *width, count};
        auto& layouts = functions_[*function].registers;
        for (std::uint32_t offset = 0; offset != count; ++offset)
          layouts.push_back(
              {common::RegisterSlot{base.value() + offset}, *width});
        next_slots[*function] += count;
      } else if (*symbol.state_space == syntax::AstStateSpace::Global ||
                 *symbol.state_space == syntax::AstStateSpace::Constant) {
        if (data_.symbols.size() > std::numeric_limits<std::uint32_t>::max()) {
          return std::unexpected(malformed(symbol.declaration_range,
                                           "symbol identifier overflow"));
        }
        const auto id =
            common::SymbolId{static_cast<std::uint32_t>(data_.symbols.size())};
        if (auto bound = context_.bind_symbol(symbol.id, id); !bound) {
          return std::unexpected(malformed(symbol.declaration_range,
                                           "duplicate data symbol mapping"));
        }
        data_.symbols.push_back({id, symbol.name});
      }
    }
    return {};
  }

  [[nodiscard]] auto block_scope(binding::ScopeId parent,
                                 const syntax::AstBlock& block)
      -> std::expected<binding::ScopeId, LoweringDiagnostic> {
    const auto nested = resolved_.symbols.blockScope(parent, block.range);
    if (!nested)
      return std::unexpected(
          malformed(block.range, "missing bound block scope"));
    return *nested;
  }

  [[nodiscard]] auto bind_label(binding::ScopeId scope,
                                const syntax::AstLabel& label, std::size_t pc)
      -> std::expected<void, LoweringDiagnostic> {
    const auto lookup = resolved_.symbols.lookup(scope, label.name.syntax.text);
    if (!lookup || lookup->parameterized_index ||
        resolved_.symbols.symbol(lookup->symbol).kind !=
            binding::SymbolKind::Label) {
      return std::unexpected(malformed(label.range, "unbound label"));
    }
    const auto target = checked_pc(pc, label.range);
    if (!target)
      return std::unexpected(target.error());
    if (auto bound = context_.bind_label(lookup->symbol, *target); !bound) {
      return std::unexpected(malformed(label.range, "duplicate label mapping"));
    }
    return {};
  }

  [[nodiscard]] auto place_labels_body(
      const std::vector<syntax::AstFunctionBodyItem>& body,
      binding::ScopeId scope, std::size_t& pc)
      -> std::expected<void, LoweringDiagnostic> {
    for (const auto& item : body) {
      if (const auto* label = std::get_if<syntax::AstLabel>(&item)) {
        if (auto result = bind_label(scope, *label, pc); !result)
          return result;
      } else if (const auto* block =
                     std::get_if<std::unique_ptr<syntax::AstBlock>>(&item);
                 block != nullptr && *block) {
        const auto nested = block_scope(scope, **block);
        if (!nested)
          return std::unexpected(nested.error());
        if (auto result = place_labels_body((*block)->body, *nested, pc);
            !result)
          return result;
      } else if (std::holds_alternative<syntax::AstInstruction>(item)) {
        ++pc;
      }
    }
    return {};
  }

  [[nodiscard]] auto place_labels() -> std::expected<void, LoweringDiagnostic> {
    std::size_t pc = 0;
    for (auto& function : functions_) {
      const auto begin = checked_pc(pc, function.ast->range);
      if (!begin)
        return std::unexpected(with_function(begin.error(), function));
      function.begin = *begin;
      if (auto result =
              place_labels_body(function.ast->body, function.scope, pc);
          !result) {
        return std::unexpected(with_function(result.error(), function));
      }
      const auto end = checked_pc(pc, function.ast->range);
      if (!end)
        return std::unexpected(with_function(end.error(), function));
      function.end = *end;
    }
    return {};
  }

  [[nodiscard]] auto register_operand(
      const resolved_ir::ResolvedRegisterRef& ref, RawWidth required,
      const frontend::SourceRange& range, std::string_view instruction)
      -> std::expected<exec_ir::RegisterOperand, LoweringDiagnostic> {
    if (!ref.symbol_id)
      return std::unexpected(malformed(range, "register has no bound symbol"));
    const auto symbol_index = static_cast<std::size_t>(ref.symbol_id->value);
    if (symbol_index >= register_bindings_.size() ||
        !register_bindings_[symbol_index]) {
      return std::unexpected(
          diagnostic(LoweringDiagnosticCode::malformed_resolved_ir,
                     location(range), std::string(instruction),
                     "register binding", "missing register mapping"));
    }
    const auto binding = *register_bindings_[symbol_index];
    if (binding.count > 1 && !ref.parameterized_index) {
      return std::unexpected(
          malformed(range, "parameterized register lacks an index"));
    }
    const auto index = ref.parameterized_index.value_or(0);
    if ((ref.parameterized_index && binding.count == 1) ||
        index >= binding.count ||
        binding.base.value() >
            std::numeric_limits<std::uint32_t>::max() - index) {
      return std::unexpected(
          malformed(range, "invalid parameterized register index"));
    }
    if (binding.width != required) {
      return std::unexpected(
          diagnostic(LoweringDiagnosticCode::unsupported_type_combination,
                     location(range), std::string(instruction),
                     "register width", "operand width mismatch"));
    }
    const auto base = context_.resolve_register(*ref.symbol_id);
    if (!base)
      return std::unexpected(
          malformed(range, "missing register context mapping"));
    return exec_ir::RegisterOperand{common::RegisterSlot{base->value() + index},
                                    required};
  }

  [[nodiscard]] auto immediate_operand(
      const resolved_ir::ResolvedImmediate& immediate, RawWidth required,
      const frontend::SourceRange& range, std::string_view instruction)
      -> std::expected<exec_ir::ImmediateOperand, LoweringDiagnostic> {
    if (raw_width(immediate.type) != required) {
      return std::unexpected(
          diagnostic(LoweringDiagnosticCode::unsupported_type_combination,
                     location(range), std::string(instruction),
                     "immediate type", "operand width mismatch"));
    }
    if ((required == RawWidth::pred && immediate.bits > 1) ||
        (required == RawWidth::b16 &&
         immediate.bits > std::numeric_limits<std::uint16_t>::max()) ||
        (required == RawWidth::b32 &&
         immediate.bits > std::numeric_limits<std::uint32_t>::max())) {
      return std::unexpected(diagnostic(
          LoweringDiagnosticCode::malformed_resolved_ir, location(range),
          std::string(instruction), "immediate bits", "not representable"));
    }
    common::RawValue value = [&] {
      switch (required) {
        case RawWidth::b16:
          return common::RawValue::b16(
              static_cast<std::uint16_t>(immediate.bits));
        case RawWidth::b32:
          return common::RawValue::b32(
              static_cast<std::uint32_t>(immediate.bits));
        case RawWidth::b64:
          return common::RawValue::b64(immediate.bits);
        case RawWidth::pred:
          return common::RawValue::pred(immediate.bits != 0);
        default:
          return common::RawValue::b32(std::uint32_t{0});
      }
    }();
    return exec_ir::ImmediateOperand{value};
  }

  [[nodiscard]] auto value_operand(const resolved_ir::RegOrImm& operand,
                                   RawWidth required,
                                   const frontend::SourceRange& range,
                                   std::string_view instruction)
      -> std::expected<exec_ir::ValueOperand, LoweringDiagnostic> {
    if (const auto* reg =
            std::get_if<resolved_ir::ResolvedRegisterRef>(&operand)) {
      const auto lowered = register_operand(*reg, required, range, instruction);
      if (!lowered)
        return std::unexpected(lowered.error());
      return *lowered;
    }
    const auto lowered =
        immediate_operand(std::get<resolved_ir::ResolvedImmediate>(operand),
                          required, range, instruction);
    if (!lowered)
      return std::unexpected(lowered.error());
    return *lowered;
  }

  [[nodiscard]] auto guard(
      const std::optional<frontend::WithLocs<resolved_ir::ResolvedPredicate>>&
          predicate,
      const frontend::SourceRange& range, std::string_view instruction)
      -> std::expected<std::optional<exec_ir::PredicateGuard>,
                       LoweringDiagnostic> {
    if (!predicate)
      return std::optional<exec_ir::PredicateGuard>{};
    if (predicate->locs.empty())
      return std::unexpected(malformed(range, "predicate has no source range"));
    const auto lowered =
        register_operand(predicate->value.register_ref, RawWidth::pred,
                         predicate->locs.front(), instruction);
    if (!lowered)
      return std::unexpected(lowered.error());
    return exec_ir::PredicateGuard{lowered->slot, predicate->value.negated};
  }

  [[nodiscard]] auto address_operand(
      const resolved_ir::ResolvedAddress& address,
      const frontend::SourceRange& range, std::string_view instruction)
      -> std::expected<exec_ir::AddressOperand, LoweringDiagnostic> {
    std::optional<std::variant<common::RegisterSlot, common::SymbolId>> base;
    if (const auto* reg =
            std::get_if<resolved_ir::ResolvedRegisterRef>(&address.base)) {
      const auto lowered =
          register_operand(*reg, RawWidth::b64, range, instruction);
      if (!lowered)
        return std::unexpected(lowered.error());
      base = lowered->slot;
    } else if (const auto* symbol =
                   std::get_if<resolved_ir::ResolvedSymbolRef>(&address.base)) {
      if (!symbol->symbol_id)
        return std::unexpected(
            malformed(range, "address symbol has no binding"));
      if (symbol->parameterized_index) {
        return std::unexpected(diagnostic(
            LoweringDiagnosticCode::unsupported_ptx_feature, location(range),
            std::string(instruction), "parameterized address symbol"));
      }
      const auto lowered = context_.resolve_symbol(*symbol->symbol_id);
      if (!lowered)
        return std::unexpected(
            diagnostic(LoweringDiagnosticCode::unsupported_ptx_feature,
                       location(range), std::string(instruction),
                       "address symbol", "not global or constant"));
      base = *lowered;
    } else {
      return std::unexpected(diagnostic(
          LoweringDiagnosticCode::unsupported_ptx_feature, location(range),
          std::string(instruction), "immediate address base"));
    }
    std::int64_t offset = 0;
    if (address.offset) {
      if (address.offset->value.is_negative) {
        return std::unexpected(
            malformed(range, "negative address offset magnitude"));
      }
      if (address.offset->value.type != resolved_ir::ScalarType::S64) {
        return std::unexpected(
            diagnostic(LoweringDiagnosticCode::unsupported_type_combination,
                       location(range), std::string(instruction),
                       "address offset type", "requires s64 magnitude"));
      }
      const auto bits = address.offset->value.bits;
      if (bits > static_cast<std::uint64_t>(
                     std::numeric_limits<std::int64_t>::max())) {
        return std::unexpected(
            diagnostic(LoweringDiagnosticCode::unsupported_type_combination,
                       location(range), std::string(instruction),
                       "address offset", "does not fit int64"));
      }
      offset = static_cast<std::int64_t>(bits);
      switch (address.offset->operation) {
        case resolved_ir::ResolvedAddressOffsetOperator::Add:
          break;
        case resolved_ir::ResolvedAddressOffsetOperator::Subtract:
          offset = -offset;
          break;
        default:
          return std::unexpected(
              malformed(range, "unknown address offset operation"));
      }
    }
    return exec_ir::AddressOperand{std::move(*base),
                                   exec_ir::AddressWidth::bits64, offset};
  }

  [[nodiscard]] auto lower_instruction(
      const resolved_ir::ResolvedInstruction& instruction,
      const frontend::SourceRange& range)
      -> std::expected<exec_ir::Instruction, LoweringDiagnostic> {
    return std::visit(
        [this, &range](const auto& value)
            -> std::expected<exec_ir::Instruction, LoweringDiagnostic> {
          return lower(value, range);
        },
        instruction);
  }

  template <typename T>
  [[nodiscard]] auto lower(const T&, const frontend::SourceRange& range)
      -> std::expected<exec_ir::Instruction, LoweringDiagnostic> {
    return std::unexpected(diagnostic(
        LoweringDiagnosticCode::unsupported_ptx_feature, location(range),
        std::string(T::get_syntax_descriptor().Opcode_name),
        "resolved instruction family"));
  }

  template <typename Variant>
  [[nodiscard]] auto binary(
      const Variant& variant, exec_ir::IntegerBinaryOp op, RawWidth width,
      exec_ir::IntegerSignedness sign,
      const std::optional<frontend::WithLocs<resolved_ir::ResolvedPredicate>>&
          predicate,
      const frontend::SourceRange& range, std::string_view name)
      -> std::expected<exec_ir::Instruction, LoweringDiagnostic> {
    const auto dst = register_operand(variant.dst.value, width, range, name);
    const auto lhs = value_operand(variant.src1.value, width, range, name);
    const auto rhs = value_operand(variant.src2.value, width, range, name);
    const auto lowered_guard = guard(predicate, range, name);
    if (!dst)
      return std::unexpected(dst.error());
    if (!lhs)
      return std::unexpected(lhs.error());
    if (!rhs)
      return std::unexpected(rhs.error());
    if (!lowered_guard)
      return std::unexpected(lowered_guard.error());
    return exec_ir::IntegerBinaryInst{op,   sign, *dst,
                                      *lhs, *rhs, *lowered_guard};
  }

  [[nodiscard]] auto lower(const resolved_ir::Add& add,
                           const frontend::SourceRange& range)
      -> std::expected<exec_ir::Instruction, LoweringDiagnostic> {
    const auto* variant =
        std::get_if<resolved_ir::Add::IntegerNoSat>(&add.variant);
    if (!variant)
      return std::unexpected(
          diagnostic(LoweringDiagnosticCode::unsupported_ptx_variant,
                     location(range), "add", "Add variant"));
    const auto sign = signedness(variant->type.value);
    const auto width = raw_width(variant->type.value);
    if (!sign || !width || !is_integer_type(variant->type.value))
      return std::unexpected(
          diagnostic(LoweringDiagnosticCode::unsupported_type_combination,
                     location(range), "add", "Add::IntegerNoSat type"));
    return binary(*variant, exec_ir::IntegerBinaryOp::add, *width, *sign,
                  add.execution_predicate, range, "add");
  }

  [[nodiscard]] auto lower(const resolved_ir::Sub& sub,
                           const frontend::SourceRange& range)
      -> std::expected<exec_ir::Instruction, LoweringDiagnostic> {
    if (const auto* variant =
            std::get_if<resolved_ir::Sub::IntegerNoSat>(&sub.variant)) {
      const auto sign = signedness(variant->type.value);
      const auto width = raw_width(variant->type.value);
      if (!sign || !width || !is_integer_type(variant->type.value))
        return std::unexpected(
            diagnostic(LoweringDiagnosticCode::unsupported_type_combination,
                       location(range), "sub", "Sub::IntegerNoSat type"));
      return binary(*variant, exec_ir::IntegerBinaryOp::sub, *width, *sign,
                    sub.execution_predicate, range, "sub");
    }
    if (const auto* variant =
            std::get_if<resolved_ir::Sub::OptionalSat>(&sub.variant)) {
      if (variant->type.value != resolved_ir::ScalarType::S32 ||
          variant->saturate.value)
        return std::unexpected(
            diagnostic(LoweringDiagnosticCode::unsupported_ptx_variant,
                       location(range), "sub", "Sub::OptionalSat", "saturate"));
      return binary(*variant, exec_ir::IntegerBinaryOp::sub, RawWidth::b32,
                    exec_ir::IntegerSignedness::signed_,
                    sub.execution_predicate, range, "sub");
    }
    return std::unexpected(
        diagnostic(LoweringDiagnosticCode::unsupported_ptx_variant,
                   location(range), "sub", "Sub variant"));
  }

  [[nodiscard]] auto lower(const resolved_ir::Mul& mul,
                           const frontend::SourceRange& range)
      -> std::expected<exec_ir::Instruction, LoweringDiagnostic> {
    auto make = [this, &mul, &range](
                    const auto& variant, exec_ir::ProductPart part,
                    exec_ir::IntegerSignedness sign, RawWidth dest_width)
        -> std::expected<exec_ir::Instruction, LoweringDiagnostic> {
      const auto dst =
          register_operand(variant.dst.value, dest_width, range, "mul");
      const auto lhs =
          value_operand(variant.src1.value, RawWidth::b32, range, "mul");
      const auto rhs =
          value_operand(variant.src2.value, RawWidth::b32, range, "mul");
      const auto lowered_guard = guard(mul.execution_predicate, range, "mul");
      if (!dst)
        return std::unexpected(dst.error());
      if (!lhs)
        return std::unexpected(lhs.error());
      if (!rhs)
        return std::unexpected(rhs.error());
      if (!lowered_guard)
        return std::unexpected(lowered_guard.error());
      return exec_ir::IntegerMulInst{part, sign, *dst,
                                     *lhs, *rhs, *lowered_guard};
    };
    if (const auto* v = std::get_if<resolved_ir::Mul::LoU32>(&mul.variant))
      return make(*v, exec_ir::ProductPart::low,
                  exec_ir::IntegerSignedness::unsigned_, RawWidth::b32);
    if (const auto* v = std::get_if<resolved_ir::Mul::HiU32>(&mul.variant))
      return make(*v, exec_ir::ProductPart::high,
                  exec_ir::IntegerSignedness::unsigned_, RawWidth::b32);
    if (const auto* v = std::get_if<resolved_ir::Mul::WideU32>(&mul.variant))
      return make(*v, exec_ir::ProductPart::wide,
                  exec_ir::IntegerSignedness::unsigned_, RawWidth::b64);
    if (const auto* v = std::get_if<resolved_ir::Mul::WideS32>(&mul.variant))
      return make(*v, exec_ir::ProductPart::wide,
                  exec_ir::IntegerSignedness::signed_, RawWidth::b64);
    return std::unexpected(
        diagnostic(LoweringDiagnosticCode::unsupported_ptx_variant,
                   location(range), "mul", "Mul variant"));
  }

  template <typename Op>
  [[nodiscard]] auto lower_bit(const Op& op, exec_ir::BitOp kind,
                               const frontend::SourceRange& range,
                               std::string_view name)
      -> std::expected<exec_ir::Instruction, LoweringDiagnostic> {
    const auto* variant = std::get_if<typename Op::B32>(&op.variant);
    if (!variant)
      return std::unexpected(
          diagnostic(LoweringDiagnosticCode::unsupported_ptx_variant,
                     location(range), std::string(name), "B32 variant"));
    const auto dst =
        register_operand(variant->dst.value, RawWidth::b32, range, name);
    const auto lhs =
        value_operand(variant->src1.value, RawWidth::b32, range, name);
    const auto rhs =
        value_operand(variant->src2.value, RawWidth::b32, range, name);
    const auto lowered_guard = guard(op.execution_predicate, range, name);
    if (!dst)
      return std::unexpected(dst.error());
    if (!lhs)
      return std::unexpected(lhs.error());
    if (!rhs)
      return std::unexpected(rhs.error());
    if (!lowered_guard)
      return std::unexpected(lowered_guard.error());
    return exec_ir::BitInst{kind, *dst, *lhs, *rhs, *lowered_guard};
  }

  [[nodiscard]] auto lower(const resolved_ir::And& op,
                           const frontend::SourceRange& range)
      -> std::expected<exec_ir::Instruction, LoweringDiagnostic> {
    return lower_bit(op, exec_ir::BitOp::and_, range, "and");
  }
  [[nodiscard]] auto lower(const resolved_ir::Or& op,
                           const frontend::SourceRange& range)
      -> std::expected<exec_ir::Instruction, LoweringDiagnostic> {
    return lower_bit(op, exec_ir::BitOp::or_, range, "or");
  }
  [[nodiscard]] auto lower(const resolved_ir::Xor& op,
                           const frontend::SourceRange& range)
      -> std::expected<exec_ir::Instruction, LoweringDiagnostic> {
    return lower_bit(op, exec_ir::BitOp::xor_, range, "xor");
  }

  [[nodiscard]] auto lower(const resolved_ir::Bra& bra,
                           const frontend::SourceRange& range)
      -> std::expected<exec_ir::Instruction, LoweringDiagnostic> {
    const auto* variant = std::get_if<resolved_ir::Bra::Direct>(&bra.variant);
    if (!variant || variant->uni.value)
      return std::unexpected(
          diagnostic(LoweringDiagnosticCode::unsupported_ptx_variant,
                     location(range), "bra", "Bra::Direct", "uni"));
    if (!variant->target.value.symbol_id)
      return std::unexpected(malformed(range, "branch target has no binding"));
    const auto target =
        context_.resolve_label(*variant->target.value.symbol_id);
    if (!target)
      return std::unexpected(
          malformed(range, "branch target label is missing"));
    const auto lowered_guard = guard(bra.execution_predicate, range, "bra");
    if (!lowered_guard)
      return std::unexpected(lowered_guard.error());
    return exec_ir::BranchInst{{*target}, *lowered_guard};
  }

  [[nodiscard]] auto lower(const resolved_ir::Mov& mov,
                           const frontend::SourceRange& range)
      -> std::expected<exec_ir::Instruction, LoweringDiagnostic> {
    if (const auto* pred = std::get_if<resolved_ir::Mov::Pred>(&mov.variant)) {
      const auto* dst = &pred->dst.value;
      const auto* src =
          std::get_if<resolved_ir::ResolvedPredicate>(&pred->src.value);
      if (!src || dst->negated || src->negated)
        return std::unexpected(diagnostic(
            LoweringDiagnosticCode::unsupported_ptx_variant, location(range),
            "mov.pred", "predicate source", "negated or special"));
      const auto lowered_dst = register_operand(
          dst->register_ref, RawWidth::pred, range, "mov.pred");
      const auto lowered_src = register_operand(
          src->register_ref, RawWidth::pred, range, "mov.pred");
      const auto lowered_guard =
          guard(mov.execution_predicate, range, "mov.pred");
      if (!lowered_dst)
        return std::unexpected(lowered_dst.error());
      if (!lowered_src)
        return std::unexpected(lowered_src.error());
      if (!lowered_guard)
        return std::unexpected(lowered_guard.error());
      return exec_ir::MovInst{*lowered_dst, *lowered_src, *lowered_guard};
    }
    const auto* scalar = std::get_if<resolved_ir::Mov::Scalar>(&mov.variant);
    if (!scalar)
      return std::unexpected(
          diagnostic(LoweringDiagnosticCode::unsupported_ptx_variant,
                     location(range), "mov", "Mov variant"));
    const auto width = raw_width(scalar->type.value);
    if (!width || *width == RawWidth::pred)
      return std::unexpected(
          diagnostic(LoweringDiagnosticCode::unsupported_type_combination,
                     location(range), "mov", "Mov::Scalar type"));
    const auto* operands =
        std::get_if<resolved_ir::Mov::Scalar::ScalarOperands>(
            &scalar->operands);
    if (!operands)
      return std::unexpected(
          diagnostic(LoweringDiagnosticCode::unsupported_ptx_variant,
                     location(range), "mov", "Mov scalar operand layout"));
    const auto dst =
        register_operand(operands->dst.value, *width, range, "mov");
    if (!dst)
      return std::unexpected(dst.error());
    std::optional<exec_ir::ValueOperand> src;
    if (const auto* reg = std::get_if<resolved_ir::ResolvedRegisterRef>(
            &operands->src.value)) {
      const auto lowered = register_operand(*reg, *width, range, "mov");
      if (!lowered)
        return std::unexpected(lowered.error());
      src = *lowered;
    } else if (const auto* immediate =
                   std::get_if<resolved_ir::ResolvedImmediate>(
                       &operands->src.value)) {
      const auto lowered = immediate_operand(*immediate, *width, range, "mov");
      if (!lowered)
        return std::unexpected(lowered.error());
      src = *lowered;
    } else {
      return std::unexpected(
          diagnostic(LoweringDiagnosticCode::unsupported_ptx_feature,
                     location(range), "mov", "Mov source"));
    }
    const auto lowered_guard = guard(mov.execution_predicate, range, "mov");
    if (!lowered_guard)
      return std::unexpected(lowered_guard.error());
    return exec_ir::MovInst{*dst, std::move(*src), *lowered_guard};
  }

  template <typename Memory>
  [[nodiscard]] auto memory_controls(const Memory& variant,
                                     const frontend::SourceRange& range,
                                     std::string_view instruction) const
      -> std::expected<void, LoweringDiagnostic> {
    if (variant.cache.value != resolved_ir::CacheOperator::Unspecified ||
        variant.semantics.value != resolved_ir::MemoryConsistency::Omitted ||
        variant.scope.value != resolved_ir::MemoryScope::None ||
        variant.mmio.value) {
      return std::unexpected(diagnostic(
          LoweringDiagnosticCode::unsupported_ptx_variant, location(range),
          std::string(instruction), "memory controls"));
    }
    return {};
  }

  [[nodiscard]] auto lower(const resolved_ir::Ld& load,
                           const frontend::SourceRange& range)
      -> std::expected<exec_ir::Instruction, LoweringDiagnostic> {
    const auto* variant =
        std::get_if<resolved_ir::Ld::ExplicitScalar>(&load.variant);
    if (!variant)
      return std::unexpected(
          diagnostic(LoweringDiagnosticCode::unsupported_ptx_variant,
                     location(range), "ld", "Ld variant"));
    if (variant->state_space.value != resolved_ir::MemoryStateSpace::Global &&
        variant->state_space.value != resolved_ir::MemoryStateSpace::Constant)
      return std::unexpected(
          diagnostic(LoweringDiagnosticCode::unsupported_ptx_feature,
                     location(range), "ld", "memory state space"));
    if (variant->type.value != resolved_ir::ScalarType::B32 &&
        variant->type.value != resolved_ir::ScalarType::U32 &&
        variant->type.value != resolved_ir::ScalarType::S32)
      return std::unexpected(
          diagnostic(LoweringDiagnosticCode::unsupported_type_combination,
                     location(range), "ld", "load type"));
    if (auto controls = memory_controls(*variant, range, "ld"); !controls)
      return std::unexpected(controls.error());
    const auto dst =
        register_operand(variant->dst.value, RawWidth::b32, range, "ld");
    const auto address = address_operand(variant->address.value, range, "ld");
    const auto lowered_guard = guard(load.execution_predicate, range, "ld");
    if (!dst)
      return std::unexpected(dst.error());
    if (!address)
      return std::unexpected(address.error());
    if (!lowered_guard)
      return std::unexpected(lowered_guard.error());
    const auto space =
        variant->state_space.value == resolved_ir::MemoryStateSpace::Global
            ? exec_ir::MemorySpace::global
            : exec_ir::MemorySpace::constant;
    return exec_ir::LoadInst{space, *dst, *address, *lowered_guard};
  }

  [[nodiscard]] auto lower(const resolved_ir::St& store,
                           const frontend::SourceRange& range)
      -> std::expected<exec_ir::Instruction, LoweringDiagnostic> {
    const auto* variant =
        std::get_if<resolved_ir::St::ExplicitScalar>(&store.variant);
    if (!variant)
      return std::unexpected(
          diagnostic(LoweringDiagnosticCode::unsupported_ptx_variant,
                     location(range), "st", "St variant"));
    if (variant->state_space.value != resolved_ir::MemoryStateSpace::Global)
      return std::unexpected(
          diagnostic(LoweringDiagnosticCode::unsupported_ptx_feature,
                     location(range), "st", "memory state space"));
    if (variant->type.value != resolved_ir::ScalarType::B32 &&
        variant->type.value != resolved_ir::ScalarType::U32 &&
        variant->type.value != resolved_ir::ScalarType::S32)
      return std::unexpected(
          diagnostic(LoweringDiagnosticCode::unsupported_type_combination,
                     location(range), "st", "store type"));
    if (auto controls = memory_controls(*variant, range, "st"); !controls)
      return std::unexpected(controls.error());
    const auto address = address_operand(variant->address.value, range, "st");
    const auto src =
        register_operand(variant->src.value, RawWidth::b32, range, "st");
    const auto lowered_guard = guard(store.execution_predicate, range, "st");
    if (!address)
      return std::unexpected(address.error());
    if (!src)
      return std::unexpected(src.error());
    if (!lowered_guard)
      return std::unexpected(lowered_guard.error());
    return exec_ir::StoreInst{exec_ir::MemorySpace::global, *address, *src,
                              *lowered_guard};
  }

  [[nodiscard]] auto lower_body(
      const std::vector<syntax::AstFunctionBodyItem>& body,
      binding::ScopeId scope, FunctionState& function)
      -> std::expected<void, LoweringDiagnostic> {
    for (const auto& item : body) {
      if (const auto* instruction =
              std::get_if<syntax::AstInstruction>(&item)) {
        if (function.resolved_index >= function.resolved->body.size())
          return std::unexpected(with_function(
              malformed(instruction->range, "missing resolved instruction"),
              function));
        const auto& resolved =
            function.resolved->body[function.resolved_index++];
        const auto resolved_opcode = std::visit(
            [](const auto& value) {
              return value.get_syntax_descriptor().Opcode_name;
            },
            resolved);
        if (instruction->opcode.syntax.text != resolved_opcode) {
          return std::unexpected(with_function(
              malformed(instruction->range, "AST/resolved opcode mismatch"),
              function));
        }
        const auto lowered = lower_instruction(resolved, instruction->range);
        if (!lowered)
          return std::unexpected(with_function(lowered.error(), function));
        if (instruction->range.start.line < 0 ||
            instruction->range.start.column < 0) {
          return std::unexpected(with_function(
              malformed(instruction->range, "negative source position"),
              function));
        }
        if (data_.instructions.size() >
            std::numeric_limits<std::uint32_t>::max())
          return std::unexpected(with_function(
              malformed(instruction->range, "instruction count overflow"),
              function));
        const auto pc = static_cast<std::uint32_t>(data_.instructions.size());
        const auto source_id = common::SourceLocationId{pc};
        data_.instructions.push_back(*lowered);
        data_.source_locations.push_back(
            {source_id, source_file_,
             static_cast<std::uint32_t>(instruction->range.start.line),
             static_cast<std::uint32_t>(instruction->range.start.column)});
        data_.source_locations_by_pc.push_back(source_id);
      } else if (const auto* block =
                     std::get_if<std::unique_ptr<syntax::AstBlock>>(&item);
                 block != nullptr && *block) {
        const auto nested = block_scope(scope, **block);
        if (!nested)
          return std::unexpected(with_function(nested.error(), function));
        if (auto result = lower_body((*block)->body, *nested, function);
            !result)
          return std::unexpected(with_function(result.error(), function));
      }
    }
    return {};
  }

  [[nodiscard]] auto lower_instructions()
      -> std::expected<void, LoweringDiagnostic> {
    for (auto& function : functions_) {
      if (auto result =
              lower_body(function.ast->body, function.scope, function);
          !result)
        return result;
      if (function.resolved_index != function.resolved->body.size())
        return std::unexpected(with_function(
            malformed(function.ast->range, "extra resolved instruction"),
            function));
      data_.functions.push_back({function.id, function.resolved->name,
                                 function.begin, function.end,
                                 std::move(function.registers)});
      if (function.resolved->is_entry)
        data_.entry_points.push_back(function.id);
    }
    return {};
  }

  const syntax::AstModule& ast_;
  const resolved_ir::ResolvedModule& resolved_;
  std::string source_file_;
  LoweringContext context_;
  std::vector<std::optional<RegisterBinding>> register_bindings_;
  std::vector<std::optional<std::size_t>> scope_functions_;
  std::vector<FunctionState> functions_;
  program::ProgramImageData data_;
};

}  // namespace

auto lower_module(const syntax::AstModule& ast,
                  const resolved_ir::ResolvedModule& resolved,
                  std::string source_file)
    -> std::expected<program::ProgramImage, LoweringDiagnostic> {
  return ModuleLowerer{ast, resolved, std::move(source_file)}.run();
}

}  // namespace ptxsim::lowering
