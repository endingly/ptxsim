#include <ptxsim/execution_model/cta.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>

#include <ptxsim/execution_model/grid.hpp>
#include <ptxsim/execution_model/thread.hpp>

namespace ptxsim::execution_model {

namespace {

/**
 * @brief Compute the number of warps required by one CTA.
 */
[[nodiscard]]
std::uint64_t compute_warp_count(Dim3 thread_shape,
                                 std::uint32_t warp_size) noexcept {
  assert(warp_size != 0);

  const auto thread_count = thread_shape.volume();

  return (thread_count + warp_size - 1) / warp_size;
}

}  // namespace

CTA::CTA(Grid& parent, CtaId id, Dim3 position, Dim3 thread_shape,
         std::uint32_t warp_size)
    : parent_(&parent),
      id_(id),
      position_(position),
      thread_shape_(thread_shape),
      warp_size_(warp_size),
      execution_state_(static_cast<std::size_t>(
          compute_warp_count(thread_shape, warp_size))) {
  assert(warp_size_ != 0);

  const std::uint64_t total_threads = thread_shape_.volume();

  const std::uint64_t total_warps =
      compute_warp_count(thread_shape_, warp_size_);

  for (std::uint64_t warp_index = 0; warp_index < total_warps; ++warp_index) {
    const std::uint64_t first_thread = warp_index * warp_size_;

    const std::uint64_t remaining = total_threads - first_thread;

    const auto thread_count = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(remaining, warp_size_));

    const WarpId warp_id{
        .grid = id_.grid,
        .value = id_.value * total_warps + warp_index,
    };

    warps_.emplace_back(*this, warp_id, static_cast<std::uint32_t>(warp_index),
                        warp_size_, static_cast<std::uint32_t>(first_thread),
                        thread_count);
  }
}

Grid& CTA::grid() noexcept {
  return *parent_;
}

const Grid& CTA::grid() const noexcept {
  return *parent_;
}

std::uint64_t CTA::live_thread_count() const noexcept {
  std::uint64_t count = 0;

  for (const auto& warp : warps_) {
    for (const auto& thread : warp) {
      if (!thread.exited()) {
        ++count;
      }
    }
  }

  return count;
}

bool CTA::completed() const noexcept {
  for (const auto& warp : warps_) {
    for (const auto& thread : warp) {
      if (!thread.exited()) {
        return false;
      }
    }
  }

  return true;
}

bool CTA::trapped() const noexcept {
  for (const auto& warp : warps_) {
    for (const auto& thread : warp) {
      if (thread.trapped()) {
        return true;
      }
    }
  }

  return false;
}

}  // namespace ptxsim::execution_model