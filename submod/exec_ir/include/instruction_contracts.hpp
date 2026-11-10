#pragma once

#include <cstdint>

namespace ptxsim::exec_ir {
/** @brief Exact implicit condition-code effect under the execution predicate. */
enum class ConditionCodeEffect : std::uint8_t {
  NONE, CARRY_OUT, CARRY_IN, CARRY_IN_OUT, BORROW_OUT, BORROW_IN, BORROW_IN_OUT
};
/** @brief Instruction-local asynchronous completion identity. */
enum class AsyncCompletionKind : std::uint8_t {
  NONE, ASYNC_GROUP, BULK_GROUP, MBARRIER_COMPLETE_TX_BYTES,
  MBARRIER_COMPLETE_TX16B, FABRIC_READ_WAIT, WGMMA_GROUP, TCGEN_LOAD_WAIT,
  TCGEN_STORE_WAIT, TCGEN_MBARRIER_ARRIVE_ONE
};
/** @brief Programmer-selected warpgroup protocol action. */
enum class WgmmaProtocolAction : std::uint8_t {
  NONE, ISSUE, REGISTER_FENCE, COMMIT, WAIT
};
/** @brief Interpretation of historically omitted floating-point controls. */
enum class FloatingPointDefault : std::uint8_t {
  APPROXIMATE_FLUSH_TO_ZERO, APPROXIMATE, ROUND_NEAREST_EVEN
};
/** @brief Complete owned form-static contract exposed by generated lookups. */
struct FormContract;
}  // namespace ptxsim::exec_ir
