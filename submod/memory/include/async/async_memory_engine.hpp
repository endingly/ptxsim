#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <vector>

#include <ptxsim/memory/address_space/address_space_error.hpp>
#include <ptxsim/memory/async/async_memory_handle.hpp>
#include <ptxsim/memory/async/async_memory_op.hpp>

namespace ptxsim::memory {

enum class AsyncMemoryStatus : std::uint8_t {
  pending,
  completed,
  failed,
};

enum class AsyncMemoryErrorCode : std::uint8_t {
  stale_handle,
  pending_operation,
  source_failure,
  destination_failure,
};

/**
 * @brief Structured failure from asynchronous memory progress or inspection.
 */
struct AsyncMemoryError {
  AsyncMemoryErrorCode code;
  std::optional<AddressSpaceError> address_space_error;

  constexpr bool operator==(const AsyncMemoryError&) const noexcept = default;
};

/**
 * @brief Deterministically progresses issued memory operations in FIFO order.
 *
 * Each progress call processes exactly one pending operation. Completion is a
 * functional-simulation event and has no host-time or cycle interpretation.
 */
class AsyncMemoryEngine final {
 public:
  AsyncMemoryEngine();

  AsyncMemoryEngine(const AsyncMemoryEngine&) = delete;
  AsyncMemoryEngine& operator=(const AsyncMemoryEngine&) = delete;
  AsyncMemoryEngine(AsyncMemoryEngine&&) = delete;
  AsyncMemoryEngine& operator=(AsyncMemoryEngine&&) = delete;

  [[nodiscard]] auto issue(AsyncMemoryOp operation) -> AsyncMemoryHandle;
  [[nodiscard]] auto progress() -> std::optional<AsyncMemoryHandle>;

  [[nodiscard]] auto status(AsyncMemoryHandle handle) const
      -> std::expected<AsyncMemoryStatus, AsyncMemoryError>;
  [[nodiscard]] auto result(AsyncMemoryHandle handle) const
      -> std::expected<void, AsyncMemoryError>;

 private:
  struct Record {
    AsyncMemoryOp operation;
    AsyncMemoryStatus status = AsyncMemoryStatus::pending;
    std::optional<AsyncMemoryError> error;
  };

  [[nodiscard]] auto find(AsyncMemoryHandle handle) const
      -> std::expected<const Record*, AsyncMemoryError>;

  std::uint64_t token_;
  std::size_t next_pending_ = 0;
  // ponytail: completed records remain; add retirement if history grows.
  std::vector<Record> records_;
};

}  // namespace ptxsim::memory
