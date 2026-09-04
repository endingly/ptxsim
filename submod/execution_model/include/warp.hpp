#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

#include <ptxsim/execution_model/forward_def.hpp>
#include <ptxsim/execution_model/ids.hpp>
#include <ptxsim/execution_model/thread.hpp>
#include <ptxsim/execution_model/warp_state.hpp>

namespace ptxsim::execution_model {

class Warp final {
 public:
  Warp(CTA& parent, WarpId id, std::uint32_t index_in_cta,
       std::uint32_t architectural_warp_size, std::uint32_t first_thread_linear,
       std::uint32_t thread_count);
  Warp(const Warp&) = delete;
  Warp& operator=(const Warp&) = delete;
  Warp(Warp&&) = delete;
  Warp& operator=(Warp&&) = delete;

  ~Warp() = default;

  [[nodiscard]]
  WarpId id() const noexcept {
    return id_;
  }

  [[nodiscard]]
  std::uint32_t index_in_cta() const noexcept {
    return index_in_cta_;
  }

  [[nodiscard]]
  std::uint32_t architectural_warp_size() const noexcept {
    return architectural_warp_size_;
  }

  [[nodiscard]]
  std::size_t thread_count() const noexcept {
    return threads_.size();
  }

  [[nodiscard]]
  bool full() const noexcept {
    return threads_.size() == architectural_warp_size_;
  }

  [[nodiscard]]
  Thread& thread(LaneId lane) noexcept;

  [[nodiscard]]
  const Thread& thread(LaneId lane) const noexcept;

  [[nodiscard]]
  std::vector<LaneId> runnable_lanes() const;

  [[nodiscard]]
  WarpExecutionState& execution_state() noexcept {
    return this->execution_state_;
  }

  [[nodiscard]]
  const WarpExecutionState& execution_state() const noexcept {
    return this->execution_state_;
  }

  [[nodiscard]]
  CTA& cta() noexcept;

  [[nodiscard]]
  const CTA& cta() const noexcept;

  [[nodiscard]]
  Grid& grid() noexcept;

  [[nodiscard]]
  const Grid& grid() const noexcept;

  auto begin() noexcept { return threads_.begin(); }

  auto end() noexcept { return threads_.end(); }

  auto begin() const noexcept { return threads_.begin(); }

  auto end() const noexcept { return threads_.end(); }

  template <typename Engine>
    requires requires(Engine& engine, Warp& warp,
                      const WarpIssueGroup& issue) {
      engine.step(warp, issue);
    }
  decltype(auto) step(Engine& engine, const WarpIssueGroup& issue)
      noexcept(noexcept(engine.step(*this, issue))) {
    return engine.step(*this, issue);
  }

  /**
   * @brief Return the immutable set of architecturally valid lanes.
   *
   * For a full warp every lane is valid. For the final partial warp of a CTA,
   * only the lanes corresponding to actual Threads are set.
   */
  [[nodiscard]]
  const LaneMask& valid_mask() const noexcept {
    return valid_mask_;
  }

  /**
   * @brief Return the lanes whose Threads are currently runnable.
   *
   * This mask is derived from ThreadStatus and is therefore not stored as
   * independent authoritative state.
   */
  [[nodiscard]]
  LaneMask ready_mask() const;

  /**
   * @brief Return the lanes whose Threads are waiting.
   */
  [[nodiscard]]
  LaneMask waiting_mask() const;

  /**
   * @brief Return the lanes whose Threads have exited.
   */
  [[nodiscard]]
  LaneMask exited_mask() const;

 private:
  friend class CTA;

  CTA* const parent_;

  WarpId id_;

  std::uint32_t index_in_cta_;
  std::uint32_t architectural_warp_size_;

  // Lanes that physically correspond to Threads in this warp.
  // For a partial warp, only the existing lanes are set.
  LaneMask valid_mask_;

  // Warp owns its Thread topology nodes.
  //
  // Thread objects remain at stable addresses for the whole lifetime
  // of this Warp. Each Thread stores an immutable back-reference to
  // this Warp.
  std::deque<Thread> threads_;

  // Mutable runtime execution state.
  WarpExecutionState execution_state_;
};

}  // namespace ptxsim::execution_model
