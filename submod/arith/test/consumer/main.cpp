#include <ptxsim/arith/arith.hpp>

int main() {
  ptxsim::arith::context context;
  const auto value = ptxsim::arith::cvt<ptxsim::arith::float32_t>(context, 1);
  return value ? 0 : 1;
}
