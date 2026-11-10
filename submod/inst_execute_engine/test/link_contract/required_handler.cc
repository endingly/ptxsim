#include "group_execution.hpp"
#include <ptxsim/exec_ir/model/control_flow/exit.gen.hpp>

namespace ptxsim::inst_execute_engine::detail {
/** @brief Supply precisely the required fixture symbol; the link fixture does not execute it. */
auto handler_required_contract_ExitBare(runtime::LaunchRuntime&,common::FunctionId,
    const arith::context&,execution_model::Warp&,const execution_model::WarpIssueGroup&,
    const exec_ir::ExitBare&,SuccessorContext) -> std::expected<PreparedIssue,ExecutionError> {
  return PreparedIssue{PrepareKind::scalar,{}, {}};
}
}
