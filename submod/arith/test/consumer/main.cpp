#include <ptxsim/arith/arith.hpp>
#include <ptxsim/common/ids.hpp>
#include <ptxsim/common/raw_value.hpp>

int main() {
  ptxsim::arith::context context;
  const auto value = ptxsim::arith::cvt<ptxsim::arith::float32_t>(context, 1);
  const auto raw = ptxsim::common::RawValue::b32(std::uint32_t{7});
  return value &&
                 ptxsim::common::to_string(ptxsim::common::ProgramCounter{7}) ==
                     "pc:7" &&
                 raw.as_b32() && *raw.as_b32() == 7 &&
                 ptxsim::common::to_string(raw) == "b32:0x00000007"
             ? 0
             : 1;
}
