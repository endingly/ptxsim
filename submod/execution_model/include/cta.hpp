#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>

#include "ptxsim/execution_model/cta_state.hpp"
#include "ptxsim/execution_model/forward_def.hpp"
#include "ptxsim/execution_model/ids.hpp"
#include "ptxsim/common/shape.hpp"
#include "ptxsim/execution_model/warp.hpp"

namespace ptxsim::execution_model {

/**
 * @brief Runtime execution-model node representing one CTA.
 *
 * CTA owns the Warp nodes that constitute its immutable execution topology.
 * Program storage such as shared memory and Tensor Memory is not owned here;
 * those resources are resolved through the memory subsystem using CtaId.
 *
 * CTA objects have stable identity and are therefore neither copyable nor
 * movable after construction.
 */
class CTA final {
 public:
  CTA(Grid& parent, CtaId id, Dim3 position, Dim3 thread_shape,
      std::uint32_t warp_size);

  CTA(const CTA&) = delete;
  CTA& operator=(const CTA&) = delete;
  CTA(CTA&&) = delete;
  CTA& operator=(CTA&&) = delete;

  ~CTA() = default;

  // -------------------------------------------------------------------------
  // Immutable topology
  // -------------------------------------------------------------------------

  [[nodiscard]]
  CtaId id() const noexcept {
    return id_;
  }

  /**
   * @brief Return the CTA coordinate within its Grid.
   *
   * This is the source of the PTX %ctaid value.
   */
  [[nodiscard]]
  Dim3 position() const noexcept {
    return position_;
  }

  /**
   * @brief Return the thread shape of this CTA.
   *
   * This is the source of the PTX %ntid value.
   */
  [[nodiscard]]
  Dim3 thread_shape() const noexcept {
    return thread_shape_;
  }

  [[nodiscard]]
  std::uint64_t thread_count() const noexcept {
    return thread_shape_.volume();
  }

  [[nodiscard]]
  std::uint32_t warp_size() const noexcept {
    return warp_size_;
  }

  [[nodiscard]]
  std::size_t warp_count() const noexcept {
    return warps_.size();
  }

  [[nodiscard]]
  Warp& warp(std::uint32_t index) noexcept {
    assert(index < warps_.size());
    return warps_[index];
  }

  [[nodiscard]]
  const Warp& warp(std::uint32_t index) const noexcept {
    assert(index < warps_.size());
    return warps_[index];
  }

  [[nodiscard]]
  Grid& grid() noexcept;

  [[nodiscard]]
  const Grid& grid() const noexcept;

  // -------------------------------------------------------------------------
  // Derived runtime information
  // -------------------------------------------------------------------------

  /**
   * @brief Return the number of Threads that have not exited.
   *
   * This value is derived from child Thread state rather than stored as a
   * second authoritative counter.
   */
  [[nodiscard]]
  std::uint64_t live_thread_count() const noexcept;

  /**
   * @brief Return true when every Thread in the CTA has exited.
   *
   * CTA completion is derived from Thread state and is intentionally not
   * stored in CtaExecutionState.
   */
  [[nodiscard]]
  bool completed() const noexcept;

  /**
   * @brief Return true when at least one Thread in the CTA is trapped.
   */
  [[nodiscard]]
  bool trapped() const noexcept;

  // -------------------------------------------------------------------------
  // Mutable CTA execution state
  // -------------------------------------------------------------------------

  [[nodiscard]]
  CtaExecutionState& execution_state() noexcept {
    return execution_state_;
  }

  [[nodiscard]]
  const CtaExecutionState& execution_state() const noexcept {
    return execution_state_;
  }

  // -------------------------------------------------------------------------
  // Child iteration
  // -------------------------------------------------------------------------

  auto begin() noexcept { return warps_.begin(); }

  auto end() noexcept { return warps_.end(); }

  auto begin() const noexcept { return warps_.begin(); }

  auto end() const noexcept { return warps_.end(); }

 private:
  // -------------------------------------------------------------------------
  // Immutable topology
  // -------------------------------------------------------------------------

  Grid* const parent_;

  CtaId id_{};

  Dim3 position_{};
  Dim3 thread_shape_{};

  std::uint32_t warp_size_ = 0;

  /**
   * @brief Warp topology nodes owned by this CTA.
   *
   * std::deque is used so that Warp addresses remain stable while the CTA is
   * built by appending child nodes. The topology is frozen after construction.
   */
  std::deque<Warp> warps_;

  // -------------------------------------------------------------------------
  // Mutable runtime state
  // -------------------------------------------------------------------------

  CtaExecutionState execution_state_;
};

}  // namespace ptxsim::execution_model