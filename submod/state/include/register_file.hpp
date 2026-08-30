#pragma once

#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <vector>

#include <ptxsim/common/ids.hpp>
#include <ptxsim/common/raw_value.hpp>

namespace ptxsim::state {

enum class RegisterErrorCode {
  invalid_layout_width,
  layout_size_not_representable,
  slot_out_of_range,
  uninitialized_read,
  width_mismatch,
};

struct RegisterError {
  RegisterErrorCode code;
  std::optional<common::RegisterSlot> slot;
  std::optional<common::RawWidth> expected;
  std::optional<common::RawWidth> actual;
  std::optional<std::size_t> index;

  constexpr bool operator==(const RegisterError&) const noexcept = default;
};

class RegisterFile {
 public:
  RegisterFile(const RegisterFile&) = default;
  RegisterFile(RegisterFile&&) noexcept = default;
  auto operator=(const RegisterFile&) -> RegisterFile& = default;
  auto operator=(RegisterFile&&) noexcept -> RegisterFile& = default;

  [[nodiscard]] static auto create(std::vector<common::RawWidth> layout)
      -> std::expected<RegisterFile, RegisterError>;

  [[nodiscard]] auto read(common::RegisterSlot slot) const
      -> std::expected<common::RawValue, RegisterError>;
  [[nodiscard]] auto write(common::RegisterSlot slot, common::RawValue value)
      -> std::expected<void, RegisterError>;
  [[nodiscard]] auto is_initialized(common::RegisterSlot slot) const
      -> std::expected<bool, RegisterError>;
  [[nodiscard]] auto declared_width(common::RegisterSlot slot) const
      -> std::expected<common::RawWidth, RegisterError>;
  [[nodiscard]] auto size() const noexcept -> std::size_t;

 private:
  explicit RegisterFile(std::vector<common::RawWidth> layout,
                        std::vector<common::RawValue> values) noexcept;

  std::vector<common::RawWidth> layout_;
  // Placeholder values only make RawValue storage dense; initialized_ gates
  // every read, so they are never architectural register values.
  std::vector<common::RawValue> values_;
  std::vector<bool> initialized_;
};

[[nodiscard]] auto dump(const RegisterFile& registers) -> std::string;

}  // namespace ptxsim::state
