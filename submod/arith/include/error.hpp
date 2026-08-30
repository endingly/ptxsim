#pragma once
namespace ptxsim::arith {
enum class arithmetic_error {
  unsupported_operation,
  unsupported_type_combination,
  unsupported_rounding,
  unsupported_subnormal_mode,
  unsupported_saturation,
  unsupported_activation,
  unsupported_minmax_modifier,
  unsupported_approximation_mode,
  unsupported_model_profile,
  unsupported_overflow_mode,
  division_by_zero,
  integer_overflow,
  invalid_stochastic_input,
  invalid_tensor_shape,
  invalid_scale_layout
};
}
