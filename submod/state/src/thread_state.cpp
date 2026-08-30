#include <ptxsim/state/thread_state.hpp>

#include <charconv>
#include <limits>
#include <utility>

namespace ptxsim::state {
namespace {

auto status_name(ThreadStatus status) -> const char* {
  switch (status) {
    case ThreadStatus::ready:
      return "ready";
    case ThreadStatus::exited:
      return "exited";
    case ThreadStatus::trapped:
      return "trapped";
  }
  return "invalid";
}

}  // namespace

ThreadState::ThreadState(common::ThreadId thread, common::FunctionId function,
                         common::ProgramCounter pc,
                         RegisterFile registers) noexcept
    : thread_(thread),
      function_(function),
      pc_(pc),
      registers_(std::move(registers)) {}

auto ThreadState::create(common::ThreadId thread, common::FunctionId function,
                         common::ProgramCounter initial_pc,
                         std::vector<common::RawWidth> register_layout)
    -> std::expected<ThreadState, RegisterError> {
  auto registers = RegisterFile::create(std::move(register_layout));
  if (!registers)
    return std::unexpected(registers.error());
  return ThreadState{thread, function, initial_pc, std::move(*registers)};
}

auto ThreadState::thread_id() const noexcept -> common::ThreadId {
  return thread_;
}

auto ThreadState::current_function() const noexcept -> common::FunctionId {
  return function_;
}

auto ThreadState::current_pc() const noexcept -> common::ProgramCounter {
  return pc_;
}

auto ThreadState::status() const noexcept -> ThreadStatus {
  return status_;
}

auto ThreadState::registers() noexcept -> RegisterFile& {
  return registers_;
}

auto ThreadState::registers() const noexcept -> const RegisterFile& {
  return registers_;
}

auto ThreadState::call_frames() const noexcept -> std::span<const CallFrame> {
  return call_frames_;
}

auto dump(const ThreadState& thread) -> std::string {
  std::string output = common::to_string(thread.thread_id());
  output += " ";
  output += common::to_string(thread.current_function());
  output += " ";
  output += common::to_string(thread.current_pc());
  output += " status:";
  output += status_name(thread.status());
  output += " call-depth:";
  char digits[std::numeric_limits<std::size_t>::digits10 + 1];
  const auto [end, error] = std::to_chars(digits, digits + sizeof(digits),
                                          thread.call_frames().size());
  (void)error;
  output.append(digits, end);
  output += "\n";
  output += dump(thread.registers());
  return output;
}

}  // namespace ptxsim::state
