#pragma once

#include <cstdint>
#include <string_view>

namespace ptxsim::memory {

/**
 * @brief PTX architectural state spaces.
 *
 * This enum mirrors the state-space taxonomy defined by the PTX ISA.
 *
 * It intentionally does not contain concepts such as:
 *
 * - generic addressing;
 * - Tensor Memory (TMEM);
 * - multimem addresses;
 * - surface resources;
 * - asynchronous transfers.
 *
 * Those concepts belong to separate parts of the memory subsystem and are
 * not PTX state spaces.
 */
enum class StateSpace : std::uint8_t {
  Register,
  SpecialRegister,
  Constant,
  Global,
  Local,
  Parameter,
  Shared,
  Texture,
};

/**
 * @brief Return whether a state space is backed by ordinary byte-addressable
 * storage in the simulator.
 *
 * Register and special-register state spaces use dedicated access models.
 * Texture state space is accessed through texture operations rather than the
 * generic byte-addressed memory interface.
 */
[[nodiscard]]
constexpr bool is_byte_addressable(StateSpace space) noexcept {
  switch (space) {
    case StateSpace::Constant:
    case StateSpace::Global:
    case StateSpace::Local:
    case StateSpace::Parameter:
    case StateSpace::Shared:
      return true;

    case StateSpace::Register:
    case StateSpace::SpecialRegister:
    case StateSpace::Texture:
      return false;
  }

  return false;
}

/**
 * @brief Return whether the state space represents register storage.
 */
[[nodiscard]]
constexpr bool is_register_space(StateSpace space) noexcept {
  return space == StateSpace::Register || space == StateSpace::SpecialRegister;
}

/**
 * @brief Return a stable textual name for diagnostics.
 */
[[nodiscard]]
constexpr std::string_view to_string(StateSpace space) noexcept {
  // clang-format off
  switch (space) {
    case StateSpace::Register:        return "reg";
    case StateSpace::SpecialRegister: return "sreg";
    case StateSpace::Constant:        return "const";
    case StateSpace::Global:          return "global";
    case StateSpace::Local:           return "local";
    case StateSpace::Parameter:       return "param";
    case StateSpace::Shared:          return "shared";
    case StateSpace::Texture:         return "tex";
  }
  // clang-format on
  return "unknown";
}

}  // namespace ptxsim::memory