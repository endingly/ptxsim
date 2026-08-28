#pragma once

#include <ptxsim/arith/scalar.hpp>

#include <array>

namespace ptxsim::arith {

template <typename Packed>
constexpr Packed pack(const std::array<typename Packed::element_type,
                                       Packed::lanes_count>& lanes) {
  using traits = typename Packed::traits;
  std::uint64_t bits = 0;
  constexpr auto lane_mask =
      traits::logical_element_bits == 64
          ? ~std::uint64_t{}
          : (std::uint64_t{1} << traits::logical_element_bits) - 1;
  for (std::size_t lane = 0; lane != Packed::lanes_count; ++lane)
    bits |= (std::uint64_t{lanes[lane].bits()} & lane_mask)
            << traits::lane_offset(lane);
  return Packed::from_bits(static_cast<typename Packed::container_type>(bits));
}

template <typename Packed>
constexpr std::array<typename Packed::element_type, Packed::lanes_count> unpack(
    Packed value) {
  std::array<typename Packed::element_type, Packed::lanes_count> lanes{};
  for (std::size_t lane = 0; lane != Packed::lanes_count; ++lane)
    lanes[lane] = value[lane];
  return lanes;
}

template <typename Packed>
struct packed_operation_capability : std::false_type {};

template <typename Element, std::size_t Lanes, typename Layout>
struct packed_operation_capability<packed_t<Element, Lanes, Layout>>
    : std::bool_constant<std::same_as<Element, float16_t> ||
                         std::same_as<Element, bfloat16_t> ||
                         std::same_as<Element, float32_t>> {};

template <typename Packed, typename Operation>
  requires packed_operation_capability<Packed>::value
inline std::expected<result<Packed, floating_status>, arithmetic_error>
map_lanes(const context& ctx, Packed lhs, Packed rhs, Operation operation,
          floating_control control = {}) {
  std::array<typename Packed::element_type, Packed::lanes_count> out{};
  floating_status status{};
  for (std::size_t lane = 0; lane != Packed::lanes_count; ++lane) {
    auto value = operation(ctx, lhs[lane], rhs[lane], control);
    if (!value)
      return std::unexpected(value.error());
    out[lane] = value->value;
    status.invalid |= value->status.invalid;
    status.divide_by_zero |= value->status.divide_by_zero;
    status.overflow |= value->status.overflow;
    status.underflow |= value->status.underflow;
    status.inexact |= value->status.inexact;
  }
  return {{pack<Packed>(out), status}};
}

template <typename Element, std::size_t Lanes, typename Layout>
  requires packed_operation_capability<packed_t<Element, Lanes, Layout>>::value
inline std::expected<result<packed_t<Element, Lanes, Layout>, floating_status>,
                     arithmetic_error>
add(const context& ctx, packed_t<Element, Lanes, Layout> lhs,
    packed_t<Element, Lanes, Layout> rhs, floating_control control = {}) {
  return map_lanes(
      ctx, lhs, rhs,
      [](const context& c, Element a, Element b, floating_control x) {
        return add(c, a, b, x);
      },
      control);
}

template <typename Element, std::size_t Lanes, typename Layout>
  requires packed_operation_capability<packed_t<Element, Lanes, Layout>>::value
inline std::expected<result<packed_t<Element, Lanes, Layout>, floating_status>,
                     arithmetic_error>
sub(const context& ctx, packed_t<Element, Lanes, Layout> lhs,
    packed_t<Element, Lanes, Layout> rhs, floating_control control = {}) {
  return map_lanes(
      ctx, lhs, rhs,
      [](const context& c, Element a, Element b, floating_control x) {
        return sub(c, a, b, x);
      },
      control);
}

template <typename Element, std::size_t Lanes, typename Layout>
  requires packed_operation_capability<packed_t<Element, Lanes, Layout>>::value
inline std::expected<result<packed_t<Element, Lanes, Layout>, floating_status>,
                     arithmetic_error>
mul(const context& ctx, packed_t<Element, Lanes, Layout> lhs,
    packed_t<Element, Lanes, Layout> rhs, floating_control control = {}) {
  return map_lanes(
      ctx, lhs, rhs,
      [](const context& c, Element a, Element b, floating_control x) {
        return mul(c, a, b, x);
      },
      control);
}

template <typename Element, std::size_t Lanes, typename Layout>
  requires packed_operation_capability<packed_t<Element, Lanes, Layout>>::value
inline std::expected<result<packed_t<Element, Lanes, Layout>, floating_status>,
                     arithmetic_error>
fma(const context& ctx, packed_t<Element, Lanes, Layout> a,
    packed_t<Element, Lanes, Layout> b, packed_t<Element, Lanes, Layout> c,
    floating_control control = {}) {
  std::array<Element, Lanes> out{};
  floating_status status{};
  for (std::size_t lane = 0; lane != Lanes; ++lane) {
    auto value = fma(ctx, a[lane], b[lane], c[lane], control);
    if (!value)
      return std::unexpected(value.error());
    out[lane] = value->value;
    status.invalid |= value->status.invalid;
    status.divide_by_zero |= value->status.divide_by_zero;
    status.overflow |= value->status.overflow;
    status.underflow |= value->status.underflow;
    status.inexact |= value->status.inexact;
  }
  return {{pack<packed_t<Element, Lanes, Layout>>(out), status}};
}

}  // namespace ptxsim::arith
