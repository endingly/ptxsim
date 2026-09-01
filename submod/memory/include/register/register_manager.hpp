#pragma once

#include <expected>
#include <memory>

#include <ptxsim/memory/register/register_error.hpp>
#include <ptxsim/memory/register/register_frame.hpp>
#include <ptxsim/memory/register/register_view.hpp>

namespace ptxsim::memory {

class RegisterManager final {
 public:
  RegisterManager();
  ~RegisterManager();

  RegisterManager(const RegisterManager&) = delete;
  RegisterManager& operator=(const RegisterManager&) = delete;
  RegisterManager(RegisterManager&&) = delete;
  RegisterManager& operator=(RegisterManager&&) = delete;

  [[nodiscard]] auto create_frame(const RegisterFrameSpec& spec)
      -> std::expected<RegisterFrameHandle, RegisterError>;
  [[nodiscard]] auto destroy_frame(RegisterFrameHandle handle)
      -> std::expected<void, RegisterError>;
  [[nodiscard]] auto view(RegisterFrameHandle handle)
      -> std::expected<RegisterView, RegisterError>;
  [[nodiscard]] auto view(RegisterFrameHandle handle) const
      -> std::expected<ConstRegisterView, RegisterError>;

 private:
  std::shared_ptr<detail::RegisterManagerState> state_;
};

}  // namespace ptxsim::memory
