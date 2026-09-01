#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include <ptxsim/common/raw_value.hpp>

namespace ptxsim::memory {

namespace detail {
struct RegisterManagerState;
}

struct RegisterFrameSpec {
  std::vector<common::RawWidth> slot_widths;
};

class RegisterFrameHandle final {
 public:
  constexpr bool operator==(const RegisterFrameHandle&) const noexcept =
      default;

 private:
  constexpr RegisterFrameHandle(std::uint64_t manager_token, std::size_t index,
                                std::uint64_t generation) noexcept
      : manager_token_(manager_token), index_(index), generation_(generation) {}

  std::uint64_t manager_token_ = 0;
  std::size_t index_ = 0;
  std::uint64_t generation_ = 0;

  friend struct detail::RegisterManagerState;
  friend class RegisterManager;
  friend class RegisterView;
  friend class ConstRegisterView;
};

class RegisterFrame final {
 public:
  [[nodiscard]] std::size_t slot_count() const noexcept {
    return slot_widths_.size();
  }

 private:
  explicit RegisterFrame(std::vector<common::RawWidth> slot_widths) noexcept
      : slot_widths_(std::move(slot_widths)), values_(slot_widths_.size()) {}

  std::vector<common::RawWidth> slot_widths_;
  std::vector<std::optional<common::RawValue>> values_;

  friend class RegisterManager;
  friend class RegisterView;
  friend class ConstRegisterView;
};

}  // namespace ptxsim::memory
