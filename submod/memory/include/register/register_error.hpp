#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include <ptxsim/common/ids.hpp>
#include <ptxsim/common/raw_value.hpp>

namespace ptxsim::memory {

enum class RegisterErrorCode : std::uint8_t {
  invalid_layout_width,
  layout_size_not_representable,
  stale_frame,
  slot_out_of_range,
  uninitialized_read,
  width_mismatch,
};

struct RegisterError {
  RegisterErrorCode code;
  std::optional<std::size_t> frame_index;
  std::optional<common::RegisterSlot> slot;
  std::optional<common::RawWidth> expected;
  std::optional<common::RawWidth> actual;
  std::optional<std::size_t> index;

  constexpr bool operator==(const RegisterError&) const noexcept = default;
};

}  // namespace ptxsim::memory
