#pragma once

#include <cstdint>
#include <ptxsim/common/shape.hpp>
#include <ptxsim/execution_model/forward_def.hpp>
#include <ptxsim/execution_model/ids.hpp>
#include <ptxsim/execution_model/execution_state.hpp>

namespace ptxsim::execution_model {

enum class ThreadStatus : std::uint8_t {
  Ready,
  Waiting,
  Exited,
  Trapped,
};

enum class WaitReason : std::uint8_t {
  None,
  WarpSync,
  CtaBarrier,
  AsyncOperation,
  Other,
};

struct ThreadExecutionState {
  ProgramCounter pc{};
  ThreadStatus status{ThreadStatus::Ready};

  WaitReason wait_reason{WaitReason::None};

  // CallStack call_stack;
};

class Thread final {
 public:
  Thread(Warp& parent, ThreadId id, Dim3 position,
         std::uint32_t linear_index_in_cta, LaneId lane_id) noexcept;
  Thread(const Thread&) = delete;
  Thread& operator=(const Thread&) = delete;
  Thread(Thread&&) = delete;
  Thread& operator=(Thread&&) = delete;

  ~Thread() = default;

  [[nodiscard]]
  ThreadId id() const noexcept {
    return id_;
  }

  [[nodiscard]]
  Dim3 position() const noexcept {
    return position_;
  }

  [[nodiscard]]
  std::uint32_t linear_index_in_cta() const noexcept {
    return linear_index_in_cta_;
  }

  [[nodiscard]]
  LaneId lane_id() const noexcept {
    return lane_id_;
  }

  [[nodiscard]]
  ProgramCounter pc() const noexcept {
    return this->state_.pc;
  }

  void set_pc(ProgramCounter pc) noexcept { this->state_.pc = pc; }

  [[nodiscard]]
  ThreadStatus status() const noexcept {
    return this->state_.status;
  }

  [[nodiscard]]
  bool ready() const noexcept {
    return this->status() == ThreadStatus::Ready;
  }

  [[nodiscard]]
  bool waiting() const noexcept {
    return this->status() == ThreadStatus::Waiting;
  }

  [[nodiscard]]
  bool exited() const noexcept {
    return this->status() == ThreadStatus::Exited;
  }

  [[nodiscard]]
  bool trapped() const noexcept {
    return this->status() == ThreadStatus::Trapped;
  }

  void mark_ready() noexcept { this->state_.status = ThreadStatus::Ready; }

  void mark_waiting() noexcept { this->state_.status = ThreadStatus::Waiting; }

  void mark_exited() noexcept { this->state_.status = ThreadStatus::Exited; }

  void mark_trapped() noexcept { this->state_.status = ThreadStatus::Trapped; }

  [[nodiscard]]
  Warp& warp() noexcept;

  [[nodiscard]]
  const Warp& warp() const noexcept;

  [[nodiscard]]
  CTA& cta() noexcept;

  [[nodiscard]]
  const CTA& cta() const noexcept;

  [[nodiscard]]
  Grid& grid() noexcept;

  [[nodiscard]]
  const Grid& grid() const noexcept;

  /*
   * Thin facade only.
   *
   * execution_model does not depend on executor. The concrete engine
   * only needs to provide:
   *
   *     engine.step(Thread&)
   *
   * No instruction semantics belong to Thread itself.
   */
  template <typename Engine>
  decltype(auto) step(Engine& engine) {
    return engine.step(*this);
  }

 private:
  friend class Warp;
  Warp* const parent_;
  ThreadId id_;
  Dim3 position_;
  std::uint32_t linear_index_in_cta_;
  LaneId lane_id_;

  ThreadExecutionState state_;
};

}  // namespace ptxsim::execution_model