#pragma once

#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <variant>

namespace ptxsim::common {

enum class RawWidth {
  pred,
  b8,
  b16,
  b32,
  b64,
  b128,
};

struct Bits128 {
  std::uint64_t low;
  std::uint64_t high;

  constexpr auto operator<=>(const Bits128&) const noexcept = default;
};

struct RawValueError {
  RawWidth expected;
  RawWidth actual;

  constexpr bool operator==(const RawValueError&) const noexcept = default;
};

class RawValue {
 public:
  RawValue() = delete;

  template <typename T>
    requires std::same_as<T, bool>
  [[nodiscard]] static constexpr auto pred(T value) -> RawValue {
    return RawValue{Storage{value}};
  }

  template <typename T>
    requires std::same_as<T, std::uint8_t>
  [[nodiscard]] static constexpr auto b8(T value) -> RawValue {
    return RawValue{Storage{value}};
  }

  template <typename T>
    requires std::same_as<T, std::uint16_t>
  [[nodiscard]] static constexpr auto b16(T value) -> RawValue {
    return RawValue{Storage{value}};
  }

  template <typename T>
    requires std::same_as<T, std::uint32_t>
  [[nodiscard]] static constexpr auto b32(T value) -> RawValue {
    return RawValue{Storage{value}};
  }

  template <typename T>
    requires std::same_as<T, std::uint64_t>
  [[nodiscard]] static constexpr auto b64(T value) -> RawValue {
    return RawValue{Storage{value}};
  }

  template <typename T>
    requires std::same_as<T, Bits128>
  [[nodiscard]] static constexpr auto b128(T value) -> RawValue {
    return RawValue{Storage{value}};
  }

  [[nodiscard]] constexpr auto width() const noexcept -> RawWidth {
    switch (storage_.index()) {
      case 0:
        return RawWidth::pred;
      case 1:
        return RawWidth::b8;
      case 2:
        return RawWidth::b16;
      case 3:
        return RawWidth::b32;
      case 4:
        return RawWidth::b64;
      case 5:
        return RawWidth::b128;
    }
    return RawWidth::pred;
  }

  [[nodiscard]] constexpr auto as_pred() const
      -> std::expected<bool, RawValueError> {
    return as<bool>(RawWidth::pred);
  }

  [[nodiscard]] constexpr auto as_b8() const
      -> std::expected<std::uint8_t, RawValueError> {
    return as<std::uint8_t>(RawWidth::b8);
  }

  [[nodiscard]] constexpr auto as_b16() const
      -> std::expected<std::uint16_t, RawValueError> {
    return as<std::uint16_t>(RawWidth::b16);
  }

  [[nodiscard]] constexpr auto as_b32() const
      -> std::expected<std::uint32_t, RawValueError> {
    return as<std::uint32_t>(RawWidth::b32);
  }

  [[nodiscard]] constexpr auto as_b64() const
      -> std::expected<std::uint64_t, RawValueError> {
    return as<std::uint64_t>(RawWidth::b64);
  }

  [[nodiscard]] constexpr auto as_b128() const
      -> std::expected<Bits128, RawValueError> {
    return as<Bits128>(RawWidth::b128);
  }

  constexpr bool operator==(const RawValue&) const noexcept = default;

 private:
  using Storage = std::variant<bool, std::uint8_t, std::uint16_t, std::uint32_t,
                               std::uint64_t, Bits128>;

  explicit constexpr RawValue(Storage storage) : storage_(storage) {}

  template <typename T>
  [[nodiscard]] constexpr auto as(RawWidth expected) const
      -> std::expected<T, RawValueError> {
    if (const auto* value = std::get_if<T>(&storage_)) {
      return *value;
    }
    return std::unexpected(RawValueError{expected, width()});
  }

  Storage storage_;
};

namespace detail {

inline void append_hex(std::string& output, std::uint64_t value,
                       std::size_t digits) {
  constexpr char hex[] = "0123456789abcdef";
  for (std::size_t index = digits; index != 0; --index) {
    output.push_back(hex[(value >> ((index - 1) * 4)) & 0xfu]);
  }
}

inline auto hex_string(const char* prefix, std::uint64_t value,
                       std::size_t digits) -> std::string {
  std::string output{prefix};
  output += ":0x";
  append_hex(output, value, digits);
  return output;
}

}  // namespace detail

[[nodiscard]] inline auto to_string(const RawValue& value) -> std::string {
  switch (value.width()) {
    case RawWidth::pred:
      return *value.as_pred() ? "pred:true" : "pred:false";
    case RawWidth::b8:
      return detail::hex_string("b8", *value.as_b8(), 2);
    case RawWidth::b16:
      return detail::hex_string("b16", *value.as_b16(), 4);
    case RawWidth::b32:
      return detail::hex_string("b32", *value.as_b32(), 8);
    case RawWidth::b64:
      return detail::hex_string("b64", *value.as_b64(), 16);
    case RawWidth::b128: {
      const auto bits = *value.as_b128();
      std::string output{"b128:0x"};
      detail::append_hex(output, bits.high, 16);
      detail::append_hex(output, bits.low, 16);
      return output;
    }
  }
  return {};
}

}  // namespace ptxsim::common
