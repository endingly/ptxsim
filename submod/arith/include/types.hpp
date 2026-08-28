#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace ptxsim::arith {
namespace detail {
struct tf32_factory;
}

using int8_t = std::int8_t;
using int16_t = std::int16_t;
using int32_t = std::int32_t;
using int64_t = std::int64_t;
using uint8_t = std::uint8_t;
using uint16_t = std::uint16_t;
using uint32_t = std::uint32_t;
using uint64_t = std::uint64_t;
using predicate_t = bool;
using bits8_t = uint8_t;
using bits16_t = uint16_t;
using bits32_t = uint32_t;
using bits64_t = uint64_t;

struct bits128_t {
  uint64_t lo{};
  uint64_t hi{};
  friend constexpr bool operator==(bits128_t, bits128_t) = default;
};

namespace formats {
struct binary16;
struct binary32;
struct binary64;
struct bfloat16;
struct e4m3;
struct e5m2;
struct e2m3;
struct e3m2;
struct e2m1;
struct ue8m0;
struct ue4m3;
}  // namespace formats

template <typename Format>
struct format_storage;

#define PTXSIM_FORMAT_STORAGE(format, storage) \
  template <>                                  \
  struct format_storage<formats::format> {     \
    using type = storage;                      \
  }
PTXSIM_FORMAT_STORAGE(binary16, uint16_t);
PTXSIM_FORMAT_STORAGE(binary32, uint32_t);
PTXSIM_FORMAT_STORAGE(binary64, uint64_t);
PTXSIM_FORMAT_STORAGE(bfloat16, uint16_t);
PTXSIM_FORMAT_STORAGE(e4m3, uint8_t);
PTXSIM_FORMAT_STORAGE(e5m2, uint8_t);
PTXSIM_FORMAT_STORAGE(e2m3, uint8_t);
PTXSIM_FORMAT_STORAGE(e3m2, uint8_t);
PTXSIM_FORMAT_STORAGE(e2m1, uint8_t);
PTXSIM_FORMAT_STORAGE(ue8m0, uint8_t);
PTXSIM_FORMAT_STORAGE(ue4m3, uint8_t);
#undef PTXSIM_FORMAT_STORAGE

template <typename Format>
class basic_float {
 public:
  using format_type = Format;
  using storage_type = typename format_storage<Format>::type;

  constexpr basic_float() noexcept = default;

  [[nodiscard]] static constexpr basic_float from_bits(
      storage_type value) noexcept {
    return basic_float(value);
  }

  [[nodiscard]] constexpr storage_type bits() const noexcept {
    return storage_;
  }

  friend constexpr bool operator==(basic_float, basic_float) = default;

 private:
  constexpr explicit basic_float(storage_type value) noexcept
      : storage_(value) {}
  storage_type storage_{};
};

using float16_t = basic_float<formats::binary16>;
using float32_t = basic_float<formats::binary32>;
using float64_t = basic_float<formats::binary64>;
using bfloat16_t = basic_float<formats::bfloat16>;
using float8_e4m3_t = basic_float<formats::e4m3>;
using float8_e5m2_t = basic_float<formats::e5m2>;
using float6_e2m3_t = basic_float<formats::e2m3>;
using float6_e3m2_t = basic_float<formats::e3m2>;
using float4_e2m1_t = basic_float<formats::e2m1>;
using ufloat8_e8m0_t = basic_float<formats::ue8m0>;
using ufloat7_e4m3_t = basic_float<formats::ue4m3>;

// PTX specifies TF32 precision, not one universal .b32 representation.
class tfloat32_t {
 public:
  [[nodiscard]] constexpr float32_t canonical_value() const noexcept {
    return canonical_;
  }
  friend constexpr bool operator==(tfloat32_t, tfloat32_t) = default;

 private:
  constexpr explicit tfloat32_t(float32_t value) noexcept : canonical_(value) {}
  float32_t canonical_{};
  friend struct detail::tf32_factory;
};

template <typename Rep, int FractionBits>
struct basic_fixed {
  Rep rep{};
  static constexpr int fraction_bits = FractionBits;
  friend constexpr bool operator==(basic_fixed, basic_fixed) = default;
};
using fixed8_s2f6_t = basic_fixed<int8_t, 6>;

