#include <ptxsim/arith/arith.hpp>
#include <ptxsim/common/ids.hpp>

int main() {
  ptxsim::arith::context context;
  const auto value = ptxsim::arith::cvt<ptxsim::arith::float32_t>(context, 1);
  return value && ptxsim::common::to_string(
                      ptxsim::common::ProgramCounter{7}) == "pc:7"
             ? 0
             : 1;
}
