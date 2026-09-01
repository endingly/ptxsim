#pragma once

#include <cstddef>
#include <expected>
#include <memory>
#include <utility>

#include <ptxsim/memory/register/register_error.hpp>
#include <ptxsim/memory/register/register_frame.hpp>

namespace ptxsim::memory {

class ConstRegisterView;

class RegisterView final {
 public:
  [[nodiscard]] auto read(common::RegisterSlot slot) const
      -> std::expected<common::RawValue, RegisterError>;
  [[nodiscard]] auto write(common::RegisterSlot slot, common::RawValue value)
      -> std::expected<void, RegisterError>;
  [[nodiscard]] auto initialized(common::RegisterSlot slot) const
      -> std::expected<bool, RegisterError>;
  [[nodiscard]] auto declared_width(common::RegisterSlot slot) const
      -> std::expected<common::RawWidth, RegisterError>;
  [[nodiscard]] auto slot_count() const
      -> std::expected<std::size_t, RegisterError>;

  [[nodiscard]] operator ConstRegisterView() const;

 private:
  RegisterView(std::weak_ptr<detail::RegisterManagerState> state,
               RegisterFrameHandle handle) noexcept
      : state_(std::move(state)), handle_(handle) {}

  std::weak_ptr<detail::RegisterManagerState> state_;
  RegisterFrameHandle handle_;

  friend class RegisterManager;
  friend class ConstRegisterView;
};

class ConstRegisterView final {
 public:
  [[nodiscard]] auto read(common::RegisterSlot slot) const
      -> std::expected<common::RawValue, RegisterError>;
  [[nodiscard]] auto initialized(common::RegisterSlot slot) const
      -> std::expected<bool, RegisterError>;
  [[nodiscard]] auto declared_width(common::RegisterSlot slot) const
      -> std::expected<common::RawWidth, RegisterError>;
  [[nodiscard]] auto slot_count() const
      -> std::expected<std::size_t, RegisterError>;

 private:
  ConstRegisterView(std::weak_ptr<detail::RegisterManagerState> state,
                    RegisterFrameHandle handle) noexcept
      : state_(std::move(state)), handle_(handle) {}

  std::weak_ptr<detail::RegisterManagerState> state_;
  RegisterFrameHandle handle_;

  friend class RegisterManager;
  friend class RegisterView;
};

}  // namespace ptxsim::memory
