#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>

#include <ptxsim/memory/core/address.hpp>
#include <ptxsim/memory/core/memory_error.hpp>

namespace ptxsim::memory {

class MBarrierView;
class MBarrierState;

/**
 * @brief Opaque observation token for one manager, mbarrier incarnation, and phase.
 */
class MBarrierToken final {
 public:
  constexpr bool operator==(const MBarrierToken&) const noexcept = default;

 private:
  constexpr MBarrierToken(std::uint64_t manager_token,
                          std::uint64_t incarnation,
                          std::uint64_t phase) noexcept
      : manager_token_(manager_token),
        incarnation_(incarnation),
        phase_(phase) {}

  std::uint64_t manager_token_ = 0;
  std::uint64_t incarnation_ = 0;
  std::uint64_t phase_ = 0;

  friend class MBarrierState;
};

/**
 * @brief Observable state of one initialized mbarrier sidecar.
 */
struct MBarrierSnapshot {
  std::uint32_t expected_arrivals;
  std::uint32_t pending_arrivals;
  std::int64_t transaction_count;
  std::uint64_t phase;

  constexpr bool operator==(const MBarrierSnapshot&) const noexcept = default;
};

enum class MBarrierErrorCode : std::uint8_t {
  stale_shared_space,
  invalid_address,
  invalid_arrival_count,
  duplicate_initialization,
  invalid_barrier,
  invalid_token,
  arrival_underflow,
  transaction_overflow,
  phase_overflow,
  incarnation_exhausted,
};

/**
 * @brief Structured failure from a shared-memory mbarrier operation.
 */
struct MBarrierError {
  MBarrierErrorCode code;
  Address address{};
  std::optional<MemoryError> memory_error;
  std::optional<std::size_t> resource_index;
};

/**
 * @brief PTX layout-v0 arrival and transaction accounting state.
 */
class MBarrierState final {
 public:
  [[nodiscard]] auto arrive(Address address, std::uint32_t count)
      -> std::expected<MBarrierToken, MBarrierError>;
  [[nodiscard]] auto expect_tx(Address address, std::uint32_t count)
      -> std::expected<void, MBarrierError>;
  [[nodiscard]] auto complete_tx(Address address, std::uint32_t count)
      -> std::expected<void, MBarrierError>;
  [[nodiscard]] auto test_wait(Address address, MBarrierToken token) const
      -> std::expected<bool, MBarrierError>;
  [[nodiscard]] auto snapshot() const noexcept -> MBarrierSnapshot;

 private:
  static constexpr std::uint32_t max_count = (std::uint32_t{1} << 20) - 1;

  MBarrierState(std::uint32_t expected_arrivals, std::uint64_t manager_token,
                std::uint64_t incarnation) noexcept
      : expected_arrivals_(expected_arrivals),
        pending_arrivals_(expected_arrivals),
        manager_token_(manager_token),
        incarnation_(incarnation) {}

  [[nodiscard]] auto finish_update(Address address,
                                   std::uint32_t pending_arrivals,
                                   std::int64_t transaction_count)
      -> std::expected<void, MBarrierError>;

  std::uint32_t expected_arrivals_ = 0;
  std::uint32_t pending_arrivals_ = 0;
  std::int64_t transaction_count_ = 0;
  std::uint64_t phase_ = 0;
  std::uint64_t manager_token_ = 0;
  std::uint64_t incarnation_ = 0;

  friend class MBarrierView;
};

}  // namespace ptxsim::memory
