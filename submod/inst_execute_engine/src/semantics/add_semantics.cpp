#include "add_semantics.hpp"

namespace ptxsim::inst_execute_engine::detail::semantics {
namespace add_detail {

/** @brief Add f32 lanes independently, preserving their packed ordering. */
auto add_float32x2(const arith::context& context, arith::float32x2_t lhs,
                   arith::float32x2_t rhs, arith::floating_control control)
    -> std::expected<arith::float32x2_t, arith::arithmetic_error> {
  std::uint64_t bits = 0;
  for (std::size_t lane = 0; lane != 2; ++lane) {
    const auto sum = arith::add(context, lhs[lane], rhs[lane], control);
    if (!sum)
      return std::unexpected(sum.error());
    bits |= std::uint64_t{sum->value.bits()} << (lane * 32U);
  }
  return arith::float32x2_t::from_bits(bits);
}

}  // namespace add_detail

auto valid_add(const exec_ir::Add::FloatF32& form) noexcept -> bool {
  return floating_detail::rounding(form.rounding).has_value();
}

auto valid_add(const exec_ir::Add::FloatF32x2& form) noexcept -> bool {
  return floating_detail::rounding(form.rounding).has_value();
}

auto valid_add(const exec_ir::Add::FloatF64& form) noexcept -> bool {
  return floating_detail::rounding(form.rounding).has_value();
}

auto valid_add(const exec_ir::Add::Half& form) noexcept -> bool {
  return form.rounding == exec_ir::RoundingMode::rn;
}

auto valid_add(const exec_ir::Add::Bfloat& form) noexcept -> bool {
  return form.rounding == exec_ir::RoundingMode::rn;
}

auto valid_add(const exec_ir::Add::MixedF32& form) noexcept -> bool {
  return floating_detail::rounding(form.rounding).has_value();
}

auto valid_add(const exec_ir::Add::IntegerNoSat&) noexcept -> bool {
  return true;
}

auto valid_add(const exec_ir::Add::Sat&) noexcept -> bool {
  return true;
}

auto valid_add(const exec_ir::Add::PackedOptionalSat&) noexcept -> bool {
  return true;
}

auto add(const arith::context& context, const exec_ir::Add::FloatF32& form,
         arith::float32_t lhs, arith::float32_t rhs)
    -> std::expected<arith::float32_t, arith::arithmetic_error> {
  const auto control =
      floating_detail::controls(form.rounding, form.ftz, form.sat);
  if (!control)
    return std::unexpected(control.error());
  const auto sum = arith::add(context, lhs, rhs, *control);
  if (!sum)
    return std::unexpected(sum.error());
  return sum->value;
}

auto add(const arith::context& context, const exec_ir::Add::FloatF32x2& form,
         arith::float32x2_t lhs, arith::float32x2_t rhs)
    -> std::expected<arith::float32x2_t, arith::arithmetic_error> {
  const auto control = floating_detail::controls(form.rounding, form.ftz);
  if (!control)
    return std::unexpected(control.error());
  return add_detail::add_float32x2(context, lhs, rhs, *control);
}

auto add(const arith::context& context, const exec_ir::Add::FloatF64& form,
         arith::float64_t lhs, arith::float64_t rhs)
    -> std::expected<arith::float64_t, arith::arithmetic_error> {
  const auto control = floating_detail::controls(form.rounding);
  if (!control)
    return std::unexpected(control.error());
  const auto sum = arith::add(context, lhs, rhs, *control);
  if (!sum)
    return std::unexpected(sum.error());
  return sum->value;
}

}  // namespace ptxsim::inst_execute_engine::detail::semantics
