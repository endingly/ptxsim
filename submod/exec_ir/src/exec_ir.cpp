#include <ptxsim/exec_ir/exec_ir.hpp>

#include <iterator>
#include <limits>
#include <utility>

#include <fmt/format.h>

namespace ptxsim::exec_ir {
namespace {

[[nodiscard]] constexpr auto valid_width(common::RawWidth width) noexcept
    -> bool {
  switch (width) {
    case common::RawWidth::pred:
    case common::RawWidth::b8:
    case common::RawWidth::b16:
    case common::RawWidth::b32:
    case common::RawWidth::b64:
    case common::RawWidth::b128:
      return true;
  }
  return false;
}

[[nodiscard]] auto error(
    ProgramErrorCode code,
    std::optional<common::FunctionId> function = std::nullopt,
    std::optional<common::ProgramCounter> pc = std::nullopt,
    std::optional<common::RawWidth> actual = std::nullopt)
    -> std::unexpected<ProgramError> {
  return std::unexpected(ProgramError{code, function, pc, actual});
}

[[nodiscard]] auto layout_for(const std::vector<FunctionLayout>& functions,
                              common::FunctionId function)
    -> const FunctionLayout* {
  const auto index = static_cast<std::size_t>(function.value());
  if (index >= functions.size()) {
    return nullptr;
  }
  return &functions[index];
}

[[nodiscard]] auto validate_location(
    const std::vector<FunctionLayout>& functions, common::CodeLocation location)
    -> std::expected<const FunctionLayout*, ProgramError> {
  const auto* layout = layout_for(functions, location.function);
  if (layout == nullptr) {
    return error(ProgramErrorCode::function_not_found, location.function,
                 location.pc);
  }
  if (location.pc.value() >= layout->instruction_count) {
    return error(ProgramErrorCode::pc_out_of_range, location.function,
                 location.pc);
  }
  return layout;
}

}  // namespace

ExecutableProgram::ExecutableProgram(ProgramDefinition definition) noexcept
    : instructions_(std::move(definition.instructions)),
      functions_(std::move(definition.functions)) {}

auto ExecutableProgram::create(ProgramDefinition definition)
    -> std::expected<ExecutableProgram, ProgramError> {
  if (definition.functions.size() >
      static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
    return error(ProgramErrorCode::function_count_not_representable);
  }

  std::size_t expected_begin = 0;
  for (std::size_t index = 0; index < definition.functions.size(); ++index) {
    const auto& layout = definition.functions[index];
    if (layout.id.value() != index) {
      return error(ProgramErrorCode::function_id_not_dense, layout.id);
    }
    if (layout.begin != expected_begin) {
      return error(ProgramErrorCode::invalid_layout, layout.id);
    }
    if (layout.begin > definition.instructions.size() ||
        static_cast<std::size_t>(layout.instruction_count) >
            definition.instructions.size() - layout.begin) {
      return error(ProgramErrorCode::invalid_layout_range, layout.id);
    }
    for (const auto width : layout.register_widths) {
      if (!valid_width(width)) {
        return error(ProgramErrorCode::invalid_register_width, layout.id,
                     std::nullopt, width);
      }
    }
    expected_begin = layout.begin + layout.instruction_count;
  }
  if (expected_begin != definition.instructions.size()) {
    return error(ProgramErrorCode::invalid_layout);
  }

  return ExecutableProgram{std::move(definition)};
}

auto ExecutableProgram::function_layout(common::FunctionId function) const
    -> std::expected<std::reference_wrapper<const FunctionLayout>,
                     ProgramError> {
  const auto* layout = layout_for(functions_, function);
  if (layout == nullptr) {
    return error(ProgramErrorCode::function_not_found, function);
  }
  return std::cref(*layout);
}

auto ExecutableProgram::fetch(common::CodeLocation location) const
    -> std::expected<std::reference_wrapper<const Instruction>, ProgramError> {
  const auto layout = validate_location(functions_, location);
  if (!layout) {
    return std::unexpected(layout.error());
  }
  return std::cref(instructions_[(*layout)->begin + location.pc.value()]);
}

auto ExecutableProgram::flat_offset(common::CodeLocation location) const
    -> std::expected<std::size_t, ProgramError> {
  const auto layout = validate_location(functions_, location);
  if (!layout) {
    return std::unexpected(layout.error());
  }
  return (*layout)->begin + location.pc.value();
}

auto ExecutableProgram::fallthrough(common::CodeLocation location) const
    -> std::expected<common::CodeLocation, ProgramError> {
  const auto layout = validate_location(functions_, location);
  if (!layout) {
    return std::unexpected(layout.error());
  }
  if (location.pc.value() + 1U >= (*layout)->instruction_count) {
    return error(ProgramErrorCode::no_fallthrough, location.function,
                 location.pc);
  }
  return common::CodeLocation{location.function,
                              common::ProgramCounter{location.pc.value() + 1U}};
}

auto to_string(const ExecutableProgram& program) -> std::string {
  std::string output;
  for (const auto& layout : program.functions_) {
    for (std::uint32_t local_pc = 0; local_pc < layout.instruction_count;
         ++local_pc) {
      if (!output.empty()) {
        output.push_back('\n');
      }
      const auto flat_offset =
          layout.begin + static_cast<std::size_t>(local_pc);
      fmt::format_to(std::back_inserter(output), "gpc{}  [func:{} pc:{}]  {}",
                     flat_offset, layout.id.value(), local_pc,
                     to_string(program.instructions_[flat_offset]));
    }
  }
  return output;
}

}  // namespace ptxsim::exec_ir
