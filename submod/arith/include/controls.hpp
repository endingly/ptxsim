#pragma once
#include <ptxsim/arith/types.hpp>
namespace ptxsim::arith {
enum class rounding_mode {
  nearest_even,
  toward_zero,
  toward_negative,
  toward_positive,
  nearest_away,
  stochastic
};
enum class subnormal_mode {
  preserve,
  flush_input,
  flush_output,
  flush_input_and_output
};
enum class saturation_mode { none, type_range, zero_to_one, finite };
enum class activation_mode { none, relu };
enum class approximation_mode { exact, ptx_approximate, ptx_full };
enum class minmax_nan_mode { number, propagate };
enum class floating_test { finite, infinite, number, nan, normal, subnormal };
enum class product_part { low, high, wide };
enum class integer_overflow_mode { wrap, saturate };
enum class comparison_relation {
  equal,
  not_equal,
  less,
  less_equal,
  greater,
  greater_equal,
  number,
  nan
};
enum class nan_comparison_mode { ordered, unordered };
struct integer_control {
  integer_overflow_mode overflow = integer_overflow_mode::wrap;
};
struct product_control {
  product_part part = product_part::low;
  integer_overflow_mode overflow = integer_overflow_mode::wrap;
};
struct floating_control {
  rounding_mode rounding = rounding_mode::nearest_even;
  subnormal_mode subnormal = subnormal_mode::preserve;
  saturation_mode saturation = saturation_mode::none;
  activation_mode activation = activation_mode::none;
};
struct minmax_control {
  minmax_nan_mode nan = minmax_nan_mode::number;
  bool absolute = false;
  bool xor_sign = false;
};
struct conversion_control {
  rounding_mode rounding = rounding_mode::nearest_even;
  subnormal_mode source_subnormal = subnormal_mode::preserve;
  subnormal_mode destination_subnormal = subnormal_mode::preserve;
  saturation_mode saturation = saturation_mode::none;
  activation_mode activation = activation_mode::none;
};
struct special_function_control {
  approximation_mode approximation = approximation_mode::exact;
  subnormal_mode subnormal = subnormal_mode::preserve;
};
struct comparison_control {
  comparison_relation relation = comparison_relation::equal;
  nan_comparison_mode nan = nan_comparison_mode::ordered;
};
struct stochastic_rounding_input {
  bits32_t random_bits{};
};

}  // namespace ptxsim::arith
