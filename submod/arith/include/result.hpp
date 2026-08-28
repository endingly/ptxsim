#pragma once
namespace ptxsim::arith {
struct no_status {};
struct floating_status {
  bool invalid{}, divide_by_zero{}, overflow{}, underflow{}, inexact{};
  // PTX leaves approximate results target/model dependent.
  bool model_dependent{};
};
struct integer_status {
  bool carry{}, borrow{}, overflow{};
};
struct tensor_status {
  bool model_dependent{}, inexact{};
  // Integer MMA reports arithmetic range loss independently from floating
  // rounding.  In particular, an integer overflow is never an `inexact`
  // event.
  bool overflow{}, saturated{};
  bool invalid{}, underflow{};
};
template <typename Value, typename Status = no_status>
struct result {
  Value value;
  Status status{};
};
}  // namespace ptxsim::arith
