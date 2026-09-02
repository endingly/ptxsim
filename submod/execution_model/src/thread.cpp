#include <ptxsim/execution_model/cta.hpp>
#include <ptxsim/execution_model/grid.hpp>
#include <ptxsim/execution_model/thread.hpp>
#include <ptxsim/execution_model/warp.hpp>

namespace ptxsim::execution_model {

Thread::Thread(Warp& parent, ThreadId id, Dim3 position,
               std::uint32_t linear_index_in_cta, LaneId lane_id) noexcept
    : parent_(&parent),
      id_(id),
      position_(position),
      linear_index_in_cta_(linear_index_in_cta),
      lane_id_(lane_id) {}

Warp& Thread::warp() noexcept {
  return *parent_;
}

const Warp& Thread::warp() const noexcept {
  return *parent_;
}

CTA& Thread::cta() noexcept {
  return parent_->cta();
}

const CTA& Thread::cta() const noexcept {
  return parent_->cta();
}

Grid& Thread::grid() noexcept {
  return parent_->grid();
}

const Grid& Thread::grid() const noexcept {
  return parent_->grid();
}

}  // namespace ptxsim::execution_model