#include <ptxsim/exec_ir_lowering/exec_ir_lowering.hpp>

#include <limits>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>

#include "binding_bridge.gen.hpp"
#include "lowering_detail.hpp"

namespace ptxsim::exec_ir_lowering {

auto lower(const ptx_frontend::resolved_ir::ResolvedModule& module)
    -> std::expected<exec_ir::ExecutableProgram, LoweringError> {
  if (module.functions.size() > std::numeric_limits<std::uint32_t>::max())
    return std::unexpected(LoweringError{LoweringErrorCode::malformed_resolved_ir});
  // The frontend class hierarchy is open. Validate actual final identity before
  // its visitors/checkers can read any kind-specific fields through a cast.
  for (std::size_t function = 0; function < module.functions.size(); ++function) {
    const auto& source = module.functions[function];
    if (source.body.size() > std::numeric_limits<std::uint32_t>::max())
      return std::unexpected(LoweringError{LoweringErrorCode::malformed_resolved_ir,
                                           static_cast<std::uint32_t>(function)});
    for (std::size_t instruction = 0; instruction < source.body.size(); ++instruction) {
      if (!detail::preflight_instruction(source.body[instruction].get()))
        return std::unexpected(LoweringError{LoweringErrorCode::malformed_resolved_ir,
            static_cast<std::uint32_t>(function), static_cast<std::uint32_t>(instruction)});
    }
  }
  // AvailableContext preserves targetless input while still checking complete
  // module identities, declaration ABI, scope/cache joins and metadata tables.
  // Instruction-local required/layout/control/leaf checks remain in the bridge.
  const auto checked = ptx_frontend::resolved_ir::validateModule(
      module, ptx_frontend::resolved_ir::ModuleValidationPolicy::AvailableContext);
  if (!checked)
    return std::unexpected(LoweringError{LoweringErrorCode::malformed_resolved_ir});
  detail::ModuleBindings state{module};
  if (const auto metadata = detail::bind_module_metadata(state); !metadata)
    return std::unexpected(metadata.error());
  for (std::size_t position = 0; position < module.functions.size(); ++position) {
    const auto& source = module.functions[position];
    if (source.is_prototype) continue;
    const auto& binding = state.declarations[position];
    auto& layout = *state.output.functions[binding.function.value()].body;
    layout.begin = state.output.instructions.size();
    for (std::size_t instruction = 0; instruction < source.body.size(); ++instruction) {
      const detail::BindingContext context{state, source, binding,
          static_cast<std::uint32_t>(position), static_cast<std::uint32_t>(instruction)};
      auto result = detail::bind_instruction(*source.body[instruction], context);
      if (!result) return std::unexpected(result.error());
      state.output.instructions.push_back(std::move(*result));
    }
  }
  auto result = exec_ir::ExecutableProgram::create(state.output);
  if (!result)
    return std::unexpected(LoweringError{LoweringErrorCode::program_validation_failed,
                                         std::nullopt, std::nullopt, std::nullopt, result.error()});
  return std::move(*result);
}
}  // namespace ptxsim::exec_ir_lowering
