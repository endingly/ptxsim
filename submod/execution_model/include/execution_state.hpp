#pragma once
#include <compare>
#include <cstdint>

namespace ptxsim::execution_model {

struct ProgramCounter {
  std::uint32_t value = 0;

  auto operator<=>(const ProgramCounter&) const = default;
};

};  // namespace ptxsim::execution_model