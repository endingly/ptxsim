#include <ptxsim/exec_ir_lowering/exec_ir_lowering.hpp>

#include "exec_ir_lowering.gen.hpp"
#include "lowering_detail.hpp"

#include <cstddef>
#include <limits>
#include <map>
#include <new>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <vector>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/semantic/ptx_declaration_semantics.hpp>

namespace ptxsim::exec_ir_lowering {
namespace {

using detail::EntryParameterBinding;
using detail::EntryParameterTable;
using detail::LabelTable;
using detail::RegisterLayout;
using ptx_frontend::base::ScalarType;
using ptx_frontend::binding::ScopeId;
using ptx_frontend::binding::ScopeKind;
using ptx_frontend::binding::Symbol;
using ptx_frontend::binding::SymbolKind;
using ptx_frontend::binding::SymbolTable;
using ptx_frontend::resolved_ir::ParameterDeclarationRole;
using ptx_frontend::resolved_ir::ResolvedParameterDeclaration;

/** @brief Retain the source identity and structured cause of a lowering failure. */
[[nodiscard]] auto error(
    LoweringErrorCode code,
    std::optional<std::uint32_t> function = std::nullopt,
    std::optional<std::uint32_t> instruction = std::nullopt,
    std::optional<std::uint32_t> symbol = std::nullopt,
    std::optional<exec_ir::ProgramError> program_error = std::nullopt)
    -> std::unexpected<LoweringError>;

/** @brief Copy normalized frontend storage into owned executable byte layouts. */
[[nodiscard]] auto storage_layout(
    const ptx_frontend::resolved_ir::ResolvedModule& module)
    -> std::expected<std::vector<exec_ir::StorageDeclaration>, LoweringError> {
  std::map<std::uint32_t, common::FunctionId> functions;
  for (std::size_t index = 0; index < module.functions.size(); ++index) {
    functions.emplace(module.functions[index].symbol_id.value,
                      common::FunctionId{static_cast<std::uint32_t>(index)});
  }
  std::vector<exec_ir::StorageDeclaration> result;
  std::map<std::uint32_t,
           const ptx_frontend::resolved_ir::ResolvedStorageDeclaration*>
      seen;
  for (const auto& source : module.storage_declarations) {
    if (source.symbol_id.value >= module.symbols.symbols().size()) {
      return error(LoweringErrorCode::malformed_resolved_ir, std::nullopt,
                   std::nullopt, source.symbol_id.value);
    }
    const auto& symbol = module.symbols.symbols()[source.symbol_id.value];
    if (symbol.id != source.symbol_id || symbol.kind != SymbolKind::Variable ||
        symbol.scope != source.scope_id || symbol.linkage != source.linkage ||
        symbol.vector_width.value_or(1) != source.vector_width ||
        symbol.address_alignment != source.alignment ||
        symbol.parameterized_count != source.parameterized_count) {
      return error(LoweringErrorCode::malformed_resolved_ir, std::nullopt,
                   std::nullopt, source.symbol_id.value);
    }
    if (source.parameterized_count || source.is_managed || source.unified_id ||
        !source.alignment ||
        (!source.byte_extent &&
         source.initialization !=
             ptx_frontend::resolved_ir::StorageInitializationKind::External) ||
        (source.byte_extent &&
         *source.byte_extent > std::numeric_limits<std::size_t>::max()) ||
        *source.alignment > std::numeric_limits<std::size_t>::max() ||
        !std::holds_alternative<ptx_frontend::base::ScalarType>(
            source.element_type)) {
      return error(LoweringErrorCode::unsupported_storage_declaration,
                   std::nullopt, std::nullopt, source.symbol_id.value);
    }
    const auto scalar = std::get<ScalarType>(source.element_type);
    const auto declared_scalar =
        symbol.type ? ptx_frontend::declaration_semantics::parameterScalarType(
                          *symbol.type)
                    : std::nullopt;
    // Packed half storage is a fundamental declaration type even though the
    // entry-parameter helper intentionally does not classify that spelling.
    if ((!declared_scalar || *declared_scalar != scalar) &&
        !(symbol.type == ".f16x2" && scalar == ScalarType::F16x2)) {
      return error(LoweringErrorCode::malformed_resolved_ir, std::nullopt,
                   std::nullopt, source.symbol_id.value);
    }
    const auto scalar_bytes = ptx_frontend::base::scalar_size_of(scalar);
    if (scalar_bytes == 0U || scalar_bytes * source.vector_width > 16U ||
        source.vector_width != 1U && source.vector_width != 2U &&
            source.vector_width != 4U ||
        *source.alignment == 0U ||
        (*source.alignment & (*source.alignment - 1U)) != 0U ||
        (source.explicit_alignment &&
         source.explicit_alignment != source.alignment)) {
      return error(LoweringErrorCode::malformed_resolved_ir, std::nullopt,
                   std::nullopt, source.symbol_id.value);
    }
    if (source.alignment !=
        source.explicit_alignment.value_or(
            static_cast<std::uint64_t>(scalar_bytes) * source.vector_width)) {
      return error(LoweringErrorCode::malformed_resolved_ir, std::nullopt,
                   std::nullopt, source.symbol_id.value);
    }
    std::uint64_t computed_extent = scalar_bytes * source.vector_width;
    bool unsized = false;
    for (std::size_t index = 0; index < source.array_extents.size(); ++index) {
      const auto dimension = source.array_extents[index];
      if (!dimension) {
        if (index != 0U ||
            source.declaration_kind !=
                ptx_frontend::resolved_ir::StorageDeclarationKind::External) {
          return error(LoweringErrorCode::malformed_resolved_ir, std::nullopt,
                       std::nullopt, source.symbol_id.value);
        }
        unsized = true;
      } else {
        if (*dimension == 0U ||
            *dimension >
                std::numeric_limits<std::uint64_t>::max() / computed_extent) {
          return error(LoweringErrorCode::malformed_resolved_ir, std::nullopt,
                       std::nullopt, source.symbol_id.value);
        }
        computed_extent *= *dimension;
      }
    }
    if (unsized ? source.byte_extent.has_value()
                : source.byte_extent != computed_extent) {
      return error(LoweringErrorCode::malformed_resolved_ir, std::nullopt,
                   std::nullopt, source.symbol_id.value);
    }
    if (computed_extent >= exec_ir::kStorageAddressSpaceSize) {
      return error(LoweringErrorCode::unsupported_storage_declaration,
                   std::nullopt, std::nullopt, source.symbol_id.value);
    }
    std::optional<ptx_frontend::binding::SymbolId> scope_owner;
    auto scope = source.scope_id;
    bool reached_owner = false;
    for (std::size_t depth = 0; depth <= module.symbols.scopes().size();
         ++depth) {
      if (scope.value >= module.symbols.scopes().size())
        break;
      const auto& record = module.symbols.scopes()[scope.value];
      if (record.id != scope)
        break;
      if (record.kind == ScopeKind::Function) {
        scope_owner = record.owner;
        reached_owner = scope_owner.has_value();
        break;
      }
      if (record.kind == ScopeKind::Module) {
        reached_owner = !record.parent && scope.value == 0U;
        break;
      }
      if (record.kind != ScopeKind::Block || !record.parent)
        break;
      scope = *record.parent;
    }
    if (!reached_owner || scope_owner != source.owner_function) {
      return error(LoweringErrorCode::malformed_resolved_ir, std::nullopt,
                   std::nullopt, source.symbol_id.value);
    }
    std::optional<common::FunctionId> owner;
    if (source.owner_function) {
      const auto found = functions.find(source.owner_function->value);
      if (found == functions.end())
        return error(LoweringErrorCode::malformed_resolved_ir, std::nullopt,
                     std::nullopt, source.symbol_id.value);
      owner = found->second;
    }
    exec_ir::StorageSpace space;
    switch (source.space) {
      case ptx_frontend::resolved_ir::StorageSpace::Global:
        space = exec_ir::StorageSpace::global;
        break;
      case ptx_frontend::resolved_ir::StorageSpace::Constant:
        space = exec_ir::StorageSpace::constant;
        break;
      case ptx_frontend::resolved_ir::StorageSpace::Shared:
        space = exec_ir::StorageSpace::shared;
        break;
      case ptx_frontend::resolved_ir::StorageSpace::Local:
        space = exec_ir::StorageSpace::local;
        break;
      default:
        return error(LoweringErrorCode::malformed_resolved_ir, std::nullopt,
                     std::nullopt, source.symbol_id.value);
    }
    using AstSpace = ptx_frontend::syntax_ast::AstStateSpace;
    const auto expected_space =
        space == exec_ir::StorageSpace::global     ? AstSpace::Global
        : space == exec_ir::StorageSpace::constant ? AstSpace::Constant
        : space == exec_ir::StorageSpace::shared   ? AstSpace::Shared
                                                   : AstSpace::Local;
    const bool external =
        source.declaration_kind ==
        ptx_frontend::resolved_ir::StorageDeclarationKind::External;
    if (symbol.state_space != expected_space ||
        (source.declaration_kind !=
             ptx_frontend::resolved_ir::StorageDeclarationKind::Definition &&
         !external) ||
        external !=
            (source.initialization ==
             ptx_frontend::resolved_ir::StorageInitializationKind::External) ||
        external != (source.linkage ==
                     ptx_frontend::binding::SymbolLinkage::External) ||
        source.is_dynamic_shared !=
            (external && unsized && space == exec_ir::StorageSpace::shared)) {
      return error(LoweringErrorCode::malformed_resolved_ir, std::nullopt,
                   std::nullopt, source.symbol_id.value);
    }
    if (external && (space == exec_ir::StorageSpace::local ||
                     (space == exec_ir::StorageSpace::shared &&
                      !source.is_dynamic_shared))) {
      return error(LoweringErrorCode::unsupported_storage_declaration,
                   std::nullopt, std::nullopt, source.symbol_id.value);
    }
    if (const auto previous = seen.find(source.symbol_id.value);
        previous != seen.end()) {
      const auto& first = *previous->second;
      if (!external || first.declaration_kind != source.declaration_kind ||
          first.space != source.space ||
          first.element_type != source.element_type ||
          first.vector_width != source.vector_width ||
          first.array_extents != source.array_extents ||
          first.byte_extent != source.byte_extent ||
          first.alignment != source.alignment ||
          first.scope_id != source.scope_id ||
          first.owner_function != source.owner_function ||
          first.linkage != source.linkage ||
          first.parameterized_count != source.parameterized_count ||
          first.initialization != source.initialization ||
          !source.initializer.empty()) {
        return error(LoweringErrorCode::malformed_resolved_ir, std::nullopt,
                     std::nullopt, source.symbol_id.value);
      }
      continue;
    }
    seen.emplace(source.symbol_id.value, &source);
    exec_ir::StorageInitialization initialization;
    switch (source.initialization) {
      case ptx_frontend::resolved_ir::StorageInitializationKind::Uninitialized:
        initialization = exec_ir::StorageInitialization::uninitialized;
        break;
      case ptx_frontend::resolved_ir::StorageInitializationKind::Zero:
        initialization = exec_ir::StorageInitialization::zero;
        break;
      case ptx_frontend::resolved_ir::StorageInitializationKind::Explicit:
        initialization = exec_ir::StorageInitialization::explicit_bytes;
        break;
      case ptx_frontend::resolved_ir::StorageInitializationKind::External:
        initialization = exec_ir::StorageInitialization::external;
        break;
      default:
        return error(LoweringErrorCode::malformed_resolved_ir, std::nullopt,
                     std::nullopt, source.symbol_id.value);
    }
    const auto extent =
        source.byte_extent ? static_cast<std::size_t>(*source.byte_extent) : 0U;
    const auto external_extent_multiple =
        unsized && !source.is_dynamic_shared
            ? static_cast<std::size_t>(computed_extent)
            : std::size_t{1};
    if ((extent == 0U &&
         initialization != exec_ir::StorageInitialization::external) ||
        (source.is_dynamic_shared &&
         (space != exec_ir::StorageSpace::shared ||
          initialization != exec_ir::StorageInitialization::external))) {
      return error(LoweringErrorCode::malformed_resolved_ir, std::nullopt,
                   std::nullopt, source.symbol_id.value);
    }
    if (initialization != exec_ir::StorageInitialization::explicit_bytes &&
        !source.initializer.empty()) {
      return error(LoweringErrorCode::malformed_resolved_ir, std::nullopt,
                   std::nullopt, source.symbol_id.value);
    }
    std::vector<std::byte> bytes;
    if (initialization == exec_ir::StorageInitialization::explicit_bytes) {
      std::unordered_set<std::uint64_t> initialized;
      for (const auto& item : source.initializer) {
        if (!std::holds_alternative<ptx_frontend::resolved_ir::StorageConstant>(
                item.value))
          return error(LoweringErrorCode::unsupported_storage_relocation,
                       std::nullopt, std::nullopt, source.symbol_id.value);
        if (item.byte_offset % scalar_bytes != 0U ||
            item.byte_offset > extent ||
            scalar_bytes > extent - item.byte_offset ||
            !initialized.insert(item.byte_offset).second)
          return error(LoweringErrorCode::malformed_resolved_ir, std::nullopt,
                       std::nullopt, source.symbol_id.value);
      }
      for (const auto& item : source.initializer) {
        const auto& constant =
            std::get<ptx_frontend::resolved_ir::StorageConstant>(item.value);
        if ((scalar_bytes <= 8U && constant.high_bits != 0U) ||
            (scalar_bytes < 8U &&
             (constant.bits >> (scalar_bytes * 8U)) != 0U)) {
          return error(LoweringErrorCode::malformed_resolved_ir, std::nullopt,
                       std::nullopt, source.symbol_id.value);
        }
      }
      try {
        bytes.assign(extent, std::byte{0});
      } catch (const std::bad_alloc&) {
        return error(LoweringErrorCode::unsupported_storage_declaration,
                     std::nullopt, std::nullopt, source.symbol_id.value);
      } catch (const std::length_error&) {
        return error(LoweringErrorCode::unsupported_storage_declaration,
                     std::nullopt, std::nullopt, source.symbol_id.value);
      }
      for (const auto& item : source.initializer) {
        const auto* constant =
            std::get_if<ptx_frontend::resolved_ir::StorageConstant>(
                &item.value);
        if (constant == nullptr || item.byte_offset % scalar_bytes != 0U ||
            item.byte_offset > extent ||
            scalar_bytes > extent - item.byte_offset)
          return error(constant
                           ? LoweringErrorCode::malformed_resolved_ir
                           : LoweringErrorCode::unsupported_storage_relocation,
                       std::nullopt, std::nullopt, source.symbol_id.value);
        for (std::size_t byte = 0; byte < scalar_bytes; ++byte) {
          const auto destination =
              static_cast<std::size_t>(item.byte_offset) + byte;
          const auto word = byte < 8U ? constant->bits : constant->high_bits;
          bytes[destination] =
              std::byte{static_cast<unsigned char>(word >> (8U * (byte % 8U)))};
        }
      }
    }
    result.push_back(
        {common::SymbolId{source.symbol_id.value}, symbol.name, space, owner,
         extent, static_cast<std::size_t>(*source.alignment), initialization,
         std::move(bytes), source.is_dynamic_shared, external_extent_multiple});
  }
  return result;
}

[[nodiscard]] auto error(LoweringErrorCode code,
                         std::optional<std::uint32_t> function,
                         std::optional<std::uint32_t> instruction,
                         std::optional<std::uint32_t> symbol,
                         std::optional<exec_ir::ProgramError> program_error)
    -> std::unexpected<LoweringError> {
  return std::unexpected(
      LoweringError{code, function, instruction, symbol, program_error});
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

/** @brief Return whether @p value is a nonzero power-of-two byte alignment. */
[[nodiscard]] constexpr auto valid_alignment(std::size_t value) noexcept
    -> bool {
  return value != 0U && (value & (value - 1U)) == 0U;
}

/** @brief Owns the layout records and symbol bindings for one entry function. */
struct EntryParameterLayoutResult {
  /** @brief Source-ordered records copied into the executable function layout. */
  std::vector<exec_ir::EntryParameterLayout> layouts;
  /** @brief Lookup table used while lowering parameter address operands. */
  EntryParameterTable bindings;
  /** @brief Exact end of the last slot, with no trailing alignment padding. */
  std::size_t size = 0U;
};

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
    const auto components = symbol.vector_width.value_or(1U);
    if (components != 1U && components != 2U && components != 4U) {
      return error(LoweringErrorCode::unsupported_type, function, std::nullopt,
                   symbol.id.value);
    }
    if (!symbol.type) {
      return error(LoweringErrorCode::malformed_resolved_ir, function,
                   std::nullopt, symbol.id.value);
    }
    const auto width = detail::raw_width_for(*symbol.type);
    const auto count = symbol.parameterized_count.value_or(1U);
    constexpr std::uint64_t max_slots =
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) +
        1U;
    const auto slot_count = static_cast<std::uint64_t>(result.widths.size());
    const auto required_slots = static_cast<std::uint64_t>(count) * components;
    if (!width || (*width == common::RawWidth::pred && components != 1U) ||
        count == 0U || slot_count > max_slots ||
        required_slots > max_slots - slot_count) {
      return error(LoweringErrorCode::malformed_resolved_ir, function,
                   std::nullopt, symbol.id.value);
    }
    for (std::uint32_t member = 0; member < count; ++member) {
      const auto index = symbol.parameterized_count
                             ? std::optional<std::uint32_t>{member}
                             : std::nullopt;
      const auto slot = common::RegisterSlot{
          static_cast<std::uint32_t>(result.widths.size())};
      if (!result.slots
               .emplace(std::pair{symbol.id.value, index},
                        detail::RegisterBinding{slot, components})
               .second) {
        return error(LoweringErrorCode::malformed_resolved_ir, function,
                     std::nullopt, symbol.id.value);
      }
      result.widths.insert(result.widths.end(), components, *width);
    }
  }
  return result;
}

