#include "sub_semantics.hpp"

namespace ptxsim::inst_execute_engine::detail::semantics {
namespace sub_detail {

/** @brief Subtract f32 lanes independently, preserving their packed ordering. */
auto sub_float32x2(const arith::context& context, arith::float32x2_t lhs,
                   arith::float32x2_t rhs, arith::floating_control control)
    -> std::expected<arith::float32x2_t, arith::arithmetic_error> {
  std::uint64_t bits = 0;
  for (std::size_t lane = 0; lane != 2; ++lane) {
    const auto difference = arith::sub(context, lhs[lane], rhs[lane], control);
    if (!difference)
      return std::unexpected(difference.error());
    bits |= std::uint64_t{difference->value.bits()} << (lane * 32U);
  }
  return arith::float32x2_t::from_bits(bits);
}

}  // namespace sub_detail

auto valid_sub(const exec_ir::Sub::FloatF32& form) noexcept -> bool {
  return floating_detail::rounding(form.rounding).has_value();
}

auto valid_sub(const exec_ir::Sub::FloatF32x2& form) noexcept -> bool {
  return floating_detail::rounding(form.rounding).has_value();
}

auto valid_sub(const exec_ir::Sub::FloatF64& form) noexcept -> bool {
  return floating_detail::rounding(form.rounding).has_value();
}

auto valid_sub(const exec_ir::Sub::Half& form) noexcept -> bool {
  return form.rounding == exec_ir::RoundingMode::rn;
}

auto valid_sub(const exec_ir::Sub::Bfloat& form) noexcept -> bool {
  return form.rounding == exec_ir::RoundingMode::rn;
}

auto valid_sub(const exec_ir::Sub::MixedF32& form) noexcept -> bool {
  return floating_detail::rounding(form.rounding).has_value();
}

auto valid_sub(const exec_ir::Sub::IntegerNoSat&) noexcept -> bool {
  return true;
}

auto valid_sub(const exec_ir::Sub::OptionalSat&) noexcept -> bool {
  return true;
}

auto sub(const arith::context& context, const exec_ir::Sub::FloatF32& form,
         arith::float32_t lhs, arith::float32_t rhs)
    -> std::expected<arith::float32_t, arith::arithmetic_error> {
  const auto control =
      floating_detail::controls(form.rounding, form.ftz, form.sat);
  if (!control)
    return std::unexpected(control.error());
  const auto difference = arith::sub(context, lhs, rhs, *control);
  if (!difference)
    return std::unexpected(difference.error());
  return difference->value;
}

auto sub(const arith::context& context, const exec_ir::Sub::FloatF32x2& form,
         arith::float32x2_t lhs, arith::float32x2_t rhs)
    -> std::expected<arith::float32x2_t, arith::arithmetic_error> {
  const auto control = floating_detail::controls(form.rounding, form.ftz);
  if (!control)
    return std::unexpected(control.error());
  return sub_detail::sub_float32x2(context, lhs, rhs, *control);
}

auto sub(const arith::context& context, const exec_ir::Sub::FloatF64& form,
         arith::float64_t lhs, arith::float64_t rhs)
    -> std::expected<arith::float64_t, arith::arithmetic_error> {
  const auto control = floating_detail::controls(form.rounding);
  if (!control)
    return std::unexpected(control.error());
  const auto difference = arith::sub(context, lhs, rhs, *control);
  if (!difference)
    return std::unexpected(difference.error());
  return difference->value;
}

}  // namespace ptxsim::inst_execute_engine::detail::semantics
