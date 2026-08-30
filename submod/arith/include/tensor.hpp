#pragma once

#include <ptxsim/arith/detail/canonical_conversion.hpp>
#include <ptxsim/arith/scalar.hpp>

#include <array>
#include <bit>
#include <limits>
#include <span>
#include <tuple>
#include <type_traits>

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

// Numeric scale models, deliberately independent of instruction spellings.
enum class tensor_scale_model { none, one_x, two_x, four_x };

namespace detail {
template <typename T>
inline constexpr bool low_tensor_operand =
    std::same_as<T, float8_e4m3_t> || std::same_as<T, float8_e5m2_t> ||
    std::same_as<T, float6_e2m3_t> || std::same_as<T, float6_e3m2_t> ||
    std::same_as<T, float4_e2m1_t>;
template <typename T>
inline constexpr bool e2m1_tensor_operand = std::same_as<T, float4_e2m1_t>;
template <typename T>
inline constexpr bool int8_tensor_operand = std::same_as<T, uint8_t> ||
                                             std::same_as<T, int8_t>;
}  // namespace detail

// The tensor data combination facts feed the central operation capability
// below.  Sub-byte/b1 integer forms stay unavailable until they have strong
// storage types and their own controls.
namespace detail {
template <typename D, typename A, typename B, typename C,
          tensor_scale_model ScaleModel>
inline constexpr bool tensor_combination_capability =
          (ScaleModel == tensor_scale_model::none &&
           ((std::same_as<D, float16_t> && std::same_as<C, float16_t> &&
             std::same_as<A, float16_t> && std::same_as<B, float16_t>) ||
            (std::same_as<D, float32_t> && std::same_as<C, float32_t> &&
             ((std::same_as<A, float16_t> && std::same_as<B, float16_t>) ||
              (std::same_as<A, bfloat16_t> && std::same_as<B, bfloat16_t>) ||
              (std::same_as<A, tfloat32_t> && std::same_as<B, tfloat32_t>))) ||
            ((std::same_as<D, float16_t> || std::same_as<D, float32_t>) &&
             std::same_as<C, D> && detail::low_tensor_operand<A> &&
             detail::low_tensor_operand<B>) ||
            (std::same_as<D, float64_t> && std::same_as<C, float64_t> &&
             std::same_as<A, float64_t> && std::same_as<B, float64_t>) ||
            (std::same_as<D, int32_t> && std::same_as<C, int32_t> &&
             detail::int8_tensor_operand<A> &&
             detail::int8_tensor_operand<B>))) ||
          (ScaleModel != tensor_scale_model::none &&
           std::same_as<D, float32_t> && std::same_as<C, float32_t> &&
           detail::low_tensor_operand<A> && detail::low_tensor_operand<B> &&
           ((ScaleModel == tensor_scale_model::one_x) ||
            (detail::e2m1_tensor_operand<A> &&
             detail::e2m1_tensor_operand<B> &&
             (ScaleModel == tensor_scale_model::two_x ||
              ScaleModel == tensor_scale_model::four_x))));
}  // namespace detail

template <tensor_scale_model ScaleModel>
struct tensor_scale_tag {};

template <typename D, typename A, typename B, typename C>
struct operation_capability<scalar_operation::mma, D, A, B, C>
    : std::bool_constant<detail::tensor_combination_capability<
          D, A, B, C, tensor_scale_model::none>> {};
template <typename D, typename A, typename B, typename C,
          tensor_scale_model ScaleModel>
struct operation_capability<scalar_operation::scaled_mma, D, A, B, C,
                            tensor_scale_tag<ScaleModel>>
    : std::bool_constant<
          detail::tensor_combination_capability<D, A, B, C, ScaleModel>> {};

template <typename D, typename A, typename B, typename C,
          tensor_scale_model ScaleModel = tensor_scale_model::none>
struct tensor_capability
    : std::conditional_t<
          ScaleModel == tensor_scale_model::none,
          operation_capability<scalar_operation::mma, D, A, B, C>,
          operation_capability<scalar_operation::scaled_mma, D, A, B, C,
                               tensor_scale_tag<ScaleModel>>> {};

template <typename D, typename A, typename B, typename C,
          tensor_scale_model ScaleModel = tensor_scale_model::none>
