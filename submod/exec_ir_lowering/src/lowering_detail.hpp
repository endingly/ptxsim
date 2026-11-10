#pragma once

#include <cstdint>
#include <expected>
#include <map>
#include <optional>
#include <utility>
#include <vector>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_module.hpp>
#include <ptxsim/exec_ir_lowering/exec_ir_lowering.hpp>

#include "binding_domains.gen.hpp"

namespace ptxsim::exec_ir_lowering::detail {

/** @brief Real function-local component slots for one bound register declaration. */
struct RegisterBinding {
  /** @brief First actual scalar slot in the function frame. */
  common::RegisterSlot first{0};
  /** @brief Actual declaration lane count: scalar one, vector two or four. */
  std::uint8_t components{1};
  /** @brief Exact scalar declaration type joined to every source cache. */
  ptx_frontend::base::ScalarType type{};
};

/** @brief Cold source identities for one specific declaration and optional body. */
struct FunctionBindings {
  /** @brief Canonical native callable; prototypes and definitions can share it. */
  common::FunctionId function{0};
  /** @brief Specific native declaration, never conflated with canonical identity. */
  exec_ir::FunctionDeclarationId declaration{0};
  /** @brief Real scalar slot allocation for each symbol and compact-group member. */
  std::map<std::pair<std::uint32_t, std::optional<std::uint32_t>>, RegisterBinding> registers;
  /** @brief Physical scalar register widths in exact frame slot order. */
  std::vector<common::RawWidth> register_widths;
  /** @brief Current function's label symbols resolved to local instruction positions. */
  std::map<std::uint32_t, common::ProgramCounter> labels;
};

/** @brief All source-to-owned identity joins, alive only during the cold lower call. */
struct ModuleBindings {
  /** @brief Complete validated source module; never published to runtime storage. */
  const ptx_frontend::resolved_ir::ResolvedModule& source;
  /** @brief Native mutable construction graph, independently frozen on publication. */
  exec_ir::ProgramDefinition output;
  /** @brief Written and canonical function symbols mapped to native canonical IDs. */
  std::map<std::uint32_t, common::FunctionId> functions;
  /** @brief Exact source declaration scopes mapped to distinct native declarations. */
  std::map<std::uint32_t, exec_ir::FunctionDeclarationId> declaration_scopes;
  /** @brief Specific function/declaration frame bindings in source declaration order. */
  std::vector<FunctionBindings> declarations;
  /** @brief Full ordinary storage declarations by actual source symbol. */
  std::map<std::uint32_t, exec_ir::StorageId> storage;
  /** @brief Full scalar parameters by actual source symbol, including body locals. */
  std::map<std::uint32_t, exec_ir::ParameterId> parameters;
  /** @brief Opaque module/entry resources by actual source declaration symbol. */
  std::map<std::uint32_t, exec_ir::ResourceId> resources;
  /** @brief Indirect call prototypes by their distinct metadata source symbols. */
  std::map<std::uint32_t, exec_ir::PrototypeId> prototypes;
  /** @brief Ordered callable sets by their distinct metadata source symbols. */
  std::map<std::uint32_t, exec_ir::CallTargetSetId> call_target_sets;
  /** @brief Ordered local branch sets by their distinct metadata source symbols. */
  std::map<std::uint32_t, exec_ir::BranchTargetSetId> branch_target_sets;
  /** @brief Consumed callable table contracts by actual underlying storage symbol. */
  std::map<std::uint32_t, exec_ir::CallTableId> call_tables;
};

/** @brief Immutable cold binding view for one instruction in one concrete function. */
struct BindingContext {
  /** @brief Full module tables and fresh native output owned by the lower call. */
  const ModuleBindings& module;
  /** @brief Exact current source declaration, retaining its real lexical scope. */
  const ptx_frontend::resolved_ir::ResolvedFunction& source_function;
  /** @brief Exact declaration/frame bindings, independent of canonical merging. */
  const FunctionBindings& function;
  /** @brief Source declaration position used only for structured diagnostics. */
  std::uint32_t function_index;
  /** @brief Local instruction position used only for structured diagnostics. */
  std::uint32_t instruction_index;
};

/** @brief Construct an identity/structure failure without an unsupported fallback. */
[[nodiscard]] inline auto malformed(const BindingContext& context,
                                    std::optional<std::uint32_t> symbol = std::nullopt)
    -> std::unexpected<LoweringError> {
  return std::unexpected(LoweringError{LoweringErrorCode::malformed_resolved_ir,
                                       context.function_index,
                                       context.instruction_index, symbol, std::nullopt});
}

/** @brief Bind all canonical declarations, signatures, storage and resource tables. */
[[nodiscard]] auto bind_module_metadata(ModuleBindings&) -> std::expected<void, LoweringError>;

}  // namespace ptxsim::exec_ir_lowering::detail
