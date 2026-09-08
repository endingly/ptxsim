#pragma once

#include <cstdint>
#include <expected>

#include <ptxsim/arith/error.hpp>
#include <ptxsim/arith/scalar.hpp>
#include <ptxsim/exec_ir/exec_ir.hpp>

namespace ptxsim::inst_execute_engine::detail::semantics {

/** @brief Validate the fixed round-to-nearest f32 multiplication control. */
auto valid_mul(const exec_ir::Mul::RnF32& form) noexcept -> bool;
/** @brief Low unsigned multiplication has no dynamic arithmetic controls. */
auto valid_mul(const exec_ir::Mul::LoU32&) noexcept -> bool;
/** @brief High unsigned multiplication has no dynamic arithmetic controls. */
auto valid_mul(const exec_ir::Mul::HiU32&) noexcept -> bool;
/** @brief Wide unsigned multiplication has no dynamic arithmetic controls. */
auto valid_mul(const exec_ir::Mul::WideU32&) noexcept -> bool;
/** @brief Wide signed multiplication has no dynamic arithmetic controls. */
auto valid_mul(const exec_ir::Mul::WideS32&) noexcept -> bool;

/** @brief Evaluate f32 multiplication with the projected rounding mode. */
auto mul(const arith::context& context, const exec_ir::Mul::RnF32& form,
         arith::float32_t lhs, arith::float32_t rhs)
    -> std::expected<arith::float32_t, arith::arithmetic_error>;

/** @brief Evaluate the low 32 bits of an unsigned 32-bit product. */
auto mul(const arith::context& context, const exec_ir::Mul::LoU32& form,
         std::uint32_t lhs, std::uint32_t rhs)
    -> std::expected<std::uint32_t, arith::arithmetic_error>;

/** @brief Evaluate the high 32 bits of an unsigned 32-bit product. */
auto mul(const arith::context& context, const exec_ir::Mul::HiU32& form,
         std::uint32_t lhs, std::uint32_t rhs)
    -> std::expected<std::uint32_t, arith::arithmetic_error>;

/** @brief Evaluate a full-width unsigned 32-bit product. */
auto mul(const arith::context& context, const exec_ir::Mul::WideU32& form,
         std::uint32_t lhs, std::uint32_t rhs)
    -> std::expected<std::uint64_t, arith::arithmetic_error>;

/** @brief Evaluate a full-width signed 32-bit product. */
auto mul(const arith::context& context, const exec_ir::Mul::WideS32& form,
         std::int32_t lhs, std::int32_t rhs)
    -> std::expected<std::int64_t, arith::arithmetic_error>;

}  // namespace ptxsim::inst_execute_engine::detail::semantics
