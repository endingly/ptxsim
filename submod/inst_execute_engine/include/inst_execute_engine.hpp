#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <utility>
#include <vector>

#include <ptxsim/arith/context.hpp>
#include <ptxsim/common/ids.hpp>
#include <ptxsim/exec_ir/exec_ir.hpp>
#include <ptxsim/execution_model/cta_state.hpp>
#include <ptxsim/execution_model/warp.hpp>
#include <ptxsim/inst_execute_engine/step_outcome.hpp>
#include <ptxsim/memory/register/register_view.hpp>
#include <ptxsim/runtime/runtime.hpp>

namespace ptxsim::inst_execute_engine {

namespace detail {
/** @brief Private lane effect prepared before collective commit. */
struct PreparedLane;
}  // namespace detail

/** @brief Executes one validated PTX instruction issue against a warp. */
class InstExecuteEngine final {
 public:
  /** @brief Bind execution to one launch runtime and function register layout. */
  InstExecuteEngine(runtime::LaunchRuntime& runtime,
                    common::FunctionId function,
                    const arith::context& arithmetic) noexcept;

  /** @brief Pending collective ownership cannot be duplicated. */
  InstExecuteEngine(const InstExecuteEngine&) = delete;
  /** @brief Pending collective ownership cannot be replaced by copying. */
  InstExecuteEngine& operator=(const InstExecuteEngine&) = delete;
  /** @brief Keep the engine bound to its original launch lifetime. */
  InstExecuteEngine(InstExecuteEngine&&) = delete;
  /** @brief Do not transfer pending arrivals between launch-bound engines. */
  InstExecuteEngine& operator=(InstExecuteEngine&&) = delete;

  /** @brief Prepare and commit an issue, returning rejections or lane faults. */
  [[nodiscard]] auto execute(execution_model::Warp& warp,
                             const execution_model::WarpIssueGroup& issue,
                             const exec_ir::Instruction& instruction,
                             std::optional<common::ProgramCounter> successor)
      -> std::expected<StepReport, StepError>;

 private:
  /** One lane retained while its CTA barrier is still active. */
  struct PendingCtaBarrierLane {
    /** Stable topology thread that reached the barrier. */
    execution_model::Thread* thread;
    /** Dynamic instruction location; warp-local arrivals must agree on it. */
    common::ProgramCounter pc;
    /** Program counter installed if this lane is released. */
    common::ProgramCounter successor;
    /** Whether the lane remains blocked until global completion. */
    bool waits;
    /** Reduction input captured before the lane became non-runnable. */
    std::optional<bool> reduction_input;
    /** Preflighted frame and destination for a deferred reduction writeback. */
    std::optional<memory::RegisterView> reduction_registers;
    /** Register selected by @ref reduction_registers when present. */
    std::optional<common::RegisterSlot> reduction_destination;
  };

  /** Engine-owned metadata for one CTA barrier generation. */
  struct PendingCtaBarrier {
    /** CTA owning the architectural barrier resource. */
    execution_model::CtaId cta;
    /** Resource number within @ref cta. */
    execution_model::CtaBarrierId id;
    /** Generation number assigned by the CTA execution state. */
    std::uint64_t generation;
    /** Protocol fixed by the first arrival. */
    execution_model::CtaBarrierProtocol protocol;
    /** Explicit count fixed by the first arrival, or CTA-wide default. */
    std::uint32_t expected_threads;
    /** Whether @ref expected_threads came from an explicit PTX operand. */
    bool explicit_thread_count;
    /** Per-lane control and writeback metadata until completion. */
    std::vector<PendingCtaBarrierLane> lanes;
  };

  /** Commit one collectively prepared CTA-barrier issue. */
  auto commit_cta_barrier(execution_model::Warp& warp,
                          const execution_model::WarpIssueGroup& issue,
                          std::vector<detail::PreparedLane>& prepared,
                          StepReport& report) -> std::expected<void, StepError>;

  /** Release collectives whose remaining participants have exited. */
  void reconcile_exited_barriers(execution_model::Warp& warp,
                                 StepReport& report);

  /** Release one completed or exit-unblocked CTA barrier generation. */
  void release_cta_barrier(PendingCtaBarrier& pending,
                           execution_model::CtaBarrierSlot& slot,
                           bool released_by_exit, StepReport& report);

  /** Carry locally incomplete warp records into the generation after completion. */
  void advance_completed_cta_generation(PendingCtaBarrier& pending,
                                        execution_model::CtaBarrierSlot& slot);

  /** Compute a reduction result after exit releases an incomplete generation. */
  [[nodiscard]] static auto exited_reduction_value(
      const PendingCtaBarrier& pending) -> common::RawValue;

  /** Resume non-waiting arrival lanes from one locally converged warp. */
  static void release_arrive_warp(PendingCtaBarrier& pending,
                                  execution_model::Warp& warp);

  /** Locate one lane record for a particular dynamic barrier PC. */
  [[nodiscard]] static auto find_record(PendingCtaBarrier& pending,
                                        const execution_model::Thread& thread,
                                        common::ProgramCounter pc)
      -> std::vector<PendingCtaBarrierLane>::iterator;

  /** Locate any pending lane record owned by a specified CTA warp. */
  [[nodiscard]] static auto first_record(PendingCtaBarrier& pending,
                                         execution_model::Warp& warp)
      -> std::vector<PendingCtaBarrierLane>::iterator;

  /** Count live reduction inputs and true predicates in one CTA warp. */
  [[nodiscard]] static auto reduction_inputs(const PendingCtaBarrier& pending,
                                             const execution_model::Warp& warp)
      -> std::pair<std::uint32_t, std::uint32_t>;

  /** Return whether every non-exited lane in one warp reached the same PC. */
  [[nodiscard]] static auto warp_converged(PendingCtaBarrier& pending,
                                           execution_model::Warp& warp,
                                           common::ProgramCounter pc) -> bool;

  /** Return whether every CTA lane still live is already recorded. */
  [[nodiscard]] static auto only_exited_arrivals_remain(
      const PendingCtaBarrier& pending, const execution_model::CTA& cta)
      -> bool;

  /** Launch state borrowed by this single active launch/function engine. */
  runtime::LaunchRuntime& runtime_;
  /** Function identity selecting each thread's register frame. */
  common::FunctionId function_;
  /** Arithmetic configuration borrowed for instruction preparation. */
  const arith::context& arithmetic_;
  /** Deferred control/writeback metadata, owned for this engine lifetime. */
  std::vector<PendingCtaBarrier> pending_cta_barriers_;
};

}  // namespace ptxsim::inst_execute_engine
