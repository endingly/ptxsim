#include "low_precision_backend.hpp"

#include "nan_policy.hpp"
#include "operation_policy.hpp"
#include "softfloat_backend.hpp"

#include <array>
#include <bit>
#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace ptxsim::arith::detail {
namespace {

struct RoundedInteger {
  std::uint64_t value;
  bool inexact;
};

RoundedInteger round_right(std::uint64_t value, unsigned shift, bool negative,
                           RoundingMode mode) noexcept {
  if (shift == 0)
    return {value, false};
  const std::uint64_t quotient = shift < 64 ? value >> shift : 0;
  const std::uint64_t remainder =
      shift < 64 ? value & ((std::uint64_t{1} << shift) - 1) : value;
  if (remainder == 0)
    return {quotient, false};
  bool increment = false;
  switch (mode) {
    case RoundingMode::NearestEven:
      if (shift < 64) {
        const auto halfway = std::uint64_t{1} << (shift - 1);
        increment = remainder > halfway ||
                    (remainder == halfway && (quotient & 1) != 0);
      }
      break;
    case RoundingMode::TowardZero:
      break;
    case RoundingMode::TowardNegative:
      increment = negative;
      break;
    case RoundingMode::TowardPositive:
      increment = !negative;
      break;
  }
  return {quotient + static_cast<std::uint64_t>(increment), true};
}

RoundedInteger round_scaled(std::uint64_t value, int binary_power,
                            bool negative, RoundingMode mode) noexcept {
  if (binary_power >= 0)
    return {value << static_cast<unsigned>(binary_power), false};
  return round_right(value, static_cast<unsigned>(-binary_power), negative,
                     mode);
}

template <typename T>
constexpr unsigned fraction_lsb =
    FormatTraits<T>::fraction_mask == 0
        ? 0
        : std::countr_zero(static_cast<typename FormatTraits<T>::Bits>(
              FormatTraits<T>::fraction_mask));

template <typename T>
constexpr auto pack(bool negative, unsigned exponent,
                    std::uint64_t fraction) noexcept {
  using Traits = FormatTraits<T>;
  using Bits = typename Traits::Bits;
  const auto sign = negative ? Traits::sign_mask : Bits{0};
  return T::from_bits(
      static_cast<Bits>(sign |
                        (static_cast<Bits>(exponent)
                         << (Traits::fraction_bits + fraction_lsb<T>)) |
                        (static_cast<Bits>(fraction) << fraction_lsb<T>)));
}

template <typename T>
constexpr T maximum_finite(bool negative) noexcept {
  using Traits = FormatTraits<T>;
  return pack<T>(negative, Traits::maximum_finite_exponent_field,
                 Traits::maximum_finite_fraction_field);
}

template <typename T>
constexpr T infinity(bool negative) noexcept {
  return pack<T>(negative, FormatTraits<T>::all_exponent_field, 0);
}

template <typename T>
Result<T> overflow(bool negative, ConversionControl control) noexcept {
  ExceptionFlags flags;
  flags |= ExceptionFlag::Overflow;
  flags |= ExceptionFlag::Inexact;
  if constexpr (!FormatTraits<T>::has_infinity) {
    return {maximum_finite<T>(negative), flags};
  } else {
    const bool finite =
        control.satfinite || control.rounding == RoundingMode::TowardZero ||
        (negative && control.rounding == RoundingMode::TowardPositive) ||
        (!negative && control.rounding == RoundingMode::TowardNegative);
    return {finite ? maximum_finite<T>(negative) : infinity<T>(negative),
            flags};
  }
}

template <typename T>
Result<T> convert_nan(float32_t value) noexcept {
  using Traits = FormatTraits<T>;
  const bool negative = is_negative(value);
  ExceptionFlags flags;
  if (is_signaling_nan(value))
    flags |= ExceptionFlag::Invalid;
  if constexpr (!Traits::has_quiet_nan) {
    flags |= ExceptionFlag::Invalid;
    return {maximum_finite<T>(negative), flags};
  } else if constexpr (!Traits::preserves_nan_payload) {
    return {pack<T>(negative, Traits::canonical_nan_exponent_field,
                    Traits::canonical_nan_fraction_field),
            flags};
  } else {
    auto payload = static_cast<std::uint64_t>(value.bits() & 0x007FFFFFu) >>
                   (23 - Traits::fraction_bits);
    payload |= std::uint64_t{1} << (Traits::fraction_bits - 1);
    return {pack<T>(negative, Traits::canonical_nan_exponent_field, payload),
            flags};
  }
}

template <typename From>
float32_t widen_nan(From value, ExceptionFlags& flags) noexcept {
  using Traits = FormatTraits<From>;
  if (is_signaling_nan(value))
    flags |= ExceptionFlag::Invalid;
  const bool negative = is_negative(value);
  std::uint32_t payload;
  if constexpr (!Traits::preserves_nan_payload) {
    payload = 0x00400000u;
  } else {
    const auto fraction =
        (normalize_encoding(value).bits() & Traits::fraction_mask) >>
        fraction_lsb<From>;
    payload = static_cast<std::uint32_t>(fraction)
              << (23 - Traits::fraction_bits);
    payload |= 0x00400000u;
  }
  return float32_t::from_bits(static_cast<std::uint32_t>(
      (negative ? 0x80000000u : 0) | 0x7F800000u | payload));
}

struct NormalizedBf16 {
  bool negative;
  int exponent;
  std::uint32_t significand;
};

NormalizedBf16 unpack_bf16(bfloat16_t value) noexcept {
  using Traits = FormatTraits<bfloat16_t>;
  constexpr unsigned fraction_lsb = std::countr_zero(Traits::fraction_mask);
  constexpr std::uint32_t hidden = std::uint32_t{1} << Traits::fraction_bits;
  const bool negative = is_negative(value);
  const unsigned exponent = (value.bits() & Traits::exponent_mask) >>
                            (Traits::fraction_bits + fraction_lsb);
  const unsigned fraction =
      (value.bits() & Traits::fraction_mask) >> fraction_lsb;
  if (exponent != 0)
    return {negative, static_cast<int>(exponent) - Traits::exponent_bias,
            hidden | fraction};
  const int top = 31 - std::countl_zero(fraction);
  const int minimum_exponent = 1 - Traits::exponent_bias;
  return {negative,
          minimum_exponent - (static_cast<int>(Traits::fraction_bits) - top),
          fraction << (Traits::fraction_bits - top)};
}

std::uint32_t shift_right_jam(std::uint32_t value, unsigned shift) noexcept {
  if (shift == 0)
    return value;
  if (shift < 32)
    return (value >> shift) | ((value << (32 - shift)) != 0);
  return value != 0;
}

Result<bfloat16_t> round_pack_bf16(bool negative, int exponent,
                                   std::uint32_t significand_with_grs,
                                   RoundingMode mode) noexcept {
  using Traits = FormatTraits<bfloat16_t>;
  constexpr int minimum_exponent = 1 - Traits::exponent_bias;
  constexpr int maximum_exponent =
      static_cast<int>(Traits::maximum_finite_exponent_field) -
      Traits::exponent_bias;
  constexpr std::uint32_t hidden = std::uint32_t{1} << Traits::fraction_bits;
  constexpr std::uint32_t carry = hidden << 1;
  bool tiny = false;
  if (exponent < minimum_exponent) {
    significand_with_grs =
        shift_right_jam(significand_with_grs,
                        static_cast<unsigned>(minimum_exponent - exponent));
    exponent = minimum_exponent;
    tiny = true;
  }
  const unsigned remainder = significand_with_grs & 7u;
  std::uint32_t significand = significand_with_grs >> 3;
  bool increment = false;
  if (remainder != 0) {
    switch (mode) {
      case RoundingMode::NearestEven:
        increment = remainder > 4 || (remainder == 4 && (significand & 1));
        break;
      case RoundingMode::TowardZero:
        break;
      case RoundingMode::TowardNegative:
        increment = negative;
        break;
      case RoundingMode::TowardPositive:
        increment = !negative;
        break;
    }
  }
  significand += increment;
  if (significand >= carry) {
    significand >>= 1;
    ++exponent;
    tiny = false;
  }

  ExceptionFlags flags;
  if (remainder != 0)
    flags |= ExceptionFlag::Inexact;
  if (exponent > maximum_exponent) {
    flags |= ExceptionFlag::Overflow;
    flags |= ExceptionFlag::Inexact;
    const bool finite = mode == RoundingMode::TowardZero ||
                        (negative && mode == RoundingMode::TowardPositive) ||
                        (!negative && mode == RoundingMode::TowardNegative);
    return {finite ? maximum_finite<bfloat16_t>(negative)
                   : infinity<bfloat16_t>(negative),
            flags};
  }

  unsigned exponent_field;
  unsigned fraction;
  if (tiny && significand < hidden) {
    exponent_field = 0;
    fraction = significand;
    if (remainder != 0)
      flags |= ExceptionFlag::Underflow;
  } else {
    exponent_field = static_cast<unsigned>(exponent + Traits::exponent_bias);
    fraction = significand & (hidden - 1);
  }
  return {pack<bfloat16_t>(negative, exponent_field, fraction), flags};
}

bool finite_bf16(bfloat16_t value) noexcept {
  const auto category = classify(value);
  return category == fp_class::zero || category == fp_class::subnormal ||
         category == fp_class::normal;
}

Result<bfloat16_t> special_bf16_binary(bfloat16_t lhs, bfloat16_t rhs,
                                       ArithmeticControl control,
                                       Operation operation) {
  if (operation == Operation::Add && is_infinity(lhs) && is_infinity(rhs) &&
      is_negative(lhs) != is_negative(rhs))
    return canonical_invalid_nan<bfloat16_t>();
  if (operation == Operation::Mul && ((is_infinity(lhs) && is_zero(rhs)) ||
                                      (is_zero(lhs) && is_infinity(rhs))))
    return canonical_invalid_nan<bfloat16_t>();
  const auto widened_a = widen_to_f32(lhs, {});
  const auto widened_b = widen_to_f32(rhs, {});
  const auto a = widened_a.value;
  const auto b = widened_b.value;
  Result<float32_t> intermediate;
  switch (operation) {
    case Operation::Add:
      intermediate = SoftFloatBackend<float32_t>::add(a, b, {control.rounding});
      break;
    case Operation::Mul:
      intermediate = SoftFloatBackend<float32_t>::mul(a, b, {control.rounding});
      break;
    default:
      std::unreachable();
  }
  auto result =
      narrow_from_f32<bfloat16_t>(intermediate.value, {control.rounding});
  result.flags |= intermediate.flags;
  result.flags |= widened_a.flags;
  result.flags |= widened_b.flags;
  return result;
}

Result<bfloat16_t> add_finite_bf16(bfloat16_t lhs, bfloat16_t rhs,
                                   RoundingMode rounding) noexcept {
  if (is_zero(lhs) && is_zero(rhs)) {
    const bool negative = is_negative(lhs) == is_negative(rhs)
                              ? is_negative(lhs)
                              : rounding == RoundingMode::TowardNegative;
    return {bfloat16_t::from_bits(static_cast<std::uint16_t>(
                negative ? FormatTraits<bfloat16_t>::sign_mask : 0)),
            {}};
  }
  if (is_zero(lhs))
    return {rhs, {}};
  if (is_zero(rhs))
    return {lhs, {}};

  auto a = unpack_bf16(lhs);
  auto b = unpack_bf16(rhs);
  if (a.exponent < b.exponent) {
    const auto temporary = a;
    a = b;
    b = temporary;
  }
  std::uint32_t a_sig = a.significand << 3;
  std::uint32_t b_sig = shift_right_jam(
      b.significand << 3, static_cast<unsigned>(a.exponent - b.exponent));
  int exponent = a.exponent;
  std::uint32_t result_sig;
  bool negative;
  if (a.negative == b.negative) {
    negative = a.negative;
    result_sig = a_sig + b_sig;
    constexpr std::uint32_t carry =
        (std::uint32_t{1} << (FormatTraits<bfloat16_t>::fraction_bits + 1))
        << 3;
    if (result_sig >= carry) {
      result_sig = shift_right_jam(result_sig, 1);
      ++exponent;
    }
  } else {
    if (a_sig < b_sig) {
      const auto temporary = a_sig;
      a_sig = b_sig;
      b_sig = temporary;
      negative = b.negative;
    } else {
      negative = a.negative;
    }
    result_sig = a_sig - b_sig;
    if (result_sig == 0)
      return {bfloat16_t::from_bits(static_cast<std::uint16_t>(
                  rounding == RoundingMode::TowardNegative
                      ? FormatTraits<bfloat16_t>::sign_mask
                      : 0)),
              {}};
    constexpr std::uint32_t hidden =
        (std::uint32_t{1} << FormatTraits<bfloat16_t>::fraction_bits) << 3;
    while (result_sig < hidden) {
      result_sig <<= 1;
      --exponent;
    }
  }
  return round_pack_bf16(negative, exponent, result_sig, rounding);
}

// The least-significant exponents of an exact finite BF16 product and a BF16
// addend differ by at most 400: product LSB is [-280, 240], addend LSB is
// [-140, 120].  The product has at most 16 significant bits, so an aligned
// exact sum needs at most 416 bits (plus one carry bit).  Seven 64-bit words
// therefore leave 31 bits of headroom without any allocation or widening FP.
struct ExactBf16Integer {
  static constexpr unsigned kWords = 7;
  static constexpr unsigned kWordBits = 64;
  static constexpr unsigned kCapacityBits = kWords * kWordBits;
  static constexpr unsigned kMaximumAlignmentShift = 400;
  static constexpr unsigned kMaximumTermBits = 16;
  static constexpr unsigned kRequiredBits =
      kMaximumAlignmentShift + kMaximumTermBits + 1;
  static_assert(kRequiredBits <= kCapacityBits);