inline constexpr bool tensor_capability_v =
    tensor_capability<D, A, B, C, ScaleModel>::value;

template <typename D, typename A, typename B, typename C>
struct mma_capability
    : tensor_capability<D, A, B, C, tensor_scale_model::none> {};
template <typename D, typename A, typename B, typename C>
inline constexpr bool mma_capability_v = mma_capability<D, A, B, C>::value;

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

enum class scale_axis { row_chunks, column_chunks };
struct block_scale_layout {
  scale_axis axis{};
  std::size_t chunks{};
  std::size_t elements_per_chunk{};
  tensor_scale_model model = tensor_scale_model::one_x;
};

// A: M x chunks, indexed by (row, k-chunk).  B: chunks x N, indexed by
// (k-chunk, column); B is intentionally not a flattened row-major tile.
template <typename Scale>
struct block_scale_view {
  std::span<const Scale> values{};
  block_scale_layout layout{};
  constexpr block_scale_view() = default;
  constexpr block_scale_view(std::span<const Scale> input,
                             block_scale_layout descriptor)
      : values(input), layout(descriptor) {}
  template <std::size_t Count>
  constexpr block_scale_view(const std::array<Scale, Count>& input,
                             block_scale_layout descriptor)
      : values(input), layout(descriptor) {}
  [[nodiscard]] constexpr Scale at_a(std::size_t row, std::size_t k) const {
    return values[row * layout.chunks + k / layout.elements_per_chunk];
  }
  [[nodiscard]] constexpr Scale at_b(std::size_t k, std::size_t column,
                                     std::size_t columns) const {
    return values[(k / layout.elements_per_chunk) * columns + column];
  }
};
template <typename Scale, std::size_t Count>
block_scale_view(const std::array<Scale, Count>&, block_scale_layout)
    -> block_scale_view<Scale>;

