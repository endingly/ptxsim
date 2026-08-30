#include <string>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

#include <ptxsim/lowering/lowering.hpp>

int main() {
  const std::string source = R"ptx(
.version 8.0
.target sm_80
.address_size 64
.visible .entry kernel() { .reg .u32 %r0; mov.u32 %r0, 1; }
)ptx";
  ptx_frontend::PtxSyntaxParser parser(source);
  const auto ast = parser.parseModule();
  if (!ast)
    return 1;
  const auto resolved = ptx_frontend::resolved_ir::resolveModule(*ast);
  if (!resolved)
    return 2;
  const auto image =
      ptxsim::lowering::lower_module(*ast, *resolved, "consumer.ptx");
  if (!image || !ptxsim::program::verify(*image))
    return 3;
  return ptxsim::program::dump(*image).empty() ? 4 : 0;
}