  std::array<std::uint64_t, kWords> words{};

  [[nodiscard]] static ExactBf16Integer shifted(std::uint32_t value,
                                                unsigned shift) noexcept {
    assert(shift < kCapacityBits);
    assert(std::bit_width(value) + shift <= kCapacityBits);
    ExactBf16Integer result;
    const unsigned word = shift / 64;
    const unsigned bits = shift % 64;
    assert(word < kWords);
    result.words[word] = static_cast<std::uint64_t>(value) << bits;
    if (bits != 0 && word + 1 < kWords)
      result.words[word + 1] = static_cast<std::uint64_t>(value) >> (64 - bits);
    return result;
  }

  [[nodiscard]] bool is_zero() const noexcept {
    for (const auto word : words) {
      if (word != 0)
        return false;
    }
    return true;
  }

  [[nodiscard]] int compare(const ExactBf16Integer& other) const noexcept {
    for (unsigned index = kWords; index-- != 0;) {
      if (words[index] != other.words[index])
        return words[index] < other.words[index] ? -1 : 1;
    }
    return 0;
  }

  void add(const ExactBf16Integer& other) noexcept {
    std::uint64_t carry = 0;
    for (unsigned index = 0; index != kWords; ++index) {
      const auto sum = words[index] + other.words[index];
      const bool carry_from_sum = sum < words[index];
      const auto result = sum + carry;
      const bool carry_from_carry = result < sum;
      words[index] = result;
      carry = static_cast<std::uint64_t>(carry_from_sum || carry_from_carry);
    }
    assert(carry == 0);
  }