/** @brief Return whether @p role names one published parameter declaration role. */
[[nodiscard]] constexpr auto valid_parameter_role(
    ParameterDeclarationRole role) noexcept -> bool {
  switch (role) {
    case ParameterDeclarationRole::EntryInput:
    case ParameterDeclarationRole::DeviceInput:
    case ParameterDeclarationRole::DeviceReturn:
    case ParameterDeclarationRole::BodyLocal:
      return true;
  }
  return false;
}

/**
 * @brief Check checked declaration extent metadata and return its exact byte count.
 *
 * A missing result denotes invalid metadata. Zero is reserved for the one
 * supported unsized DeviceInput form; every sized declaration returns nonzero.
 */
[[nodiscard]] auto declared_byte_extent(
    const ResolvedParameterDeclaration& parameter, bool is_entry)
    -> std::optional<std::uint64_t> {
  if (parameter.scalar_type == ScalarType::Invalid ||
      parameter.scalar_type == ScalarType::Pred ||
      parameter.vector_width != 1U) {
    return std::nullopt;
  }
  std::uint64_t size =
      ptx_frontend::base::scalar_size_of(parameter.scalar_type);
  if (size == 0U)
    return std::nullopt;
  bool unsized = false;
  for (const auto extent : parameter.array_extents) {
    if (!extent) {
      if (unsized)
        return std::nullopt;
      unsized = true;
      continue;
    }
    if (*extent == 0U ||
        size > std::numeric_limits<std::uint64_t>::max() / *extent)
      return std::nullopt;
    size *= *extent;
  }
  if (unsized) {
    if (is_entry || parameter.role != ParameterDeclarationRole::DeviceInput ||
        parameter.scalar_type != ScalarType::B8 ||
        parameter.array_extents.size() != 1U || parameter.byte_extent) {
      return std::nullopt;
    }
    return std::uint64_t{0};
  }
  if (!parameter.byte_extent || *parameter.byte_extent != size)
    return std::nullopt;
  return size;
}

