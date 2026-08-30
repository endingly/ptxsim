#pragma once

#include <expected>
#include <span>
#include <string>
#include <vector>

#include <ptxsim/common/ids.hpp>
#include <ptxsim/common/raw_value.hpp>
#include <ptxsim/state/register_file.hpp>

namespace ptxsim::state {

enum class ThreadStatus {
  ready,
  exited,
  trapped,
};

struct CallFrame {
  common::FunctionId function;
  common::ProgramCounter return_pc;

  constexpr bool operator==(const CallFrame&) const noexcept = default;
};

class ThreadState {
 public:
  ThreadState(const ThreadState&) = default;
  ThreadState(ThreadState&&) noexcept = default;
  auto operator=(const ThreadState&) -> ThreadState& = default;
  auto operator=(ThreadState&&) noexcept -> ThreadState& = default;

  [[nodiscard]] static auto create(
      common::ThreadId thread, common::FunctionId function,
      common::ProgramCounter initial_pc,
      std::vector<common::RawWidth> register_layout)
      -> std::expected<ThreadState, RegisterError>;

  [[nodiscard]] auto thread_id() const noexcept -> common::ThreadId;
  [[nodiscard]] auto current_function() const noexcept -> common::FunctionId;
  [[nodiscard]] auto current_pc() const noexcept -> common::ProgramCounter;
  [[nodiscard]] auto status() const noexcept -> ThreadStatus;
  [[nodiscard]] auto registers() noexcept -> RegisterFile&;
  [[nodiscard]] auto registers() const noexcept -> const RegisterFile&;
  [[nodiscard]] auto call_frames() const noexcept -> std::span<const CallFrame>;

 private:
  ThreadState(common::ThreadId thread, common::FunctionId function,
              common::ProgramCounter pc, RegisterFile registers) noexcept;

  common::ThreadId thread_;
  common::FunctionId function_;
  common::ProgramCounter pc_;
  ThreadStatus status_{ThreadStatus::ready};
  RegisterFile registers_;
  std::vector<CallFrame> call_frames_;
};

[[nodiscard]] auto dump(const ThreadState& thread) -> std::string;

}  // namespace ptxsim::state