  void subtract(const ExactBf16Integer& other) noexcept {
    std::uint64_t borrow = 0;
    for (unsigned index = 0; index != kWords; ++index) {
      const auto left = words[index];
      const auto right = other.words[index];
      words[index] = left - right - borrow;
      borrow = static_cast<std::uint64_t>(borrow != 0 ? left <= right
                                                      : left < right);
    }
    assert(borrow == 0);
  }

  [[nodiscard]] unsigned bit_length() const noexcept {
    for (unsigned index = kWords; index-- != 0;) {
      if (words[index] != 0)
        return index * 64 + 64 - std::countl_zero(words[index]);
    }
    return 0;
  }

  [[nodiscard]] bool bit(unsigned index) const noexcept {
    assert(index < kCapacityBits);
    return ((words[index / 64] >> (index % 64)) & 1u) != 0;
  }

  [[nodiscard]] std::uint32_t extract(unsigned begin,
                                      unsigned count) const noexcept {
    assert(count <= 32);
    assert(begin <= kCapacityBits);
    assert(count <= kCapacityBits - begin);
    std::uint32_t result = 0;
    for (unsigned index = 0; index != count; ++index)
      result |= static_cast<std::uint32_t>(bit(begin + index)) << index;
    return result;
  }

  [[nodiscard]] bool any_below(unsigned end) const noexcept {
    assert(end <= kCapacityBits);
    const unsigned whole_words = end / 64;
    for (unsigned index = 0; index != whole_words; ++index) {
      if (words[index] != 0)
        return true;
    }
    const unsigned remaining = end % 64;
    return remaining != 0 &&
           (words[whole_words] & ((std::uint64_t{1} << remaining) - 1)) != 0;
  }
};

Result<bfloat16_t> round_exact_bf16(const ExactBf16Integer& value,
                                    int lsb_exponent, bool negative,
                                    RoundingMode mode) noexcept {
  const unsigned bits = value.bit_length();
  const int exponent = lsb_exponent + static_cast<int>(bits) - 1;
  std::uint32_t significand_with_grs;
  if (bits <= 8) {
    significand_with_grs = static_cast<std::uint32_t>(value.words[0])
                           << (8 - bits + 3);
  } else {
    const unsigned shift = bits - 8;
    const auto significand = value.extract(shift, 8);
    const bool guard = value.bit(shift - 1);
    const bool round = shift >= 2 && value.bit(shift - 2);
    const bool sticky = shift >= 3 && value.any_below(shift - 2);
    significand_with_grs = (significand << 3) |
                           (static_cast<std::uint32_t>(guard) << 2) |
                           (static_cast<std::uint32_t>(round) << 1) |
                           static_cast<std::uint32_t>(sticky);
  }
  return round_pack_bf16(negative, exponent, significand_with_grs, mode);
}

}  // namespace

