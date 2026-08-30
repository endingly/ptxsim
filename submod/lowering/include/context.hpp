#pragma once

#include <cstddef>
#include <expected>
#include <optional>
#include <vector>

#include <ptx_frontend/binding/ptx_symbol_table.hpp>

#include <ptxsim/common/ids.hpp>

namespace ptxsim::lowering {

enum class LoweringContextErrorCode {
  out_of_range,
  duplicate_mapping,
  missing_mapping,
};

enum class LoweringContextIdentityKind {
  function,
  symbol,
  register_symbol,
  label,
};

struct LoweringContextError {
  LoweringContextErrorCode code;
  LoweringContextIdentityKind identity_kind;
  ptx_frontend::binding::SymbolId frontend_symbol;

  constexpr bool operator==(const LoweringContextError&) const noexcept =
      default;
};

class LoweringContext {
 public:
  explicit LoweringContext(std::size_t frontend_symbol_count);

  [[nodiscard]] auto bind_function(
      ptx_frontend::binding::SymbolId frontend_symbol,
      common::FunctionId function) -> std::expected<void, LoweringContextError>;
  [[nodiscard]] auto bind_symbol(
      ptx_frontend::binding::SymbolId frontend_symbol, common::SymbolId symbol)
      -> std::expected<void, LoweringContextError>;
  [[nodiscard]] auto bind_register(
      ptx_frontend::binding::SymbolId frontend_symbol,
      common::RegisterSlot register_slot)
      -> std::expected<void, LoweringContextError>;
  [[nodiscard]] auto bind_label(ptx_frontend::binding::SymbolId frontend_symbol,
                                common::ProgramCounter pc)
      -> std::expected<void, LoweringContextError>;

  [[nodiscard]] auto resolve_function(
      ptx_frontend::binding::SymbolId frontend_symbol) const
      -> std::expected<common::FunctionId, LoweringContextError>;
  [[nodiscard]] auto resolve_symbol(
      ptx_frontend::binding::SymbolId frontend_symbol) const
      -> std::expected<common::SymbolId, LoweringContextError>;
  [[nodiscard]] auto resolve_register(
      ptx_frontend::binding::SymbolId frontend_symbol) const
      -> std::expected<common::RegisterSlot, LoweringContextError>;
  [[nodiscard]] auto resolve_label(
      ptx_frontend::binding::SymbolId frontend_symbol) const
      -> std::expected<common::ProgramCounter, LoweringContextError>;

 private:
  template <typename Id>
  [[nodiscard]] auto bind(std::vector<std::optional<Id>>& mappings,
                          ptx_frontend::binding::SymbolId frontend_symbol,
                          Id value, LoweringContextIdentityKind identity_kind)
      -> std::expected<void, LoweringContextError>;

  template <typename Id>
  [[nodiscard]] auto resolve(const std::vector<std::optional<Id>>& mappings,
                             ptx_frontend::binding::SymbolId frontend_symbol,
                             LoweringContextIdentityKind identity_kind) const
      -> std::expected<Id, LoweringContextError>;

  std::vector<std::optional<common::FunctionId>> functions_;
  std::vector<std::optional<common::SymbolId>> symbols_;
  std::vector<std::optional<common::RegisterSlot>> registers_;
  std::vector<std::optional<common::ProgramCounter>> labels_;
};

}  // namespace ptxsim::lowering
