#include "commit.hpp"

#include <cassert>
#include <cstdint>
#include <optional>
#include <utility>

#include "issue_validation.hpp"

namespace ptxsim::inst_execute_engine::detail {
namespace {

/** @brief Convert a b32 bitmap to the warp's architectural lane-set width. */
auto participant_mask(const execution_model::Warp& warp, std::uint32_t bits)
    -> std::optional<execution_model::LaneMask> {
  const auto width = warp.architectural_warp_size();
  if (width == 0 || width > 32 || (width < 32 && (bits >> width) != 0U)) {
    return std::nullopt;
  }
  execution_model::LaneMask mask{width};
  for (std::uint32_t index = 0; index < width; ++index) {
    if ((bits & (std::uint32_t{1} << index)) != 0U) {
      mask.set(execution_model::LaneId{index});
    }
  }
  return mask;
}

/** @brief Apply a prepared control state to one thread after data effects. */
struct ControlCommitter final {
  /** Thread modified by the selected control alternative. */
  execution_model::Thread& thread;

  /** @brief Commit a direct or fallthrough program counter. */
  void operator()(common::ProgramCounter pc) const { thread.set_pc(pc); }

  /** @brief Commit architectural thread exit. */
  void operator()(ExitControl) const { thread.mark_exited(); }
};

/** @brief Dispatch one prepared control effect to its thread mutation. */
void apply_control(execution_model::Thread& thread,
                   const PreparedControl& control) {
  std::visit(ControlCommitter{thread}, control);
}

}  // namespace

auto commit_warp_sync(execution_model::Warp& warp,
                      const execution_model::WarpIssueGroup& issue,
                      const std::vector<PreparedLane>& prepared)
    -> std::expected<void, StepError> {
  const auto& first = prepared.front().effect.warp_sync;
  assert(first.has_value());
  const auto participants = participant_mask(warp, first->membermask);
  if (!participants || participants->none() ||
      !participants->contains(issue.lanes) ||
      !warp.valid_mask().contains(*participants)) {
    return step_error(StepErrorCode::collective_invalid_mask);
  }
  for (const auto& lane : prepared) {
    if (!lane.effect.warp_sync ||
        lane.effect.warp_sync->membermask != first->membermask) {
      return step_error(StepErrorCode::collective_mask_mismatch,
                        lane.thread->lane_id());
    }
  }

  auto& sync = warp.execution_state().sync;
  if (sync.active()) {
    const auto& pending = sync.pending();
    if (pending.pc() != issue.pc || pending.participants() != *participants) {
      return step_error(StepErrorCode::collective_pending_mismatch);
    }
    if ((pending.arrivals() & issue.lanes).any()) {
      return step_error(StepErrorCode::collective_duplicate_arrival);
    }
  } else {
    for (std::uint32_t index = 0; index < warp.architectural_warp_size();
         ++index) {
      const execution_model::LaneId lane{index};
      if (!participants->test(lane)) {
        continue;
      }
      const auto& thread = warp.thread(lane);
      if (!thread.ready()) {
        return step_error(StepErrorCode::collective_unreachable_participant,
                          lane);
      }
    }
    sync.begin(issue.pc, *participants);
  }

  auto& pending = sync.pending();
  pending.arrive(issue.lanes);
  if (!pending.complete()) {
    for (const auto& lane : prepared) {
      lane.thread->mark_waiting(execution_model::WaitReason::WarpSync);
    }
    return {};
  }
  const auto successor =
      std::get<common::ProgramCounter>(prepared.front().effect.control);
  for (std::uint32_t index = 0; index < warp.architectural_warp_size();
       ++index) {
    const execution_model::LaneId lane{index};
    if (!pending.participants().test(lane)) {
      continue;
    }
    auto& thread = warp.thread(lane);
    thread.set_pc(successor);
    thread.mark_ready();
  }
  sync.clear_completed();
  return {};
}

void trap_faulted_lanes(execution_model::Warp& warp, const StepReport& report) {
  for (const auto& fault : report.faults) {
    warp.thread(fault.lane).mark_trapped();
  }
}

void commit_scalar(execution_model::Warp& warp,
                   std::vector<PreparedLane>& prepared, StepReport& report) {
  for (auto& lane : prepared) {
    if (lane.effect.write) {
      if (const auto write = lane.effect.write->registers.write(
              lane.effect.write->destination, lane.effect.write->value);
          !write) {
        report.faults.push_back({lane.thread->lane_id(), write.error()});
        continue;
      }
    }
    if (lane.effect.memory_write) {
      auto& write = *lane.effect.memory_write;
      if (const auto result = write.space.write(write.address, write.value, 4);
          !result) {
        report.faults.push_back({lane.thread->lane_id(), result.error()});
        continue;
      }
    }
    apply_control(*lane.thread, lane.effect.control);
  }
  trap_faulted_lanes(warp, report);
}

}  // namespace ptxsim::inst_execute_engine::detail
