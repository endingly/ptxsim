#include <ptxsim/bootstrap/thread_bootstrap.hpp>

#include <utility>
#include <vector>

namespace ptxsim::bootstrap {

auto create_entry_thread(const program::ProgramImage& image,
                         common::ThreadId thread, common::FunctionId entry)
    -> std::expected<state::ThreadState, ThreadBootstrapError> {
  const auto functions = image.functions();
  if (entry.value() >= functions.size() ||
      functions[entry.value()].id != entry) {
    return std::unexpected(ThreadBootstrapError{
        ThreadBootstrapErrorCode::unknown_function, entry, std::nullopt});
  }
  const auto& function = functions[entry.value()];

  bool is_entry = false;
  for (const auto id : image.entry_points()) {
    if (id == entry) {
      is_entry = true;
      break;
    }
  }
  if (!is_entry) {
    return std::unexpected(ThreadBootstrapError{
        ThreadBootstrapErrorCode::not_entry_function, entry, std::nullopt});
  }

  std::vector<common::RawWidth> layout;
  layout.reserve(function.registers.size());
  for (std::size_t index = 0; index < function.registers.size(); ++index) {
    const auto& register_layout = function.registers[index];
    if (register_layout.slot.value() != index) {
      return std::unexpected(ThreadBootstrapError{
          ThreadBootstrapErrorCode::invalid_register_layout, entry,
          std::nullopt});
    }
    layout.push_back(register_layout.width);
  }

  auto state = state::ThreadState::create(thread, function.id,
                                          function.begin_pc, std::move(layout));
  if (!state) {
    return std::unexpected(
        ThreadBootstrapError{ThreadBootstrapErrorCode::invalid_register_layout,
                             entry, state.error()});
  }
  return std::move(*state);
}

}  // namespace ptxsim::bootstrap
