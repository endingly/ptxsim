#pragma once

#include <ptxsim/arith/scalar.hpp>

#include <array>
#include <span>

namespace ptxsim::arith {

enum class accumulation_order { k_ascending };
enum class accumulation_precision { format_defined };
enum class tensor_nan_mode { profile_default };
struct tensor_control {
  rounding_mode accumulator_rounding = rounding_mode::nearest_even;
  subnormal_mode product_subnormal = subnormal_mode::preserve;
  subnormal_mode accumulator_subnormal = subnormal_mode::preserve;
  accumulation_order order = accumulation_order::k_ascending;
  accumulation_precision precision = accumulation_precision::format_defined;
  saturation_mode saturation = saturation_mode::none;
  tensor_nan_mode nan = tensor_nan_mode::profile_default;
};

template <typename D, typename A, typename B, typename C>
struct mma_capability : std::false_type {};

template <typename D, typename A, typename B, typename C>
inline constexpr bool mma_capability_v = mma_capability<D, A, B, C>::value;

template <typename D, typename A, typename B, typename C>
  requires((std::same_as<D, float32_t> && std::same_as<C, float32_t> &&
            std::same_as<A, B> &&
            (std::same_as<A, float16_t> || std::same_as<A, bfloat16_t> ||
             std::same_as<A, float32_t> || std::same_as<A, tfloat32_t> ||
             std::same_as<A, float8_e4m3_t> || std::same_as<A, float8_e5m2_t> ||
             std::same_as<A, float6_e2m3_t> || std::same_as<A, float6_e3m2_t> ||
             std::same_as<A, float4_e2m1_t>)) ||
           (std::same_as<D, float16_t> && std::same_as<C, float16_t> &&
            std::same_as<A, float16_t> && std::same_as<B, float16_t>) ||
           (std::same_as<D, float64_t> && std::same_as<C, float64_t> &&
            std::same_as<A, float64_t> && std::same_as<B, float64_t>) ||
           (arithmetic_integer<D> && std::same_as<D, C> && std::same_as<D, A> &&
            std::same_as<D, B>))
struct mma_capability<D, A, B, C> : std::true_type {};

namespace tensor {

template <std::size_t Rows, std::size_t Cols, typename T>
struct tile {
  std::array<T, Rows * Cols> values{};
  constexpr T& operator()(std::size_t r, std::size_t c) {
    return values[r * Cols + c];
  }
  constexpr const T& operator()(std::size_t r, std::size_t c) const {
    return values[r * Cols + c];
  }
};

template <typename T>
struct matrix_view {
  T* data{};
  std::size_t rows{};
  std::size_t cols{};
  std::size_t stride{};
  constexpr T& operator()(std::size_t row, std::size_t col) const {
    return data[row * stride + col];
  }
};

template <typename Scale>
struct block_scale_view {
  // Groups are contiguous row-major logical elements; exact group coverage is
  // checked against each A/B tile before execution.
  std::span<const Scale> values{};
  std::size_t group_elements = 1;
  constexpr block_scale_view() = default;
  constexpr block_scale_view(std::span<const Scale> input,
                             std::size_t group = 1)
      : values(input), group_elements(group) {}
  template <std::size_t Count>
  constexpr block_scale_view(const std::array<Scale, Count>& input,
                             std::size_t group = 1)
      : values(input), group_elements(group) {}
  [[nodiscard]] constexpr Scale at(std::size_t index) const {
    return values[index / group_elements];
  }
};
template <typename Scale, std::size_t Count>
block_scale_view(const std::array<Scale, Count>&, std::size_t = 1)
    -> block_scale_view<Scale>;

namespace detail {
inline constexpr bool valid_tensor_control(const tensor_control& c) {
  return c.accumulator_rounding != rounding_mode::nearest_away &&
         c.accumulator_rounding != rounding_mode::stochastic &&
         c.product_subnormal == subnormal_mode::preserve &&
         c.accumulator_subnormal == subnormal_mode::preserve &&
         c.order == accumulation_order::k_ascending &&
         c.precision == accumulation_precision::format_defined &&
         c.saturation == saturation_mode::none &&
         c.nan == tensor_nan_mode::profile_default;
}
inline constexpr arithmetic_error tensor_control_error(
    const tensor_control& c) {
  if (c.accumulator_rounding == rounding_mode::nearest_away ||
      c.accumulator_rounding == rounding_mode::stochastic)
    return arithmetic_error::unsupported_rounding;
  if (c.product_subnormal != subnormal_mode::preserve ||
      c.accumulator_subnormal != subnormal_mode::preserve)
    return arithmetic_error::unsupported_subnormal_mode;
  if (c.saturation != saturation_mode::none)
    return arithmetic_error::unsupported_saturation;
  return arithmetic_error::unsupported_operation;
}
template <typename Scale>
inline constexpr bool supported_scale =
    std::same_as<Scale, ufloat8_e8m0_t> || std::same_as<Scale, ufloat7_e4m3_t>;
template <typename Scale>
bool valid_scale(const block_scale_view<Scale>& scale, std::size_t elements) {
  return supported_scale<Scale> && scale.group_elements != 0 &&
         scale.values.size() ==
             (elements + scale.group_elements - 1) / scale.group_elements;
}

template <typename D, typename A, typename B>
std::expected<result<D, tensor_status>, arithmetic_error> mac(
    const context& ctx, D accumulator, A a, B b,
    const tensor_control& control) {
  if constexpr (arithmetic_integer<D>) {
    auto product = mul(ctx, a, b);
    if (!product)
      return std::unexpected(product.error());
    auto sum = add(ctx, accumulator, product->value);
    if (!sum)
      return std::unexpected(sum.error());
    return {{sum->value,
             {false, product->status.overflow || sum->status.overflow}}};
  } else {
    auto lhs = cvt<D>(ctx, a), rhs = cvt<D>(ctx, b);
    if (!lhs || !rhs)
      return std::unexpected(arithmetic_error::unsupported_type_combination);
    auto sum = fma(ctx, lhs->value, rhs->value, accumulator,
                   {.rounding = control.accumulator_rounding});
    if (!sum)
      return std::unexpected(sum.error());
    return {{sum->value,
             {false, sum->status.inexact || lhs->status.inexact ||
                         rhs->status.inexact}}};
  }
}

template <typename A, typename B, typename ScaleA, typename ScaleB>
std::expected<result<float32_t, tensor_status>, arithmetic_error> scaled_mac(
    const context& ctx, float32_t accumulator, A a, B b, ScaleA scale_a,
    ScaleB scale_b, const tensor_control& control) {
  auto lhs = cvt<float32_t>(ctx, a), rhs = cvt<float32_t>(ctx, b);
  auto lhs_scale = cvt<float32_t>(ctx, scale_a),
       rhs_scale = cvt<float32_t>(ctx, scale_b);
  if (!lhs || !rhs || !lhs_scale || !rhs_scale)
    return std::unexpected(arithmetic_error::unsupported_type_combination);
  // FP8/FP6/FP4 operands and the supported UE8M0/UE4M3 scales together
  // carry at most eight binary significand bits; F32 therefore represents
  // each operand×scale product exactly before the accumulator FMA.
  auto scaled_lhs = mul(ctx, lhs->value, lhs_scale->value);
  auto scaled_rhs = mul(ctx, rhs->value, rhs_scale->value);
  if (!scaled_lhs || !scaled_rhs)
    return std::unexpected(arithmetic_error::unsupported_type_combination);
  auto sum = fma(ctx, scaled_lhs->value, scaled_rhs->value, accumulator,
                 {.rounding = control.accumulator_rounding});
  if (!sum)
    return std::unexpected(sum.error());
  return {{sum->value,
           {false, sum->status.inexact || scaled_lhs->status.inexact ||
                       scaled_rhs->status.inexact}}};
}
}  // namespace detail

template <typename D, typename A, typename B, typename C, std::size_t M,
          std::size_t N, std::size_t K>
std::expected<result<tile<M, N, D>, tensor_status>, arithmetic_error> mma(
    const context& ctx, const tile<M, K, A>& a, const tile<K, N, B>& b,
    const tile<M, N, C>& c, const tensor_control& control = {}) {
  if constexpr (!mma_capability_v<D, A, B, C>)
    return std::unexpected(arithmetic_error::unsupported_type_combination);
  if (!detail::valid_tensor_control(control))
    return std::unexpected(detail::tensor_control_error(control));
  tile<M, N, D> out{};
  tensor_status status{
      !arithmetic_integer<D> && ctx.profile().tensor.model_dependent, false};
  for (std::size_t row = 0; row != M; ++row)
    for (std::size_t col = 0; col != N; ++col) {
      D value = c(row, col);
      for (std::size_t k = 0; k != K; ++k) {
        auto next = detail::mac(ctx, value, a(row, k), b(k, col), control);
        if (!next)
          return std::unexpected(next.error());
        value = next->value;
        status.inexact |= next->status.inexact;
      }
      out(row, col) = value;
    }
  return {{out, status}};
}

template <typename D, typename A, typename B, typename C, std::size_t M,
          std::size_t N, std::size_t K, typename ScaleA, typename ScaleB>
std::expected<result<tile<M, N, D>, tensor_status>, arithmetic_error> mma(
    const context& ctx, const tile<M, K, A>& a, const tile<K, N, B>& b,
    const tile<M, N, C>& c, const tensor_control& control,
    const block_scale_view<ScaleA>& scales_a,
    const block_scale_view<ScaleB>& scales_b) {
  if constexpr (!mma_capability_v<D, A, B, C> || !std::same_as<D, float32_t>)
    return std::unexpected(arithmetic_error::unsupported_type_combination);
  if (!detail::valid_tensor_control(control))
    return std::unexpected(detail::tensor_control_error(control));
  if (!detail::valid_scale(scales_a, M * K) ||
      !detail::valid_scale(scales_b, K * N))
    return std::unexpected(arithmetic_error::invalid_scale_layout);
  tile<M, N, D> out{};
  tensor_status status{
      !arithmetic_integer<D> && ctx.profile().tensor.model_dependent, false};
  for (std::size_t row = 0; row != M; ++row)
    for (std::size_t col = 0; col != N; ++col) {
      D value = c(row, col);
      for (std::size_t k = 0; k != K; ++k) {
        auto next = detail::scaled_mac(ctx, value, a(row, k), b(k, col),
                                       scales_a.at(row * K + k),
                                       scales_b.at(k * N + col), control);
        if (!next)
          return std::unexpected(next.error());
        value = next->value;
        status.inexact |= next->status.inexact;
      }
      out(row, col) = value;
    }
  return {{out, status}};
}

template <typename D, typename A, typename B, typename C>
std::expected<tensor_status, arithmetic_error> mma(
    const context& ctx, matrix_view<const A> a, matrix_view<const B> b,
    matrix_view<const C> c, matrix_view<D> d,
    const tensor_control& control = {}) {
  if constexpr (!mma_capability_v<D, A, B, C>)
    return std::unexpected(arithmetic_error::unsupported_type_combination);
  if (!detail::valid_tensor_control(control))
    return std::unexpected(detail::tensor_control_error(control));
  const auto needs_data = [](const auto& view) {
    return view.rows != 0 && view.cols != 0;
  };
  if (a.cols != b.rows || a.rows != c.rows || b.cols != c.cols ||
      d.rows != c.rows || d.cols != c.cols || a.stride < a.cols ||
      b.stride < b.cols || c.stride < c.cols || d.stride < d.cols ||
      (needs_data(a) && a.data == nullptr) ||
      (needs_data(b) && b.data == nullptr) ||
      (needs_data(c) && c.data == nullptr) ||
      (needs_data(d) && d.data == nullptr))
    return std::unexpected(arithmetic_error::invalid_tensor_shape);
  tensor_status status{
      !arithmetic_integer<D> && ctx.profile().tensor.model_dependent, false};
  for (std::size_t row = 0; row != d.rows; ++row)
    for (std::size_t col = 0; col != d.cols; ++col) {
      D value = c(row, col);
      for (std::size_t k = 0; k != a.cols; ++k) {
        auto next = detail::mac(ctx, value, a(row, k), b(k, col), control);
        if (!next)
          return std::unexpected(next.error());
        value = next->value;
        status.inexact |= next->status.inexact;
      }
      d(row, col) = value;
    }
  return status;
}

}  // namespace tensor
}  // namespace ptxsim::arith
