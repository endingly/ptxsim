#pragma once

// The conversion core intentionally owns no pair-specific routes.  Each
// source is decoded once into an exact binary canonical value and each target
// is encoded once from that value.  The public `cvt` façade only validates
// controls and invokes this pipeline.

#include <ptxsim/arith/context.hpp>
#include <ptxsim/arith/concepts.hpp>
#include <ptxsim/arith/detail/dispatch.hpp>
#include <ptxsim/arith/detail/format_traits.hpp>
#include <ptxsim/arith/error.hpp>
#include <ptxsim/arith/result.hpp>

#include <bit>
#include <cstdint>
#include <expected>
#include <limits>
#include <type_traits>

namespace ptxsim::arith::detail::canonical {

enum class number_class { zero, finite, infinity, nan };

// finite value = (-1)^negative * significand * 2^exponent.  The source
// formats currently supported by arith need at most 64 exact significand
// bits (u64 integers); no host floating point participates.
struct number {
  number_class classification = number_class::zero;
  bool negative = false;
  bool signaling_nan = false;
  std::uint64_t nan_payload = 0;
  unsigned nan_payload_bits = 0;
  std::uint64_t significand = 0;
  int exponent = 0;
};

[[nodiscard]] constexpr unsigned bit_length(std::uint64_t value) noexcept {
  return value == 0 ? 0u : 64u - std::countl_zero(value);
}

[[nodiscard]] constexpr floating_status merge(floating_status lhs,
                                               floating_status rhs) noexcept {
  return {.invalid = lhs.invalid || rhs.invalid,
          .divide_by_zero = lhs.divide_by_zero || rhs.divide_by_zero,
          .overflow = lhs.overflow || rhs.overflow,
          .underflow = lhs.underflow || rhs.underflow,
          .inexact = lhs.inexact || rhs.inexact};
}

struct rounded {
  std::uint64_t value{};
  bool inexact{};
};

[[nodiscard]] constexpr rounded round_right(std::uint64_t value,
                                             unsigned shift, bool negative,
                                             rounding_mode mode,
                                             std::uint32_t stochastic_bits = 0) noexcept {
  if (shift == 0)
    return {value, false};
  const auto quotient = shift >= 64 ? std::uint64_t{0} : value >> shift;
  const bool remainder = shift >= 64 ? value != 0
                                     : (value & ((std::uint64_t{1} << shift) - 1)) != 0;
  if (!remainder)
    return {quotient, false};

  bool increment = false;
  switch (mode) {
    case rounding_mode::nearest_even:
      if (shift < 64) {
        const auto remainder_bits = value & ((std::uint64_t{1} << shift) - 1);
        const auto half = std::uint64_t{1} << (shift - 1);
        increment = remainder_bits > half ||
                    (remainder_bits == half && (quotient & 1u) != 0);
      } else if (shift == 64) {
        // The quotient is zero.  2^63 is the exact tie and remains even;
        // larger U64 values round up.
        increment = value > (std::uint64_t{1} << 63);
      }
      break;
    case rounding_mode::toward_positive:
      increment = !negative;
      break;
    case rounding_mode::toward_negative:
      increment = negative;
      break;
    case rounding_mode::toward_zero:
      break;
    case rounding_mode::nearest_away:
      if (shift < 64)
        increment = (value & ((std::uint64_t{1} << shift) - 1)) >=
                    (std::uint64_t{1} << (shift - 1));
      else if (shift == 64)
        increment = value >= (std::uint64_t{1} << 63);
      break;
    case rounding_mode::stochastic:
      // Replay contract: random_bits denotes the U[0, 2^32) threshold.  A
      // discarded fraction r / 2^shift rounds up iff random_bits is below
      // floor(r * 2^32 / 2^shift).  This consumes no mutable/global PRNG.
      if (shift <= 32)
        increment = stochastic_bits <
                    static_cast<std::uint32_t>(
                        (value & ((std::uint64_t{1} << shift) - 1))
                        << (32 - shift));
      else if (shift < 64)
        increment = stochastic_bits <
                    static_cast<std::uint32_t>(
                        (value & ((std::uint64_t{1} << shift) - 1)) >>
                        (shift - 32));
      else if (shift == 64)
        increment = stochastic_bits < static_cast<std::uint32_t>(value >> 32);
      break;
  }
  return {quotient + static_cast<std::uint64_t>(increment), true};
}

template <FloatingFormat T>
[[nodiscard]] constexpr number decode_float(T raw) noexcept {
  using traits = FormatTraits<T>;
  const auto value = normalize_encoding(raw);
  const auto category = classify(value);
  constexpr unsigned fraction_lsb =
      traits::fraction_mask == 0 ? 0 : std::countr_zero(traits::fraction_mask);
  const auto exponent_field = static_cast<unsigned>(
      (value.bits() & traits::exponent_mask) >> (traits::fraction_bits + fraction_lsb));
  const auto fraction = static_cast<std::uint64_t>(
      (value.bits() & traits::fraction_mask) >> fraction_lsb);
  if (category == fp_class::zero)
    return {.classification = number_class::zero, .negative = is_negative(value)};
  if (category == fp_class::infinity)
    return {.classification = number_class::infinity,
            .negative = is_negative(value)};
  if (category == fp_class::quiet_nan || category == fp_class::signaling_nan)
    return {.classification = number_class::nan,
            .negative = is_negative(value),
            .signaling_nan = category == fp_class::signaling_nan,
            .nan_payload = traits::preserves_nan_payload ? fraction : 0,
            .nan_payload_bits = traits::preserves_nan_payload ? traits::fraction_bits : 0};
  const bool subnormal = category == fp_class::subnormal;
  return {.classification = number_class::finite,
          .negative = is_negative(value),
          .significand = subnormal ? fraction
                                   : ((std::uint64_t{1} << traits::fraction_bits) | fraction),
          .exponent = (subnormal ? 1 : static_cast<int>(exponent_field)) -
                      traits::exponent_bias - static_cast<int>(traits::fraction_bits)};
}

template <arithmetic_integer T>
[[nodiscard]] constexpr number decode_integer(T value) noexcept {
  using U = std::make_unsigned_t<T>;
  const auto bits = static_cast<U>(value);
  if (bits == 0)
    return {};
  if constexpr (std::is_signed_v<T>) {
    if (value < 0) {
      const auto magnitude = static_cast<U>(~bits) + U{1};
      return {.classification = number_class::finite,
              .negative = true,
              .significand = static_cast<std::uint64_t>(magnitude)};
    }
  }
  return {.classification = number_class::finite,
          .significand = static_cast<std::uint64_t>(bits)};
}

[[nodiscard]] constexpr number decode(fixed8_s2f6_t value) noexcept {
  auto decoded = decode_integer(value.rep);
  decoded.exponent = -fixed8_s2f6_t::fraction_bits;
  return decoded;
}

[[nodiscard]] constexpr number decode(tfloat32_t value) noexcept {
  return decode_float(value.canonical_value());
}

template <FloatingFormat To>
[[nodiscard]] constexpr result<To, floating_status> encode_float(
    number input, conversion_control control,
    std::uint32_t stochastic_bits = 0) noexcept {
  using traits = FormatTraits<To>;
  using bits = typename traits::Bits;
  const auto pack = [](bool negative, unsigned exponent,
                       std::uint64_t fraction) constexpr {
    constexpr unsigned fraction_lsb =
        traits::fraction_mask == 0 ? 0 : std::countr_zero(traits::fraction_mask);
    return To::from_bits(static_cast<bits>(
        (negative ? traits::sign_mask : bits{0}) |
        (static_cast<bits>(exponent) << (traits::fraction_bits + fraction_lsb)) |
        (static_cast<bits>(fraction) << fraction_lsb)));
  };
  const auto maximum_finite = [&](bool negative) {
    return pack(negative, traits::maximum_finite_exponent_field,
                traits::maximum_finite_fraction_field);
  };
  const auto input_maximum_finite = [&] { return maximum_finite(input.negative); };
  const auto canonical_nan = [&] {
    std::uint64_t payload = traits::canonical_nan_fraction_field;
    if constexpr (traits::fraction_bits != 0) {
      if (input.nan_payload_bits <= traits::fraction_bits)
        payload |= input.nan_payload << (traits::fraction_bits - input.nan_payload_bits);
      else
        payload |= input.nan_payload >> (input.nan_payload_bits - traits::fraction_bits);
      payload |= traits::canonical_nan_fraction_field;
      payload &= (std::uint64_t{1} << traits::fraction_bits) - 1;
    }
    return pack(input.negative, traits::canonical_nan_exponent_field, payload);
  };

  if (input.classification == number_class::nan) {
    if constexpr (traits::has_quiet_nan)
      return {canonical_nan(), {.invalid = input.signaling_nan}};
    // A format without NaN has no signed NaN representation.  PTX finite
    // saturation maps it to its positive finite endpoint.
    return {maximum_finite(false), {.invalid = true}};
  }
  if (input.classification == number_class::infinity) {
    if constexpr (traits::has_infinity) {
      if (control.saturation == saturation_mode::finite)
        return {input_maximum_finite(), {.overflow = true, .inexact = true}};
      return {pack(input.negative, traits::all_exponent_field, 0), {}};
    }
    return {input_maximum_finite(), {.overflow = true, .inexact = true}};
  }
  if (input.classification == number_class::zero) {
    if constexpr (traits::has_zero)
      return {pack(input.negative, 0, 0), {}};
    return {pack(false, 0, 0), {.underflow = true, .inexact = true}};
  }
  if constexpr (traits::sign_mask == 0) {
    if (input.negative)
      return {pack(false, 0, 0), {.invalid = true, .inexact = true}};
  }

  const unsigned source_bits = bit_length(input.significand);
  int leading_exponent = input.exponent + static_cast<int>(source_bits) - 1;
  const int minimum_normal =
      traits::has_zero ? 1 - traits::exponent_bias : -traits::exponent_bias;
  const int maximum_normal =
      static_cast<int>(traits::maximum_finite_exponent_field) - traits::exponent_bias;

  if constexpr (!traits::has_zero && !traits::has_subnormal) {
    if (leading_exponent < minimum_normal)
      return {pack(false, 0, 0), {.underflow = true, .inexact = true}};
  }

  std::uint64_t rounded_significand{};
  bool inexact = false;
  unsigned target_exponent{};
  if (leading_exponent >= minimum_normal) {
    const int shift = static_cast<int>(source_bits) -
                      static_cast<int>(traits::fraction_bits + 1);
    if (shift > 0) {
      const auto rounded = round_right(input.significand, static_cast<unsigned>(shift),
                                       input.negative, control.rounding,
                                       stochastic_bits);
      rounded_significand = rounded.value;
      inexact = rounded.inexact;
    } else {
      rounded_significand = input.significand << static_cast<unsigned>(-shift);
    }
    if (rounded_significand ==
        (std::uint64_t{1} << (traits::fraction_bits + 1))) {
      rounded_significand >>= 1;
      ++leading_exponent;
    }
    target_exponent = static_cast<unsigned>(leading_exponent + traits::exponent_bias);
    if (leading_exponent > maximum_normal ||
        traits::is_nan_fields(target_exponent,
                              rounded_significand & ((std::uint64_t{1} << traits::fraction_bits) - 1)))
      if constexpr (traits::has_infinity) {
        const bool finite = control.saturation == saturation_mode::finite ||
                            control.rounding == rounding_mode::toward_zero ||
                            (input.negative &&
                             control.rounding == rounding_mode::toward_positive) ||
                            (!input.negative &&
                             control.rounding == rounding_mode::toward_negative);
        return {finite ? input_maximum_finite()
                       : pack(input.negative, traits::all_exponent_field, 0),
                {.overflow = true, .inexact = true}};
      } else {
        return {input_maximum_finite(), {.overflow = true, .inexact = true}};
      }
    return {pack(input.negative, target_exponent,
                 rounded_significand & ((std::uint64_t{1} << traits::fraction_bits) - 1)),
            {.inexact = inexact}};
  }

  // Subnormal target units are 2^(minimum_normal - fraction_bits).
  const int shift = input.exponent -
                    (minimum_normal - static_cast<int>(traits::fraction_bits));
  if (shift >= 0) {
    rounded_significand = input.significand << static_cast<unsigned>(shift);
  } else {
    const auto rounded = round_right(input.significand, static_cast<unsigned>(-shift),
                                     input.negative, control.rounding,
                                     stochastic_bits);
    rounded_significand = rounded.value;
    inexact = rounded.inexact;
  }
  if (rounded_significand >= (std::uint64_t{1} << traits::fraction_bits))
    return {pack(input.negative, 1, 0), {.inexact = inexact}};
  if (rounded_significand == 0 && !traits::has_zero)
    return {pack(false, 0, 0), {.underflow = true, .inexact = true}};
  return {pack(input.negative, 0, rounded_significand),
          {.underflow = inexact, .inexact = inexact}};
}

template <arithmetic_integer To>
[[nodiscard]] constexpr result<To, floating_status> encode_integer(
    number input, conversion_control control,
    std::uint32_t stochastic_bits = 0) noexcept {
  floating_status status{};
  if (input.classification == number_class::nan ||
      input.classification == number_class::infinity) {
    status.invalid = true;
    status.overflow = true;
    return {To{}, status};
  }
  if (input.classification == number_class::zero)
    return {To{}, status};

  std::uint64_t magnitude{};
  bool too_wide = false;
  if (input.exponent >= 0) {
    const auto bits = bit_length(input.significand);
    too_wide = input.exponent >= 64 ||
                bits + static_cast<unsigned>(input.exponent) > 64;
    magnitude = too_wide ? 0 : input.significand << static_cast<unsigned>(input.exponent);
  } else {
    const auto rounded = round_right(input.significand,
                                     static_cast<unsigned>(-input.exponent),
                                     input.negative, control.rounding,
                                     stochastic_bits);
    magnitude = rounded.value;
    status.inexact = rounded.inexact;
  }

  constexpr unsigned width = std::numeric_limits<std::make_unsigned_t<To>>::digits;
  const auto signed_limit = std::uint64_t{1} << (width - 1);
  bool out_of_range = too_wide;
  if constexpr (std::is_unsigned_v<To>)
    out_of_range = out_of_range || input.negative;
  else
    out_of_range = out_of_range ||
                   (!input.negative && magnitude >= signed_limit) ||
                   (input.negative && magnitude > signed_limit);

  if (out_of_range) {
    status.overflow = true;
    status.invalid = true;
    if (control.saturation == saturation_mode::type_range) {
      if constexpr (std::is_unsigned_v<To>)
        return {input.negative ? To{} : std::numeric_limits<To>::max(), status};
      return {input.negative ? std::numeric_limits<To>::min()
                             : std::numeric_limits<To>::max(),
              status};
    }
  }

  using U = std::make_unsigned_t<To>;
  const U bits = static_cast<U>(magnitude);
  if constexpr (std::is_unsigned_v<To>)
    return {input.negative ? static_cast<To>(U{0} - bits) : static_cast<To>(bits), status};
  const U signed_bits = input.negative ? U{0} - bits : bits;
  return {std::bit_cast<To>(signed_bits), status};
}

[[nodiscard]] constexpr result<fixed8_s2f6_t, floating_status> encode_fixed(
    number input, conversion_control control,
    std::uint32_t stochastic_bits = 0) noexcept {
  if (input.classification == number_class::finite)
    input.exponent += fixed8_s2f6_t::fraction_bits;
  const auto encoded = encode_integer<std::int8_t>(input, control,
                                                    stochastic_bits);
  return {{encoded.value}, encoded.status};
}

template <typename To>
[[nodiscard]] inline std::expected<result<To, floating_status>, arithmetic_error>
encode(number input, const context& ctx, conversion_control control,
       std::uint32_t stochastic_bits = 0) {
  if constexpr (std::same_as<To, tfloat32_t>) {
    const auto f32 = encode_float<float32_t>(input, control, stochastic_bits);
    auto encoded = dispatch::quantize_tf32(f32.value, control, ctx.profile().tf32);
    if (!encoded)
      return std::unexpected(encoded.error());
    encoded->status = merge(f32.status, encoded->status);
    return *encoded;
  } else if constexpr (FloatingFormat<To>) {
    return encode_float<To>(input, control, stochastic_bits);
  } else if constexpr (arithmetic_integer<To>) {
    return encode_integer<To>(input, control, stochastic_bits);
  } else {
    static_assert(std::same_as<To, fixed8_s2f6_t>);
    return encode_fixed(input, control, stochastic_bits);
  }
}

template <typename From>
[[nodiscard]] constexpr number decode(From value) noexcept {
  if constexpr (std::same_as<From, tfloat32_t>)
    return decode(value);
  else if constexpr (FloatingFormat<From>)
    return decode_float(value);
  else if constexpr (arithmetic_integer<From>)
    return decode_integer(value);
  else
    return decode(static_cast<fixed8_s2f6_t>(value));
}

}  // namespace ptxsim::arith::detail::canonical