template <typename To>
Result<To> narrow_from_f32(float32_t value, ConversionControl control) {
  static_assert(ConversionTraits<To, float32_t>::supported);
  validate_conversion_control(control);
  using Traits = FormatTraits<To>;
  const bool negative = is_negative(value);
  const auto category = classify(value);
  if (category == fp_class::quiet_nan || category == fp_class::signaling_nan)
    return convert_nan<To>(value);
  if (category == fp_class::infinity)
    return Traits::has_infinity && !control.satfinite
               ? Result<To>{infinity<To>(negative), {}}
               : overflow<To>(negative, control);
  if (category == fp_class::zero)
    return {pack<To>(negative, 0, 0), {}};

  const std::uint32_t source_exp = (value.bits() >> 23) & 0xFFu;
  const std::uint32_t significand =
      source_exp == 0 ? value.bits() & 0x007FFFFFu
                      : 0x00800000u | (value.bits() & 0x007FFFFFu);
  const int scale =
      source_exp == 0 ? -149 : static_cast<int>(source_exp) - 127 - 23;
  const int top = 31 - std::countl_zero(significand);
  const int exponent = top + scale;
  const int minimum_normal_exponent = 1 - Traits::exponent_bias;
  RoundedInteger rounded{};
  unsigned target_exp = 0;

  if (exponent < minimum_normal_exponent) {
    rounded = round_scaled(significand,
                           scale - (minimum_normal_exponent -
                                    static_cast<int>(Traits::fraction_bits)),
                           negative, control.rounding);
    if (rounded.value >= (std::uint64_t{1} << Traits::fraction_bits)) {
      target_exp = 1;
      rounded.value = 0;
    }
  } else {
    rounded =
        round_scaled(significand, static_cast<int>(Traits::fraction_bits) - top,
                     negative, control.rounding);
    target_exp = static_cast<unsigned>(exponent + Traits::exponent_bias);
    if (rounded.value == (std::uint64_t{1} << (Traits::fraction_bits + 1))) {
      rounded.value >>= 1;
      ++target_exp;
    }
    rounded.value &= (std::uint64_t{1} << Traits::fraction_bits) - 1;
  }

  if (target_exp > Traits::maximum_finite_exponent_field ||
      Traits::is_nan_fields(target_exp, rounded.value))
    return overflow<To>(negative, control);

  ExceptionFlags flags;
  if (rounded.inexact) {
    flags |= ExceptionFlag::Inexact;
    if (target_exp == 0)
      flags |= ExceptionFlag::Underflow;
  }
  return {pack<To>(negative, target_exp, rounded.value), flags};
}

