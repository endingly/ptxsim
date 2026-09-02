#pragma once

#include <cassert>
#include <cstdint>
#include <optional>
#include <utility>
#include <ptxsim/execution_model/lane_mask.hpp>
#include <ptxsim/execution_model/execution_state.hpp>

namespace ptxsim::execution_model {

/**
 * @brief A transient group of lanes selected to issue one instruction.
 *
 * A WarpIssueGroup is produced by the scheduling layer by grouping runnable
 * lanes according to their current per-thread program counters.
 *
 * It is intentionally not persistent warp state. The authoritative program
 * counter remains stored in each Thread.
 *
 * Example:
 *
 *   lane 0 -> PC 10
 *   lane 1 -> PC 10
 *   lane 2 -> PC 20
 *   lane 3 -> PC 10
 *
 * may produce:
 *
 *   WarpIssueGroup {
 *     pc    = 10,
 *     lanes = {0, 1, 3}
 *   }
 */
struct WarpIssueGroup {
  ProgramCounter pc{0};
  LaneMask lanes;

  /**
   * @brief Return true if no lanes participate in this issue group.
   */
  [[nodiscard]]
  bool empty() const noexcept {
    return lanes.none();
  }

  /**
   * @brief Return the number of participating lanes.
   */
  [[nodiscard]]
  std::size_t size() const noexcept {
    return lanes.count();
  }
};

/**
 * @brief Persistent state for one in-progress warp-scoped rendezvous.
 *
 * Some PTX instructions require a specified set of lanes to reach the same
 * synchronization point before execution may continue.
 *
 * This object records:
 *
 * - the instruction location associated with the rendezvous;
 * - the logical generation of the synchronization point;
 * - the set of lanes required to participate;
 * - the subset that has already arrived.
 *
 * ProgramCounter and generation together distinguish repeated execution of
 * the same synchronization instruction, for example when it appears inside
 * a loop.
 */
class WarpRendezvous final {
 public:
  WarpRendezvous(ProgramCounter pc, std::uint64_t generation,
                 LaneMask participants)
      : pc_(pc),
        generation_(generation),
        participants_(std::move(participants)),
        arrivals_(participants_.size()) {}

  /**
   * @brief Return the instruction location that owns this rendezvous.
   */
  [[nodiscard]]
  ProgramCounter pc() const noexcept {
    return pc_;
  }

  /**
   * @brief Return the logical rendezvous generation.
   */
  [[nodiscard]]
  std::uint64_t generation() const noexcept {
    return generation_;
  }

  /**
   * @brief Return the lanes expected to participate.
   */
  [[nodiscard]]
  const LaneMask& participants() const noexcept {
    return participants_;
  }

  /**
   * @brief Return the lanes that have already arrived.
   */
  [[nodiscard]]
  const LaneMask& arrivals() const noexcept {
    return arrivals_;
  }

  /**
   * @brief Record the arrival of one lane.
   *
   * The lane must belong to the participant set.
   */
  void arrive(LaneId lane) noexcept {
    assert(participants_.test(lane));
    arrivals_.set(lane);
  }

  /**
   * @brief Record the arrival of multiple lanes.
   *
   * Every arriving lane must belong to the participant set.
   */
  void arrive(const LaneMask& lanes) noexcept {
    assert(participants_.contains(lanes));
    arrivals_.merge(lanes);
  }

  /**
   * @brief Return true if a lane has already arrived.
   */
  [[nodiscard]]
  bool has_arrived(LaneId lane) const noexcept {
    return arrivals_.test(lane);
  }

  /**
   * @brief Return true when every required lane has arrived.
   */
  [[nodiscard]]
  bool complete() const noexcept {
    return arrivals_.contains(participants_);
  }

 private:
  ProgramCounter pc_{0};

  std::uint64_t generation_ = 0;

  LaneMask participants_;
  LaneMask arrivals_;
};

/**
 * @brief Persistent warp-scoped synchronization state.
 *
 * This state belongs to the execution model because it describes the
 * progress of warp-level execution rather than program data.
 *
 * The current implementation intentionally permits at most one outstanding
 * warp rendezvous per Warp.
 *
 * This is an MVP invariant rather than a permanent architectural
 * restriction. If future PTX semantics require multiple independent
 * rendezvous to coexist within one warp, `pending_` may be generalized into
 * a collection without changing Thread or memory ownership.
 */
class WarpSyncState final {
 public:
  WarpSyncState() = default;

  /**
   * @brief Return true if the warp currently has a pending rendezvous.
   */
  [[nodiscard]]
  bool active() const noexcept {
    return pending_.has_value();
  }

  /**
   * @brief Return the current rendezvous.
   *
   * The caller must ensure that active() is true.
   */
  [[nodiscard]]
  WarpRendezvous& pending() noexcept {
    assert(pending_.has_value());
    return *pending_;
  }

  /**
   * @brief Return the current rendezvous.
   *
   * The caller must ensure that active() is true.
   */
  [[nodiscard]]
  const WarpRendezvous& pending() const noexcept {
    assert(pending_.has_value());
    return *pending_;
  }

  /**
   * @brief Start a new warp rendezvous.
   *
   * A warp may have only one pending rendezvous in the current execution
   * model. Starting another rendezvous before the previous one is cleared is
   * considered an execution-model error.
   */
  WarpRendezvous& begin(ProgramCounter pc, LaneMask participants) {
    assert(!pending_.has_value());

    pending_.emplace(pc, next_generation_++, std::move(participants));

    return *pending_;
  }

  /**
   * @brief Clear the completed rendezvous.
   *
   * The rendezvous must have completed before it may be removed.
   */
  void clear_completed() noexcept {
    assert(pending_.has_value());
    assert(pending_->complete());

    pending_.reset();
  }

  /**
   * @brief Forcefully discard the current rendezvous.
   *
   * This operation is intended for trap/reset/destruction paths where normal
   * synchronization completion is no longer required.
   */
  void reset() noexcept { pending_.reset(); }

  /**
   * @brief Return the generation number that will be assigned to the next
   * rendezvous.
   */
  [[nodiscard]]
  std::uint64_t next_generation() const noexcept {
    return next_generation_;
  }

 private:
  std::uint64_t next_generation_ = 0;

  std::optional<WarpRendezvous> pending_;
};

/**
 * @brief Mutable execution state owned by one Warp.
 *
 * Only state that must persist across execution steps and is genuinely
 * warp-scoped belongs here.
 *
 * Derived information such as ready, waiting, or exited lane masks is not
 * stored here because the authoritative status is held by each Thread.
 *
 * Likewise, the currently issued WarpIssueGroup is not stored here: it is a
 * transient scheduling result derived from per-thread state.
 */
struct WarpExecutionState {
  WarpSyncState sync;
};

}  // namespace ptxsim::execution_model
