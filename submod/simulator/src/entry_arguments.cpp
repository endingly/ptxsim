#include <ptxsim/simulator/simulator.hpp>

#include <algorithm>

namespace ptxsim::simulator {

auto pack_entry_arguments(const exec_ir::ExecutableProgram& program,
                          common::FunctionId function,
                          std::span<const std::span<const std::byte>> arguments)
    -> std::expected<std::vector<std::byte>, EntryArgumentError> {
  const auto layout = program.function_layout(function);
  if (!layout) {
    return std::unexpected(
        EntryArgumentError{.code = EntryArgumentErrorCode::program_error,
                           .program_error = layout.error()});
  }
  const auto& parameters = layout->get().entry_parameters;
  if (arguments.size() != parameters.size()) {
    return std::unexpected(EntryArgumentError{
        .code = EntryArgumentErrorCode::argument_count_mismatch,
        .expected = parameters.size(),
        .actual = arguments.size()});
  }
  for (std::size_t index = 0; index < parameters.size(); ++index) {
    if (arguments[index].size() != parameters[index].size) {
      return std::unexpected(EntryArgumentError{
          .code = EntryArgumentErrorCode::argument_size_mismatch,
          .argument_index = index,
          .expected = parameters[index].size,
          .actual = arguments[index].size()});
    }
  }

  std::vector<std::byte> packed(layout->get().entry_parameter_size);
  for (std::size_t index = 0; index < parameters.size(); ++index) {
    const auto destination = std::span{packed}.subspan(parameters[index].offset,
                                                       parameters[index].size);
    std::ranges::copy(arguments[index], destination.begin());
  }
  return packed;
}

}  // namespace ptxsim::simulator