template <typename From>
Result<float32_t> widen_to_f32(From raw, ConversionControl control) {
  static_assert(ConversionTraits<float32_t, From>::supported);
  validate_exact_widening_control(control);
  using Traits = FormatTraits<From>;
  const auto value = normalize_encoding(raw);
  const auto category = classify(value);
  ExceptionFlags flags;
  if (category == fp_class::quiet_nan || category == fp_class::signaling_nan)
    return {widen_nan(value, flags), flags};
  const bool negative = is_negative(value);
  if (category == fp_class::infinity)
    return {float32_t::from_bits(static_cast<std::uint32_t>(
                (negative ? 0x80000000u : 0) | 0x7F800000u)),
            {}};
  const auto fraction = static_cast<std::uint32_t>(
      (value.bits() & Traits::fraction_mask) >> fraction_lsb<From>);
  const auto exponent_field =
      static_cast<unsigned>((value.bits() & Traits::exponent_mask) >>
                            (Traits::fraction_bits + fraction_lsb<From>));
  if (exponent_field == 0 && fraction == 0)
    return {float32_t::from_bits(negative ? 0x80000000u : 0u), {}};

  if (exponent_field == 0) {
    const int quantum_exponent =
        1 - Traits::exponent_bias - static_cast<int>(Traits::fraction_bits);
    if (quantum_exponent < -126) {
      const auto subnormal = fraction << (quantum_exponent + 149);
      return {float32_t::from_bits(static_cast<std::uint32_t>(
                  (negative ? 0x80000000u : 0) | subnormal)),
              {}};
    }
  }

  int exponent;
  std::uint32_t f32_fraction;
  if (exponent_field == 0) {
    const int top = 31 - std::countl_zero(fraction);
    exponent = 1 - Traits::exponent_bias -
               static_cast<int>(Traits::fraction_bits) + top;
    f32_fraction = (fraction << (23 - top)) & 0x007FFFFFu;
  } else {
    exponent = static_cast<int>(exponent_field) - Traits::exponent_bias;
    f32_fraction = fraction << (23 - Traits::fraction_bits);
  }
  return {
      float32_t::from_bits(static_cast<std::uint32_t>(
          (negative ? 0x80000000u : 0) |
          (static_cast<std::uint32_t>(exponent + 127) << 23) | f32_fraction)),
      {}};
}

