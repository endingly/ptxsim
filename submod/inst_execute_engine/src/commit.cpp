#include "commit.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <optional>
#include <span>

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

/** @brief Verify a deferred register write without changing architectural state. */
auto validate_write(const PreparedWrite& write)
    -> std::expected<void, memory::RegisterError> {
  const auto width = write.registers.declared_width(write.destination);
  if (!width) {
    return std::unexpected(width.error());
  }
  if (*width != write.value.width()) {
    return std::unexpected(memory::RegisterError{
        memory::RegisterErrorCode::width_mismatch,
        {},
        write.destination,
        *width,
        write.value.width(),
        static_cast<std::size_t>(write.destination.value())});
  }
  return {};
}

}  // namespace

auto commit_warp_sync(execution_model::Warp& warp,
                      const execution_model::WarpIssueGroup& issue,
                      const std::vector<PreparedLane>& prepared)
    -> std::expected<void, StepError> {
  const auto first_lane = std::find_if(
      prepared.begin(), prepared.end(), [](const PreparedLane& lane) {
        return lane.effect.warp_sync.has_value();
      });
  if (first_lane == prepared.end()) {
    for (const auto& lane : prepared) {
      apply_control(*lane.thread, lane.effect.control);
    }
    return {};
  }
  const auto& first = first_lane->effect.warp_sync;
  assert(first.has_value());
  execution_model::LaneMask arrivals{warp.architectural_warp_size()};
  for (const auto& lane : prepared) {
    if (lane.effect.warp_sync) {
      arrivals.set(lane.thread->lane_id());
    }
  }
  auto participants = participant_mask(warp, first->membermask);
  if (!participants || participants->none() ||
      !participants->contains(arrivals)) {
    return step_error(StepErrorCode::collective_invalid_mask);
  }
  participants->intersect(warp.valid_mask());
  if (participants->none()) {
    return step_error(StepErrorCode::collective_invalid_mask);
  }
  for (const auto& lane : prepared) {
    if (lane.effect.warp_sync &&
        lane.effect.warp_sync->membermask != first->membermask) {
      return step_error(StepErrorCode::collective_mask_mismatch,
                        lane.thread->lane_id());
    }
  }

  auto& sync = warp.execution_state().sync;
  if (sync.active()) {
    const auto& pending = sync.pending();
    if (pending.pc() != issue.pc || pending.participants() != *participants ||
        pending.successor() !=
            std::get<common::ProgramCounter>(first_lane->effect.control)) {
      return step_error(StepErrorCode::collective_pending_mismatch);
    }
    if ((pending.arrivals() & arrivals).any()) {
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
      if (!thread.ready() && !thread.exited()) {
        return step_error(StepErrorCode::collective_unreachable_participant,
                          lane);
      }
    }
    sync.begin(issue.pc, *participants,
               std::get<common::ProgramCounter>(first_lane->effect.control));
  }

  auto& pending = sync.pending();
  pending.arrive(arrivals);
  bool complete = pending.complete();
  if (!complete) {
    complete = true;
    for (std::uint32_t index = 0; index < warp.architectural_warp_size();
         ++index) {
      const execution_model::LaneId lane{index};
      if (pending.participants().test(lane) && !pending.arrivals().test(lane) &&
          !warp.thread(lane).exited()) {
        complete = false;
        break;
      }
    }
  }
  if (!complete) {
    for (const auto& lane : prepared) {
      if (lane.effect.warp_sync) {
        lane.thread->mark_waiting(execution_model::WaitReason::WarpSync);
      } else {
        apply_control(*lane.thread, lane.effect.control);
      }
    }
    return {};
  }
  const auto successor = pending.successor();
  const bool naturally_complete = pending.complete();
  for (std::uint32_t index = 0; index < warp.architectural_warp_size();
       ++index) {
    const execution_model::LaneId lane{index};
    if (!pending.participants().test(lane) || warp.thread(lane).exited()) {
      continue;
    }
    auto& thread = warp.thread(lane);
    thread.set_pc(successor);
    thread.mark_ready();
  }
  if (naturally_complete) {
    sync.clear_completed();
  } else {
    sync.clear_exited();
  }
  return {};
}

void trap_faulted_lanes(execution_model::Warp& warp, const StepReport& report) {
  for (const auto& fault : report.faults) {
    if (!fault.warp || *fault.warp == warp.id()) {
      warp.thread(fault.lane).mark_trapped();
    }
  }
}

void commit_scalar(execution_model::Warp& warp,
                   std::vector<PreparedLane>& prepared, StepReport& report) {
  for (auto& lane : prepared) {
    std::array<PreparedWrite*, 8> writes{};
    for (std::size_t index = 0; index < lane.effect.writes.size(); ++index) {
      if (lane.effect.writes[index]) {
        writes[index] = &*lane.effect.writes[index];
      }
    }
    bool register_fault = false;
    for (const auto* write : writes) {
      if (write == nullptr) {
        continue;
      }
      if (const auto valid = validate_write(*write); !valid) {
        report.faults.push_back({lane.thread->lane_id(), valid.error()});
        register_fault = true;
        break;
      }
    }
    if (register_fault) {
      continue;
    }
    for (auto* write : writes) {
      if (write == nullptr) {
        continue;
      }
      if (const auto result =
              write->registers.write(write->destination, write->value);
          !result) {
        report.faults.push_back({lane.thread->lane_id(), result.error()});
        register_fault = true;
        break;
      }
    }
    if (register_fault) {
      continue;
    }
    if (lane.effect.memory_write) {
      auto& write = *lane.effect.memory_write;
      if (const auto result = write.space.write(
              write.address, std::span{write.value}.first(write.size),
              write.alignment);
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
