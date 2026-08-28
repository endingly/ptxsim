#pragma once

#include <ptxsim/arith/scalar.hpp>

#include <array>

namespace ptxsim::arith {

enum class packed_operation { add, sub, mul, fma };

// Format legality is deliberately separate from arithmetic legality.  A
// format may be valid packed storage for conversion/tensor input while PTX
// exposes no corresponding lane-wise arithmetic form.
template <typename Element, std::size_t Lanes, typename Layout>
struct packed_format_capability : std::false_type {};

template <typename Element>
struct packed_format_capability<Element, 2, dense_packed_layout>
    : std::bool_constant<std::same_as<Element, float16_t> ||
                         std::same_as<Element, bfloat16_t> ||
                         std::same_as<Element, float8_e4m3_t> ||
                         std::same_as<Element, float8_e5m2_t> ||
                         std::same_as<Element, float4_e2m1_t> ||
                         std::same_as<Element, ufloat8_e8m0_t> ||
                         std::same_as<Element, fixed8_s2f6_t>> {};

template <typename Element>
struct packed_format_capability<Element, 2, byte_packed_layout>
    : std::bool_constant<std::same_as<Element, float6_e2m3_t> ||
                         std::same_as<Element, float6_e3m2_t>> {};

template <typename Element>
struct packed_format_capability<Element, 4, dense_packed_layout>
    : std::bool_constant<std::same_as<Element, float8_e4m3_t> ||
                         std::same_as<Element, float8_e5m2_t> ||
                         std::same_as<Element, float4_e2m1_t>> {};

template <typename Element>
struct packed_format_capability<Element, 4, byte_packed_layout>
    : std::bool_constant<std::same_as<Element, float6_e2m3_t> ||
                         std::same_as<Element, float6_e3m2_t>> {};

template <typename Element, std::size_t Lanes, typename Layout>
inline constexpr bool packed_format_capability_v =
    packed_format_capability<Element, Lanes, Layout>::value;

template <typename Packed>
  requires operation_capability<scalar_operation::pack, Packed,
                                std::array<typename Packed::element_type,
                                           Packed::lanes_count>>::value
constexpr Packed pack(const std::array<typename Packed::element_type,
                                       Packed::lanes_count>& lanes) {
  using traits = typename Packed::traits;
  std::uint64_t bits = 0;
  constexpr auto lane_mask =
      traits::logical_element_bits == 64
          ? ~std::uint64_t{}
          : (std::uint64_t{1} << traits::logical_element_bits) - 1;
  for (std::size_t lane = 0; lane != Packed::lanes_count; ++lane)
    bits |= (packed_element_codec<typename Packed::element_type>::to_bits(
                 lanes[lane]) &
             lane_mask)
            << traits::lane_offset(lane);
  return Packed::from_bits(static_cast<typename Packed::container_type>(bits));
}

template <typename Packed>
  requires operation_capability<scalar_operation::unpack,
                                std::array<typename Packed::element_type,
                                           Packed::lanes_count>, Packed>::value
constexpr std::array<typename Packed::element_type, Packed::lanes_count> unpack(
    Packed value) {
  std::array<typename Packed::element_type, Packed::lanes_count> lanes{};
  for (std::size_t lane = 0; lane != Packed::lanes_count; ++lane)
    lanes[lane] = value[lane];
  return lanes;
}

template <packed_operation Operation, typename Packed>
struct packed_operation_capability : std::false_type {};

// PTX 9.3 has lane-wise scalar arithmetic for the standard x2 half and BF16
// containers.  Do not infer arithmetic from storage capability (in
// particular, FP8/FP6/FP4, UE8M0 and S2F6 are storage-only here).
template <typename Element, std::size_t Lanes, typename Layout>
struct operation_capability<
    scalar_operation::pack, packed_t<Element, Lanes, Layout>,
    std::array<Element, Lanes>>
    : packed_format_capability<Element, Lanes, Layout> {};
template <typename Element, std::size_t Lanes, typename Layout>
struct operation_capability<
    scalar_operation::unpack, std::array<Element, Lanes>,
    packed_t<Element, Lanes, Layout>>
    : packed_format_capability<Element, Lanes, Layout> {};

template <typename Element>
struct operation_capability<scalar_operation::add,
                            packed_t<Element, 2, dense_packed_layout>,
                            packed_t<Element, 2, dense_packed_layout>,
                            packed_t<Element, 2, dense_packed_layout>>
    : std::bool_constant<(std::same_as<Element, float16_t> ||
                          std::same_as<Element, bfloat16_t>) &&
                         operation_capability<scalar_operation::add, Element,
                                              Element, Element>::value> {};
template <typename Element>
struct operation_capability<scalar_operation::sub,
                            packed_t<Element, 2, dense_packed_layout>,
                            packed_t<Element, 2, dense_packed_layout>,
                            packed_t<Element, 2, dense_packed_layout>>
    : std::bool_constant<(std::same_as<Element, float16_t> ||
                          std::same_as<Element, bfloat16_t>) &&
                         operation_capability<scalar_operation::sub, Element,
                                              Element, Element>::value> {};
template <typename Element>
struct operation_capability<scalar_operation::mul,
                            packed_t<Element, 2, dense_packed_layout>,
                            packed_t<Element, 2, dense_packed_layout>,
                            packed_t<Element, 2, dense_packed_layout>>
    : std::bool_constant<(std::same_as<Element, float16_t> ||
                          std::same_as<Element, bfloat16_t>) &&
                         operation_capability<scalar_operation::mul, Element,
                                              Element, Element>::value> {};
template <typename Element>
struct operation_capability<scalar_operation::fma,
                            packed_t<Element, 2, dense_packed_layout>,
                            packed_t<Element, 2, dense_packed_layout>,
                            packed_t<Element, 2, dense_packed_layout>,
                            packed_t<Element, 2, dense_packed_layout>>
    : std::bool_constant<(std::same_as<Element, float16_t> ||
                          std::same_as<Element, bfloat16_t>) &&
                         operation_capability<scalar_operation::fma, Element,
                                              Element, Element, Element>::value> {};

template <packed_operation Operation, typename Element>
struct packed_operation_capability<Operation,
                                   packed_t<Element, 2, dense_packed_layout>>
    : std::bool_constant<
          Operation == packed_operation::add
              ? operation_capability<scalar_operation::add,
                                     packed_t<Element, 2>, packed_t<Element, 2>,
                                     packed_t<Element, 2>>::value
              : Operation == packed_operation::sub
                    ? operation_capability<scalar_operation::sub,
                                           packed_t<Element, 2>, packed_t<Element, 2>,
                                           packed_t<Element, 2>>::value
                    : Operation == packed_operation::mul
                          ? operation_capability<scalar_operation::mul,
                                                 packed_t<Element, 2>, packed_t<Element, 2>,
                                                 packed_t<Element, 2>>::value
                          : operation_capability<scalar_operation::fma,
                                                 packed_t<Element, 2>, packed_t<Element, 2>,
                                                 packed_t<Element, 2>,
                                                 packed_t<Element, 2>>::value> {};

template <packed_operation Kind, typename Packed, typename Operation>
  requires packed_operation_capability<Kind, Packed>::value
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
  requires packed_operation_capability<packed_operation::add,
                                       packed_t<Element, Lanes, Layout>>::value
inline std::expected<result<packed_t<Element, Lanes, Layout>, floating_status>,
                     arithmetic_error>
add(const context& ctx, packed_t<Element, Lanes, Layout> lhs,
    packed_t<Element, Lanes, Layout> rhs, floating_control control = {}) {
  return map_lanes<packed_operation::add>(
      ctx, lhs, rhs,
      [](const context& c, Element a, Element b, floating_control x) {
        return add(c, a, b, x);
      },
      control);
}

template <typename Element, std::size_t Lanes, typename Layout>
  requires packed_operation_capability<packed_operation::sub,
                                       packed_t<Element, Lanes, Layout>>::value
inline std::expected<result<packed_t<Element, Lanes, Layout>, floating_status>,
                     arithmetic_error>
sub(const context& ctx, packed_t<Element, Lanes, Layout> lhs,
    packed_t<Element, Lanes, Layout> rhs, floating_control control = {}) {
  return map_lanes<packed_operation::sub>(
      ctx, lhs, rhs,
      [](const context& c, Element a, Element b, floating_control x) {
        return sub(c, a, b, x);
      },
      control);
}

template <typename Element, std::size_t Lanes, typename Layout>
  requires packed_operation_capability<packed_operation::mul,
                                       packed_t<Element, Lanes, Layout>>::value
inline std::expected<result<packed_t<Element, Lanes, Layout>, floating_status>,
                     arithmetic_error>
mul(const context& ctx, packed_t<Element, Lanes, Layout> lhs,
    packed_t<Element, Lanes, Layout> rhs, floating_control control = {}) {
  return map_lanes<packed_operation::mul>(
      ctx, lhs, rhs,
      [](const context& c, Element a, Element b, floating_control x) {
        return mul(c, a, b, x);
      },
      control);
}

template <typename Element, std::size_t Lanes, typename Layout>
  requires packed_operation_capability<packed_operation::fma,
                                       packed_t<Element, Lanes, Layout>>::value
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