Result<bfloat16_t> Bf16Backend::add(bfloat16_t lhs, bfloat16_t rhs,
                                    ArithmeticControl control) {
  validate_control<Operation::Add, bfloat16_t>(control);
  if (is_nan(lhs) || is_nan(rhs))
    return propagate_nan(lhs, rhs);
  if (!finite_bf16(lhs) || !finite_bf16(rhs))
    return special_bf16_binary(lhs, rhs, control, Operation::Add);
  return add_finite_bf16(lhs, rhs, control.rounding);
}
Result<bfloat16_t> Bf16Backend::sub(bfloat16_t lhs, bfloat16_t rhs,
                                    ArithmeticControl control) {
  validate_control<Operation::Sub, bfloat16_t>(control);
  if (is_nan(lhs) || is_nan(rhs))
    return propagate_nan(lhs, rhs);
  rhs = bfloat16_t::from_bits(rhs.bits() ^ FormatTraits<bfloat16_t>::sign_mask);
  if (!finite_bf16(lhs) || !finite_bf16(rhs))
    return special_bf16_binary(lhs, rhs, control, Operation::Add);
  return add_finite_bf16(lhs, rhs, control.rounding);
}
Result<bfloat16_t> Bf16Backend::mul(bfloat16_t lhs, bfloat16_t rhs,
                                    ArithmeticControl control) {
  validate_control<Operation::Mul, bfloat16_t>(control);
  if (is_nan(lhs) || is_nan(rhs))
    return propagate_nan(lhs, rhs);
  if (!finite_bf16(lhs) || !finite_bf16(rhs))
    return special_bf16_binary(lhs, rhs, control, Operation::Mul);
  const bool negative = is_negative(lhs) != is_negative(rhs);
  if (is_zero(lhs) || is_zero(rhs))
    return {bfloat16_t::from_bits(static_cast<std::uint16_t>(
                negative ? FormatTraits<bfloat16_t>::sign_mask : 0)),
            {}};
  const auto a = unpack_bf16(lhs);
  const auto b = unpack_bf16(rhs);
  const std::uint32_t product = a.significand * b.significand;
  const int top = 31 - std::countl_zero(product);
  const int exponent = a.exponent + b.exponent + top - 14;
  const auto significand_with_grs =
      shift_right_jam(product, static_cast<unsigned>(top - 10));
  return round_pack_bf16(negative, exponent, significand_with_grs,
                         control.rounding);
}