/**
 * @brief Derive validated ABI slots from owned source-ordered entry metadata.
 *
 * Only EntryInput records become launch slots. Device signatures and body-local
 * declarations are validated as frontend metadata without becoming launch ABI.
 */
[[nodiscard]] auto entry_parameter_layout(
    const ptx_frontend::resolved_ir::ResolvedFunction& function,
    const SymbolTable& symbols, const Symbol& function_symbol,
    std::uint32_t function_index)
    -> std::expected<EntryParameterLayoutResult, LoweringError> {
  if (function.is_entry != function_symbol.function_is_entry) {
    return error(LoweringErrorCode::malformed_resolved_ir, function_index,
                 std::nullopt, function_symbol.id.value);
  }
  if (!function_symbol.owned_scope ||
      function_symbol.owned_scope->value >= symbols.scopes().size()) {
    if (!function.is_entry && function.parameter_declarations.empty())
      return EntryParameterLayoutResult{};
    return error(LoweringErrorCode::malformed_resolved_ir, function_index,
                 std::nullopt, function_symbol.id.value);
  }

  const auto function_scope = *function_symbol.owned_scope;
  EntryParameterLayoutResult result;
  result.layouts.reserve(function.parameter_declarations.size());
  result.bindings.reserve(function.parameter_declarations.size());
  std::unordered_set<std::uint32_t> declared_symbols;
  /** True after the terminal unsized DeviceInput declaration in source order. */
  bool unsized_device_input_seen = false;
  for (const auto& parameter : function.parameter_declarations) {
    if (!valid_parameter_role(parameter.role) ||
        parameter.symbol_id.value >= symbols.symbols().size()) {
      return error(LoweringErrorCode::invalid_entry_parameter_layout,
                   function_index, std::nullopt, parameter.symbol_id.value);
    }
    const auto& symbol = symbols.symbols()[parameter.symbol_id.value];
    const auto expected_kind = [&] {
      switch (parameter.role) {
        case ParameterDeclarationRole::EntryInput:
        case ParameterDeclarationRole::DeviceInput:
          return SymbolKind::InputParameter;
        case ParameterDeclarationRole::DeviceReturn:
          return SymbolKind::ReturnParameter;
        case ParameterDeclarationRole::BodyLocal:
          return SymbolKind::CallParameter;
      }
      return SymbolKind::Variable;
    }();
    const auto role_matches_function =
        parameter.role == ParameterDeclarationRole::EntryInput
            ? function.is_entry
        : parameter.role == ParameterDeclarationRole::DeviceInput ||
                parameter.role == ParameterDeclarationRole::DeviceReturn
            ? !function.is_entry
            : true;
    const auto scope_matches = [&] {
      if (parameter.role == ParameterDeclarationRole::EntryInput)
        return parameter.scope_id == function_scope;
      if (parameter.role == ParameterDeclarationRole::BodyLocal)
        return descendant_of(symbols, parameter.scope_id, function_scope);
      if (parameter.scope_id.value >= symbols.scopes().size())
        return false;
      // Prototypes and definitions have distinct parameter scopes even though
      // their shared function symbol records the latest definition scope.
      const auto& scope = symbols.scopes()[parameter.scope_id.value];
      return scope.id == parameter.scope_id &&
             scope.kind == ScopeKind::Function &&
             scope.owner == function.symbol_id;
    }();
    const auto symbol_scalar =
        symbol.type ? ptx_frontend::declaration_semantics::parameterScalarType(
                          *symbol.type)
                    : std::nullopt;
    const auto byte_extent = declared_byte_extent(parameter, function.is_entry);
    const auto follows_unsized_device_input =
        unsized_device_input_seen &&
        parameter.role == ParameterDeclarationRole::DeviceInput;
    if (!role_matches_function || !scope_matches ||
        follows_unsized_device_input ||
        parameter.alignment > std::numeric_limits<std::size_t>::max() ||
        !valid_alignment(static_cast<std::size_t>(parameter.alignment)) ||
        !byte_extent || symbol.id != parameter.symbol_id ||
        symbol.scope != parameter.scope_id || symbol.kind != expected_kind ||
        symbol.state_space !=
            ptx_frontend::syntax_ast::AstStateSpace::Parameter ||
        symbol_scalar != parameter.scalar_type ||
        symbol.address_alignment != parameter.alignment ||
        !declared_symbols.insert(parameter.symbol_id.value).second) {
      return error(LoweringErrorCode::invalid_entry_parameter_layout,
                   function_index, std::nullopt, parameter.symbol_id.value);
    }
    if (parameter.role == ParameterDeclarationRole::DeviceInput &&
        *byte_extent == 0U) {
      unsized_device_input_seen = true;
    }
    if (parameter.role != ParameterDeclarationRole::EntryInput)
      continue;
    if (parameter.array_extents.size() > 1U || symbol.vector_width ||
        symbol.parameterized_count) {
      return error(LoweringErrorCode::invalid_entry_parameter_layout,
                   function_index, std::nullopt, parameter.symbol_id.value);
    }
    const auto size = *byte_extent;
    if (size > std::numeric_limits<std::size_t>::max()) {
      return error(LoweringErrorCode::invalid_entry_parameter_layout,
                   function_index, std::nullopt, parameter.symbol_id.value);
    }
    const auto alignment = static_cast<std::size_t>(parameter.alignment);
    const auto remainder = result.size % alignment;
    const auto padding = remainder == 0U ? 0U : alignment - remainder;
    if (padding > std::numeric_limits<std::size_t>::max() - result.size ||
        size >
            std::numeric_limits<std::size_t>::max() - (result.size + padding)) {
      return error(LoweringErrorCode::invalid_entry_parameter_layout,
                   function_index, std::nullopt, parameter.symbol_id.value);
    }
    const auto layout = exec_ir::EntryParameterLayout{
        .offset = result.size + padding,
        .size = static_cast<std::size_t>(size),
        .alignment = alignment,
    };
    if (!result.bindings
             .emplace(parameter.symbol_id.value,
                      EntryParameterBinding{layout, parameter.scalar_type})
             .second) {
      return error(LoweringErrorCode::invalid_entry_parameter_layout,
                   function_index, std::nullopt, parameter.symbol_id.value);
    }
    result.layouts.push_back(layout);
    result.size = layout.offset + layout.size;
  }

  if (function.is_entry) {
    for (const auto& symbol : symbols.symbols()) {
      if (symbol.scope == function_scope &&
          symbol.kind == SymbolKind::InputParameter &&
          !result.bindings.contains(symbol.id.value)) {
        return error(LoweringErrorCode::invalid_entry_parameter_layout,
                     function_index, std::nullopt, symbol.id.value);
      }
    }
  }
  return result;
}

