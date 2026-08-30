#pragma once
#include <bit>
#include <expected>
#include <limits>
#include <ptxsim/arith/concepts.hpp>
#include <ptxsim/arith/context.hpp>
#include <ptxsim/arith/controls.hpp>
#include <ptxsim/arith/detail/dispatch.hpp>
#include <ptxsim/arith/detail/canonical_conversion.hpp>
#include <ptxsim/arith/detail/format_traits.hpp>
#include <ptxsim/arith/error.hpp>
#include <ptxsim/arith/result.hpp>

namespace ptxsim::arith {
namespace detail {
constexpr bool supported_approximation_profile(const context& ctx) {
  const auto& p = ctx.profile().approximation;
  return p.revision == ptx_numeric_revision::v9_3 &&
         p.model == approximation_model::ptx_9_3_reference &&
         p.provenance == approximation_provenance::model_dependent_reference;
}
template <typename T>
constexpr bool special_subnormal_valid(special_function_control c) {
  (void)c;
  return true;
}
template <scalar_operation Op, typename T>
constexpr std::expected<void, arithmetic_error> validate_special_control(
    special_function_control control) {
  using Capability = special_function_operation_capability<Op, T>;
  if (!Capability::supported)
    return std::unexpected(arithmetic_error::unsupported_operation);
  if (!Capability::supports(control.approximation))
    return std::unexpected(arithmetic_error::unsupported_approximation_mode);
  if (!Capability::supports(control.subnormal))
    return std::unexpected(arithmetic_error::unsupported_subnormal_mode);
  if (!Capability::supports(control))
    return std::unexpected(arithmetic_error::unsupported_subnormal_mode);
  return {};
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
  requires(std::same_as<Result, void> || std::same_as<Result, T>)
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
  requires(std::same_as<Result, void> || std::same_as<Result, T>)
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
    if constexpr (std::same_as<R, std::int32_t> &&
                  std::same_as<T, std::int32_t>) {
      if (control.part == product_part::high &&
          control.overflow == integer_overflow_mode::saturate) {
        using U = detail::unsigned_integer_t<T>;
        using W = detail::integer_wide_t<T>;
        using UW = std::make_unsigned_t<W>;
        constexpr unsigned n = std::numeric_limits<U>::digits;
        const auto product = static_cast<UW>(W(a) * W(b));
        const auto high = detail::from_bits<T>(U(product >> n));
        return detail::saturate_from<T>(W(high) + W(c));
      }
    }
    auto p = mul<Result>(ctx, a, b, control);
    if (!p)
      return std::unexpected(p.error());
    auto sum = add(ctx, p->value, c, {control.overflow});
    if (!sum)
      return std::unexpected(sum.error());
    // Compound integer instructions report range loss from every stage.
    sum->status.carry |= p->status.carry;
    sum->status.borrow |= p->status.borrow;
    sum->status.overflow |= p->status.overflow;
    return sum;
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
             (std::same_as<Result, void> || std::same_as<Result, T>) &&    \
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
  requires(std::same_as<Result, void> || std::same_as<Result, T>) &&
          operation_capability<scalar_operation::fma, T, T, T, T>::value
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
  requires(std::same_as<Result, void> || std::same_as<Result, T>) &&
          operation_capability<scalar_operation::mad, T, T, T, T>::value
inline std::expected<result<T, floating_status>, arithmetic_error> mad(
    const context& ctx, T a, T b, T c, floating_control control = {}) {
  // The reference profile defines floating mad as fused.  Legacy separate
  // multiply/add behaviour belongs to an explicit legacy backend, not this
  // public PTX 9.3 path.
  return fma(ctx, a, b, c, control);
}
template <typename T>
  requires operation_capability<scalar_operation::sqrt, T, T>::value
inline std::expected<result<T, floating_status>, arithmetic_error> sqrt(
    const context&, T a, floating_control c = {}) {
  return detail::dispatch::sqrt(a, c);
}
template <typename T>
  requires floating_operation_control_capability<scalar_operation::rcp,
                                                 T>::supported
inline std::expected<result<T, floating_status>, arithmetic_error> rcp(
    const context&, T a, floating_control c = {}) {
  return detail::dispatch::rcp(a, c);
}
template <typename T>
  requires floating_operation_control_capability<scalar_operation::abs,
                                                 T>::supported
inline std::expected<result<T, floating_status>, arithmetic_error> abs(
    const context&, T a, floating_control c = {}) {
  return detail::dispatch::abs(a, c);
}
template <typename T>
  requires floating_operation_control_capability<scalar_operation::neg,
                                                 T>::supported
inline std::expected<result<T, floating_status>, arithmetic_error> neg(
    const context&, T a, floating_control c = {}) {
  return detail::dispatch::neg(a, c);
}
template <typename T>
  requires floating_operation_control_capability<scalar_operation::min,
                                                 T>::supported
inline std::expected<result<T, floating_status>, arithmetic_error> min(
    const context&, T a, T b, floating_control c = {}) {
  return detail::dispatch::min(a, b, c);
}
template <typename T>
  requires floating_operation_control_capability<scalar_operation::max,
                                                 T>::supported
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
  if (auto valid = detail::validate_special_control<scalar_operation::div,
                                                    float32_t>(c);
      !valid)
    return std::unexpected(valid.error());
  if (c.approximation == approximation_mode::exact)
    return div(ctx, a, b, floating_control{.subnormal = c.subnormal});
  if (!detail::supported_approximation_profile(ctx))
    return std::unexpected(arithmetic_error::unsupported_approximation_mode);
  return c.approximation == approximation_mode::ptx_approximate
             ? detail::dispatch::div_approx(a, b, c, ctx.profile().approximation)
             : detail::dispatch::div_full(a, b, c, ctx.profile().approximation);
}
template <typename T>
  requires(std::same_as<T, float32_t> || std::same_as<T, float64_t>)
inline std::expected<result<T, floating_status>, arithmetic_error> sqrt(
    const context& ctx, T a, special_function_control c) {
  if (auto valid = detail::validate_special_control<scalar_operation::sqrt,
                                                    T>(c);
      !valid)
    return std::unexpected(valid.error());
  if (c.approximation == approximation_mode::exact)
    return sqrt(ctx, a, floating_control{.subnormal = c.subnormal});
  if (!detail::supported_approximation_profile(ctx))
    return std::unexpected(arithmetic_error::unsupported_approximation_mode);
  if constexpr (std::same_as<T, float32_t>)
    return detail::dispatch::sqrt_approx(a, c, ctx.profile().approximation);
  else
    return std::unexpected(arithmetic_error::unsupported_operation);
}
template <typename T>
  requires(std::same_as<T, float32_t> || std::same_as<T, float64_t>)
inline std::expected<result<T, floating_status>, arithmetic_error> rcp(
    const context& ctx, T a, special_function_control c) {
  if (auto valid = detail::validate_special_control<scalar_operation::rcp,
                                                    T>(c);
      !valid)
    return std::unexpected(valid.error());
  if (c.approximation == approximation_mode::exact)
    return rcp(ctx, a, floating_control{.subnormal = c.subnormal});
  if (!detail::supported_approximation_profile(ctx))
    return std::unexpected(arithmetic_error::unsupported_approximation_mode);
  if constexpr (std::same_as<T, float32_t>)
    return detail::dispatch::rcp_approx(a, c, ctx.profile().approximation);
  else
    return detail::dispatch::rcp_approx_ftz(a, c, ctx.profile().approximation);
}
template <typename T>
  requires(std::same_as<T, float32_t> || std::same_as<T, float64_t>)
inline std::expected<result<T, floating_status>, arithmetic_error> rsqrt(
    const context& ctx, T a, special_function_control c = {}) {
  if (auto valid = detail::validate_special_control<scalar_operation::rsqrt,
                                                    T>(c);
      !valid)
    return std::unexpected(valid.error());
  if (!detail::supported_approximation_profile(ctx))
    return std::unexpected(arithmetic_error::unsupported_approximation_mode);
  if constexpr (std::same_as<T, float32_t>)
    return detail::dispatch::rsqrt_approx(a, c, ctx.profile().approximation);
  else if (c.subnormal == subnormal_mode::preserve)
    return detail::dispatch::rsqrt_approx(a, c, ctx.profile().approximation);
  else
    return detail::dispatch::rsqrt_approx_ftz(a, c,
                                              ctx.profile().approximation);
}
#define PTXSIM_ARITH_APPROX_UNARY(name, operation)                        \
  inline std::expected<result<float32_t, floating_status>, arithmetic_error> \
  name(const context& ctx, float32_t a, special_function_control c = {}) { \
    if (auto valid = detail::validate_special_control<scalar_operation::operation, \
                                                      float32_t>(c); !valid) \
      return std::unexpected(valid.error());                                \
    if (!detail::supported_approximation_profile(ctx))                      \
      return std::unexpected(arithmetic_error::unsupported_approximation_mode); \
    return detail::dispatch::name##_approx(a, c, ctx.profile().approximation); \
  }
PTXSIM_ARITH_APPROX_UNARY(sin, sin)
PTXSIM_ARITH_APPROX_UNARY(cos, cos)
PTXSIM_ARITH_APPROX_UNARY(lg2, lg2)
PTXSIM_ARITH_APPROX_UNARY(ex2, ex2)
#undef PTXSIM_ARITH_APPROX_UNARY
inline std::expected<result<float32_t, floating_status>, arithmetic_error> tanh(
    const context& ctx, float32_t a, special_function_control c = {}) {
  if (auto valid = detail::validate_special_control<scalar_operation::tanh,
                                                    float32_t>(c);
      !valid)
    return std::unexpected(valid.error());
  if (!detail::supported_approximation_profile(ctx))
    return std::unexpected(arithmetic_error::unsupported_approximation_mode);
  return detail::dispatch::tanh_approx(a, c, ctx.profile().approximation);
}
template <typename T>
  requires(std::same_as<T, float16_t> || std::same_as<T, bfloat16_t>)
inline std::expected<result<T, floating_status>, arithmetic_error> tanh(
    const context& ctx, T a, special_function_control c = {}) {
  if (auto valid = detail::validate_special_control<scalar_operation::tanh,
                                                    T>(c);
      !valid)
    return std::unexpected(valid.error());
  if (!detail::supported_approximation_profile(ctx))
    return std::unexpected(arithmetic_error::unsupported_approximation_mode);
  return detail::dispatch::tanh_approx(a, c, ctx.profile().approximation);
}
template <typename T>
  requires(std::same_as<T, float16_t> || std::same_as<T, bfloat16_t>)
inline std::expected<result<T, floating_status>, arithmetic_error> ex2(
    const context& ctx, T a, special_function_control c = {}) {
  if (auto valid = detail::validate_special_control<scalar_operation::ex2,
                                                    T>(c);
      !valid)
    return std::unexpected(valid.error());
  if (!detail::supported_approximation_profile(ctx))
    return std::unexpected(arithmetic_error::unsupported_approximation_mode);
  if (c.subnormal == subnormal_mode::flush_input ||
      c.subnormal == subnormal_mode::flush_input_and_output)
    a = flush_subnormal(a);
  return detail::dispatch::ex2_approx(a, c, ctx.profile().approximation);
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
namespace detail {
template <typename To>
inline constexpr bool signed_low_conversion_target =
    std::same_as<To, float8_e4m3_t> ||
    std::same_as<To, float8_e5m2_t> ||
    std::same_as<To, float6_e2m3_t> ||
    std::same_as<To, float6_e3m2_t> ||
    std::same_as<To, float4_e2m1_t>;
template <typename To>
inline constexpr bool relu_conversion_target =
    std::same_as<To, float16_t> || std::same_as<To, bfloat16_t> ||
    std::same_as<To, tfloat32_t> || std::same_as<To, fixed8_s2f6_t> ||
    signed_low_conversion_target<To>;
template <typename To>
inline constexpr bool unit_interval_conversion_target =
    std::same_as<To, float16_t> || std::same_as<To, float32_t> ||
    std::same_as<To, float64_t>;
template <typename To>
inline constexpr bool finite_conversion_target =
    std::same_as<To, float16_t> || std::same_as<To, bfloat16_t> ||
    std::same_as<To, tfloat32_t> || std::same_as<To, fixed8_s2f6_t> ||
    std::same_as<To, ufloat8_e8m0_t> || signed_low_conversion_target<To>;

template <typename To>
constexpr std::expected<void, arithmetic_error> validate_destination_control(
    conversion_control c) {
  if (c.activation != activation_mode::none && !relu_conversion_target<To>)
    return std::unexpected(arithmetic_error::unsupported_activation);
  if (c.saturation == saturation_mode::none)
    return {};
  if (c.saturation == saturation_mode::type_range &&
      (arithmetic_integer<To> || std::same_as<To, fixed8_s2f6_t>))
    return {};
  if (c.saturation == saturation_mode::finite && finite_conversion_target<To>)
    return {};
  if (c.saturation == saturation_mode::zero_to_one &&
      unit_interval_conversion_target<To>)
    return {};
  return std::unexpected(arithmetic_error::unsupported_saturation);
}

template <typename To>
constexpr void apply_destination_controls(canonical::number& value,
                                          conversion_control c) {
  using canonical::number_class;
  const auto positive_zero = [&] { value = {}; };
  if (c.saturation == saturation_mode::zero_to_one) {
    if (value.classification == number_class::nan || value.negative) {
      positive_zero();
    } else if (value.classification == number_class::infinity ||
               (value.classification == number_class::finite &&
                value.exponent + static_cast<int>(canonical::bit_length(value.significand)) -
                        1 >=
                    0)) {
      value = {.classification = number_class::finite, .significand = 1};
    }
  }
  if constexpr (std::same_as<To, fixed8_s2f6_t>) {
    if (c.saturation == saturation_mode::finite &&
        (value.classification == number_class::nan ||
         value.classification == number_class::infinity)) {
      // S2F6 has no non-finite encoding.  Its finite form saturates to the
      // signed fixed endpoint; NaN is deliberately canonicalized positive.
      value = {.classification = number_class::finite,
               .negative = value.classification == number_class::infinity &&
                           value.negative,
               .significand = value.classification == number_class::infinity &&
                                      value.negative
                                  ? std::uint64_t{128}
                                  : std::uint64_t{127},
               .exponent = -fixed8_s2f6_t::fraction_bits};
    }
  }
  if (c.activation == activation_mode::relu && value.negative &&
      value.classification != number_class::nan)
    positive_zero();
}

template <typename To, typename From>
std::expected<result<To, floating_status>, arithmetic_error> canonical_cvt(
    const context& ctx, From value, conversion_control control,
    bool has_stochastic_input, std::uint32_t stochastic_bits = 0) {
  if (control.rounding == rounding_mode::stochastic) {
    if (!has_stochastic_input)
      return std::unexpected(arithmetic_error::invalid_stochastic_input);
    if constexpr (!conversion_control_capability<
                      To, From, conversion_control_feature::stochastic>::value)
      return std::unexpected(arithmetic_error::unsupported_rounding);
  } else if (has_stochastic_input) {
    return std::unexpected(arithmetic_error::invalid_stochastic_input);
  } else if (control.rounding == rounding_mode::nearest_away) {
    if constexpr (!conversion_control_capability<
                      To, From,
                      conversion_control_feature::nearest_away>::value)
      return std::unexpected(arithmetic_error::unsupported_rounding);
  }
  if constexpr (arithmetic_integer<To> && arithmetic_integer<From>)
    if (control.rounding != rounding_mode::nearest_even)
      return std::unexpected(arithmetic_error::unsupported_rounding);
  if (auto valid = validate_destination_control<To>(control); !valid)
    return std::unexpected(valid.error());
  if (control.source_subnormal == subnormal_mode::flush_output ||
      control.source_subnormal == subnormal_mode::flush_input_and_output)
    return std::unexpected(arithmetic_error::unsupported_subnormal_mode);
  if (control.destination_subnormal == subnormal_mode::flush_input ||
      control.destination_subnormal == subnormal_mode::flush_input_and_output)
    return std::unexpected(arithmetic_error::unsupported_subnormal_mode);
  if constexpr (!(scalar_float<From> || std::same_as<From, tfloat32_t>))
    if (control.source_subnormal != subnormal_mode::preserve)
      return std::unexpected(arithmetic_error::unsupported_subnormal_mode);
  if constexpr (!(scalar_float<To> || std::same_as<To, tfloat32_t>))
    if (control.destination_subnormal != subnormal_mode::preserve)
      return std::unexpected(arithmetic_error::unsupported_subnormal_mode);

  auto decoded = canonical::decode(value);
  if constexpr (FloatingFormat<From>) {
    if ((control.source_subnormal == subnormal_mode::flush_input ||
         control.source_subnormal == subnormal_mode::flush_input_and_output) &&
        is_subnormal(value)) {
      decoded = {};
      decoded.negative = is_negative(value);
    }
  }
  apply_destination_controls<To>(decoded, control);
  auto encoded = canonical::encode<To>(decoded, ctx, control, stochastic_bits);
  if (!encoded)
    return std::unexpected(encoded.error());
  if constexpr (FloatingFormat<To>) {
    if ((control.destination_subnormal == subnormal_mode::flush_output ||
         control.destination_subnormal == subnormal_mode::flush_input_and_output) &&
        is_subnormal(encoded->value))
      encoded->value = flush_subnormal(encoded->value);
  }
  return encoded;
}
}  // namespace detail

// Generic conversion façade: source and target are never selected as a pair.
// Format-specific work lives exclusively in canonical::decode and ::encode.
template <typename To, typename From>
  requires convertible_to<To, From>
inline std::expected<result<To, floating_status>, arithmetic_error> cvt(
    const context& ctx, From value, conversion_control control = {}) {
  return detail::canonical_cvt<To>(ctx, value, control, false);
}

// `.rs` carries exactly one explicit 32-bit replay threshold.  No global PRNG
// state is read or advanced by conversion.
template <typename To, typename From>
  requires convertible_to<To, From>
inline std::expected<result<To, floating_status>, arithmetic_error> cvt(
    const context& ctx, From value, conversion_control c,
    stochastic_rounding_input random) {
  return detail::canonical_cvt<To>(ctx, value, c, true, random.random_bits);
}
}  // namespace ptxsim::arith
