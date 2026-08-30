#pragma once
#include <bit>
#include <cstdint>
#include <limits>
#include <ptxsim/arith/concepts.hpp>
#include <type_traits>
namespace ptxsim::arith {
namespace detail {
template <arithmetic_integer T>
using bit_unsigned_t = std::make_unsigned_t<T>;
template <arithmetic_integer T>
constexpr bit_unsigned_t<T> bit_unsigned(T v) noexcept {
  return static_cast<bit_unsigned_t<T>>(v);
}
template <arithmetic_integer T>
constexpr T bit_value(bit_unsigned_t<T> v) noexcept {
  return std::bit_cast<T>(v);
}

template <arithmetic_integer T>
constexpr bit_unsigned_t<T> low_bits(unsigned count) noexcept {
  using U = bit_unsigned_t<T>;
  constexpr unsigned n = std::numeric_limits<U>::digits;
  if (count == 0)
    return U{};
  if (count >= n)
    return ~U{};
  return (U{1} << count) - U{1};
}

constexpr unsigned ptx_bitfield_operand(std::uint32_t value) noexcept {
  return value & 0xffu;
}

template <arithmetic_integer T>
constexpr std::uint32_t ptx_bfind_position(bit_unsigned_t<T> value) noexcept {
  using U = bit_unsigned_t<T>;
  constexpr unsigned n = std::numeric_limits<U>::digits;
  return value == 0
             ? std::numeric_limits<std::uint32_t>::max()
             : static_cast<std::uint32_t>(n - 1 - std::countl_zero(value));
}
}  // namespace detail

template <arithmetic_integer T>
constexpr unsigned popcount(T v) noexcept {
  return std::popcount(detail::bit_unsigned(v));
}
template <arithmetic_integer T>
constexpr unsigned count_leading_zeros(T v) noexcept {
  return std::countl_zero(detail::bit_unsigned(v));
}
// PTX bfind: return the position of the most-significant non-sign bit, or
// 0xffffffff when none exists. For signed negative values this searches the
// most-significant zero bit by first complementing the bit pattern.
template <arithmetic_integer T>
constexpr std::uint32_t find_most_significant_non_sign(T v) noexcept {
  using U = detail::bit_unsigned_t<T>;
  constexpr unsigned n = std::numeric_limits<U>::digits;
  U u = detail::bit_unsigned(v);
  if constexpr (std::signed_integral<T>) {
    if ((u & (U{1} << (n - 1))) != 0)
      u = ~u;
  }
  return detail::ptx_bfind_position<T>(u);
}

// PTX bfind.shiftamt: return the number of positions required to move the
// found non-sign bit to the MSB. The no-bit sentinel remains 0xffffffff.
template <arithmetic_integer T>
constexpr std::uint32_t find_shift_amount(T v) noexcept {
  using U = detail::bit_unsigned_t<T>;
  constexpr unsigned n = std::numeric_limits<U>::digits;
  const std::uint32_t position = find_most_significant_non_sign(v);
  return position == std::numeric_limits<std::uint32_t>::max()
             ? position
             : static_cast<std::uint32_t>(n - 1 - position);
}

// Compatibility entry point. It now follows bfind's signed semantics; -1 is
// retained as the legacy representation of PTX's 0xffffffff sentinel.
template <arithmetic_integer T>
constexpr int find_most_significant(T v) noexcept {
  const std::uint32_t position = find_most_significant_non_sign(v);
  return position == std::numeric_limits<std::uint32_t>::max()
             ? -1
             : static_cast<int>(position);
}
template <arithmetic_integer T>
constexpr T bit_reverse(T v) noexcept {
  using U = detail::bit_unsigned_t<T>;
  const U u = detail::bit_unsigned(v);
  U r{};
  for (unsigned i = 0; i < std::numeric_limits<U>::digits; ++i)
    r |= ((u >> i) & U{1}) << (std::numeric_limits<U>::digits - 1 - i);
  return detail::bit_value<T>(r);
}
// PTX bfe.u{32,64}. Pos and len are .u32 operands whose low eight bits are
// used. The destination is padded with zero, including when pos is beyond the
// source MSB.
template <arithmetic_integer T>
constexpr T bit_extract_unsigned(T v, std::uint32_t offset,
                                 std::uint32_t width) noexcept {
  using U = detail::bit_unsigned_t<T>;
  constexpr unsigned n = std::numeric_limits<U>::digits;
  const unsigned pos = detail::ptx_bitfield_operand(offset);
  const unsigned len = detail::ptx_bitfield_operand(width);
  if (len == 0 || pos >= n)
    return {};

  const unsigned count = (len < n - pos) ? len : n - pos;
  const U field = (detail::bit_unsigned(v) >> pos) & detail::low_bits<T>(count);
  return detail::bit_value<T>(field);
}

// PTX bfe.s{32,64}. The field's sign bit is a[min(pos + len - 1, msb)]; it
// therefore also fills the entire result when pos is beyond the source MSB.
template <std::signed_integral T>
  requires arithmetic_integer<T>
constexpr T bit_extract_signed(T v, std::uint32_t offset,
                               std::uint32_t width) noexcept {
  using U = detail::bit_unsigned_t<T>;
  constexpr unsigned n = std::numeric_limits<U>::digits;
  const unsigned pos = detail::ptx_bitfield_operand(offset);
  const unsigned len = detail::ptx_bitfield_operand(width);
  if (len == 0)
    return {};

  const U input = detail::bit_unsigned(v);
  const unsigned sign_position = (pos + len - 1 < n) ? pos + len - 1 : n - 1;
  const bool sign_bit = ((input >> sign_position) & U{1}) != 0;
  const unsigned count = pos < n ? ((len < n - pos) ? len : n - pos) : 0;
  U field = count == 0 ? U{} : (input >> pos) & detail::low_bits<T>(count);
  if (sign_bit)
    field |= ~detail::low_bits<T>(count);
  return detail::bit_value<T>(field);
}

// Compatibility entry point: its selected semantics now match the signedness
// of the PTX instruction type instead of always zero-extending signed fields.
template <arithmetic_integer T>
constexpr T bit_extract(T v, std::uint32_t offset,
                        std::uint32_t width) noexcept {
  if constexpr (std::signed_integral<T>)
    return bit_extract_signed(v, offset, width);
  else
    return bit_extract_unsigned(v, offset, width);
}
template <arithmetic_integer T>
constexpr T bit_insert(T base, T field, std::uint32_t offset,
                       std::uint32_t width) noexcept {
  using U = detail::bit_unsigned_t<T>;
  constexpr unsigned n = std::numeric_limits<U>::digits;
  const unsigned pos = detail::ptx_bitfield_operand(offset);
  const unsigned len = detail::ptx_bitfield_operand(width);
  if (pos >= n || len == 0)
    return base;
  const unsigned count = (len < n - pos) ? len : n - pos;
  const U mask = detail::low_bits<T>(count);
  const U shifted = mask << pos;
  return detail::bit_value<T>((detail::bit_unsigned(base) & ~shifted) |
                              ((detail::bit_unsigned(field) & mask) << pos));
}
template <arithmetic_integer T>
constexpr T funnel_shift(T lo, T hi, unsigned shift) noexcept {
  using U = detail::bit_unsigned_t<T>;
  constexpr unsigned n = std::numeric_limits<U>::digits;
  shift %= n;
  const U result = shift ? (detail::bit_unsigned(lo) >> shift) |
                               (detail::bit_unsigned(hi) << (n - shift))
                         : detail::bit_unsigned(lo);
  return detail::bit_value<T>(result);
}
}  // namespace ptxsim::arith
