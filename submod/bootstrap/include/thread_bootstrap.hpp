#pragma once

#include <expected>
#include <optional>

#include <ptxsim/common/ids.hpp>
#include <ptxsim/program/program_image.hpp>
#include <ptxsim/state/thread_state.hpp>

namespace ptxsim::bootstrap {

enum class ThreadBootstrapErrorCode {
  unknown_function,
  not_entry_function,
  invalid_register_layout,
};

struct ThreadBootstrapError {
  ThreadBootstrapErrorCode code;
  common::FunctionId function;
  std::optional<state::RegisterError> register_error;

  constexpr bool operator==(const ThreadBootstrapError&) const noexcept =
      default;
};

[[nodiscard]] auto create_entry_thread(const program::ProgramImage& image,
                                       common::ThreadId thread,
                                       common::FunctionId entry)
    -> std::expected<state::ThreadState, ThreadBootstrapError>;

}  // namespace ptxsim::bootstrap
