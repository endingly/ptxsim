#pragma once
#include <bit>
#include <expected>
#include <limits>
#include <ptxsim/arith/concepts.hpp>
#include <ptxsim/arith/context.hpp>
#include <ptxsim/arith/controls.hpp>
#include <ptxsim/arith/detail/dispatch.hpp>
#include <ptxsim/arith/detail/format_traits.hpp>
#include <ptxsim/arith/error.hpp>
#include <ptxsim/arith/result.hpp>

namespace ptxsim::arith {
namespace detail {
constexpr bool supported_approximation_profile(const context& ctx) {
  // The reference profile is deliberately the only implemented approximation
  // model.  Do not pretend an arbitrary target model has these bit patterns.
  return ctx.profile().approximation.model == 0;
}
template <typename T>
constexpr bool special_subnormal_valid(special_function_control c) {
  (void)c;
  return true;
}
template <typename T>
constexpr bool valid(floating_control c) {
  return c.rounding != rounding_mode::nearest_away &&
         c.rounding != rounding_mode::stochastic &&
         c.saturation == saturation_mode::none &&
         c.activation == activation_mode::none &&
         !(std::same_as<T, float16_t> &&
           c.rounding != rounding_mode::nearest_even);
}
template <typename T>
constexpr T wrap_add(T a, T b) {
  using U = std::make_unsigned_t<T>;
  return std::bit_cast<T>(
      static_cast<U>(std::bit_cast<U>(a) + std::bit_cast<U>(b)));
}
template <typename T>
constexpr T wrap_sub(T a, T b) {
  using U = std::make_unsigned_t<T>;
  return std::bit_cast<T>(
      static_cast<U>(std::bit_cast<U>(a) - std::bit_cast<U>(b)));
}
template <arithmetic_integer T>
using unsigned_integer_t = std::make_unsigned_t<T>;
template <arithmetic_integer T>
using integer_wide_t = std::conditional_t<
    (sizeof(T) <= 1),
    std::conditional_t<std::is_signed_v<T>, std::int16_t, std::uint16_t>,
    std::conditional_t<
        (sizeof(T) <= 2),
        std::conditional_t<std::is_signed_v<T>, std::int32_t, std::uint32_t>,
        std::conditional_t<
            (sizeof(T) <= 4),
            std::conditional_t<std::is_signed_v<T>, std::int64_t,
                               std::uint64_t>,
            std::conditional_t<std::is_signed_v<T>, __int128_t, __uint128_t>>>>;
template <arithmetic_integer T>
constexpr unsigned_integer_t<T> bits(T v) noexcept {
  return static_cast<unsigned_integer_t<T>>(v);
}
template <arithmetic_integer T>
constexpr T from_bits(unsigned_integer_t<T> v) noexcept {
  return std::bit_cast<T>(v);
}
template <arithmetic_integer T>
constexpr T wrap_mul_low(T a, T b) noexcept {
  using U = unsigned_integer_t<T>;
  using W = integer_wide_t<U>;
  return from_bits<T>(static_cast<U>(W(bits(a)) * W(bits(b))));
}
template <arithmetic_integer T>
constexpr result<T, integer_status> saturate_from(
    integer_wide_t<T> v) noexcept {
  using W = integer_wide_t<T>;
  if constexpr (std::is_signed_v<T>) {
    if (v > W(std::numeric_limits<T>::max()))
      return {std::numeric_limits<T>::max(), {false, false, true}};
    if (v < W(std::numeric_limits<T>::min()))
      return {std::numeric_limits<T>::min(), {false, false, true}};
  } else if (v > W(std::numeric_limits<T>::max())) {
    return {std::numeric_limits<T>::max(), {false, false, true}};
  }
  return {static_cast<T>(v), {}};
}
template <typename T>
constexpr T apply_output_ftz(T v, subnormal_mode m) {
  return (m == subnormal_mode::flush_output ||
          m == subnormal_mode::flush_input_and_output)
             ? flush_subnormal(v)
             : v;
}
}  // namespace detail

template <arithmetic_integer T>
using integer_wide_t = detail::integer_wide_t<T>;

template <typename Result = void, arithmetic_integer T>
constexpr std::expected<result<T, integer_status>, arithmetic_error> add(
    const context&, T a, T b, integer_control c = {}) {
  if (c.overflow == integer_overflow_mode::wrap)
    return result<T, integer_status>{detail::wrap_add(a, b), {}};
  if constexpr (std::is_unsigned_v<T>) {
    T v = detail::wrap_add(a, b);
    return result<T, integer_status>{v < a ? std::numeric_limits<T>::max() : v,
                                     {false, false, v < a}};
  } else {
    return detail::saturate_from<T>(detail::integer_wide_t<T>(a) +
                                    detail::integer_wide_t<T>(b));
  }
}
template <typename Result = void, arithmetic_integer T>
constexpr std::expected<result<T, integer_status>, arithmetic_error> sub(
    const context&, T a, T b, integer_control c = {}) {
  if (c.overflow == integer_overflow_mode::wrap)
    return result<T, integer_status>{detail::wrap_sub(a, b), {}};
  if constexpr (std::is_unsigned_v<T>)
    return result<T, integer_status>{a < b ? T{} : T(a - b),
                                     {false, a < b, a < b}};
  else {
    return detail::saturate_from<T>(detail::integer_wide_t<T>(a) -
                                    detail::integer_wide_t<T>(b));
  }
}
template <typename Result = void, arithmetic_integer T>
  requires(std::same_as<Result, void> || arithmetic_integer<Result>)
constexpr std::expected<
    result<std::conditional_t<std::same_as<Result, void>, T, Result>,
           integer_status>,
    arithmetic_error>
mul(const context&, T a, T b, product_control c = {}) {
  using R = std::conditional_t<std::same_as<Result, void>, T, Result>;
  using U = detail::unsigned_integer_t<T>;
  using W = detail::integer_wide_t<T>;
  using UW = std::make_unsigned_t<W>;
  constexpr unsigned n = std::numeric_limits<U>::digits;
  if (c.part == product_part::wide) {
    if constexpr (!std::same_as<R, integer_wide_t<T>>) {
      return std::unexpected(arithmetic_error::unsupported_type_combination);
    } else if (c.overflow != integer_overflow_mode::wrap) {
      return std::unexpected(arithmetic_error::unsupported_overflow_mode);
    } else if constexpr (std::is_signed_v<T>) {
      return result<R, integer_status>{static_cast<R>(W(a) * W(b)), {}};
    } else {
      return result<R, integer_status>{
          static_cast<R>(W(detail::bits(a)) * W(detail::bits(b))), {}};
    }
  }
  if constexpr (!std::same_as<R, T>) {
    return std::unexpected(arithmetic_error::unsupported_type_combination);
  } else {
    if (c.part == product_part::high &&
        c.overflow != integer_overflow_mode::wrap)
      return std::unexpected(arithmetic_error::unsupported_overflow_mode);
    if (c.part == product_part::low &&
        c.overflow == integer_overflow_mode::saturate) {
      if constexpr (std::is_signed_v<T>)
        return detail::saturate_from<T>(W(a) * W(b));
      else
        return detail::saturate_from<T>(W(detail::bits(a)) *
                                        W(detail::bits(b)));
    }
    if (c.part == product_part::high) {
      const UW product = std::is_signed_v<T>
                             ? static_cast<UW>(W(a) * W(b))
                             : UW(detail::bits(a)) * UW(detail::bits(b));
      return result<T, integer_status>{detail::from_bits<T>(U(product >> n)),
                                       {}};
    }
    return result<T, integer_status>{detail::wrap_mul_low(a, b), {}};
  }
}
template <typename Result = void, arithmetic_integer T,
          arithmetic_integer C = T>
  requires(std::same_as<Result, void> || arithmetic_integer<Result>)
constexpr std::expected<
    result<std::conditional_t<std::same_as<Result, void>, T, Result>,
           integer_status>,
    arithmetic_error>
mad(const context& ctx, T a, T b, C c, product_control control = {}) {
  using R = std::conditional_t<std::same_as<Result, void>, T, Result>;
  if constexpr (!std::same_as<C, R>) {
    return std::unexpected(arithmetic_error::unsupported_type_combination);
  } else {
    auto p = mul<Result>(ctx, a, b, control);
    if (!p)
      return std::unexpected(p.error());
    return add(ctx, p->value, c, {control.overflow});
  }
}
template <arithmetic_integer T>
constexpr result<T, integer_status> add_with_carry(T a, T b,
                                                   bool carry = false) {
  using U = std::make_unsigned_t<T>;
  U x = std::bit_cast<U>(a), y = std::bit_cast<U>(b), s = x + y + U(carry);
  return {std::bit_cast<T>(s), {s < x || (carry && s == x)}};
}
template <arithmetic_integer T>
constexpr result<T, integer_status> sub_with_borrow(T a, T b,
                                                    bool borrow = false) {
  using U = std::make_unsigned_t<T>;
  U x = std::bit_cast<U>(a), y = std::bit_cast<U>(b), d = x - y - U(borrow);
  return {std::bit_cast<T>(d), {false, x < y || (borrow && x == y)}};
}
template <arithmetic_integer T>
constexpr result<T, integer_status> mad_with_carry(T a, T b, T c,
                                                   bool carry = false) {
  return add_with_carry(detail::wrap_mul_low(a, b), c, carry);
}
template <arithmetic_integer T>
constexpr std::expected<result<T, integer_status>, arithmetic_error> div(
    const context&, T a, T b, integer_control control = {}) {
  if (b == 0)
    return std::unexpected(arithmetic_error::division_by_zero);
  if constexpr (std::is_signed_v<T>)
    if (a == std::numeric_limits<T>::min() && b == T{-1})
      return control.overflow == integer_overflow_mode::saturate
                 ? result<T, integer_status>{std::numeric_limits<T>::max(),
                                             {false, false, true}}
                 : result<T, integer_status>{a, {false, false, true}};
  return result<T, integer_status>{static_cast<T>(a / b), {}};
}
template <arithmetic_integer T>
constexpr std::expected<result<T, integer_status>, arithmetic_error> rem(
    const context&, T a, T b, integer_control control = {}) {
  if (b == 0)
    return std::unexpected(arithmetic_error::division_by_zero);
  if constexpr (std::is_signed_v<T>)
    if (a == std::numeric_limits<T>::min() && b == T{-1})
      return result<T, integer_status>{T{}, {false, false,
                                             control.overflow == integer_overflow_mode::saturate}};
  return result<T, integer_status>{static_cast<T>(a % b), {}};
}
template <arithmetic_integer T>
constexpr std::expected<result<T, integer_status>, arithmetic_error> neg(
    const context&, T a, integer_control c = {}) {
  if constexpr (std::is_unsigned_v<T>)
    return result<T, integer_status>{detail::wrap_sub(T{}, a), {}};
  if (a != std::numeric_limits<T>::min())
    return result<T, integer_status>{static_cast<T>(-a), {}};
  if (c.overflow == integer_overflow_mode::saturate)
    return result<T, integer_status>{std::numeric_limits<T>::max(),
                                     {false, false, true}};
  return result<T, integer_status>{a, {false, false, true}};
}
template <arithmetic_integer T>
constexpr std::expected<result<T, integer_status>, arithmetic_error> abs(
    const context& ctx, T a, integer_control c = {}) {
  if constexpr (std::is_unsigned_v<T>)
    return result<T, integer_status>{a, {}};
  return a < 0 ? neg(ctx, a, c) : result<T, integer_status>{a, {}};
}
template <arithmetic_integer T>
constexpr std::expected<result<T, integer_status>, arithmetic_error> min(
    const context&, T a, T b) {
  return result<T, integer_status>{a < b ? a : b, {}};
}
template <arithmetic_integer T>
constexpr std::expected<result<T, integer_status>, arithmetic_error> max(
    const context&, T a, T b) {
  return result<T, integer_status>{a < b ? b : a, {}};
}
template <arithmetic_integer T>
constexpr std::expected<result<T, integer_status>, arithmetic_error> sad(
    const context&, T a, T b, T c, integer_control control = {}) {
  using W = detail::integer_wide_t<T>;
  if constexpr (std::is_signed_v<T>) {
    const W d = W(a) - W(b);
    return control.overflow == integer_overflow_mode::saturate
               ? detail::saturate_from<T>((d < 0 ? -d : d) + W(c))
               : result<T, integer_status>{
                     detail::from_bits<T>(
                         static_cast<detail::unsigned_integer_t<T>>(
                             (d < 0 ? -d : d) + W(c))),
                     {}};
  } else {
    const W d = a >= b ? W(a) - W(b) : W(b) - W(a);
    return control.overflow == integer_overflow_mode::saturate
               ? detail::saturate_from<T>(d + W(c))
               : result<T, integer_status>{static_cast<T>(d + W(c)), {}};
  }
}

#define PTXSIM_ARITH_FLOAT_BINARY(name, capability)                        \
  template <typename Result = void, typename T>                            \
    requires scalar_float<T> &&                                            \
             operation_capability<scalar_operation::capability, T, T,      \
                                  T>::value                                \
  inline std::expected<result<T, floating_status>, arithmetic_error> name( \
      const context&, T a, T b, floating_control c = {}) {                 \
    return detail::dispatch::name(a, b, c);                                \
  }
PTXSIM_ARITH_FLOAT_BINARY(add, add)
PTXSIM_ARITH_FLOAT_BINARY(sub, sub)
PTXSIM_ARITH_FLOAT_BINARY(mul, mul)
PTXSIM_ARITH_FLOAT_BINARY(div, div)
#undef PTXSIM_ARITH_FLOAT_BINARY
template <typename Result, typename A, typename B>
  requires operation_capability<scalar_operation::add, Result, A, B>::value &&
           (!std::same_as<A, Result> || !std::same_as<B, Result>)
inline std::expected<result<Result, floating_status>, arithmetic_error> add(
    const context&, A a, B b, floating_control control = {}) {
  return detail::dispatch::add(a, b, control);
}
template <typename Result, typename A, typename B>
  requires operation_capability<scalar_operation::sub, Result, A, B>::value &&
           (!std::same_as<A, Result> || !std::same_as<B, Result>)
inline std::expected<result<Result, floating_status>, arithmetic_error> sub(
    const context&, A a, B b, floating_control control = {}) {
  return detail::dispatch::sub(a, b, control);
}
template <typename Result = void, typename T>
  requires operation_capability<scalar_operation::fma, T, T, T, T>::value
inline std::expected<result<T, floating_status>, arithmetic_error> fma(
    const context&, T a, T b, T c, floating_control control = {}) {
  return detail::dispatch::fma(a, b, c, control);
}

template <typename Result, typename A, typename B, typename C>
  requires operation_capability<scalar_operation::fma, Result, A, B,
                                C>::value &&
           (!std::same_as<A, Result> || !std::same_as<B, Result>)
inline std::expected<result<Result, floating_status>, arithmetic_error> fma(
    const context&, A a, B b, C c, floating_control control = {}) {
  return detail::dispatch::fma(a, b, c, control);
}
template <typename Result = void, typename T>
  requires operation_capability<scalar_operation::fma, T, T, T, T>::value
inline std::expected<result<T, floating_status>, arithmetic_error> mad(
    const context& ctx, T a, T b, T c, floating_control control = {}) {
  auto p = mul(ctx, a, b, control);
  if (!p)
    return std::unexpected(p.error());
  return add(ctx, p->value, c, control);
}
template <typename T>
  requires operation_capability<scalar_operation::sqrt, T, T>::value
inline std::expected<result<T, floating_status>, arithmetic_error> sqrt(
    const context&, T a, floating_control c = {}) {
  return detail::dispatch::sqrt(a, c);
}
template <typename T>
  requires(std::same_as<T, float32_t> || std::same_as<T, float64_t>)
inline std::expected<result<T, floating_status>, arithmetic_error> rcp(
    const context&, T a, floating_control c = {}) {
  return detail::dispatch::rcp(a, c);
}
template <typename T>
  requires(std::same_as<T, float16_t> || std::same_as<T, bfloat16_t> ||
           std::same_as<T, float32_t> || std::same_as<T, float64_t>)
inline std::expected<result<T, floating_status>, arithmetic_error> abs(
    const context&, T a, floating_control c = {}) {
  return detail::dispatch::abs(a, c);
}
template <typename T>
  requires(std::same_as<T, float16_t> || std::same_as<T, bfloat16_t> ||
           std::same_as<T, float32_t> || std::same_as<T, float64_t>)
inline std::expected<result<T, floating_status>, arithmetic_error> neg(
    const context&, T a, floating_control c = {}) {
  return detail::dispatch::neg(a, c);
}
template <typename T>
  requires(std::same_as<T, float16_t> || std::same_as<T, bfloat16_t> ||
           std::same_as<T, float32_t> || std::same_as<T, float64_t>)
inline std::expected<result<T, floating_status>, arithmetic_error> min(
    const context&, T a, T b, floating_control c = {}) {
  return detail::dispatch::min(a, b, c);
}
template <typename T>
  requires(std::same_as<T, float16_t> || std::same_as<T, bfloat16_t> ||
           std::same_as<T, float32_t> || std::same_as<T, float64_t>)
inline std::expected<result<T, floating_status>, arithmetic_error> max(
    const context&, T a, T b, floating_control c = {}) {
  return detail::dispatch::max(a, b, c);
}
template <typename T>
  requires(std::same_as<T, float16_t> || std::same_as<T, bfloat16_t> ||
           std::same_as<T, float32_t> || std::same_as<T, float64_t>)
constexpr result<T> copysign(const context&, T sign, T magnitude) noexcept {
  using Bits = typename FormatTraits<T>::Bits;
  return {T::from_bits(static_cast<Bits>(
      (sign.bits() & FormatTraits<T>::sign_mask) |
      (magnitude.bits() & static_cast<Bits>(~FormatTraits<T>::sign_mask))))};
}
inline std::expected<result<float32_t, floating_status>, arithmetic_error> div(
    const context& ctx, float32_t a, float32_t b,
    special_function_control c) {
  if (!detail::special_subnormal_valid<float32_t>(c))
    return std::unexpected(arithmetic_error::unsupported_subnormal_mode);
  if (c.approximation == approximation_mode::exact)
    return div(ctx, a, b, floating_control{.subnormal = c.subnormal});
  if (!detail::supported_approximation_profile(ctx))
    return std::unexpected(arithmetic_error::unsupported_operation);
  return c.approximation == approximation_mode::ptx_approximate
             ? detail::dispatch::div_approx(a, b, c)
             : detail::dispatch::div_full(a, b, c);
}
template <typename T>
  requires(std::same_as<T, float32_t> || std::same_as<T, float64_t>)
inline std::expected<result<T, floating_status>, arithmetic_error> sqrt(
    const context& ctx, T a, special_function_control c) {
  if (!detail::special_subnormal_valid<T>(c))
    return std::unexpected(arithmetic_error::unsupported_subnormal_mode);
  if (c.approximation == approximation_mode::exact)
    return sqrt(ctx, a, floating_control{.subnormal = c.subnormal});
  if constexpr (!std::same_as<T, float32_t>)
    return std::unexpected(arithmetic_error::unsupported_operation);
  else if (!detail::supported_approximation_profile(ctx))
    return std::unexpected(arithmetic_error::unsupported_operation);
  else
    return detail::dispatch::sqrt_approx(a, c);
}
template <typename T>
  requires(std::same_as<T, float32_t> || std::same_as<T, float64_t>)
inline std::expected<result<T, floating_status>, arithmetic_error> rcp(
    const context& ctx, T a, special_function_control c) {
  if (!detail::special_subnormal_valid<T>(c))
    return std::unexpected(arithmetic_error::unsupported_subnormal_mode);
  if (c.approximation == approximation_mode::exact)
    return rcp(ctx, a, floating_control{.subnormal = c.subnormal});
  if (c.approximation == approximation_mode::ptx_full ||
      !detail::supported_approximation_profile(ctx))
    return std::unexpected(arithmetic_error::unsupported_operation);
  if constexpr (std::same_as<T, float32_t>)
    return detail::dispatch::rcp_approx(a, c);
  else
    return std::unexpected(arithmetic_error::unsupported_operation);
}
template <typename T>
  requires(std::same_as<T, float32_t> || std::same_as<T, float64_t>)
inline std::expected<result<T, floating_status>, arithmetic_error> rsqrt(
    const context& ctx, T a, special_function_control c = {}) {
  if (!detail::special_subnormal_valid<T>(c))
    return std::unexpected(arithmetic_error::unsupported_subnormal_mode);
  if (c.approximation == approximation_mode::exact) {
    auto root = sqrt(ctx, a, floating_control{.subnormal = c.subnormal});
    if (!root)
      return std::unexpected(root.error());
    auto reciprocal = rcp(ctx, root->value,
                          floating_control{.subnormal = c.subnormal});
    if (!reciprocal)
      return std::unexpected(reciprocal.error());
    reciprocal->status.invalid |= root->status.invalid;
    reciprocal->status.divide_by_zero |= root->status.divide_by_zero;
    reciprocal->status.overflow |= root->status.overflow;
    reciprocal->status.underflow |= root->status.underflow;
    reciprocal->status.inexact |= root->status.inexact;
    return reciprocal;
  }
  if (c.approximation == approximation_mode::ptx_full ||
      !detail::supported_approximation_profile(ctx))
    return std::unexpected(arithmetic_error::unsupported_operation);
  if constexpr (std::same_as<T, float32_t>)
    return detail::dispatch::rsqrt_approx(a, c);
  else if (c.subnormal == subnormal_mode::flush_input ||
           c.subnormal == subnormal_mode::flush_input_and_output) {
    return std::unexpected(arithmetic_error::unsupported_operation);
  }
  else
    return std::unexpected(arithmetic_error::unsupported_subnormal_mode);
}
#define PTXSIM_ARITH_APPROX_UNARY(name)                                   \
  inline std::expected<result<float32_t, floating_status>, arithmetic_error> \
  name(const context& ctx, float32_t a, special_function_control c = {}) { \
    if (c.approximation != approximation_mode::ptx_approximate ||          \
        !detail::special_subnormal_valid<float32_t>(c) ||                   \
        !detail::supported_approximation_profile(ctx))                      \
      return std::unexpected(arithmetic_error::unsupported_operation);      \
    return detail::dispatch::name##_approx(a, c);                           \
  }
PTXSIM_ARITH_APPROX_UNARY(sin)
PTXSIM_ARITH_APPROX_UNARY(cos)
PTXSIM_ARITH_APPROX_UNARY(lg2)
PTXSIM_ARITH_APPROX_UNARY(ex2)
#undef PTXSIM_ARITH_APPROX_UNARY
inline std::expected<result<float32_t, floating_status>, arithmetic_error> tanh(
    const context& ctx, float32_t a, special_function_control c = {}) {
  if (c.approximation != approximation_mode::ptx_approximate ||
      !detail::special_subnormal_valid<float32_t>(c) ||
      !detail::supported_approximation_profile(ctx))
    return std::unexpected(arithmetic_error::unsupported_operation);
  if (c.subnormal == subnormal_mode::flush_input ||
      c.subnormal == subnormal_mode::flush_input_and_output)
    a = flush_subnormal(a);
  return detail::dispatch::tanh_approx(a, c);
}
template <typename T>
  requires(std::same_as<T, float16_t> || std::same_as<T, bfloat16_t>)
inline std::expected<result<T, floating_status>, arithmetic_error> tanh(
    const context& ctx, T a, special_function_control c = {}) {
  if (c.approximation != approximation_mode::ptx_approximate ||
      !detail::supported_approximation_profile(ctx))
    return std::unexpected(arithmetic_error::unsupported_operation);
  if constexpr (std::same_as<T, bfloat16_t>)
    if (c.subnormal != subnormal_mode::flush_input_and_output)
      return std::unexpected(arithmetic_error::unsupported_subnormal_mode);
  if (c.subnormal == subnormal_mode::flush_input ||
      c.subnormal == subnormal_mode::flush_input_and_output)
    a = flush_subnormal(a);
  return detail::dispatch::tanh_approx(a, c);
}
template <typename T>
  requires(std::same_as<T, float16_t> || std::same_as<T, bfloat16_t>)
inline std::expected<result<T, floating_status>, arithmetic_error> ex2(
    const context& ctx, T a, special_function_control c = {}) {
  if (c.approximation != approximation_mode::ptx_approximate ||
      !detail::supported_approximation_profile(ctx))
    return std::unexpected(arithmetic_error::unsupported_operation);
  if constexpr (std::same_as<T, bfloat16_t>)
    if (c.subnormal != subnormal_mode::flush_input_and_output)
      return std::unexpected(arithmetic_error::unsupported_subnormal_mode);
  if (c.subnormal == subnormal_mode::flush_input ||
      c.subnormal == subnormal_mode::flush_input_and_output)
    a = flush_subnormal(a);
  return detail::dispatch::ex2_approx(a, c);
}
template <typename T>
constexpr result<T> select(predicate_t p, T yes, T no) noexcept {
  return {p ? yes : no};
}
template <typename T>
  requires FloatingFormat<T>
constexpr result<predicate_t> compare(const context&, T a, T b,
                                      comparison_control c = {}) {
  if (is_nan(a) || is_nan(b))
    return {c.nan == nan_comparison_mode::unordered};
  if (is_zero(a) && is_zero(b)) {
    return {c.relation == comparison_relation::equal ||
            c.relation == comparison_relation::less_equal ||
            c.relation == comparison_relation::greater_equal};
  }
  using U = typename FormatTraits<T>::Bits;
  constexpr U sign = FormatTraits<T>::sign_mask;
  const auto order = [](U x) {
    return (x & sign) ? static_cast<U>(~x) : static_cast<U>(x | sign);
  };
  const auto x = order(normalize_encoding(a).bits()),
             y = order(normalize_encoding(b).bits());
  switch (c.relation) {
    case comparison_relation::equal:
      return {x == y};
    case comparison_relation::not_equal:
      return {x != y};
    case comparison_relation::less:
      return {x < y};
    case comparison_relation::less_equal:
      return {x <= y};
    case comparison_relation::greater:
      return {x > y};
    case comparison_relation::greater_equal:
      return {x >= y};
  }
  return {false};
}
template <typename To, typename From>
  requires convertible_to<To, From>
inline std::expected<result<To, floating_status>, arithmetic_error> cvt(
    const context& ctx, From value, conversion_control c = {}) {
  if (c.rounding == rounding_mode::nearest_away ||
      c.rounding == rounding_mode::stochastic)
    return std::unexpected(arithmetic_error::unsupported_rounding);
  if (c.activation != activation_mode::none)
    return std::unexpected(arithmetic_error::unsupported_operation);
  if constexpr (std::same_as<To, From>) {
    if constexpr (std::same_as<To, tfloat32_t>)
      if (ctx.profile().tf32.model != tf32_encoding_model::f32_top_19_bits)
        return std::unexpected(arithmetic_error::unsupported_operation);
    if (c.rounding != rounding_mode::nearest_even)
      return std::unexpected(arithmetic_error::unsupported_rounding);
    if (c.source_subnormal != subnormal_mode::preserve ||
        c.destination_subnormal != subnormal_mode::preserve)
      return std::unexpected(arithmetic_error::unsupported_subnormal_mode);
    if constexpr (arithmetic_integer<To>) {
      if (c.saturation != saturation_mode::none &&
          c.saturation != saturation_mode::type_range)
        return std::unexpected(arithmetic_error::unsupported_saturation);
    } else if (c.saturation != saturation_mode::none) {
      return std::unexpected(arithmetic_error::unsupported_saturation);
    }
    return {{value, {}}};
  }
  else if constexpr (arithmetic_integer<To> && arithmetic_integer<From>) {
    if (c.rounding != rounding_mode::nearest_even)
      return std::unexpected(arithmetic_error::unsupported_rounding);
    if (c.source_subnormal != subnormal_mode::preserve ||
        c.destination_subnormal != subnormal_mode::preserve)
      return std::unexpected(arithmetic_error::unsupported_subnormal_mode);
    if (c.saturation != saturation_mode::none &&
        c.saturation != saturation_mode::type_range)
      return std::unexpected(arithmetic_error::unsupported_saturation);
    using W = __int128_t;
    const W wide = static_cast<W>(value);
    if (c.saturation == saturation_mode::type_range) {
      if (wide > W(std::numeric_limits<To>::max()))
        return {{std::numeric_limits<To>::max(), {false, false, true}}};
      if constexpr (std::is_signed_v<To>)
        if (wide < W(std::numeric_limits<To>::min()))
          return {{std::numeric_limits<To>::min(), {false, false, true}}};
      if constexpr (std::is_unsigned_v<To>)
        if (wide < 0)
          return {{To{}, {false, false, true}}};
      return {{static_cast<To>(value), {}}};
    }
    using U = std::make_unsigned_t<To>;
    const U bits = static_cast<U>(value);
    const To wrapped = [&] {
      if constexpr (std::is_signed_v<To>) return std::bit_cast<To>(bits);
      else return bits;
    }();
    return {{wrapped, {}}};
  } else if constexpr (std::same_as<To, float32_t> &&
                       std::same_as<From, fixed8_s2f6_t>) {
    if (c.saturation != saturation_mode::none ||
        c.source_subnormal != subnormal_mode::preserve ||
        c.destination_subnormal != subnormal_mode::preserve)
      return std::unexpected(c.saturation != saturation_mode::none
                                 ? arithmetic_error::unsupported_saturation
                                 : arithmetic_error::unsupported_subnormal_mode);
    auto integer = cvt<float32_t>(ctx, static_cast<int32_t>(value.rep),
                                  {.rounding = rounding_mode::nearest_even});
    if (!integer)
      return std::unexpected(integer.error());
    auto scaled = mul(ctx, integer->value, float32_t::from_bits(0x3c800000));
    if (!scaled)
      return std::unexpected(scaled.error());
    return {{scaled->value, integer->status}};
  } else if constexpr (std::same_as<To, fixed8_s2f6_t> &&
                       std::same_as<From, float32_t>) {
    if (c.saturation != saturation_mode::none &&
        c.saturation != saturation_mode::type_range)
      return std::unexpected(arithmetic_error::unsupported_saturation);
    if (c.source_subnormal == subnormal_mode::flush_output ||
        c.source_subnormal == subnormal_mode::flush_input_and_output ||
        c.destination_subnormal != subnormal_mode::preserve)
      return std::unexpected(arithmetic_error::unsupported_subnormal_mode);
    const auto input = (c.source_subnormal == subnormal_mode::flush_input ||
                        c.source_subnormal == subnormal_mode::flush_input_and_output)
                           ? flush_subnormal(value)
                           : value;
    // S2F6 has range [-2, 127/64].  Its scale is a power of two, so F32
    // multiply by 64 is exact before the requested integer rounding.
    if (c.saturation == saturation_mode::type_range) {
      if (compare(ctx, input, float32_t::from_bits(0x3ffe0000),
                  {.relation = comparison_relation::greater}).value)
        return {{fixed8_s2f6_t{static_cast<int8_t>(127)}, {false, false, true}}};
      if (compare(ctx, input, float32_t::from_bits(0xc0000000),
                  {.relation = comparison_relation::less}).value)
        return {{fixed8_s2f6_t{static_cast<int8_t>(-128)}, {false, false, true}}};
    }
    auto scaled = mul(ctx, input, float32_t::from_bits(0x42800000),
                      {.rounding = c.rounding});
    if (!scaled)
      return std::unexpected(scaled.error());
    auto integer = cvt<int32_t>(ctx, scaled->value,
                                {.rounding = c.rounding});
    if (!integer)
      return std::unexpected(integer.error());
    return {{fixed8_s2f6_t{static_cast<int8_t>(integer->value)},
             {integer->status.invalid, integer->status.divide_by_zero,
              integer->status.overflow, integer->status.underflow,
              integer->status.inexact || scaled->status.inexact}}};
  } else if constexpr (std::same_as<To, fixed8_s2f6_t> &&
                       arithmetic_integer<From>) {
    auto widened = cvt<float32_t>(ctx, value, c);
    if (!widened)
      return std::unexpected(widened.error());
    auto fixed = cvt<fixed8_s2f6_t>(ctx, widened->value, c);
    if (!fixed)
      return std::unexpected(fixed.error());
    fixed->status.inexact |= widened->status.inexact;
    return fixed;
  } else if constexpr (arithmetic_integer<To> &&
                       std::same_as<From, fixed8_s2f6_t>) {
    auto widened = cvt<float32_t>(ctx, value, c);
    if (!widened)
      return std::unexpected(widened.error());
    auto integer = cvt<To>(ctx, widened->value, c);
    if (!integer)
      return std::unexpected(integer.error());
    integer->status.inexact |= widened->status.inexact;
    return integer;
  } else if constexpr (std::same_as<To, tfloat32_t> && std::same_as<From, float32_t>) {
    return detail::dispatch::quantize_tf32(value, c, ctx.profile().tf32);
  } else if constexpr (std::same_as<From, tfloat32_t> && std::same_as<To, float32_t>) {
    if (ctx.profile().tf32.model != tf32_encoding_model::f32_top_19_bits)
      return std::unexpected(arithmetic_error::unsupported_operation);
    if (c.saturation != saturation_mode::none ||
        c.source_subnormal != subnormal_mode::preserve ||
        c.destination_subnormal != subnormal_mode::preserve)
      return std::unexpected(c.saturation != saturation_mode::none
                                 ? arithmetic_error::unsupported_saturation
                                 : arithmetic_error::unsupported_subnormal_mode);
    return {{value.canonical_value(), {}}};
  } else if constexpr (std::same_as<To, float32_t> && std::same_as<From, std::int32_t>) {
    return detail::dispatch::i32_to_f32(value, c);
  } else if constexpr (std::same_as<To, float32_t> && std::same_as<From, std::uint32_t>) {
    return detail::dispatch::u32_to_f32(value, c);
  } else if constexpr (std::same_as<To, float32_t> && std::same_as<From, std::int64_t>) {
    return detail::dispatch::i64_to_f32(value, c);
  } else if constexpr (std::same_as<To, float32_t> && std::same_as<From, std::uint64_t>) {
    return detail::dispatch::u64_to_f32(value, c);
  } else if constexpr (std::same_as<To, float64_t> && std::same_as<From, std::int32_t>) {
    return detail::dispatch::i32_to_f64(value, c);
  } else if constexpr (std::same_as<To, float64_t> && std::same_as<From, std::uint32_t>) {
    return detail::dispatch::u32_to_f64(value, c);
  } else if constexpr (std::same_as<To, float64_t> && std::same_as<From, std::int64_t>) {
    return detail::dispatch::i64_to_f64(value, c);
  } else if constexpr (std::same_as<To, float64_t> && std::same_as<From, std::uint64_t>) {
    return detail::dispatch::u64_to_f64(value, c);
  } else if constexpr (std::same_as<To, float32_t> && scalar_float<From> &&
                       !std::same_as<From, tfloat32_t>) {
    return detail::dispatch::to_f32(value, c);
  } else if constexpr (std::same_as<To, float64_t> &&
                       (std::same_as<From, float32_t> ||
                        std::same_as<From, float16_t>)) {
    return detail::dispatch::to_f64(value, c);
  } else if constexpr (std::same_as<To, float16_t> &&
                       (std::same_as<From, float32_t> ||
                        std::same_as<From, float64_t>)) {
    return detail::dispatch::to_f16(value, c);
  } else if constexpr (std::same_as<To, bfloat16_t> && std::same_as<From, float32_t>) {
    return detail::dispatch::to_bf16(value, c);
  } else if constexpr (std::same_as<To, float8_e4m3_t> && std::same_as<From, float32_t>) {
    return detail::dispatch::to_f8e4m3(value, c);
  } else if constexpr (std::same_as<To, float8_e5m2_t> && std::same_as<From, float32_t>) {
    return detail::dispatch::to_f8e5m2(value, c);
  } else if constexpr (std::same_as<To, float6_e2m3_t> && std::same_as<From, float32_t>) {
    return detail::dispatch::to_f6e2m3(value, c);
  } else if constexpr (std::same_as<To, float6_e3m2_t> && std::same_as<From, float32_t>) {
    return detail::dispatch::to_f6e3m2(value, c);
  } else if constexpr (std::same_as<To, float4_e2m1_t> && std::same_as<From, float32_t>) {
    return detail::dispatch::to_f4e2m1(value, c);
  } else if constexpr (std::same_as<To, ufloat8_e8m0_t> && std::same_as<From, float32_t>) {
    return detail::dispatch::to_ue8m0(value, c);
  } else if constexpr (std::same_as<To, ufloat7_e4m3_t> && std::same_as<From, float32_t>) {
    return detail::dispatch::to_ue4m3(value, c);
  } else if constexpr (std::same_as<To, std::int32_t> && std::same_as<From, float32_t>) {
    return detail::dispatch::f32_to_i32(value, c);
  } else if constexpr (std::same_as<To, std::uint32_t> && std::same_as<From, float32_t>) {
    return detail::dispatch::f32_to_u32(value, c);
  } else if constexpr (std::same_as<To, std::int64_t> && std::same_as<From, float32_t>) {
    return detail::dispatch::f32_to_i64(value, c);
  } else if constexpr (std::same_as<To, std::uint64_t> && std::same_as<From, float32_t>) {
    return detail::dispatch::f32_to_u64(value, c);
  } else if constexpr (std::same_as<To, std::int32_t> && std::same_as<From, float64_t>) {
    return detail::dispatch::f64_to_i32(value, c);
  } else if constexpr (std::same_as<To, std::uint32_t> && std::same_as<From, float64_t>) {
    return detail::dispatch::f64_to_u32(value, c);
  } else if constexpr (std::same_as<To, std::int64_t> && std::same_as<From, float64_t>) {
    return detail::dispatch::f64_to_i64(value, c);
  } else if constexpr (std::same_as<To, std::uint64_t> && std::same_as<From, float64_t>) {
    return detail::dispatch::f64_to_u64(value, c);
  } else if constexpr (scalar_float<From> && scalar_float<To> &&
                       !std::same_as<From, float64_t> &&
                       !std::same_as<To, float64_t> &&
                       !std::same_as<From, tfloat32_t> &&
                       !std::same_as<To, tfloat32_t>) {
    auto widened = cvt<float32_t>(ctx, value, c);
    if (!widened)
      return std::unexpected(widened.error());
    auto narrowed = cvt<To>(ctx, widened->value, c);
    if (!narrowed)
      return std::unexpected(narrowed.error());
    narrowed->status.inexact |= widened->status.inexact;
    return narrowed;
  } else if constexpr (scalar_float<From> && arithmetic_integer<To> &&
                       !std::same_as<From, float64_t>) {
    auto widened = cvt<float32_t>(ctx, value, c);
    if (!widened)
      return std::unexpected(widened.error());
    auto converted = cvt<To>(ctx, widened->value, c);
    if (!converted)
      return std::unexpected(converted.error());
    converted->status.inexact |= widened->status.inexact;
    return converted;
  } else {
    return std::unexpected(arithmetic_error::unsupported_type_combination);
  }
}
}  // namespace ptxsim::arith