Result<bfloat16_t> Bf16Backend::fma(bfloat16_t a, bfloat16_t b, bfloat16_t c,
                                    ArithmeticControl control) {
  validate_control<Operation::Fma, bfloat16_t>(control);

  const bool product_negative = is_negative(a) != is_negative(b);
  const bool invalid_product =
      (is_infinity(a) && is_zero(b)) || (is_zero(a) && is_infinity(b));
  // Invalid inf*0 has precedence over every NaN operand, including c.  This
  // is the explicit BF16 policy, rather than an incidental SoftFloat order.
  if (invalid_product)
    return canonical_invalid_nan<bfloat16_t>();
  if (is_nan(a) || is_nan(b) || is_nan(c))
    return propagate_nan(a, b, c);

  if (is_infinity(a) || is_infinity(b)) {
    if (is_infinity(c) && is_negative(c) != product_negative)
      return canonical_invalid_nan<bfloat16_t>();
    return {infinity<bfloat16_t>(product_negative), {}};
  }
  if (is_infinity(c))
    return {c, {}};

  const bool product_zero = is_zero(a) || is_zero(b);
  if (product_zero && is_zero(c)) {
    const bool negative =
        product_negative == is_negative(c)
            ? product_negative
            : control.rounding == RoundingMode::TowardNegative;
    return {bfloat16_t::from_bits(static_cast<std::uint16_t>(
                negative ? FormatTraits<bfloat16_t>::sign_mask : 0)),
            {}};
  }
  if (product_zero)
    return {c, {}};

  const auto unpacked_a = unpack_bf16(a);
  const auto unpacked_b = unpack_bf16(b);
  const std::uint32_t product = unpacked_a.significand * unpacked_b.significand;
  const int product_lsb = unpacked_a.exponent + unpacked_b.exponent - 14;

  if (is_zero(c)) {
    const auto exact_product = ExactBf16Integer::shifted(product, 0);
    return round_exact_bf16(exact_product, product_lsb, product_negative,
                            control.rounding);
  }

  const auto unpacked_c = unpack_bf16(c);
  const int c_lsb = unpacked_c.exponent - 7;
  const int common_lsb = product_lsb < c_lsb ? product_lsb : c_lsb;
  auto product_term = ExactBf16Integer::shifted(
      product, static_cast<unsigned>(product_lsb - common_lsb));
  auto c_term = ExactBf16Integer::shifted(
      unpacked_c.significand, static_cast<unsigned>(c_lsb - common_lsb));

  ExactBf16Integer exact_sum;
  bool negative;
  if (product_negative == unpacked_c.negative) {
    exact_sum = product_term;
    exact_sum.add(c_term);
    negative = product_negative;
  } else {
    const int comparison = product_term.compare(c_term);
    if (comparison == 0) {
      return {bfloat16_t::from_bits(static_cast<std::uint16_t>(
                  control.rounding == RoundingMode::TowardNegative
                      ? FormatTraits<bfloat16_t>::sign_mask
                      : 0)),
              {}};
    }
    if (comparison > 0) {
      exact_sum = product_term;
      exact_sum.subtract(c_term);
      negative = product_negative;
    } else {
      exact_sum = c_term;
      exact_sum.subtract(product_term);
      negative = unpacked_c.negative;
    }
  }
  return round_exact_bf16(exact_sum, common_lsb, negative, control.rounding);
}

template Result<bfloat16_t> narrow_from_f32(float32_t, ConversionControl);
template Result<float8_e4m3_t> narrow_from_f32(float32_t, ConversionControl);
template Result<float8_e5m2_t> narrow_from_f32(float32_t, ConversionControl);
template Result<float6_e2m3_t> narrow_from_f32(float32_t, ConversionControl);
template Result<float6_e3m2_t> narrow_from_f32(float32_t, ConversionControl);
template Result<float4_e2m1_t> narrow_from_f32(float32_t, ConversionControl);
template Result<ufloat8_e8m0_t> narrow_from_f32(float32_t, ConversionControl);
template Result<ufloat7_e4m3_t> narrow_from_f32(float32_t, ConversionControl);
template Result<float32_t> widen_to_f32(bfloat16_t, ConversionControl);
template Result<float32_t> widen_to_f32(float8_e4m3_t, ConversionControl);
template Result<float32_t> widen_to_f32(float8_e5m2_t, ConversionControl);
template Result<float32_t> widen_to_f32(float6_e2m3_t, ConversionControl);
template Result<float32_t> widen_to_f32(float6_e3m2_t, ConversionControl);
template Result<float32_t> widen_to_f32(float4_e2m1_t, ConversionControl);
template Result<float32_t> widen_to_f32(ufloat8_e8m0_t, ConversionControl);
template Result<float32_t> widen_to_f32(ufloat7_e4m3_t, ConversionControl);

}  // namespace ptxsim::arith::detail
