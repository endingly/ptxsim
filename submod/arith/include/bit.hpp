#pragma once
#include <bit>
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
}  // namespace detail

template <arithmetic_integer T>
constexpr unsigned popcount(T v) noexcept {
  return std::popcount(detail::bit_unsigned(v));
}
template <arithmetic_integer T>
constexpr unsigned count_leading_zeros(T v) noexcept {
  return std::countl_zero(detail::bit_unsigned(v));
}
template <arithmetic_integer T>
constexpr int find_most_significant(T v) noexcept {
  using U = detail::bit_unsigned_t<T>;
  const U u = detail::bit_unsigned(v);
  return u == 0 ? -1 : std::numeric_limits<U>::digits - 1 - std::countl_zero(u);
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
template <arithmetic_integer T>
constexpr T bit_extract(T v, unsigned offset, unsigned width) noexcept {
  using U = detail::bit_unsigned_t<T>;
  constexpr unsigned n = std::numeric_limits<U>::digits;
  if (offset >= n || width == 0)
    return {};
  const U mask = width >= n ? ~U{} : (U{1} << width) - 1;
  return detail::bit_value<T>((detail::bit_unsigned(v) >> offset) & mask);
}
template <arithmetic_integer T>
constexpr T bit_insert(T base, T field, unsigned offset,
                       unsigned width) noexcept {
  using U = detail::bit_unsigned_t<T>;
  constexpr unsigned n = std::numeric_limits<U>::digits;
  if (offset >= n || width == 0)
    return base;
  const U mask = width >= n ? ~U{} : (U{1} << width) - 1;
  const U shifted = mask << offset;
  return detail::bit_value<T>((detail::bit_unsigned(base) & ~shifted) |
                              ((detail::bit_unsigned(field) & mask) << offset));
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