[[nodiscard]] auto labels_for(
    const ptx_frontend::resolved_ir::ResolvedFunction& function,
    const SymbolTable& symbols, std::uint32_t function_index)
    -> std::expected<LabelTable, LoweringError> {
  LabelTable labels;
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

}  // namespace

auto lower(const ptx_frontend::resolved_ir::ResolvedModule& module)
    -> std::expected<exec_ir::ExecutableProgram, LoweringError> {
  if (module.functions.size() > std::numeric_limits<std::uint32_t>::max()) {
    return error(LoweringErrorCode::malformed_resolved_ir);
  }

  exec_ir::ProgramDefinition definition;
  const auto storage = storage_layout(module);
  if (!storage)
    return std::unexpected(storage.error());
  definition.storage_declarations = *storage;
  detail::StorageSymbolTable storage_symbols;
  for (const auto& declaration : definition.storage_declarations) {
    if (declaration.symbol.value() >= module.symbols.symbols().size()) {
      return error(LoweringErrorCode::malformed_resolved_ir, std::nullopt,
                   std::nullopt, declaration.symbol.value());
    }
    const auto& symbol = module.symbols.symbols()[declaration.symbol.value()];
    const auto type =
        symbol.type ? detail::scalar_type_for(*symbol.type) : std::nullopt;
    if (!type || !symbol.address_alignment ||
        *symbol.address_alignment != declaration.alignment) {
      return error(LoweringErrorCode::malformed_resolved_ir, std::nullopt,
                   std::nullopt, declaration.symbol.value());
    }
    std::optional<ptx_frontend::syntax_ast::AstStateSpace> space;
    switch (declaration.space) {
      case exec_ir::StorageSpace::global:
        space = ptx_frontend::syntax_ast::AstStateSpace::Global;
        break;
      case exec_ir::StorageSpace::constant:
        space = ptx_frontend::syntax_ast::AstStateSpace::Constant;
        break;
      case exec_ir::StorageSpace::shared:
        space = ptx_frontend::syntax_ast::AstStateSpace::Shared;
        break;
      case exec_ir::StorageSpace::local:
        space = ptx_frontend::syntax_ast::AstStateSpace::Local;
        break;
      default:
        return error(LoweringErrorCode::malformed_resolved_ir, std::nullopt,
                     std::nullopt, declaration.symbol.value());
    }
    storage_symbols.emplace(
        declaration.symbol.value(),
        detail::StorageSymbolBinding{*type, *space, *symbol.address_alignment,
                                     declaration.owner_function});
  }
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
    auto entry_parameters = entry_parameter_layout(
        function, module.symbols, function_symbol, function_index);
    const auto labels = labels_for(function, module.symbols, function_index);
    if (!registers)
      return std::unexpected(registers.error());
    if (!entry_parameters)
      return std::unexpected(entry_parameters.error());
    if (!labels)
      return std::unexpected(labels.error());
    if (function.body.size() > std::numeric_limits<std::uint32_t>::max()) {
      return error(LoweringErrorCode::malformed_resolved_ir, function_index);
    }

    const auto begin = definition.instructions.size();
    for (std::size_t instruction_index = 0;
         instruction_index < function.body.size(); ++instruction_index) {
      const detail::BindingContext context{
          *registers,
          *labels,
          entry_parameters->bindings,
          storage_symbols,
          function.is_entry,
          static_cast<std::uint32_t>(function.body.size()),
          function_index,
          static_cast<std::uint32_t>(instruction_index),
      };
      auto lowered = generated::lower_instruction(
          function.body[instruction_index], context);
      if (!lowered)
        return std::unexpected(lowered.error());
      definition.instructions.push_back(std::move(*lowered));
    }
    definition.functions.push_back(
        {common::FunctionId{function_index}, begin,
         static_cast<std::uint32_t>(function.body.size()),
         std::move(registers->widths), entry_parameters->size,
         std::move(entry_parameters->layouts)});
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