enum class packed_lane_order { lane_zero_least_significant };
struct dense_packed_layout {};
struct byte_packed_layout {};

template <typename Element, std::size_t Lanes>
struct default_packed_layout {
  using type = dense_packed_layout;
};
template <std::size_t Lanes>
struct default_packed_layout<float6_e2m3_t, Lanes> {
  using type = byte_packed_layout;
};
template <std::size_t Lanes>
struct default_packed_layout<float6_e3m2_t, Lanes> {
  using type = byte_packed_layout;
};
template <typename Element, std::size_t Lanes>
using default_packed_layout_t =
    typename default_packed_layout<Element, Lanes>::type;

template <typename Element>
struct packed_element_bits {
  static constexpr unsigned value = sizeof(Element) * 8;
};

// A packed container stores the architectural raw encoding of an element.
// Most elements are basic_float and expose bits()/from_bits(); fixed-point
// storage instead uses its signed integer representation.  Keeping this
// conversion in one trait lets packed_t represent the PTX S2F6x2 storage
// without teaching the generic lane engine about individual element classes.
template <typename Element>
struct packed_element_codec {
  static constexpr std::uint64_t to_bits(Element value) noexcept {
    return value.bits();
  }
  static constexpr Element from_bits(std::uint64_t bits) noexcept {
    return Element::from_bits(
        static_cast<typename Element::storage_type>(bits));
  }
};
template <>
struct packed_element_codec<fixed8_s2f6_t> {
  static constexpr std::uint64_t to_bits(fixed8_s2f6_t value) noexcept {
    return static_cast<std::uint8_t>(value.rep);
  }
  static constexpr fixed8_s2f6_t from_bits(std::uint64_t bits) noexcept {
    return {std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(bits))};
  }
};
template <>
struct packed_element_bits<float16_t> {
  static constexpr unsigned value = 16;
};
template <>
struct packed_element_bits<float32_t> {
  static constexpr unsigned value = 32;
};
template <>
struct packed_element_bits<bfloat16_t> {
  static constexpr unsigned value = 16;
};
template <>
struct packed_element_bits<float8_e4m3_t> {
  static constexpr unsigned value = 8;
};
template <>
struct packed_element_bits<float8_e5m2_t> {
  static constexpr unsigned value = 8;
};
template <>
struct packed_element_bits<float6_e2m3_t> {
  static constexpr unsigned value = 6;
};
template <>
struct packed_element_bits<float6_e3m2_t> {
  static constexpr unsigned value = 6;
};
template <>
struct packed_element_bits<float4_e2m1_t> {
  static constexpr unsigned value = 4;
};
template <>
struct packed_element_bits<ufloat8_e8m0_t> {
  static constexpr unsigned value = 8;
};
template <>
struct packed_element_bits<ufloat7_e4m3_t> {
  static constexpr unsigned value = 7;
};

template <typename Element, std::size_t Lanes, typename Layout>
struct packed_layout_traits;

template <typename Element, std::size_t Lanes>
struct packed_layout_traits<Element, Lanes, dense_packed_layout> {
  static constexpr unsigned logical_element_bits =
      packed_element_bits<Element>::value;
  static constexpr unsigned logical_bits = logical_element_bits * Lanes;
  static constexpr unsigned container_bits = logical_bits;
  static constexpr packed_lane_order lane_order =
      packed_lane_order::lane_zero_least_significant;
  static constexpr std::size_t lane_offset(std::size_t lane) {
    return lane * logical_element_bits;
  }
  static constexpr std::uint64_t padding_mask = 0;
};

template <typename Element, std::size_t Lanes>
struct packed_layout_traits<Element, Lanes, byte_packed_layout> {
  static constexpr unsigned logical_element_bits =
      packed_element_bits<Element>::value;
  static constexpr unsigned logical_bits = logical_element_bits * Lanes;
  static constexpr unsigned container_bits = 8 * Lanes;
  static constexpr packed_lane_order lane_order =
      packed_lane_order::lane_zero_least_significant;
  static constexpr std::size_t lane_offset(std::size_t lane) {
    return lane * 8;
  }
  static constexpr std::uint64_t padding_mask = [] {
    std::uint64_t mask = 0;
    for (std::size_t lane = 0; lane != Lanes; ++lane)
      mask |= std::uint64_t{0x3} << (lane * 8 + 6);
    return mask;
  }();
};

