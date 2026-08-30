#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace ptxsim::state::detail {

inline constexpr std::uint64_t register_slot_count =
    std::uint64_t{std::numeric_limits<std::uint32_t>::max()} + 1;

// A 32-bit size_t cannot express more slots than RegisterSlot, while wider
// size_t values must fit the inclusive uint32_t slot range.
constexpr auto layout_size_representable(std::size_t size) -> bool {
  if constexpr (std::numeric_limits<std::size_t>::digits <
                std::numeric_limits<std::uint64_t>::digits) {
    return true;
  } else {
    return size <= static_cast<std::size_t>(register_slot_count);
  }
}

inline constexpr bool size_type_can_exceed_register_slots =
    std::numeric_limits<std::size_t>::max() > register_slot_count;

}  // namespace ptxsim::state::detail
