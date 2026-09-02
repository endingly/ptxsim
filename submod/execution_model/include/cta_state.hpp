#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <sul/dynamic_bitset.hpp>

namespace ptxsim::execution_model {

/**
 * @brief Identifier of one architectural CTA barrier resource.
 *
 * PTX exposes sixteen CTA barrier resources numbered from 0 through 15.
 */
struct CtaBarrierId {
  std::uint32_t value = 0;

  auto operator<=>(const CtaBarrierId&) const = default;
};

/**
 * @brief Number of architecturally available CTA barrier resources.
 */
inline constexpr std::size_t kCtaBarrierCount = 16;

/**
 * @brief A set of warps within one CTA.
 *
 * WarpMask is used for CTA-scoped execution state where membership is
 * naturally expressed in terms of warps rather than lanes.
 *
 * Bit position N corresponds to the warp whose CTA-local index is N.
 *
 * The underlying dynamic_bitset is intentionally hidden so that callers do
 * not depend on its concrete representation.
 */
class WarpMask final {
 public:
  WarpMask() = default;

  /**
   * @brief Construct an empty mask for @p warp_count warps.
   */
  explicit WarpMask(std::size_t warp_count) { resize(warp_count); }

  /**
   * @brief Resize the mask and clear all bits.
   *
   * This function is intended for topology initialization. A WarpMask that
   * already participates in runtime state should not normally be resized.
   */
  void resize(std::size_t warp_count) {
    bits_.resize(warp_count);

    for (std::size_t i = 0; i < warp_count; ++i) {
      bits_[i] = false;
    }
  }

  [[nodiscard]]
  std::size_t size() const noexcept {
    return bits_.size();
  }

  [[nodiscard]]
  bool empty() const noexcept {
    return size() == 0;
  }

  [[nodiscard]]
  bool test(std::size_t warp_index) const noexcept {
    assert(warp_index < size());
    return bits_[warp_index];
  }

  void set(std::size_t warp_index) noexcept {
    assert(warp_index < size());
    bits_[warp_index] = true;
  }

  void reset(std::size_t warp_index) noexcept {
    assert(warp_index < size());
    bits_[warp_index] = false;
  }

  void clear() noexcept {
    for (std::size_t i = 0; i < size(); ++i) {
      bits_[i] = false;
    }
  }

  [[nodiscard]]
  bool any() const noexcept {
    for (std::size_t i = 0; i < size(); ++i) {
      if (bits_[i]) {
        return true;
      }
    }

    return false;
  }

  [[nodiscard]]
  bool none() const noexcept {
    return !any();
  }

  [[nodiscard]]
  std::size_t count() const noexcept {
    std::size_t result = 0;

    for (std::size_t i = 0; i < size(); ++i) {
      if (bits_[i]) {
        ++result;
      }
    }

    return result;
  }

  [[nodiscard]]
  bool contains(const WarpMask& other) const noexcept {
    assert(size() == other.size());

    for (std::size_t i = 0; i < size(); ++i) {
      if (other.bits_[i] && !bits_[i]) {
        return false;
      }
    }

    return true;
  }

  void merge(const WarpMask& other) noexcept {
    assert(size() == other.size());

    for (std::size_t i = 0; i < size(); ++i) {
      bits_[i] = bits_[i] || other.bits_[i];
    }
  }

  [[nodiscard]]
  friend bool operator==(const WarpMask& lhs, const WarpMask& rhs) noexcept {
    if (lhs.size() != rhs.size()) {
      return false;
    }

    for (std::size_t i = 0; i < lhs.size(); ++i) {
      if (lhs.bits_[i] != rhs.bits_[i]) {
        return false;
      }
    }

    return true;
  }

 private:
  sul::dynamic_bitset<> bits_;
};

/**
 * @brief Protocol used by one active generation of a CTA barrier.
 *
 * Sync and arrive share the same barrier protocol because PTX explicitly
 * allows different warps to use barrier.sync and barrier.arrive on the same
 * active barrier resource.
 *
 * Reduction barriers are kept distinct because PTX does not permit mixing
 * barrier.red with sync/arrive on the same active barrier.
 */
enum class CtaBarrierProtocol : std::uint8_t {
  SyncArrive,
  ReduceAnd,
  ReduceOr,
  ReducePopc,
};

/**
 * @brief Persistent state of one active generation of a CTA barrier.
 *
 * A CTA barrier is a named reusable synchronization resource. Each use of a
 * barrier belongs to one logical generation. Once the required number of
 * participating threads has arrived and the generation is released, the same
 * barrier resource may be reused for the next generation.
 *
 * CTA barrier arrival is represented at warp granularity here. Lane-level
 * convergence within one warp is resolved before calling arrive_warp().
 *
 * This separation follows the PTX execution model: a thread executing a CTA
 * barrier first waits for the other required non-exited threads in its warp,
 * and the warp then contributes an arrival to the CTA barrier.
 */
class CtaBarrierGeneration final {
 public:
  CtaBarrierGeneration(std::size_t warp_count, std::uint64_t generation,
                       std::uint32_t expected_threads,
                       CtaBarrierProtocol protocol)
      : generation_(generation),
        expected_threads_(expected_threads),
        protocol_(protocol),
        arrived_warps_(warp_count),
        waiting_warps_(warp_count) {
    assert(expected_threads_ != 0);

    switch (protocol_) {
      case CtaBarrierProtocol::ReduceAnd:
        reduction_predicate_ = true;
        break;

      case CtaBarrierProtocol::ReduceOr:
        reduction_predicate_ = false;
        break;

      case CtaBarrierProtocol::ReducePopc:
      case CtaBarrierProtocol::SyncArrive:
        break;
    }
  }

