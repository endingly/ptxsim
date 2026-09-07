#pragma once

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <type_traits>
#include <variant>

#include <ptxsim/arith/concepts.hpp>
#include <ptxsim/arith/types.hpp>
#include <ptxsim/common/raw_value.hpp>
#include <ptxsim/exec_ir/exec_ir.hpp>
#include <ptxsim/inst_execute_engine/step_outcome.hpp>

#include "prepared_effect.hpp"

namespace ptxsim::inst_execute_engine::detail {

/** @brief Integer element types that PTX Add exposes in a b32 packed container. */
template <typename Element>
concept PackedIntegerElement = arith::arithmetic_integer<Element> &&
                               (sizeof(Element) == sizeof(std::uint8_t) ||
                                sizeof(Element) == sizeof(std::uint16_t));

/** @brief Raw PTX storage for an integer vector with independent lanes. */
template <PackedIntegerElement Element, std::size_t Lanes>
  requires((sizeof(Element) == sizeof(std::uint8_t) && Lanes == 4) ||
           (sizeof(Element) == sizeof(std::uint16_t) && Lanes == 2))
struct packed_integer final {
  static_assert(Lanes * sizeof(Element) == sizeof(std::uint32_t));

  /** Architectural little-endian lane encoding; lane zero occupies low bits. */
  std::uint32_t bits{};

  /** @brief Compare the complete architectural bit encoding. */
  constexpr bool operator==(const packed_integer&) const noexcept = default;
};

/** @brief Two independently wrapping unsigned 16-bit lanes. */
using u16x2_t = packed_integer<std::uint16_t, 2>;
/** @brief Two independently wrapping signed 16-bit lanes. */
using s16x2_t = packed_integer<std::int16_t, 2>;
/** @brief Four independently wrapping unsigned 8-bit lanes. */
using u8x4_t = packed_integer<std::uint8_t, 4>;
/** @brief Four independently wrapping signed 8-bit lanes. */
using s8x4_t = packed_integer<std::int8_t, 4>;

/** @brief Convert one decoded value to and from its exact RawValue width. */
template <typename T>
struct value_codec;

/** @brief Types with an exact, lossless PTX RawValue encoding. */
template <typename T>
concept ValueCodec = requires(const common::RawValue& raw, T value) {
  { value_codec<T>::width } -> std::convertible_to<common::RawWidth>;
  {
    value_codec<T>::decode(raw)
  } -> std::same_as<std::expected<T, common::RawValueError>>;
  { value_codec<T>::encode(value) } -> std::same_as<common::RawValue>;
};

/** @brief Scalar-operand aliases represented as a register-or-immediate variant. */
template <typename Operand>
concept ScalarRegisterOrImmediate = requires(const Operand& operand) {
  {
    std::get_if<common::RegisterSlot>(&operand)
  } -> std::same_as<const common::RegisterSlot*>;
  {
    std::get_if<common::RawValue>(&operand)
  } -> std::same_as<const common::RawValue*>;
};

namespace value_alu_detail {

/** @brief Decode an unsigned raw encoding, preserving all source bits. */
template <typename T, common::RawWidth Width>
  requires((Width == common::RawWidth::b16 && sizeof(T) == 2) ||
           (Width == common::RawWidth::b32 && sizeof(T) == 4) ||
           (Width == common::RawWidth::b64 && sizeof(T) == 8))
auto decode_unsigned(const common::RawValue& value)
    -> std::expected<T, common::RawValueError> {
  if constexpr (Width == common::RawWidth::b16) {
    const auto bits = value.as_b16();
    if (!bits)
      return std::unexpected(bits.error());
    return std::bit_cast<T>(*bits);
  } else if constexpr (Width == common::RawWidth::b32) {
    const auto bits = value.as_b32();
    if (!bits)
      return std::unexpected(bits.error());
    return std::bit_cast<T>(*bits);
  } else {
    const auto bits = value.as_b64();
    if (!bits)
      return std::unexpected(bits.error());
    return std::bit_cast<T>(*bits);
  }
}

/** @brief Encode a scalar bit pattern using the specified exact raw width. */
template <typename T, common::RawWidth Width>
  requires((Width == common::RawWidth::b16 && sizeof(T) == 2) ||
           (Width == common::RawWidth::b32 && sizeof(T) == 4) ||
           (Width == common::RawWidth::b64 && sizeof(T) == 8))
auto encode_unsigned(T value) -> common::RawValue {
  if constexpr (Width == common::RawWidth::b16)
    return common::RawValue::b16(std::bit_cast<std::uint16_t>(value));
  else if constexpr (Width == common::RawWidth::b32)
    return common::RawValue::b32(std::bit_cast<std::uint32_t>(value));
  else
    return common::RawValue::b64(std::bit_cast<std::uint64_t>(value));
}

}  // namespace value_alu_detail

#define PTXSIM_VALUE_ALU_INTEGER_CODEC(type, raw_width)                    \
  /** @brief Exact-width, bit-preserving scalar integer codec. */          \
  template <>                                                              \
  struct value_codec<type> {                                               \
    /** Required architectural storage width for this integer type. */     \
    static constexpr common::RawWidth width = common::RawWidth::raw_width; \
    /** @brief Decode exactly one integer's architectural bits. */         \
    static auto decode(const common::RawValue& value)                      \
        -> std::expected<type, common::RawValueError> {                    \
      return value_alu_detail::decode_unsigned<type, width>(value);        \
    }                                                                      \
    /** @brief Encode exactly one integer's architectural bits. */         \
    static auto encode(type value) -> common::RawValue {                   \
      return value_alu_detail::encode_unsigned<type, width>(value);        \
    }                                                                      \
  }

PTXSIM_VALUE_ALU_INTEGER_CODEC(std::uint16_t, b16);
PTXSIM_VALUE_ALU_INTEGER_CODEC(std::int16_t, b16);
PTXSIM_VALUE_ALU_INTEGER_CODEC(std::uint32_t, b32);
PTXSIM_VALUE_ALU_INTEGER_CODEC(std::int32_t, b32);
PTXSIM_VALUE_ALU_INTEGER_CODEC(std::uint64_t, b64);
PTXSIM_VALUE_ALU_INTEGER_CODEC(std::int64_t, b64);

#undef PTXSIM_VALUE_ALU_INTEGER_CODEC

/** @brief Codec specialization for scalar floating formats stored as raw bits. */
template <typename T, common::RawWidth Width>
  requires((std::same_as<T, arith::float16_t> ||
            std::same_as<T, arith::bfloat16_t>) &&
               Width == common::RawWidth::b16 ||
           std::same_as<T, arith::float32_t> &&
               Width == common::RawWidth::b32 ||
           std::same_as<T, arith::float64_t> && Width == common::RawWidth::b64)
struct floating_value_codec {
  /** Exact PTX storage width required for this scalar format. */
  static constexpr common::RawWidth width = Width;

