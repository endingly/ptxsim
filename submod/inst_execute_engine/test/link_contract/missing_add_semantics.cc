#include "instruction_preparation.gen.hpp"

/**
 * @brief Reference generated Add preparation while intentionally omitting its
 * concrete semantics definition.
 */
int main() {
  using PrepareAdd = decltype(
      &ptxsim::inst_execute_engine::detail::group_AddFloatF32);
  /** Volatile use prevents link-time collection of generated preparation. */
  volatile PrepareAdd prepare =
      &ptxsim::inst_execute_engine::detail::group_AddFloatF32;
  return prepare == nullptr;
}
