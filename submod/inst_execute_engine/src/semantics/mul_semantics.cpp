#include "mul_semantics.hpp"

#include "../floating_controls.hpp"

namespace ptxsim::inst_execute_engine::detail::semantics {

auto valid_mul(const exec_ir::Mul::RnF32&) noexcept -> bool {
  return true;
}

auto valid_mul(const exec_ir::Mul::LoU32&) noexcept -> bool {
  return true;
}

auto valid_mul(const exec_ir::Mul::HiU32&) noexcept -> bool {
  return true;
}

auto valid_mul(const exec_ir::Mul::WideU32&) noexcept -> bool {
  return true;
}

auto valid_mul(const exec_ir::Mul::WideS32&) noexcept -> bool {
  return true;
}

auto mul(const arith::context& context, const exec_ir::Mul::RnF32& form,
         arith::float32_t lhs, arith::float32_t rhs)
    -> std::expected<arith::float32_t, arith::arithmetic_error> {
  const auto control = floating_detail::controls(exec_ir::Mul::RnF32::rounding);
  if (!control)
    return std::unexpected(control.error());
  const auto product = arith::mul(context, lhs, rhs, *control);
  if (!product)
    return std::unexpected(product.error());
  return product->value;
}

auto mul(const arith::context& context, const exec_ir::Mul::LoU32&,
         std::uint32_t lhs, std::uint32_t rhs)
    -> std::expected<std::uint32_t, arith::arithmetic_error> {
  const auto product = arith::mul(context, lhs, rhs);
  if (!product)
    return std::unexpected(product.error());
  return product->value;
}

auto mul(const arith::context& context, const exec_ir::Mul::HiU32&,
         std::uint32_t lhs, std::uint32_t rhs)
    -> std::expected<std::uint32_t, arith::arithmetic_error> {
  const auto product = arith::mul(
      context, lhs, rhs, {.part = arith::product_part::high});
  if (!product)
    return std::unexpected(product.error());
  return product->value;
}

auto mul(const arith::context& context, const exec_ir::Mul::WideU32&,
         std::uint32_t lhs, std::uint32_t rhs)
    -> std::expected<std::uint64_t, arith::arithmetic_error> {
  const auto product = arith::mul<std::uint64_t>(
      context, lhs, rhs, {.part = arith::product_part::wide});
  if (!product)
    return std::unexpected(product.error());
  return product->value;
}

auto mul(const arith::context& context, const exec_ir::Mul::WideS32&,
         std::int32_t lhs, std::int32_t rhs)
    -> std::expected<std::int64_t, arith::arithmetic_error> {
  const auto product = arith::mul<std::int64_t>(
      context, lhs, rhs, {.part = arith::product_part::wide});
  if (!product)
    return std::unexpected(product.error());
  return product->value;
}

}  // namespace ptxsim::inst_execute_engine::detail::semantics
