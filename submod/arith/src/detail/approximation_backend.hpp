#pragma once

#include <ptxsim/arith/controls.hpp>
#include "internal_controls.hpp"
#include "internal_result.hpp"
#include <ptxsim/arith/types.hpp>

namespace ptxsim::arith::detail {

Result<float32_t> div_approx(float32_t, float32_t, ApproximationControl);
Result<float32_t> div_full(float32_t, float32_t, ApproximationControl);
Result<float32_t> rcp_approx(float32_t, ApproximationControl);
Result<float32_t> sqrt_approx(float32_t, ApproximationControl);
Result<float32_t> rsqrt_approx(float32_t, ApproximationControl);
Result<float32_t> sin_approx(float32_t, ApproximationControl);
Result<float32_t> cos_approx(float32_t, ApproximationControl);
Result<float32_t> lg2_approx(float32_t, ApproximationControl);
Result<float32_t> ex2_approx(float32_t, ApproximationControl);
Result<float32_t> tanh_approx(float32_t, ApproximationControl);

Result<float64_t> rsqrt_approx(float64_t);
Result<float64_t> rcp_approx_ftz(float64_t);
Result<float64_t> rsqrt_approx_ftz(float64_t);

Result<float16_t> tanh_approx(float16_t, ApproximationControl);
Result<float16_t> ex2_approx(float16_t, ApproximationControl);
Result<bfloat16_t> tanh_approx(bfloat16_t, ApproximationControl);
Result<bfloat16_t> ex2_approx(bfloat16_t, ApproximationControl);

}  // namespace ptxsim::arith::detail
