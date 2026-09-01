#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

#include <ptxsim/common/shape.hpp>
#include <ptxsim/execution_model/cta.hpp>
#include <ptxsim/execution_model/forward_def.hpp>
#include <ptxsim/execution_model/ids.hpp>

namespace ptxsim::execution_model {

/**
 * @brief Runtime execution-model node representing one launched PTX grid.
 *
 * Grid is the root of one execution-topology ownership tree:
 *
 *   Grid
 *    └── CTA
 *         └── Warp
 *              └── Thread
 *
 * The topology is immutable after construction. Grid, CTA, Warp, and Thread
 * nodes have stable runtime identity and are therefore neither copyable nor
 * movable.
 *
 * Program data and storage resources are intentionally not owned by this
 * class. Register files, shared/global/local memory, Tensor Memory, and
 * asynchronous transfer state belong to the memory subsystem and are resolved
 * through architectural IDs.
 *
 * Grid currently owns no independent mutable execution state. Grid-level
 * completion and trap state are derived from its child CTAs rather than stored
 * redundantly.
 */
class Grid final {
 public:
  Grid(GridId id, GridShape shape);

  Grid(const Grid&) = delete;
  Grid& operator=(const Grid&) = delete;
  Grid(Grid&&) = delete;
  Grid& operator=(Grid&&) = delete;

  ~Grid() = default;

  // -------------------------------------------------------------------------
  // Immutable topology
  // -------------------------------------------------------------------------

  /**
   * @brief Return the simulator identity of this grid instance.
   *
   * GridId identifies one launch instance and is distinct from a static kernel
   * definition.
   */
  [[nodiscard]]
  GridId id() const noexcept {
    return id_;
  }

  /**
   * @brief Return the complete immutable launch shape.
   */
  [[nodiscard]]
  const GridShape& shape() const noexcept {
    return shape_;
  }

  /**
   * @brief Return the number of CTAs in each grid dimension.
   *
   * This is the source of the PTX %nctaid value.
   */
  [[nodiscard]]
  Dim3 cta_shape() const noexcept {
    return shape_.cta_dim;
  }

  /**
   * @brief Return the number of threads in each CTA dimension.
   *
   * Every CTA in one PTX grid has the same thread shape.
   */
  [[nodiscard]]
  Dim3 thread_shape() const noexcept {
    return shape_.thread_dim;
  }

  /**
   * @brief Return the architectural warp size used by this grid.
   */
  [[nodiscard]]
  std::uint32_t warp_size() const noexcept {
    return shape_.warp_size;
  }

  [[nodiscard]]
  std::size_t cta_count() const noexcept {
    return cta_index_.size();
  }

  [[nodiscard]]
  std::size_t warp_count() const noexcept {
    return warp_index_.size();
  }

  [[nodiscard]]
  std::size_t thread_count() const noexcept {
    return thread_index_.size();
  }

  // -------------------------------------------------------------------------
  // O(1) architectural-ID lookup
  // -------------------------------------------------------------------------

  /**
   * @brief Find a CTA by its grid-scoped ID.
   *
   * Returns nullptr when the ID belongs to another grid or is out of range.
   */
  [[nodiscard]]
  CTA* find_cta(CtaId id) noexcept;

  [[nodiscard]]
  const CTA* find_cta(CtaId id) const noexcept;

  /**
   * @brief Find a Warp by its grid-scoped ID.
   */
  [[nodiscard]]
  Warp* find_warp(WarpId id) noexcept;

  [[nodiscard]]
  const Warp* find_warp(WarpId id) const noexcept;

  /**
   * @brief Find a Thread by its grid-scoped ID.
   */
  [[nodiscard]]
  Thread* find_thread(ThreadId id) noexcept;

  [[nodiscard]]
  const Thread* find_thread(ThreadId id) const noexcept;

  /**
   * @brief Return a CTA by ID.
   *
   * The ID must refer to a valid CTA in this grid.
   */
  [[nodiscard]]
  CTA& cta(CtaId id) noexcept;

  [[nodiscard]]
  const CTA& cta(CtaId id) const noexcept;

  /**
   * @brief Return a Warp by ID.
   */
  [[nodiscard]]
  Warp& warp(WarpId id) noexcept;

  [[nodiscard]]
  const Warp& warp(WarpId id) const noexcept;

  /**
   * @brief Return a Thread by ID.
   */
  [[nodiscard]]
  Thread& thread(ThreadId id) noexcept;

  [[nodiscard]]
  const Thread& thread(ThreadId id) const noexcept;

  // -------------------------------------------------------------------------
  // Derived runtime information
  // -------------------------------------------------------------------------

  /**
   * @brief Return the number of CTAs whose Threads have all exited.
   *
   * This value is derived from CTA/Thread state and is deliberately not stored
   * as an independent runtime counter.
   */
  [[nodiscard]]
  std::size_t completed_cta_count() const noexcept;

  /**
   * @brief Return the number of CTAs that have not completed.
   */
  [[nodiscard]]
  std::size_t live_cta_count() const noexcept {
    return cta_count() - completed_cta_count();
  }

  /**
   * @brief Return the number of Threads that have not exited.
   *
   * This is derived from child CTA state.
   */
  [[nodiscard]]
  std::uint64_t live_thread_count() const noexcept;

  /**
   * @brief Return true when every CTA has completed.
   */
  [[nodiscard]]
  bool completed() const noexcept;

  /**
   * @brief Return true when any child CTA contains a trapped Thread.
   *
   * The meaning of a trap for the enclosing simulator/runtime is intentionally
   * not handled here. Grid only exposes the derived execution-model fact.
   */
  [[nodiscard]]
  bool trapped() const noexcept;

  // -------------------------------------------------------------------------
  // Child iteration
  // -------------------------------------------------------------------------

  auto begin() noexcept { return ctas_.begin(); }

  auto end() noexcept { return ctas_.end(); }

  auto begin() const noexcept { return ctas_.begin(); }

  auto end() const noexcept { return ctas_.end(); }

 private:
  /**
   * @brief Build non-owning O(1) lookup tables after topology construction.
   *
   * Grid owns all topology nodes through the owner tree. The index tables only
   * cache stable pointers and never participate in object ownership.
   */
  void build_index();

  // -------------------------------------------------------------------------
  // Immutable topology
  // -------------------------------------------------------------------------

  GridId id_{};

  GridShape shape_{};

  /**
   * @brief CTA topology nodes owned by this Grid.
   *
   * std::deque provides stable element addresses during the append-only build
   * phase. The topology is frozen once the Grid constructor completes.
   */
  std::deque<CTA> ctas_;

  // -------------------------------------------------------------------------
  // Non-owning lookup indices
  // -------------------------------------------------------------------------

  std::vector<CTA*> cta_index_;
  std::vector<Warp*> warp_index_;
  std::vector<Thread*> thread_index_;
};

}  // namespace ptxsim::execution_model