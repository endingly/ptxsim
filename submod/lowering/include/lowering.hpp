#pragma once

#include <expected>
#include <string>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_ast.hpp>

#include <ptxsim/lowering/diagnostic.hpp>
#include <ptxsim/program/program_image.hpp>

namespace ptxsim::lowering {

// The syntax module supplies statement placement and source ranges.  Semantic
// operands and instruction variants are read exclusively from resolved.
[[nodiscard]] auto lower_module(
    const ptx_frontend::syntax_ast::AstModule& ast,
    const ptx_frontend::resolved_ir::ResolvedModule& resolved,
    std::string source_file)
    -> std::expected<program::ProgramImage, LoweringDiagnostic>;

}  // namespace ptxsim::lowering