namespace detail {

template <typename D>
constexpr std::expected<void, arithmetic_error> validate_tensor_control(
    const tensor_control& c) {
  if constexpr (std::same_as<D, int32_t>) {
    if (c.accumulator_rounding != rounding_mode::nearest_even)
      return std::unexpected(arithmetic_error::unsupported_rounding);
  } else if (!floating_operation_control_capability<
                 scalar_operation::fma, D>::supports(c.accumulator_rounding)) {
    return std::unexpected(arithmetic_error::unsupported_rounding);
  }
  if (c.product_subnormal != subnormal_mode::preserve ||
      c.accumulator_subnormal != subnormal_mode::preserve)
    return std::unexpected(arithmetic_error::unsupported_subnormal_mode);
  if constexpr (std::same_as<D, int32_t>) {
    if (c.saturation != saturation_mode::none &&
        c.saturation != saturation_mode::type_range)
      return std::unexpected(arithmetic_error::unsupported_saturation);
  } else if (c.saturation != saturation_mode::none) {
    return std::unexpected(arithmetic_error::unsupported_saturation);
  }
  if (c.order != accumulation_order::k_ascending ||
      c.precision != accumulation_precision::format_defined ||
      c.nan != tensor_nan_mode::profile_default)
    return std::unexpected(arithmetic_error::unsupported_operation);
  return {};
}

[[nodiscard]] constexpr bool supported_tensor_profile(const context& ctx) {
  const auto& p = ctx.profile().tensor;
  return p.revision == ptx_numeric_revision::v9_3 &&
         p.model == tensor_model::ptx_9_3_reference &&
         p.provenance == tensor_provenance::model_dependent_reference;
}

template <typename Scale, std::size_t Major, std::size_t Minor>
[[nodiscard]] constexpr bool valid_scale_layout(
    const block_scale_view<Scale>& scale, scale_axis expected_axis) {
  return scale.layout.axis == expected_axis &&
         scale.layout.elements_per_chunk != 0 &&
         Minor % scale.layout.elements_per_chunk == 0 &&
         scale.layout.chunks == Minor / scale.layout.elements_per_chunk &&
         scale.values.size() == Major * scale.layout.chunks;
}

template <typename A, typename B, typename ScaleA, typename ScaleB>
[[nodiscard]] constexpr bool valid_scale_model(
    const block_scale_view<ScaleA>& scales_a,
    const block_scale_view<ScaleB>& scales_b) {
  if constexpr (!std::same_as<ScaleA, ScaleB>)
    return false;
  const auto model = scales_a.layout.model;
  if (model != scales_b.layout.model)
    return false;
  if constexpr (std::same_as<ScaleA, ufloat8_e8m0_t>)
    return model == tensor_scale_model::one_x ||
           ((model == tensor_scale_model::two_x ||
             model == tensor_scale_model::four_x) &&
            ::ptxsim::arith::detail::e2m1_tensor_operand<A> &&
            ::ptxsim::arith::detail::e2m1_tensor_operand<B>);
  else if constexpr (std::same_as<ScaleA, ufloat7_e4m3_t>)
    return model == tensor_scale_model::four_x &&
           ::ptxsim::arith::detail::e2m1_tensor_operand<A> &&
           ::ptxsim::arith::detail::e2m1_tensor_operand<B>;
  else
    return false;
}

template <typename A, typename B, typename ScaleA, typename ScaleB,
          std::size_t M, std::size_t N, std::size_t K>
[[nodiscard]] constexpr bool valid_scales(
    const block_scale_view<ScaleA>& scales_a,
    const block_scale_view<ScaleB>& scales_b) {
  return valid_scale_layout<ScaleA, M, K>(scales_a, scale_axis::row_chunks) &&
         valid_scale_layout<ScaleB, N, K>(scales_b,
                                           scale_axis::column_chunks) &&
         valid_scale_model<A, B>(scales_a, scales_b);
}

template <typename D, typename A, typename B>
std::expected<result<D, tensor_status>, arithmetic_error> mac(
    const context& ctx, D accumulator, A a, B b,
    const tensor_control& control) {
  if constexpr (std::same_as<D, int32_t>) {
    const auto exact = std::int64_t{accumulator} +
                       std::int64_t{a} * std::int64_t{b};
    constexpr auto lower = std::int64_t{std::numeric_limits<int32_t>::min()};
    constexpr auto upper = std::int64_t{std::numeric_limits<int32_t>::max()};
    if (exact > upper || exact < lower) {
      if (control.saturation == saturation_mode::type_range)
        return {{exact > upper ? std::numeric_limits<int32_t>::max()
                               : std::numeric_limits<int32_t>::min(),
                 {false, false, true, true}}};
      return {{std::bit_cast<int32_t>(static_cast<std::uint32_t>(exact)),
               {false, false, true, false}}};
    }
    return {{static_cast<int32_t>(exact), {}}};
  } else {
    auto lhs = cvt<D>(ctx, a), rhs = cvt<D>(ctx, b);
    if (!lhs || !rhs)
      return std::unexpected(arithmetic_error::unsupported_type_combination);
    auto sum = fma(ctx, lhs->value, rhs->value, accumulator,
                   {.rounding = control.accumulator_rounding});
    if (!sum)
      return std::unexpected(sum.error());
    return {{sum->value, {false, sum->status.inexact || lhs->status.inexact ||
                                     rhs->status.inexact}}};
  }
}

[[nodiscard]] constexpr result<int32_t, tensor_status> finalize_int32_mma(
    __int128 exact, const tensor_control& control) {
  constexpr auto lower = static_cast<__int128>(std::numeric_limits<int32_t>::min());
  constexpr auto upper = static_cast<__int128>(std::numeric_limits<int32_t>::max());
  if (exact > upper || exact < lower) {
    if (control.saturation == saturation_mode::type_range)
      return {exact > upper ? std::numeric_limits<int32_t>::max()
                            : std::numeric_limits<int32_t>::min(),
              {.overflow = true, .saturated = true}};
    return {std::bit_cast<int32_t>(static_cast<std::uint32_t>(exact)),
            {.overflow = true}};
  }
  return {static_cast<int32_t>(exact), {}};
}

struct widened_product {
  ::ptxsim::arith::detail::canonical::number value{};
  bool invalid{};
};
template <typename... Values>
[[nodiscard]] constexpr widened_product exact_product(Values... values) {
  std::array<::ptxsim::arith::detail::canonical::number, sizeof...(Values)> parts{
      ::ptxsim::arith::detail::canonical::decode(values)...};
  ::ptxsim::arith::detail::canonical::number out{
      .classification = ::ptxsim::arith::detail::canonical::number_class::finite,
                        .significand = 1};
  bool has_zero = false, has_infinity = false;
  for (const auto& part : parts) {
    out.negative = out.negative != part.negative;
    if (part.classification == ::ptxsim::arith::detail::canonical::number_class::nan)
      return {{.classification = ::ptxsim::arith::detail::canonical::number_class::nan,
               .negative = part.negative, .signaling_nan = part.signaling_nan,
               .nan_payload = part.nan_payload, .nan_payload_bits = part.nan_payload_bits},
              part.signaling_nan};
    has_zero |= part.classification == ::ptxsim::arith::detail::canonical::number_class::zero;
    has_infinity |= part.classification == ::ptxsim::arith::detail::canonical::number_class::infinity;
  }
  if (has_zero && has_infinity)
    return {{.classification = ::ptxsim::arith::detail::canonical::number_class::nan}, true};
  if (has_infinity)
    return {{.classification = ::ptxsim::arith::detail::canonical::number_class::infinity,
             .negative = out.negative}, false};
  if (has_zero)
    return {{.classification = ::ptxsim::arith::detail::canonical::number_class::zero,
             .negative = out.negative}, false};
  for (const auto& part : parts) {
    out.significand *= part.significand;
    out.exponent += part.exponent;
  }
  return {out, false};
}

// The most distant exact scaled-product bit and F32 accumulator bit are less
// than 512 positions apart for the supported FP8/FP6/FP4 + two UE scale
// inputs (the extrema are below -286 and above +280).  768 bits leaves a
// deliberate margin without introducing a public dependency or host FP.
class exact_binary_sum {
 public:
  static constexpr unsigned k_bits = 768;
  static constexpr unsigned k_limbs = k_bits / 64;