  /** @brief Reconstruct a floating format directly from its PTX bit encoding. */
  static auto decode(const common::RawValue& value)
      -> std::expected<T, common::RawValueError> {
    const auto bits =
        value_alu_detail::decode_unsigned<typename T::storage_type, Width>(
            value);
    if (!bits)
      return std::unexpected(bits.error());
    return T::from_bits(*bits);
  }

  /** @brief Preserve a floating format's exact PTX bit encoding. */
  static auto encode(T value) -> common::RawValue {
    return value_alu_detail::encode_unsigned<typename T::storage_type, Width>(
        value.bits());
  }
};

/** @brief Exact b16 codec for IEEE binary16 values. */
template <>
struct value_codec<arith::float16_t>
    : floating_value_codec<arith::float16_t, common::RawWidth::b16> {};
/** @brief Exact b16 codec for bfloat16 values. */
template <>
struct value_codec<arith::bfloat16_t>
    : floating_value_codec<arith::bfloat16_t, common::RawWidth::b16> {};
/** @brief Exact b32 codec for IEEE binary32 values. */
template <>
struct value_codec<arith::float32_t>
    : floating_value_codec<arith::float32_t, common::RawWidth::b32> {};
/** @brief Exact b64 codec for IEEE binary64 values. */
template <>
struct value_codec<arith::float64_t>
    : floating_value_codec<arith::float64_t, common::RawWidth::b64> {};

/** @brief Codec specialization for b32-packed integer lane containers. */
template <PackedIntegerElement Element, std::size_t Lanes>
struct value_codec<packed_integer<Element, Lanes>> {
  /** All supported integer lane groups occupy exactly one b32 register. */
  static constexpr common::RawWidth width = common::RawWidth::b32;

  /** @brief Decode the complete packed lane encoding without lane conversion. */
  static auto decode(const common::RawValue& value)
      -> std::expected<packed_integer<Element, Lanes>, common::RawValueError> {
    const auto bits = value.as_b32();
    if (!bits)
      return std::unexpected(bits.error());
    return packed_integer<Element, Lanes>{*bits};
  }

