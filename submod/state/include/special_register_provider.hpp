#pragma once

#include <expected>

#include <ptxsim/common/ids.hpp>
#include <ptxsim/common/raw_value.hpp>
#include <ptxsim/state/thread_state.hpp>

namespace ptxsim::state {

enum class SpecialRegisterErrorCode {
  unsupported_register,
};

struct SpecialRegisterError {
  SpecialRegisterErrorCode code;
  common::SpecialRegisterId register_id;

  constexpr bool operator==(const SpecialRegisterError&) const noexcept =
      default;
};

class SpecialRegisterProvider {
 public:
  virtual ~SpecialRegisterProvider() = default;

  [[nodiscard]] virtual auto read(common::SpecialRegisterId register_id,
                                  const ThreadState& thread) const
      -> std::expected<common::RawValue, SpecialRegisterError> = 0;
};

}  // namespace ptxsim::state