  constexpr void set_shifted(std::uint64_t value, unsigned shift) {
    limbs_.fill(0);
    if (value == 0 || shift >= k_bits)
      return;
    const auto limb = shift / 64;
    const auto offset = shift % 64;
    limbs_[limb] = value << offset;
    if (offset != 0 && limb + 1 != k_limbs)
      limbs_[limb + 1] = value >> (64 - offset);
  }
  [[nodiscard]] constexpr bool is_zero() const {
    for (const auto limb : limbs_)
      if (limb != 0) return false;
    return true;
  }
  [[nodiscard]] constexpr int compare(const exact_binary_sum& other) const {
    for (unsigned i = k_limbs; i-- != 0;) {
      if (limbs_[i] < other.limbs_[i]) return -1;
      if (limbs_[i] > other.limbs_[i]) return 1;
    }
    return 0;
  }
  constexpr void add(const exact_binary_sum& other) {
    std::uint64_t carry = 0;
    for (unsigned i = 0; i != k_limbs; ++i) {
      const auto first = limbs_[i] + other.limbs_[i];
      const auto first_carry = first < limbs_[i];
      const auto total = first + carry;
      const auto second_carry = total < first;
      limbs_[i] = total;
      carry = first_carry || second_carry;
    }
  }
  // Requires *this >= other.
  constexpr void subtract(const exact_binary_sum& other) {
    std::uint64_t borrow = 0;
    for (unsigned i = 0; i != k_limbs; ++i) {
      const auto subtrahend = other.limbs_[i] + borrow;
      const auto wrapped = subtrahend < other.limbs_[i];
      const auto next_borrow = limbs_[i] < subtrahend || wrapped;
      limbs_[i] -= subtrahend;
      borrow = next_borrow;
    }
  }
  [[nodiscard]] constexpr unsigned bit_length() const {
    for (unsigned i = k_limbs; i-- != 0;)
      if (limbs_[i] != 0)
        return i * 64 + 64 - std::countl_zero(limbs_[i]);
    return 0;
  }
  [[nodiscard]] constexpr bool bit(unsigned index) const {
    return index < k_bits && ((limbs_[index / 64] >> (index % 64)) & 1u);
  }
  [[nodiscard]] constexpr bool any_low(unsigned count) const {
    if (count == 0) return false;
    if (count >= k_bits) return !is_zero();
    const auto full = count / 64;
    for (unsigned i = 0; i != full; ++i)
      if (limbs_[i] != 0) return true;
    const auto rest = count % 64;
    return rest != 0 && (limbs_[full] & ((std::uint64_t{1} << rest) - 1)) != 0;
  }
  [[nodiscard]] constexpr std::uint64_t right_u64(unsigned shift) const {
    if (shift >= k_bits) return 0;
    const auto limb = shift / 64;
    const auto offset = shift % 64;
    auto value = limbs_[limb] >> offset;
    if (offset != 0 && limb + 1 != k_limbs)
      value |= limbs_[limb + 1] << (64 - offset);
    return value;
  }
  [[nodiscard]] constexpr std::uint64_t left_u64(unsigned shift) const {
    if (shift >= 64) return 0;
    return limbs_[0] << shift;
  }

