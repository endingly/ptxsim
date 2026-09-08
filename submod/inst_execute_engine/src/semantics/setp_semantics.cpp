#include "setp_semantics.hpp"

namespace ptxsim::inst_execute_engine::detail::semantics {

auto combine(exec_ir::BooleanOperator boolean, bool comparison, bool predicate)
    -> std::expected<bool, arith::arithmetic_error> {
  switch (boolean) {
    case exec_ir::BooleanOperator::and_:
      return comparison && predicate;
    case exec_ir::BooleanOperator::or_:
      return comparison || predicate;
    case exec_ir::BooleanOperator::xor_:
      return comparison != predicate;
  }
  return std::unexpected(arith::arithmetic_error::unsupported_operation);
}

}  // namespace ptxsim::inst_execute_engine::detail::semantics
