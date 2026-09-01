#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <ptxsim/memory/core/state_space.hpp>
#include <ptxsim/memory/core/address.hpp>


namespace ptxsim::memory {

/**
 * @brief Failure category produced by low-level memory storage operations.
 */
enum class MemoryErrorCode : std::uint8_t {
  OutOfBounds,
  Misaligned,
  UninitializedRead,
  WriteToReadOnlyRegion,
  InvalidAlignment,
};

/**
 * @brief Structured low-level memory error.
 *
 * This object describes storage failure only. Mapping the error to PTX-level
 * trap, undefined behavior, diagnostic, or simulator policy is the
 * responsibility of a higher layer.
 */
struct MemoryError {
  MemoryErrorCode code = MemoryErrorCode::OutOfBounds;

  Address address{};

  std::size_t size = 0;

  /**
   * @brief Required alignment when code is Misaligned or InvalidAlignment.
   */
  std::size_t required_alignment = 1;
};

[[nodiscard]]
constexpr std::string_view to_string(MemoryErrorCode code) noexcept {
  switch (code) {
    case MemoryErrorCode::OutOfBounds:
      return "out-of-bounds memory access";

    case MemoryErrorCode::Misaligned:
      return "misaligned memory access";

    case MemoryErrorCode::UninitializedRead:
      return "read from uninitialized memory";

    case MemoryErrorCode::WriteToReadOnlyRegion:
      return "write to read-only memory region";

    case MemoryErrorCode::InvalidAlignment:
      return "invalid memory alignment";
  }

  return "unknown memory error";
}

}  // namespace ptxsim::memory