  [[nodiscard]]
  std::uint64_t generation() const noexcept {
    return generation_;
  }

  [[nodiscard]]
  std::uint32_t expected_threads() const noexcept {
    return expected_threads_;
  }

  [[nodiscard]]
  std::uint32_t arrived_threads() const noexcept {
    return arrived_threads_;
  }

  [[nodiscard]]
  CtaBarrierProtocol protocol() const noexcept {
    return protocol_;
  }

  [[nodiscard]]
  const WarpMask& arrived_warps() const noexcept {
    return arrived_warps_;
  }

  [[nodiscard]]
  const WarpMask& waiting_warps() const noexcept {
    return waiting_warps_;
  }

  [[nodiscard]]
  bool warp_arrived(std::size_t warp_index) const noexcept {
    return arrived_warps_.test(warp_index);
  }

  [[nodiscard]]
  bool warp_waiting(std::size_t warp_index) const noexcept {
    return waiting_warps_.test(warp_index);
  }

  /**
   * @brief Record one logical warp arrival for sync/arrive barriers.
   *
   * @param warp_index CTA-local warp index.
   * @param participating_threads Number of threads contributed by this warp.
   * @param waits Whether the warp executes a waiting form such as bar.sync.
   *
   * Each warp may contribute at most one arrival to one barrier generation.
   */
  void arrive_warp(std::size_t warp_index, std::uint32_t participating_threads,
                   bool waits) noexcept {
    assert(protocol_ == CtaBarrierProtocol::SyncArrive);
    assert(participating_threads != 0);
    assert(!arrived_warps_.test(warp_index));
    assert(arrived_threads_ + participating_threads <= expected_threads_);

    arrived_warps_.set(warp_index);
    arrived_threads_ += participating_threads;

    if (waits) {
      waiting_warps_.set(warp_index);
    }
  }

  /**
   * @brief Record one logical warp arrival for a reduction barrier.
   *
   * @param warp_index CTA-local warp index.
   * @param participating_threads Number of participating threads in the warp.
   * @param true_predicates Number of participating threads whose reduction
   *        predicate evaluates to true.
   *
   * Reduction semantics are accumulated at CTA-barrier scope, while the
   * actual destination register writeback remains the responsibility of the
   * instruction execution layer and RegisterManager.
   */
  void arrive_reduction_warp(std::size_t warp_index,
                             std::uint32_t participating_threads,
                             std::uint32_t true_predicates) noexcept {
    assert(protocol_ != CtaBarrierProtocol::SyncArrive);
    assert(participating_threads != 0);
    assert(true_predicates <= participating_threads);
    assert(!arrived_warps_.test(warp_index));
    assert(arrived_threads_ + participating_threads <= expected_threads_);

    arrived_warps_.set(warp_index);
    waiting_warps_.set(warp_index);

    arrived_threads_ += participating_threads;

    switch (protocol_) {
      case CtaBarrierProtocol::ReduceAnd:
        reduction_predicate_ =
            reduction_predicate_ && (true_predicates == participating_threads);
        break;

      case CtaBarrierProtocol::ReduceOr:
        reduction_predicate_ = reduction_predicate_ || (true_predicates != 0);
        break;

      case CtaBarrierProtocol::ReducePopc:
        reduction_popc_ += true_predicates;
        break;

      case CtaBarrierProtocol::SyncArrive:
        assert(false);
        break;
    }
  }

  /**
   * @brief Return true once the required number of threads has arrived.
   */
  [[nodiscard]]
  bool complete() const noexcept {
    return arrived_threads_ == expected_threads_;
  }

  /**
   * @brief Return the result of barrier.red.and / barrier.red.or.
   *
   * The barrier must be complete and use a predicate reduction protocol.
   */
  [[nodiscard]]
  bool predicate_result() const noexcept {
    assert(complete());
    assert(protocol_ == CtaBarrierProtocol::ReduceAnd ||
           protocol_ == CtaBarrierProtocol::ReduceOr);

    return reduction_predicate_;
  }

  /**
   * @brief Return the result of barrier.red.popc.
   *
   * The barrier must be complete and use ReducePopc.
   */
  [[nodiscard]]
  std::uint32_t popc_result() const noexcept {
    assert(complete());
    assert(protocol_ == CtaBarrierProtocol::ReducePopc);

    return reduction_popc_;
  }

