#pragma once

#include <concepts>
#include <expected>

#include <ptxsim/arith/concepts.hpp>
#include <ptxsim/arith/context.hpp>
#include <ptxsim/arith/error.hpp>
#include <ptxsim/exec_ir/exec_ir.hpp>

namespace ptxsim::inst_execute_engine::detail::semantics {

/** @brief Combine comparison and predicate values with one PTX boolean selector. */
auto combine(exec_ir::BooleanOperator boolean, bool comparison, bool predicate)
    -> std::expected<bool, arith::arithmetic_error>;

/** @brief Compare two exact integer operands with the selected PTX relation. */
template <arith::arithmetic_integer T>
auto compare(const arith::context& context,
             exec_ir::ComparisonOperator relation, T lhs, T rhs)
    -> std::expected<bool, arith::arithmetic_error> {
  static_cast<void>(context);
  switch (relation) {
    case exec_ir::ComparisonOperator::eq:
      return lhs == rhs;
    case exec_ir::ComparisonOperator::lt:
      return lhs < rhs;
    case exec_ir::ComparisonOperator::ge:
      return lhs >= rhs;
  }
  return std::unexpected(arith::arithmetic_error::unsupported_operation);
}

}  // namespace ptxsim::inst_execute_engine::detail::semantics
