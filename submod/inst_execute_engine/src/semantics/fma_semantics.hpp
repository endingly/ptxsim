#pragma once

#include <concepts>
#include <expected>

#include <ptxsim/arith/error.hpp>
#include <ptxsim/arith/packed.hpp>
#include <ptxsim/arith/scalar.hpp>
#include <ptxsim/exec_ir/exec_ir.hpp>

#include "../floating_controls.hpp"

namespace ptxsim::inst_execute_engine::detail::semantics {
namespace fma_detail {

/** @brief Projected FMA forms retain a resolved PTX rounding field. */
template <typename Form>
concept FmaForm = requires(const Form& form) {
  { form.rounding } -> std::convertible_to<exec_ir::RoundingMode>;
};

/** @brief Translate the controls retained by one already-selected FMA form. */
template <FmaForm Form>
auto control(const Form& form)
    -> std::expected<arith::floating_control, arith::arithmetic_error> {
  const auto ftz = [&] {
    if constexpr (requires { form.ftz; })
      return static_cast<bool>(form.ftz);
    return false;
  }();
  const auto sat = [&] {
    if constexpr (requires { form.sat; })
      return static_cast<bool>(form.sat);
    return false;
  }();
  const auto relu = [&] {
    if constexpr (requires { form.relu; })
      return static_cast<bool>(form.relu);
    return false;
  }();
  const auto oob = [&] {
    if constexpr (requires { form.oob; })
      return static_cast<bool>(form.oob);
    return false;
  }();
  return floating_detail::fma_controls(form.rounding, ftz, sat, relu, oob);
}

/** @brief Check whether a homogeneous value type is legal for one FMA form. */
template <typename Form, typename T>
concept HomogeneousFma =
    (std::same_as<T, arith::float32_t> &&
     (std::same_as<Form, exec_ir::Fma::RnF32> ||
      std::same_as<Form, exec_ir::Fma::DirectedF32>)) ||
    (std::same_as<T, arith::float64_t> &&
     (std::same_as<Form, exec_ir::Fma::RnF64> ||
      std::same_as<Form, exec_ir::Fma::DirectedF64>)) ||
    (std::same_as<T, arith::float32x2_t> &&
     std::same_as<Form, exec_ir::Fma::F32x2>) ||
    (std::same_as<T, arith::float16_t> &&
     (std::same_as<Form, exec_ir::Fma::RnF16> ||
      std::same_as<Form, exec_ir::Fma::HalfRelu> ||
      std::same_as<Form, exec_ir::Fma::HalfOob> ||
      std::same_as<Form, exec_ir::Fma::HalfOobRelu>)) ||
    (std::same_as<T, arith::float16x2_t> &&
     (std::same_as<Form, exec_ir::Fma::RnF16x2> ||
      std::same_as<Form, exec_ir::Fma::HalfRelu> ||
      std::same_as<Form, exec_ir::Fma::HalfOob> ||
      std::same_as<Form, exec_ir::Fma::HalfOobRelu>)) ||
    (std::same_as<T, arith::bfloat16_t> &&
     (std::same_as<Form, exec_ir::Fma::Bf16> ||
      std::same_as<Form, exec_ir::Fma::Bf16Oob>)) ||
    (std::same_as<T, arith::bfloat16x2_t> &&
     (std::same_as<Form, exec_ir::Fma::Bf16x2> ||
      std::same_as<Form, exec_ir::Fma::Bf16x2Oob>));

/** @brief Evaluate one homogeneous FMA after its PTX controls are decoded. */
template <FmaForm Form, typename T>
  requires HomogeneousFma<Form, T>
auto evaluate(const arith::context& context, const Form& form, T lhs, T rhs,
              T addend) -> std::expected<T, arith::arithmetic_error> {
  const auto selected_control = control(form);
  if (!selected_control)
    return std::unexpected(selected_control.error());
  const auto value = arith::fma(context, lhs, rhs, addend, *selected_control);
  if (!value)
    return std::unexpected(value.error());
  return value->value;
}

/** @brief Check whether a low-precision source type belongs to one mixed FMA form. */
template <typename Form, typename Low>
concept MixedFma =
    (std::same_as<Form, exec_ir::Fma::MixedF32F16> &&
     std::same_as<Low, arith::float16_t>) ||
    (std::same_as<Form, exec_ir::Fma::MixedF32Bf16> &&
     std::same_as<Low, arith::bfloat16_t>);

/** @brief Evaluate a mixed low-precision FMA with a single f32 final rounding. */
template <FmaForm Form, typename Low>
  requires MixedFma<Form, Low>
auto evaluate_mixed(const arith::context& context, const Form& form, Low lhs,
                    Low rhs, arith::float32_t addend)
    -> std::expected<arith::float32_t, arith::arithmetic_error> {
  const auto selected_control = control(form);
  if (!selected_control)
    return std::unexpected(selected_control.error());
  const auto value = arith::fma<arith::float32_t>(
      context, lhs, rhs, addend, *selected_control);
  if (!value)
    return std::unexpected(value.error());
  return value->value;
}

}  // namespace fma_detail

/** @brief Validate the projected FMA rounding control before lane resources are read. */
template <fma_detail::FmaForm Form>
auto valid_fma(const Form& form) noexcept -> bool {
  if constexpr (std::same_as<Form, exec_ir::Fma::DirectedF32> ||
                std::same_as<Form, exec_ir::Fma::DirectedF64>) {
    return form.rounding == exec_ir::RoundingMode::rz ||
           form.rounding == exec_ir::RoundingMode::rm ||
           form.rounding == exec_ir::RoundingMode::rp;
  }
  if constexpr (std::same_as<Form, exec_ir::Fma::F32x2> ||
                std::same_as<Form, exec_ir::Fma::MixedF32F16> ||
                std::same_as<Form, exec_ir::Fma::MixedF32Bf16>) {
    return floating_detail::rounding(form.rounding).has_value();
  }
  return form.rounding == exec_ir::RoundingMode::rn;
}

/** @brief Evaluate every homogeneous projected FMA form through fused arithmetic. */
template <fma_detail::FmaForm Form, typename T>
  requires fma_detail::HomogeneousFma<Form, T>
auto fma(const arith::context& context, const Form& form, T lhs, T rhs,
         T addend) -> std::expected<T, arith::arithmetic_error> {
  return fma_detail::evaluate(context, form, lhs, rhs, addend);
}

/** @brief Evaluate a mixed f16 or bf16 FMA into an independently typed f32 result. */
template <fma_detail::FmaForm Form, typename Low>
  requires fma_detail::MixedFma<Form, Low>
auto fma(const arith::context& context, const Form& form, Low lhs, Low rhs,
         arith::float32_t addend)
    -> std::expected<arith::float32_t, arith::arithmetic_error> {
  return fma_detail::evaluate_mixed(context, form, lhs, rhs, addend);
}

}  // namespace ptxsim::inst_execute_engine::detail::semantics
