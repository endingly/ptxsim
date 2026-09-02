#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include <ptxsim/memory/core/memory_error.hpp>

namespace ptxsim::memory {

enum class AddressSpaceErrorCode : std::uint8_t {
  stale_resource,
  storage_failure,
  allocation_failure,
};

struct AddressSpaceError {
  AddressSpaceErrorCode code;
  std::optional<MemoryError> memory_error;
  std::optional<std::size_t> resource_index;

  constexpr bool operator==(const AddressSpaceError&) const noexcept = default;
};

}  // namespace ptxsim::memory
