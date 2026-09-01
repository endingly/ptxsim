#include <ptxsim/execution_model/warp.hpp>

#include <cassert>
#include <cstdint>

#include <ptxsim/execution_model/cta.hpp>
#include <ptxsim/execution_model/grid.hpp>

namespace ptxsim::execution_model {
namespace {

LaneMask status_mask(const Warp& warp,
                     bool (Thread::*matches)() const noexcept) {
  LaneMask mask(warp.architectural_warp_size());
  for (const auto& thread : warp) {
    if ((thread.*matches)()) {
      mask.set(thread.lane_id());
    }
  }
  return mask;
}

}  // namespace

Warp::Warp(CTA& parent, WarpId id, std::uint32_t index_in_cta,
           std::uint32_t architectural_warp_size,
           std::uint32_t first_thread_linear, std::uint32_t thread_count)
    : parent_(&parent),
      id_(id),
      index_in_cta_(index_in_cta),
      architectural_warp_size_(architectural_warp_size),
      valid_mask_(architectural_warp_size) {
  assert(thread_count <= architectural_warp_size_);

  const auto thread_shape = parent.thread_shape();

  for (std::uint32_t lane = 0; lane < thread_count; ++lane) {
    const std::uint32_t thread_linear = first_thread_linear + lane;

    const auto position = delinearize(thread_linear, thread_shape);

    const ThreadId thread_id{
        .grid = id_.grid,
        .value = parent.id().value * parent.thread_count() + thread_linear,
    };

    threads_.emplace_back(*this, thread_id, position, thread_linear,
                          LaneId{lane});
    valid_mask_.set(LaneId{lane});
  }
}

Thread& Warp::thread(LaneId lane) noexcept {
  assert(lane.value < threads_.size());
  return threads_[lane.value];
}

const Thread& Warp::thread(LaneId lane) const noexcept {
  assert(lane.value < threads_.size());
  return threads_[lane.value];
}

std::vector<LaneId> Warp::runnable_lanes() const {
  std::vector<LaneId> lanes;
  lanes.reserve(threads_.size());
  for (const auto& thread : threads_) {
    if (thread.ready()) {
      lanes.push_back(thread.lane_id());
    }
  }
  return lanes;
}

LaneMask Warp::ready_mask() const {
  return status_mask(*this, &Thread::ready);
}

LaneMask Warp::waiting_mask() const {
  return status_mask(*this, &Thread::waiting);
}

LaneMask Warp::exited_mask() const {
  return status_mask(*this, &Thread::exited);
}

CTA& Warp::cta() noexcept {
  return *parent_;
}

const CTA& Warp::cta() const noexcept {
  return *parent_;
}

Grid& Warp::grid() noexcept {
  return parent_->grid();
}

const Grid& Warp::grid() const noexcept {
  return parent_->grid();
}

}  // namespace ptxsim::execution_model
