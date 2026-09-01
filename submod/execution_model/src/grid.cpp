#include <ptxsim/execution_model/grid.hpp>

#include <cassert>
#include <cstdint>
#include <stdexcept>

namespace ptxsim::execution_model {

Grid::Grid(GridId id, GridShape shape) : id_(id), shape_(shape) {
  if (!shape_.valid()) {
    throw std::invalid_argument("execution_model::Grid: invalid grid shape");
  }

  const std::uint64_t total_ctas = shape_.cta_count();

  for (std::uint64_t linear_cta = 0; linear_cta < total_ctas; ++linear_cta) {
    const CtaId cta_id{
        .grid = id_,
        .value = linear_cta,
    };

    const Dim3 cta_position = delinearize(linear_cta, shape_.cta_dim);

    ctas_.emplace_back(*this, cta_id, cta_position, shape_.thread_dim,
                       shape_.warp_size);
  }

  build_index();
}

void Grid::build_index() {
  cta_index_.assign(shape_.cta_count(), nullptr);

  warp_index_.assign(shape_.warp_count(), nullptr);

  thread_index_.assign(shape_.thread_count(), nullptr);

  for (auto& cta : ctas_) {
    assert(cta.id().grid == id_);
    assert(cta.id().value < cta_index_.size());

    cta_index_[cta.id().value] = &cta;

    for (auto& warp : cta) {
      assert(warp.id().grid == id_);
      assert(warp.id().value < warp_index_.size());

      warp_index_[warp.id().value] = &warp;

      for (auto& thread : warp) {
        assert(thread.id().grid == id_);
        assert(thread.id().value < thread_index_.size());

        thread_index_[thread.id().value] = &thread;
      }
    }
  }

#ifndef NDEBUG
  for (const auto* cta : cta_index_) {
    assert(cta != nullptr);
  }

  for (const auto* warp : warp_index_) {
    assert(warp != nullptr);
  }

  for (const auto* thread : thread_index_) {
    assert(thread != nullptr);
  }
#endif
}

// -----------------------------------------------------------------------------
// Lookup
// -----------------------------------------------------------------------------

CTA* Grid::find_cta(CtaId id) noexcept {
  if (id.grid != id_ || id.value >= cta_index_.size()) {
    return nullptr;
  }

  return cta_index_[id.value];
}

const CTA* Grid::find_cta(CtaId id) const noexcept {
  if (id.grid != id_ || id.value >= cta_index_.size()) {
    return nullptr;
  }

  return cta_index_[id.value];
}

Warp* Grid::find_warp(WarpId id) noexcept {
  if (id.grid != id_ || id.value >= warp_index_.size()) {
    return nullptr;
  }

  return warp_index_[id.value];
}

const Warp* Grid::find_warp(WarpId id) const noexcept {
  if (id.grid != id_ || id.value >= warp_index_.size()) {
    return nullptr;
  }

  return warp_index_[id.value];
}

Thread* Grid::find_thread(ThreadId id) noexcept {
  if (id.grid != id_ || id.value >= thread_index_.size()) {
    return nullptr;
  }

  return thread_index_[id.value];
}

const Thread* Grid::find_thread(ThreadId id) const noexcept {
  if (id.grid != id_ || id.value >= thread_index_.size()) {
    return nullptr;
  }

  return thread_index_[id.value];
}

CTA& Grid::cta(CtaId id) noexcept {
  auto* ptr = find_cta(id);

  assert(ptr != nullptr);

  return *ptr;
}

const CTA& Grid::cta(CtaId id) const noexcept {
  auto* ptr = find_cta(id);

  assert(ptr != nullptr);

  return *ptr;
}

Warp& Grid::warp(WarpId id) noexcept {
  auto* ptr = find_warp(id);

  assert(ptr != nullptr);

  return *ptr;
}

const Warp& Grid::warp(WarpId id) const noexcept {
  auto* ptr = find_warp(id);

  assert(ptr != nullptr);

  return *ptr;
}

Thread& Grid::thread(ThreadId id) noexcept {
  auto* ptr = find_thread(id);

  assert(ptr != nullptr);

  return *ptr;
}

const Thread& Grid::thread(ThreadId id) const noexcept {
  auto* ptr = find_thread(id);

  assert(ptr != nullptr);

  return *ptr;
}

// -----------------------------------------------------------------------------
// Derived runtime state
// -----------------------------------------------------------------------------

std::size_t Grid::completed_cta_count() const noexcept {
  std::size_t count = 0;

  for (const auto& cta : ctas_) {
    if (cta.completed()) {
      ++count;
    }
  }

  return count;
}

std::uint64_t Grid::live_thread_count() const noexcept {
  std::uint64_t count = 0;

  for (const auto& cta : ctas_) {
    count += cta.live_thread_count();
  }

  return count;
}

bool Grid::completed() const noexcept {
  for (const auto& cta : ctas_) {
    if (!cta.completed()) {
      return false;
    }
  }

  return true;
}

bool Grid::trapped() const noexcept {
  for (const auto& cta : ctas_) {
    if (cta.trapped()) {
      return true;
    }
  }

  return false;
}

}  // namespace ptxsim::execution_model