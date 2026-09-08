#pragma once

#include <expected>

#include <ptxsim/arith/controls.hpp>
#include <ptxsim/arith/error.hpp>
#include <ptxsim/exec_ir/exec_ir.hpp>

namespace ptxsim::inst_execute_engine::detail::semantics::floating_detail {

/** @brief Map one PTX directed-rounding spelling to an arithmetic control. */
inline auto rounding(exec_ir::RoundingMode value)
    -> std::expected<arith::rounding_mode, arith::arithmetic_error> {
  switch (value) {
    case exec_ir::RoundingMode::rn:
      return arith::rounding_mode::nearest_even;
    case exec_ir::RoundingMode::rz:
      return arith::rounding_mode::toward_zero;
    case exec_ir::RoundingMode::rm:
      return arith::rounding_mode::toward_negative;
    case exec_ir::RoundingMode::rp:
      return arith::rounding_mode::toward_positive;
    case exec_ir::RoundingMode::rzi:
      return std::unexpected(arith::arithmetic_error::unsupported_rounding);
  }
  return std::unexpected(arith::arithmetic_error::unsupported_rounding);
}

/** @brief Build controls for PTX binary floating arithmetic modifiers. */
inline auto controls(exec_ir::RoundingMode value, bool ftz = false,
                     bool sat = false)
    -> std::expected<arith::floating_control, arith::arithmetic_error> {
  const auto selected_rounding = rounding(value);
  if (!selected_rounding)
    return std::unexpected(selected_rounding.error());
  return arith::floating_control{
      .rounding = *selected_rounding,
      .subnormal = ftz ? arith::subnormal_mode::flush_input_and_output
                       : arith::subnormal_mode::preserve,
      .saturation = sat ? arith::saturation_mode::zero_to_one
                        : arith::saturation_mode::none,
  };
}

/**
 * @brief Build controls for the FMA-only ReLU and OOB-NaN modifiers.
 *
 * The caller is responsible for admitting these flags only from projected FMA
 * forms; the shared arithmetic layer deliberately has no dependency on PTX IR.
 */
inline auto fma_controls(exec_ir::RoundingMode value, bool ftz = false,
                         bool sat = false, bool relu = false, bool oob = false)
    -> std::expected<arith::floating_control, arith::arithmetic_error> {
  const auto control = controls(value, ftz, sat);
  if (!control)
    return std::unexpected(control.error());
  return arith::floating_control{
      .rounding = control->rounding,
      .subnormal = control->subnormal,
      .saturation = control->saturation,
      .activation = relu ? arith::activation_mode::relu
                         : arith::activation_mode::none,
      .oob_nan = oob ? arith::oob_nan_mode::zero_result
                     : arith::oob_nan_mode::none,
  };
}

}  // namespace ptxsim::inst_execute_engine::detail::semantics::floating_detail
