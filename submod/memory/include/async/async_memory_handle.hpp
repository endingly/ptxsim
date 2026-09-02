#pragma once

#include <cstddef>
#include <cstdint>

namespace ptxsim::memory {

/**
 * @brief Stable completion identity owned by one AsyncMemoryEngine.
 */
class AsyncMemoryHandle final {
 public:
  constexpr bool operator==(const AsyncMemoryHandle&) const noexcept = default;

 private:
  constexpr AsyncMemoryHandle(std::uint64_t engine_token,
                              std::size_t record_index) noexcept
      : engine_token_(engine_token), record_index_(record_index) {}

  std::uint64_t engine_token_ = 0;
  std::size_t record_index_ = 0;

  friend class AsyncMemoryEngine;
};

}  // namespace ptxsim::memory