template <unsigned Bits>
using packed_container_t = std::conditional_t<
    Bits <= 8, uint8_t,
    std::conditional_t<Bits <= 16, uint16_t,
                       std::conditional_t<Bits <= 32, uint32_t, uint64_t>>>;

template <typename Element, std::size_t Lanes,
          typename Layout = default_packed_layout_t<Element, Lanes>>
class packed_t {
 public:
  using element_type = Element;
  using layout_type = Layout;
  using traits = packed_layout_traits<Element, Lanes, Layout>;
  using container_type = packed_container_t<traits::container_bits>;
  static constexpr std::size_t lanes_count = Lanes;
  static_assert(traits::container_bits <= 64);

  constexpr packed_t() = default;
  [[nodiscard]] static constexpr packed_t from_bits(container_type bits) {
    packed_t value;
    value.bits_ = canonicalize(bits);
    return value;
  }
  [[nodiscard]] constexpr container_type bits() const noexcept { return bits_; }
  [[nodiscard]] constexpr Element operator[](std::size_t lane) const noexcept {
    const auto mask = element_mask();
    return packed_element_codec<Element>::from_bits(
        (std::uint64_t{bits_} >> traits::lane_offset(lane)) & mask);
  }
  friend constexpr bool operator==(packed_t, packed_t) = default;

 private:
  static constexpr container_type canonicalize(container_type bits) {
    return static_cast<container_type>(std::uint64_t{bits} & valid_mask());
  }
  static constexpr std::uint64_t valid_mask() {
    const auto lane_mask = element_mask();
    std::uint64_t mask = 0;
    for (std::size_t lane = 0; lane != Lanes; ++lane)
      mask |= lane_mask << traits::lane_offset(lane);
    return mask;
  }
  static constexpr std::uint64_t element_mask() {
    return traits::logical_element_bits == 64
               ? ~std::uint64_t{}
               : (std::uint64_t{1} << traits::logical_element_bits) - 1;
  }
  container_type bits_{};
};
using float16x2_t = packed_t<float16_t, 2>;
using float32x2_t = packed_t<float32_t, 2>;
using bfloat16x2_t = packed_t<bfloat16_t, 2>;
using float8_e4m3x2_t = packed_t<float8_e4m3_t, 2>;
using float8_e4m3x4_t = packed_t<float8_e4m3_t, 4>;
using float8_e5m2x2_t = packed_t<float8_e5m2_t, 2>;
using float8_e5m2x4_t = packed_t<float8_e5m2_t, 4>;
using float6_e2m3x2_t = packed_t<float6_e2m3_t, 2>;
using float6_e3m2x2_t = packed_t<float6_e3m2_t, 2>;
using float6_e2m3x4_t = packed_t<float6_e2m3_t, 4>;
using float6_e3m2x4_t = packed_t<float6_e3m2_t, 4>;
using float4_e2m1x2_t = packed_t<float4_e2m1_t, 2>;
using float4_e2m1x4_t = packed_t<float4_e2m1_t, 4>;
// PTX specifies UE8M0 storage in paired form.  The scalar lane type remains
// available for numeric conversion and tensor scale semantics only.
using ufloat8_e8m0x2_t = packed_t<ufloat8_e8m0_t, 2>;
// PTX names this storage S2F6x2.  It is a storage/conversion format, not a
// claim that S2F6 has scalar or packed arithmetic instructions.
using fixed8_s2f6x2_t = packed_t<fixed8_s2f6_t, 2>;
using ufloat7_e4m3x2_t = packed_t<ufloat7_e4m3_t, 2>;

enum class fp_class {
  zero,
  subnormal,
  normal,
  infinity,
  quiet_nan,
  signaling_nan
};

static_assert(std::is_trivially_copyable_v<float32_t> &&
              std::is_standard_layout_v<float32_t>);
static_assert(std::is_trivially_copyable_v<tfloat32_t> &&
              std::is_standard_layout_v<tfloat32_t>);

}  // namespace ptxsim::arith
