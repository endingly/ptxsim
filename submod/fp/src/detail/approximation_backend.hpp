#pragma once

#include <ptxsim/fp/controls.hpp>
#include <ptxsim/fp/exceptions.hpp>
#include <ptxsim/fp/types.hpp>

namespace ptxsim::fp::detail {

Result<Fp32> div_approx(Fp32, Fp32, ApproximationControl);
Result<Fp32> div_full(Fp32, Fp32, ApproximationControl);
Result<Fp32> rcp_approx(Fp32, ApproximationControl);
Result<Fp32> sqrt_approx(Fp32, ApproximationControl);
Result<Fp32> rsqrt_approx(Fp32, ApproximationControl);
Result<Fp32> sin_approx(Fp32, ApproximationControl);
Result<Fp32> cos_approx(Fp32, ApproximationControl);
Result<Fp32> lg2_approx(Fp32, ApproximationControl);
Result<Fp32> ex2_approx(Fp32, ApproximationControl);
Result<Fp32> tanh_approx(Fp32);

Result<Fp64> rsqrt_approx(Fp64);
Result<Fp64> rcp_approx_ftz(Fp64);
Result<Fp64> rsqrt_approx_ftz(Fp64);

Result<Fp16> tanh_approx(Fp16);
Result<Fp16> ex2_approx(Fp16);
Result<Bf16> tanh_approx(Bf16);
Result<Bf16> ex2_approx(Bf16);

}  // namespace ptxsim::fp::detail