  /** @brief Encode the complete packed lane encoding without lane conversion. */
  static auto encode(packed_integer<Element, Lanes> value) -> common::RawValue {
    return common::RawValue::b32(value.bits);
  }
};

/** @brief Codec specialization for standard packed floating PTX encodings. */
template <typename Element, std::size_t Lanes, typename Layout>
  requires(std::same_as<Layout, arith::dense_packed_layout> && Lanes == 2 &&
           (std::same_as<Element, arith::float16_t> ||
            std::same_as<Element, arith::bfloat16_t> ||
            std::same_as<Element, arith::float32_t>))
struct packed_float_value_codec {
  using value_type = arith::packed_t<Element, Lanes, Layout>;
  /** Exact PTX storage width required for this packed container. */
  static constexpr common::RawWidth width =
      sizeof(typename value_type::container_type) == sizeof(std::uint32_t)
          ? common::RawWidth::b32
          : common::RawWidth::b64;

  /** @brief Decode a packed float from its container bits. */
  static auto decode(const common::RawValue& value)
      -> std::expected<value_type, common::RawValueError> {
    if constexpr (width == common::RawWidth::b32) {
      const auto bits = value.as_b32();
      if (!bits)
        return std::unexpected(bits.error());
      return value_type::from_bits(*bits);
    } else {
      const auto bits = value.as_b64();
      if (!bits)
        return std::unexpected(bits.error());
      return value_type::from_bits(*bits);
    }
  }

  /** @brief Encode a packed float's container bits without host evaluation. */
  static auto encode(value_type value) -> common::RawValue {
    if constexpr (width == common::RawWidth::b32)
      return common::RawValue::b32(value.bits());
    else
      return common::RawValue::b64(value.bits());
  }
};

/** @brief Exact b32 codec for two packed IEEE binary16 lanes. */
template <>
struct value_codec<arith::float16x2_t>
    : packed_float_value_codec<arith::float16_t, 2,
                               arith::dense_packed_layout> {};
/** @brief Exact b32 codec for two packed bfloat16 lanes. */
template <>
struct value_codec<arith::bfloat16x2_t>
    : packed_float_value_codec<arith::bfloat16_t, 2,
                               arith::dense_packed_layout> {};
/** @brief Exact b64 codec for two packed IEEE binary32 lanes. */
template <>
struct value_codec<arith::float32x2_t>
    : packed_float_value_codec<arith::float32_t, 2,
                               arith::dense_packed_layout> {};

/** @brief Read an exact typed value from a register slot. */
template <ValueCodec T>
auto read_value(const memory::RegisterView& registers,
                common::RegisterSlot slot) -> std::expected<T, LaneFaultCause> {
  const auto raw = registers.read(slot);
  if (!raw)
    return std::unexpected(LaneFaultCause{raw.error()});
  const auto value = value_codec<T>::decode(*raw);
  if (!value)
    return std::unexpected(LaneFaultCause{value.error()});
  return *value;
}

/** @brief Read an exact typed value from a scalar register-or-immediate operand. */
template <ValueCodec T, ScalarRegisterOrImmediate Operand>
auto read_value(const memory::RegisterView& registers, const Operand& operand)
    -> std::expected<T, LaneFaultCause> {
  if (const auto* slot = std::get_if<common::RegisterSlot>(&operand))
    return read_value<T>(registers, *slot);
  const auto* immediate = std::get_if<common::RawValue>(&operand);
  if (immediate == nullptr)
    return std::unexpected(LaneFaultCause{
        common::RawValueError{value_codec<T>::width, common::RawWidth::pred}});
  const auto value = value_codec<T>::decode(*immediate);
  if (!value)
    return std::unexpected(LaneFaultCause{value.error()});
  return *value;
}

/** @brief Confirm that a destination register has the exact result width. */
template <ValueCodec T>
auto resolve_destination(const memory::RegisterView& registers,
                         common::RegisterSlot destination)
    -> std::expected<void, LaneFaultCause> {
  const auto width = registers.declared_width(destination);
  if (!width)
    return std::unexpected(LaneFaultCause{width.error()});
  if (*width != value_codec<T>::width)
    return std::unexpected(
        LaneFaultCause{common::RawValueError{value_codec<T>::width, *width}});
  return {};
}

/** @brief Form one deferred scalar register write after all inputs are validated. */
template <ValueCodec T>
auto stage_register_write(const memory::RegisterView& registers,
                          common::RegisterSlot destination, T value,
                          common::ProgramCounter successor) -> PreparedEffect {
  return PreparedEffect{.write = PreparedWrite{registers, destination,
                                               value_codec<T>::encode(value)},
                        .memory_write = std::nullopt,
                        .control = successor};
}

}  // namespace ptxsim::inst_execute_engine::detail