 private:
  std::uint64_t generation_ = 0;

  std::uint32_t expected_threads_ = 0;
  std::uint32_t arrived_threads_ = 0;

  CtaBarrierProtocol protocol_ = CtaBarrierProtocol::SyncArrive;

  WarpMask arrived_warps_;
  WarpMask waiting_warps_;

  // Reduction accumulators are meaningful only for reduction protocols.
  bool reduction_predicate_ = false;
  std::uint32_t reduction_popc_ = 0;
};

/**
 * @brief State of one reusable architectural CTA barrier resource.
 *
 * A barrier resource is inactive between generations. The first arrival of a
 * new use begins a generation and fixes its thread count and protocol.
 *
 * The execution layer is responsible for validating instruction-level PTX
 * requirements before beginning or joining a generation.
 */
class CtaBarrierSlot final {
 public:
  CtaBarrierSlot() = default;

  /**
   * @brief Configure this barrier resource for a CTA topology.
   *
   * This must be called during CTA construction before runtime use.
   */
  void initialize(std::size_t warp_count) noexcept {
    assert(!active());
    warp_count_ = warp_count;
  }

  [[nodiscard]]
  bool active() const noexcept {
    return active_.has_value();
  }

  [[nodiscard]]
  std::uint64_t next_generation() const noexcept {
    return next_generation_;
  }

  [[nodiscard]]
  CtaBarrierGeneration& current() noexcept {
    assert(active_.has_value());
    return *active_;
  }

  [[nodiscard]]
  const CtaBarrierGeneration& current() const noexcept {
    assert(active_.has_value());
    return *active_;
  }

  /**
   * @brief Begin a new barrier generation.
   *
   * The resource must currently be inactive.
   */
  CtaBarrierGeneration& begin(std::uint32_t expected_threads,
                              CtaBarrierProtocol protocol) {
    assert(warp_count_ != 0);
    assert(!active_.has_value());

    active_.emplace(warp_count_, next_generation_++, expected_threads,
                    protocol);

    return *active_;
  }

  /**
   * @brief Finish and clear the current generation.
   *
   * Waiting warps must be released by the execution/runtime layer before the
   * generation is cleared.
   */
  void clear_completed() noexcept {
    assert(active_.has_value());
    assert(active_->complete());

    active_.reset();
  }

  /**
   * @brief Forcefully discard the current generation.
   *
   * Intended for trap/reset/destruction paths.
   */
  void reset() noexcept { active_.reset(); }

 private:
  std::size_t warp_count_ = 0;

  std::uint64_t next_generation_ = 0;

  std::optional<CtaBarrierGeneration> active_;
};

/**
 * @brief Collection of the sixteen architectural CTA barrier resources.
 */
class CtaBarrierState final {
 public:
  CtaBarrierState() = default;

  explicit CtaBarrierState(std::size_t warp_count) { initialize(warp_count); }

  /**
   * @brief Bind every barrier slot to the CTA's immutable warp topology.
   */
  void initialize(std::size_t warp_count) noexcept {
    assert(warp_count != 0);

    warp_count_ = warp_count;

    for (auto& barrier : barriers_) {
      barrier.initialize(warp_count_);
    }
  }

  [[nodiscard]]
  std::size_t warp_count() const noexcept {
    return warp_count_;
  }

  [[nodiscard]]
  CtaBarrierSlot& barrier(CtaBarrierId id) noexcept {
    assert(id.value < kCtaBarrierCount);
    return barriers_[id.value];
  }

  [[nodiscard]]
  const CtaBarrierSlot& barrier(CtaBarrierId id) const noexcept {
    assert(id.value < kCtaBarrierCount);
    return barriers_[id.value];
  }

  /**
   * @brief Reset all active CTA barrier generations.
   *
   * Generation counters are intentionally preserved.
   */
  void reset_active() noexcept {
    for (auto& barrier : barriers_) {
      barrier.reset();
    }
  }

 private:
  std::size_t warp_count_ = 0;

  std::array<CtaBarrierSlot, kCtaBarrierCount> barriers_{};
};

/**
 * @brief Mutable execution state owned by one CTA.
 *
 * Only genuinely CTA-scoped execution state belongs here.
 *
 * Thread readiness and CTA completion are derived from the child Thread
 * objects and are therefore not duplicated here.
 *
 * Program data such as shared memory and Tensor Memory is owned by the memory
 * subsystem and is intentionally absent from this structure.
 *
 * mbarrier objects are also absent because they are addressable objects stored
 * in shared memory rather than implicit CTA execution resources.
 */
struct CtaExecutionState {
  CtaExecutionState() = default;

  explicit CtaExecutionState(std::size_t warp_count) : barriers(warp_count) {}

  CtaBarrierState barriers;
};

}  // namespace ptxsim::execution_model