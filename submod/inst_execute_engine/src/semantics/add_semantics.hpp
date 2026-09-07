#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <type_traits>

#include <ptxsim/arith/controls.hpp>
#include <ptxsim/arith/error.hpp>
#include <ptxsim/arith/packed.hpp>
#include <ptxsim/arith/scalar.hpp>
#include <ptxsim/exec_ir/exec_ir.hpp>

#include "../floating_controls.hpp"
#include "../value_alu_helpers.hpp"

namespace ptxsim::inst_execute_engine::detail::semantics {
namespace add_detail {

/** @brief Add packed integer lanes independently with the requested overflow rule. */
template <PackedIntegerElement Element, std::size_t Lanes>
auto add_packed_integer(const arith::context& context,
                        packed_integer<Element, Lanes> lhs,
                        packed_integer<Element, Lanes> rhs,
                        arith::integer_control control)
    -> std::expected<packed_integer<Element, Lanes>, arith::arithmetic_error> {
  using Unsigned = std::make_unsigned_t<Element>;
  constexpr auto lane_bits = sizeof(Element) * 8U;
  constexpr auto lane_mask = (std::uint32_t{1} << lane_bits) - 1U;
  std::uint32_t bits = 0;
  for (std::size_t lane = 0; lane != Lanes; ++lane) {
    const auto shift = lane * lane_bits;
    const auto left = std::bit_cast<Element>(
        static_cast<Unsigned>((lhs.bits >> shift) & lane_mask));
    const auto right = std::bit_cast<Element>(
        static_cast<Unsigned>((rhs.bits >> shift) & lane_mask));
    const auto sum = arith::add(context, left, right, control);
    if (!sum)
      return std::unexpected(sum.error());
    bits |= std::uint32_t{std::bit_cast<Unsigned>(sum->value)} << shift;
  }
  return packed_integer<Element, Lanes>{bits};
}

}  // namespace add_detail

/** @brief Validate controls retained by the scalar f32 add form. */
auto valid_add(const exec_ir::Add::FloatF32& form) noexcept -> bool;
/** @brief Validate controls retained by the packed f32 add form. */
auto valid_add(const exec_ir::Add::FloatF32x2& form) noexcept -> bool;
/** @brief Validate controls retained by the f64 add form. */
auto valid_add(const exec_ir::Add::FloatF64& form) noexcept -> bool;
/** @brief Validate the rn-only PTX half-precision add control. */
auto valid_add(const exec_ir::Add::Half& form) noexcept -> bool;
/** @brief Validate the rn-only PTX bfloat16 add control. */
auto valid_add(const exec_ir::Add::Bfloat& form) noexcept -> bool;
/** @brief Validate controls retained by mixed low-precision-to-f32 add. */
auto valid_add(const exec_ir::Add::MixedF32& form) noexcept -> bool;
/** @brief Integer forms have no dynamic arithmetic controls to validate. */
auto valid_add(const exec_ir::Add::IntegerNoSat&) noexcept -> bool;
/** @brief Saturating integer forms have no dynamic arithmetic controls to validate. */
auto valid_add(const exec_ir::Add::Sat&) noexcept -> bool;
/** @brief Packed integer forms have no dynamic arithmetic controls to validate. */
auto valid_add(const exec_ir::Add::PackedOptionalSat&) noexcept -> bool;

/** @brief Evaluate wrapping same-width scalar integer addition. */
template <arith::arithmetic_integer T>
auto add(const arith::context& context, const exec_ir::Add::IntegerNoSat&,
         T lhs, T rhs) -> std::expected<T, arith::arithmetic_error> {
  const auto sum = arith::add(context, lhs, rhs);
  if (!sum)
    return std::unexpected(sum.error());
  return sum->value;
}

/** @brief Evaluate wrapping packed integer addition without cross-lane carry. */
template <PackedIntegerElement Element, std::size_t Lanes>
auto add(const arith::context& context, const exec_ir::Add::IntegerNoSat&,
         packed_integer<Element, Lanes> lhs, packed_integer<Element, Lanes> rhs)
    -> std::expected<packed_integer<Element, Lanes>, arith::arithmetic_error> {
  return add_detail::add_packed_integer(
      context, lhs, rhs, {.overflow = arith::integer_overflow_mode::wrap});
}

/** @brief Evaluate saturating same-width scalar integer addition. */
template <arith::arithmetic_integer T>
auto add(const arith::context& context, const exec_ir::Add::Sat&, T lhs, T rhs)
    -> std::expected<T, arith::arithmetic_error> {
  const auto sum = arith::add(
      context, lhs, rhs, {.overflow = arith::integer_overflow_mode::saturate});
  if (!sum)
    return std::unexpected(sum.error());
  return sum->value;
}

/** @brief Evaluate saturating packed integer addition without cross-lane carry. */
template <PackedIntegerElement Element, std::size_t Lanes>
auto add(const arith::context& context, const exec_ir::Add::Sat&,
         packed_integer<Element, Lanes> lhs, packed_integer<Element, Lanes> rhs)
    -> std::expected<packed_integer<Element, Lanes>, arith::arithmetic_error> {
  return add_detail::add_packed_integer(
      context, lhs, rhs, {.overflow = arith::integer_overflow_mode::saturate});
}

/** @brief Evaluate scalar f32 addition with its PTX rounding, FTZ, and sat controls. */
auto add(const arith::context& context, const exec_ir::Add::FloatF32& form,
         arith::float32_t lhs, arith::float32_t rhs)
    -> std::expected<arith::float32_t, arith::arithmetic_error>;

/** @brief Evaluate f32x2 as two independent scalar f32 additions. */
auto add(const arith::context& context, const exec_ir::Add::FloatF32x2& form,
         arith::float32x2_t lhs, arith::float32x2_t rhs)
    -> std::expected<arith::float32x2_t, arith::arithmetic_error>;

/** @brief Evaluate scalar f64 addition with the selected PTX rounding mode. */
auto add(const arith::context& context, const exec_ir::Add::FloatF64& form,
         arith::float64_t lhs, arith::float64_t rhs)
    -> std::expected<arith::float64_t, arith::arithmetic_error>;

/** @brief Evaluate rn-only scalar or packed half addition. */
template <typename T>
  requires(std::same_as<T, arith::float16_t> ||
           std::same_as<T, arith::float16x2_t>)
auto add(const arith::context& context, const exec_ir::Add::Half& form, T lhs,
         T rhs) -> std::expected<T, arith::arithmetic_error> {
  const auto control =
      floating_detail::controls(form.rounding, form.ftz, form.sat);
  if (!control)
    return std::unexpected(control.error());
  const auto sum = arith::add(context, lhs, rhs, *control);
  if (!sum)
    return std::unexpected(sum.error());
  return sum->value;
}

/** @brief Evaluate rn-only scalar or packed bfloat16 addition. */
template <typename T>
  requires(std::same_as<T, arith::bfloat16_t> ||
           std::same_as<T, arith::bfloat16x2_t>)
auto add(const arith::context& context, const exec_ir::Add::Bfloat& form, T lhs,
         T rhs) -> std::expected<T, arith::arithmetic_error> {
  const auto control = floating_detail::controls(form.rounding);
  if (!control)
    return std::unexpected(control.error());
  const auto sum = arith::add(context, lhs, rhs, *control);
  if (!sum)
    return std::unexpected(sum.error());
  return sum->value;
}

/** @brief Evaluate one low-precision source plus f32 without intermediate rounding. */
template <typename Low>
  requires(std::same_as<Low, arith::float16_t> ||
           std::same_as<Low, arith::bfloat16_t>)
auto add(const arith::context& context, const exec_ir::Add::MixedF32& form,
         Low lhs, arith::float32_t rhs)
    -> std::expected<arith::float32_t, arith::arithmetic_error> {
  const auto control =
      floating_detail::controls(form.rounding, false, form.sat);
  if (!control)
    return std::unexpected(control.error());
  const auto sum = arith::add<arith::float32_t>(context, lhs, rhs, *control);
  if (!sum)
    return std::unexpected(sum.error());
  return sum->value;
}

/** @brief Evaluate lane-wise packed integer add with optional saturation. */
template <PackedIntegerElement Element, std::size_t Lanes>
auto add(const arith::context& context,
         const exec_ir::Add::PackedOptionalSat& form,
         packed_integer<Element, Lanes> lhs, packed_integer<Element, Lanes> rhs)
    -> std::expected<packed_integer<Element, Lanes>, arith::arithmetic_error> {
  return add_detail::add_packed_integer(
      context, lhs, rhs,
      {.overflow = form.sat ? arith::integer_overflow_mode::saturate
                            : arith::integer_overflow_mode::wrap});
}

}  // namespace ptxsim::inst_execute_engine::detail::semantics