 private:
  std::array<std::uint64_t, k_limbs> limbs_{};
};

struct exact_f32_encode { float32_t value{}; floating_status status{}; };

[[nodiscard]] constexpr exact_f32_encode encode_exact_f32(
    const exact_binary_sum& magnitude, bool negative, int exponent,
    rounding_mode mode) {
  if (magnitude.is_zero())
    return {float32_t::from_bits(mode == rounding_mode::toward_negative
                                     ? 0x80000000u : 0u), {}};
  const auto round = [&](unsigned shift) {
    auto value = magnitude.right_u64(shift);
    const auto inexact = magnitude.any_low(shift);
    if (!inexact) return std::pair{value, false};
    bool increment = false;
    switch (mode) {
      case rounding_mode::nearest_even:
        increment = magnitude.bit(shift - 1) &&
                    (magnitude.any_low(shift - 1) || (value & 1u));
        break;
      case rounding_mode::toward_positive: increment = !negative; break;
      case rounding_mode::toward_negative: increment = negative; break;
      case rounding_mode::toward_zero:
      case rounding_mode::nearest_away:
      case rounding_mode::stochastic: break;
    }
    return std::pair{value + static_cast<std::uint64_t>(increment), true};
  };

  auto bits = magnitude.bit_length();
  int leading = exponent + static_cast<int>(bits) - 1;
  const auto sign = negative ? 0x80000000u : 0u;
  if (leading >= -126) {
    std::uint64_t significand = 0;
    bool inexact = false;
    if (bits > 24) {
      std::tie(significand, inexact) = round(bits - 24);
    } else {
      // The stored integer has its binary point after its least-significant
      // bit; normalize it to the implicit-one F32 significand position.
      significand = magnitude.right_u64(0) << (24 - bits);
    }
    if (significand == (std::uint64_t{1} << 24)) {
      significand >>= 1;
      ++leading;
    }
    if (leading > 127) {
      const bool finite = mode == rounding_mode::toward_zero ||
                          (negative && mode == rounding_mode::toward_positive) ||
                          (!negative && mode == rounding_mode::toward_negative);
      return {float32_t::from_bits(sign | (finite ? 0x7f7fffffu : 0x7f800000u)),
              {.overflow = true, .inexact = true}};
    }
    return {float32_t::from_bits(sign |
            (static_cast<uint32_t>(leading + 127) << 23) |
            (static_cast<uint32_t>(significand) & 0x007fffffu)),
            {.inexact = inexact}};
  }

  // A subnormal F32 is an integer number of 2^-149 units.  A negative shift
  // can only occur for values that round directly into the normal boundary.
  const int shift_signed = -149 - exponent;
  std::uint64_t significand = 0;
  bool inexact = false;
  if (shift_signed > 0) {
    std::tie(significand, inexact) = round(static_cast<unsigned>(shift_signed));
  } else {
    significand = magnitude.left_u64(static_cast<unsigned>(-shift_signed));
  }
  if (significand == 0)
    return {float32_t::from_bits(sign), {.underflow = inexact, .inexact = inexact}};
  if (significand >= (std::uint64_t{1} << 23))
    return {float32_t::from_bits(sign | 0x00800000u), {.inexact = inexact}};
  return {float32_t::from_bits(sign | static_cast<uint32_t>(significand)),
          {.underflow = inexact, .inexact = inexact}};
}

[[nodiscard]] inline std::expected<exact_f32_encode, arithmetic_error>
exact_scaled_accumulate(const context& ctx, const widened_product& product,
                        float32_t accumulator, rounding_mode mode) {
  namespace canonical = ::ptxsim::arith::detail::canonical;
  const auto c = canonical::decode(accumulator);
  const auto finite = [](canonical::number_class kind) {
    return kind == canonical::number_class::finite ||
           kind == canonical::number_class::zero;
  };
  if (!finite(product.value.classification) || !finite(c.classification)) {
    const auto encoded = canonical::encode_float<float32_t>(product.value,
                                                             {.rounding = mode});
    auto sum = fma(ctx, encoded.value, float32_t::from_bits(0x3f800000u),
                   accumulator, {.rounding = mode});
    if (!sum) return std::unexpected(sum.error());
    sum->status.invalid |= encoded.status.invalid;
    sum->status.overflow |= encoded.status.overflow;
    sum->status.underflow |= encoded.status.underflow;
    sum->status.inexact |= encoded.status.inexact;
    return exact_f32_encode{sum->value, sum->status};
  }
  const int product_exponent = product.value.classification == canonical::number_class::zero
                                   ? 0 : product.value.exponent;
  const int c_exponent = c.classification == canonical::number_class::zero
                             ? 0 : c.exponent;
  const int common = std::min(product_exponent, c_exponent);
  exact_binary_sum p, addend;
  if (product.value.classification != canonical::number_class::zero)
    p.set_shifted(product.value.significand,
                  static_cast<unsigned>(product.value.exponent - common));
  if (c.classification != canonical::number_class::zero)
    addend.set_shifted(c.significand, static_cast<unsigned>(c.exponent - common));
  bool negative = false;
  if (product.value.negative == c.negative) {
    p.add(addend);
    negative = product.value.negative;
  } else if (p.compare(addend) >= 0) {
    p.subtract(addend);
    negative = product.value.negative;
  } else {
    addend.subtract(p);
    p = addend;
    negative = c.negative;
  }
  return encode_exact_f32(p, negative, common, mode);
}

template <typename A, typename B, typename ScaleA, typename ScaleB>
std::expected<result<float32_t, tensor_status>, arithmetic_error> scaled_mac(
    const context& ctx, float32_t accumulator, A a, B b, ScaleA scale_a,
    ScaleB scale_b, const tensor_control& control) {
  // There is one canonical product A*scaleA*B*scaleB.  It and C are exactly
  // aligned in a fixed binary limb array and rounded only once to F32.
  const auto product = exact_product(a, scale_a, b, scale_b);
  const auto sum = exact_scaled_accumulate(ctx, product, accumulator,
                                           control.accumulator_rounding);
  if (!sum)
    return std::unexpected(sum.error());
  return {{sum->value,
           {.inexact = sum->status.inexact,
            .overflow = sum->status.overflow,
            .invalid = product.invalid || sum->status.invalid,
            .underflow = sum->status.underflow}}};
}

template <typename D>
constexpr tensor_status initial_status(const context& ctx) {
  return {.model_dependent =
              !std::same_as<D, int32_t> && supported_tensor_profile(ctx)};
}
inline constexpr void merge_status(tensor_status& into,
                                   const tensor_status& next) {
  into.inexact |= next.inexact;
  into.overflow |= next.overflow;
  into.saturated |= next.saturated;
  into.invalid |= next.invalid;
  into.underflow |= next.underflow;
}
}  // namespace detail

