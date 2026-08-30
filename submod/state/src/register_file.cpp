#include <ptxsim/state/register_file.hpp>

#include <cstdint>
#include <utility>

#include "register_file_detail.hpp"

namespace ptxsim::state {
namespace {

auto is_valid_width(common::RawWidth width) -> bool {
  switch (width) {
    case common::RawWidth::pred:
    case common::RawWidth::b8:
    case common::RawWidth::b16:
    case common::RawWidth::b32:
    case common::RawWidth::b64:
    case common::RawWidth::b128:
      return true;
  }
  return false;
}

auto zero_value(common::RawWidth width) -> common::RawValue {
  switch (width) {
    case common::RawWidth::pred:
      return common::RawValue::pred(false);
    case common::RawWidth::b8:
      return common::RawValue::b8(std::uint8_t{0});
    case common::RawWidth::b16:
      return common::RawValue::b16(std::uint16_t{0});
    case common::RawWidth::b32:
      return common::RawValue::b32(std::uint32_t{0});
    case common::RawWidth::b64:
      return common::RawValue::b64(std::uint64_t{0});
    case common::RawWidth::b128:
      return common::RawValue::b128(common::Bits128{0, 0});
  }
  return common::RawValue::pred(false);
}

auto width_name(common::RawWidth width) -> const char* {
  switch (width) {
    case common::RawWidth::pred:
      return "pred";
    case common::RawWidth::b8:
      return "b8";
    case common::RawWidth::b16:
      return "b16";
    case common::RawWidth::b32:
      return "b32";
    case common::RawWidth::b64:
      return "b64";
    case common::RawWidth::b128:
      return "b128";
  }
  return "invalid";
}

auto slot_error(common::RegisterSlot slot) -> RegisterError {
  return {RegisterErrorCode::slot_out_of_range, slot, std::nullopt,
          std::nullopt, static_cast<std::size_t>(slot.value())};
}

}  // namespace

RegisterFile::RegisterFile(std::vector<common::RawWidth> layout,
                           std::vector<common::RawValue> values) noexcept
    : layout_(std::move(layout)),
      values_(std::move(values)),
      initialized_(layout_.size(), false) {}

auto RegisterFile::create(std::vector<common::RawWidth> layout)
    -> std::expected<RegisterFile, RegisterError> {
  if (!detail::layout_size_representable(layout.size())) {
    return std::unexpected(
        RegisterError{RegisterErrorCode::layout_size_not_representable,
                      std::nullopt, std::nullopt, std::nullopt, layout.size()});
  }
  std::vector<common::RawValue> values;
  values.reserve(layout.size());
  for (std::size_t index = 0; index < layout.size(); ++index) {
    if (!is_valid_width(layout[index])) {
      return std::unexpected(
          RegisterError{RegisterErrorCode::invalid_layout_width, std::nullopt,
                        std::nullopt, layout[index], index});
    }
    values.push_back(zero_value(layout[index]));
  }
  return RegisterFile{std::move(layout), std::move(values)};
}

auto RegisterFile::read(common::RegisterSlot slot) const
    -> std::expected<common::RawValue, RegisterError> {
  if (slot.value() >= values_.size())
    return std::unexpected(slot_error(slot));
  if (!initialized_[slot.value()]) {
    return std::unexpected(RegisterError{
        RegisterErrorCode::uninitialized_read, slot, layout_[slot.value()],
        std::nullopt, static_cast<std::size_t>(slot.value())});
  }
  return values_[slot.value()];
}

auto RegisterFile::write(common::RegisterSlot slot, common::RawValue value)
    -> std::expected<void, RegisterError> {
  if (slot.value() >= values_.size())
    return std::unexpected(slot_error(slot));
  if (layout_[slot.value()] != value.width()) {
    return std::unexpected(RegisterError{
        RegisterErrorCode::width_mismatch, slot, layout_[slot.value()],
        value.width(), static_cast<std::size_t>(slot.value())});
  }
  values_[slot.value()] = std::move(value);
  initialized_[slot.value()] = true;
  return {};
}

auto RegisterFile::is_initialized(common::RegisterSlot slot) const
    -> std::expected<bool, RegisterError> {
  if (slot.value() >= values_.size())
    return std::unexpected(slot_error(slot));
  return initialized_[slot.value()];
}

auto RegisterFile::declared_width(common::RegisterSlot slot) const
    -> std::expected<common::RawWidth, RegisterError> {
  if (slot.value() >= layout_.size())
    return std::unexpected(slot_error(slot));
  return layout_[slot.value()];
}

auto RegisterFile::size() const noexcept -> std::size_t {
  return layout_.size();
}

auto dump(const RegisterFile& registers) -> std::string {
  std::string output;
  for (std::size_t index = 0; index < registers.size(); ++index) {
    const auto slot = common::RegisterSlot{static_cast<std::uint32_t>(index)};
    output += common::to_string(slot);
    output += " ";
    output += width_name(*registers.declared_width(slot));
    output += " ";
    if (const auto value = registers.read(slot))
      output += common::to_string(*value);
    else
      output += "uninitialized";
    output += "\n";
  }
  return output;
}

}  // namespace ptxsim::state
