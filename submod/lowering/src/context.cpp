#include <ptxsim/lowering/context.hpp>

namespace ptxsim::lowering {

LoweringContext::LoweringContext(std::size_t frontend_symbol_count)
    : functions_(frontend_symbol_count),
      symbols_(frontend_symbol_count),
      registers_(frontend_symbol_count),
      labels_(frontend_symbol_count) {}

template <typename Id>
auto LoweringContext::bind(std::vector<std::optional<Id>>& mappings,
                           ptx_frontend::binding::SymbolId frontend_symbol,
                           Id value, LoweringContextIdentityKind identity_kind)
    -> std::expected<void, LoweringContextError> {
  const auto index = static_cast<std::size_t>(frontend_symbol.value);
  if (index >= mappings.size()) {
    return std::unexpected(
        LoweringContextError{LoweringContextErrorCode::out_of_range,
                             identity_kind, frontend_symbol});
  }
  if (mappings[index]) {
    return std::unexpected(
        LoweringContextError{LoweringContextErrorCode::duplicate_mapping,
                             identity_kind, frontend_symbol});
  }
  mappings[index] = value;
  return {};
}

template <typename Id>
auto LoweringContext::resolve(const std::vector<std::optional<Id>>& mappings,
                              ptx_frontend::binding::SymbolId frontend_symbol,
                              LoweringContextIdentityKind identity_kind) const
    -> std::expected<Id, LoweringContextError> {
  const auto index = static_cast<std::size_t>(frontend_symbol.value);
  if (index >= mappings.size()) {
    return std::unexpected(
        LoweringContextError{LoweringContextErrorCode::out_of_range,
                             identity_kind, frontend_symbol});
  }
  if (!mappings[index]) {
    return std::unexpected(
        LoweringContextError{LoweringContextErrorCode::missing_mapping,
                             identity_kind, frontend_symbol});
  }
  return *mappings[index];
}

auto LoweringContext::bind_function(
    ptx_frontend::binding::SymbolId frontend_symbol,
    common::FunctionId function) -> std::expected<void, LoweringContextError> {
  return bind(functions_, frontend_symbol, function,
              LoweringContextIdentityKind::function);
}

auto LoweringContext::bind_symbol(
    ptx_frontend::binding::SymbolId frontend_symbol, common::SymbolId symbol)
    -> std::expected<void, LoweringContextError> {
  return bind(symbols_, frontend_symbol, symbol,
              LoweringContextIdentityKind::symbol);
}

auto LoweringContext::bind_register(
    ptx_frontend::binding::SymbolId frontend_symbol,
    common::RegisterSlot register_slot)
    -> std::expected<void, LoweringContextError> {
  return bind(registers_, frontend_symbol, register_slot,
              LoweringContextIdentityKind::register_symbol);
}

auto LoweringContext::bind_label(
    ptx_frontend::binding::SymbolId frontend_symbol, common::ProgramCounter pc)
    -> std::expected<void, LoweringContextError> {
  return bind(labels_, frontend_symbol, pc, LoweringContextIdentityKind::label);
}

auto LoweringContext::resolve_function(
    ptx_frontend::binding::SymbolId frontend_symbol) const
    -> std::expected<common::FunctionId, LoweringContextError> {
  return resolve(functions_, frontend_symbol,
                 LoweringContextIdentityKind::function);
}

auto LoweringContext::resolve_symbol(
    ptx_frontend::binding::SymbolId frontend_symbol) const
    -> std::expected<common::SymbolId, LoweringContextError> {
  return resolve(symbols_, frontend_symbol,
                 LoweringContextIdentityKind::symbol);
}

auto LoweringContext::resolve_register(
    ptx_frontend::binding::SymbolId frontend_symbol) const
    -> std::expected<common::RegisterSlot, LoweringContextError> {
  return resolve(registers_, frontend_symbol,
                 LoweringContextIdentityKind::register_symbol);
}

auto LoweringContext::resolve_label(
    ptx_frontend::binding::SymbolId frontend_symbol) const
    -> std::expected<common::ProgramCounter, LoweringContextError> {
  return resolve(labels_, frontend_symbol, LoweringContextIdentityKind::label);
}

}  // namespace ptxsim::lowering