template <typename D, typename A, typename B, typename C, std::size_t M,
          std::size_t N, std::size_t K>
std::expected<result<tile<M, N, D>, tensor_status>, arithmetic_error> mma(
    const context& ctx, const tile<M, K, A>& a, const tile<K, N, B>& b,
    const tile<M, N, C>& c, const tensor_control& control = {}) {
  if constexpr (!tensor_capability_v<D, A, B, C>)
    return std::unexpected(arithmetic_error::unsupported_type_combination);
  if (!detail::supported_tensor_profile(ctx))
    return std::unexpected(arithmetic_error::unsupported_model_profile);
  if (auto valid = detail::validate_tensor_control<D>(control); !valid)
    return std::unexpected(valid.error());
  tile<M, N, D> out{};
  auto status = detail::initial_status<D>(ctx);
  for (std::size_t row = 0; row != M; ++row)
    for (std::size_t col = 0; col != N; ++col) {
      if constexpr (std::same_as<D, int32_t>) {
        __int128 exact = c(row, col);
        for (std::size_t k = 0; k != K; ++k)
          exact += static_cast<__int128>(a(row, k)) *
                   static_cast<__int128>(b(k, col));
        auto next = detail::finalize_int32_mma(exact, control);
        out(row, col) = next.value;
        detail::merge_status(status, next.status);
      } else {
        D value = c(row, col);
        for (std::size_t k = 0; k != K; ++k) {
          auto next = detail::mac(ctx, value, a(row, k), b(k, col), control);
          if (!next) return std::unexpected(next.error());
          value = next->value;
          detail::merge_status(status, next->status);
        }
        out(row, col) = value;
      }
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
  if constexpr (!tensor_capability_v<D, A, B, C, tensor_scale_model::one_x>)
    return std::unexpected(arithmetic_error::unsupported_type_combination);
  if (!detail::supported_tensor_profile(ctx))
    return std::unexpected(arithmetic_error::unsupported_model_profile);
  if (auto valid = detail::validate_tensor_control<D>(control); !valid)
    return std::unexpected(valid.error());
  if (!detail::valid_scales<A, B, ScaleA, ScaleB, M, N, K>(scales_a, scales_b))
    return std::unexpected(arithmetic_error::invalid_scale_layout);
  tile<M, N, D> out{};
  auto status = detail::initial_status<D>(ctx);
  for (std::size_t row = 0; row != M; ++row)
    for (std::size_t col = 0; col != N; ++col) {
      D value = c(row, col);
      for (std::size_t k = 0; k != K; ++k) {
        auto next = detail::scaled_mac(ctx, value, a(row, k), b(k, col),
                                       scales_a.at_a(row, k),
                                       scales_b.at_b(k, col, N), control);
        if (!next) return std::unexpected(next.error());
        value = next->value;
        detail::merge_status(status, next->status);
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
  if constexpr (!tensor_capability_v<D, A, B, C>)
    return std::unexpected(arithmetic_error::unsupported_type_combination);
  if (!detail::supported_tensor_profile(ctx))
    return std::unexpected(arithmetic_error::unsupported_model_profile);
  if (auto valid = detail::validate_tensor_control<D>(control); !valid)
    return std::unexpected(valid.error());
  const auto needs_data = [](const auto& view) { return view.rows && view.cols; };
  if (a.cols != b.rows || a.rows != c.rows || b.cols != c.cols ||
      d.rows != c.rows || d.cols != c.cols || a.stride < a.cols ||
      b.stride < b.cols || c.stride < c.cols || d.stride < d.cols ||
      (needs_data(a) && !a.data) || (needs_data(b) && !b.data) ||
      (needs_data(c) && !c.data) || (needs_data(d) && !d.data))
    return std::unexpected(arithmetic_error::invalid_tensor_shape);
  auto status = detail::initial_status<D>(ctx);
  for (std::size_t row = 0; row != d.rows; ++row)
    for (std::size_t col = 0; col != d.cols; ++col) {
      if constexpr (std::same_as<D, int32_t>) {
        __int128 exact = c(row, col);
        for (std::size_t k = 0; k != a.cols; ++k)
          exact += static_cast<__int128>(a(row, k)) *
                   static_cast<__int128>(b(k, col));
        auto next = detail::finalize_int32_mma(exact, control);
        d(row, col) = next.value;
        detail::merge_status(status, next.status);
      } else {
        D value = c(row, col);
        for (std::size_t k = 0; k != a.cols; ++k) {
          auto next = detail::mac(ctx, value, a(row, k), b(k, col), control);
          if (!next) return std::unexpected(next.error());
          value = next->value;
          detail::merge_status(status, next->status);
        }
        d(row, col) = value;
      }
    }
  return status;
}

}  // namespace tensor
}  // namespace ptxsim::arith
