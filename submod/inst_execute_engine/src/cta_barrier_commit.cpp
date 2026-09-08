#include <ptxsim/inst_execute_engine/inst_execute_engine.hpp>

#include <algorithm>
#include <cassert>
#include <concepts>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

#include <ptxsim/execution_model/cta.hpp>

#include "issue_validation.hpp"
#include "prepared_effect.hpp"

namespace ptxsim::inst_execute_engine {
namespace {

/** @brief Return the reduction value for a complete barrier generation. */
auto completed_reduction_value(
    const execution_model::CtaBarrierGeneration& state) -> common::RawValue {
  switch (state.protocol()) {
    case execution_model::CtaBarrierProtocol::ReduceAnd:
    case execution_model::CtaBarrierProtocol::ReduceOr:
      return common::RawValue::pred(state.predicate_result());
    case execution_model::CtaBarrierProtocol::ReducePopc:
      return common::RawValue::b32(state.popc_result());
    case execution_model::CtaBarrierProtocol::SyncArrive:
      return common::RawValue::b32(0U);
  }
  std::unreachable();
}

}  // namespace

/** @brief Recompute an early-exit reduction from non-exited recorded lanes. */
auto InstExecuteEngine::exited_reduction_value(const PendingCtaBarrier& pending)
    -> common::RawValue {
  bool predicate =
      pending.protocol == execution_model::CtaBarrierProtocol::ReduceAnd;
  std::uint32_t popc = 0;
  for (const auto& lane : pending.lanes) {
    if (lane.thread->exited() || !lane.reduction_input) {
      continue;
    }
    switch (pending.protocol) {
      case execution_model::CtaBarrierProtocol::ReduceAnd:
        predicate = predicate && *lane.reduction_input;
        break;
      case execution_model::CtaBarrierProtocol::ReduceOr:
        predicate = predicate || *lane.reduction_input;
        break;
      case execution_model::CtaBarrierProtocol::ReducePopc:
        popc += *lane.reduction_input ? 1U : 0U;
        break;
      case execution_model::CtaBarrierProtocol::SyncArrive:
        break;
    }
  }
  return pending.protocol == execution_model::CtaBarrierProtocol::ReducePopc
             ? common::RawValue::b32(popc)
             : common::RawValue::pred(predicate);
}

/** @brief Resume non-waiting lanes from a warp that completed local arrival. */
void InstExecuteEngine::release_arrive_warp(PendingCtaBarrier& pending,
                                            execution_model::Warp& warp) {
  for (const auto& lane : pending.lanes) {
    if (&lane.thread->warp() != &warp || lane.waits || lane.thread->exited() ||
        lane.thread->trapped()) {
      continue;
    }
    lane.thread->set_pc(lane.successor);
    lane.thread->mark_ready();
  }
}

/** @brief Return a recorded lane for @p thread, if it belongs to @p pc. */
auto InstExecuteEngine::find_record(PendingCtaBarrier& pending,
                                    const execution_model::Thread& thread,
                                    common::ProgramCounter pc)
    -> std::vector<PendingCtaBarrierLane>::iterator {
  return std::find_if(pending.lanes.begin(), pending.lanes.end(),
                      [&thread, pc](const auto& lane) {
                        return lane.thread == &thread && lane.pc == pc;
                      });
}

/** @brief Return a representative recorded lane from @p warp. */
auto InstExecuteEngine::first_record(PendingCtaBarrier& pending,
                                     execution_model::Warp& warp)
    -> std::vector<PendingCtaBarrierLane>::iterator {
  return std::find_if(
      pending.lanes.begin(), pending.lanes.end(),
      [&warp](const auto& lane) { return &lane.thread->warp() == &warp; });
}

/** @brief Count current non-exited reduction inputs in one locally converged warp. */
auto InstExecuteEngine::reduction_inputs(const PendingCtaBarrier& pending,
                                         const execution_model::Warp& warp)
    -> std::pair<std::uint32_t, std::uint32_t> {
  std::uint32_t live = 0;
  std::uint32_t truths = 0;
  for (const auto& lane : pending.lanes) {
    if (&lane.thread->warp() != &warp || lane.thread->exited()) {
      continue;
    }
    ++live;
    truths += lane.reduction_input && *lane.reduction_input ? 1U : 0U;
  }
  return {live, truths};
}

/** @brief Return true when every live lane in @p warp reached the same barrier PC. */
auto InstExecuteEngine::warp_converged(PendingCtaBarrier& pending,
                                       execution_model::Warp& warp,
                                       common::ProgramCounter pc) -> bool {
  for (const auto& thread : warp) {
    if (thread.exited()) {
      continue;
    }
    if (find_record(pending, thread, pc) == pending.lanes.end()) {
      return false;
    }
  }
  return true;
}

/** @brief Return whether unfinished arrivals can only have belonged to exited lanes. */
auto InstExecuteEngine::only_exited_arrivals_remain(
    const PendingCtaBarrier& pending, const execution_model::CTA& cta) -> bool {
  for (const auto& warp : cta) {
    for (const auto& thread : warp) {
      if (thread.exited()) {
        continue;
      }
      const auto record = std::find_if(
          pending.lanes.begin(), pending.lanes.end(),
          [&thread](const auto& lane) { return lane.thread == &thread; });
      if (record == pending.lanes.end()) {
        return false;
      }
    }
  }
  return true;
}

auto InstExecuteEngine::commit_cta_barrier(
    execution_model::Warp& warp, const execution_model::WarpIssueGroup& issue,
    std::vector<detail::PreparedLane>& prepared, StepReport& report)
    -> std::expected<void, StepError> {
  const auto first = std::find_if(prepared.begin(), prepared.end(),
                                  [](const detail::PreparedLane& lane) {
                                    return lane.effect.cta_barrier.has_value();
                                  });
  if (first == prepared.end()) {
    for (const auto& lane : prepared) {
      std::visit(
          [thread = lane.thread](const auto& control) {
            if constexpr (std::same_as<std::remove_cvref_t<decltype(control)>,
                                       common::ProgramCounter>) {
              thread->set_pc(control);
            } else {
              thread->mark_exited();
            }
          },
          lane.effect.control);
    }
    return {};
  }
  if (warp.execution_state().sync.active()) {
    return detail::step_error(StepErrorCode::collective_pending_mismatch);
  }

  const auto& request = *first->effect.cta_barrier;
  if (request.id.value >= execution_model::kCtaBarrierCount) {
    return detail::step_error(StepErrorCode::collective_invalid_mask);
  }
  const auto cta_threads = warp.cta().thread_count();
  if (cta_threads == 0 ||
      cta_threads > std::numeric_limits<std::uint32_t>::max()) {
    return detail::step_error(StepErrorCode::invalid_instruction);
  }
  const auto expected_threads = request.expected_threads.value_or(
      static_cast<std::uint32_t>(cta_threads));
  // An explicit zero is not the omitted, CTA-wide form; reject it rather than
  // inventing an architectural interpretation for sync or reduction barriers.
  if (expected_threads == 0 || expected_threads > cta_threads ||
      (request.expected_threads &&
       expected_threads % warp.architectural_warp_size() != 0U)) {
    return detail::step_error(StepErrorCode::collective_invalid_mask);
  }
  const bool reduction =
      request.protocol != execution_model::CtaBarrierProtocol::SyncArrive;
  if ((reduction && (!request.reduction_input || !request.reduction_write)) ||
      (!reduction && (request.reduction_input || request.reduction_write))) {
    return detail::step_error(StepErrorCode::invalid_instruction);
  }
  for (const auto& lane : prepared) {
    if (!lane.effect.cta_barrier) {
      continue;
    }
    const auto& other = *lane.effect.cta_barrier;
    if (other.id != request.id ||
        other.expected_threads != request.expected_threads ||
        other.protocol != request.protocol || other.waits != request.waits) {
      return detail::step_error(StepErrorCode::collective_mask_mismatch,
                                lane.thread->lane_id());
    }
    if (std::get<common::ProgramCounter>(lane.effect.control) !=
        std::get<common::ProgramCounter>(first->effect.control)) {
      return detail::step_error(StepErrorCode::collective_pending_mismatch,
                                lane.thread->lane_id());
    }
  }

  for (const auto& other_pending : pending_cta_barriers_) {
    if (other_pending.cta != warp.cta().id() ||
        other_pending.id == request.id) {
      continue;
    }
    const auto& other_slot =
        warp.cta().execution_state().barriers.barrier(other_pending.id);
    if (!other_slot.active() ||
        other_slot.current().generation() != other_pending.generation ||
        other_slot.current().warp_arrived(warp.index_in_cta())) {
      continue;
    }
    if (std::any_of(other_pending.lanes.begin(), other_pending.lanes.end(),
                    [&warp](const auto& lane) {
                      return &lane.thread->warp() == &warp;
                    })) {
      return detail::step_error(StepErrorCode::collective_pending_mismatch);
    }
  }

  auto& slot = warp.cta().execution_state().barriers.barrier(request.id);
  auto pending = std::find_if(
      pending_cta_barriers_.begin(), pending_cta_barriers_.end(),
      [&warp, &request](const PendingCtaBarrier& item) {
        return item.cta == warp.cta().id() && item.id == request.id;
      });
  if (slot.active()) {
    const auto& generation = slot.current();
    if (generation.protocol() != request.protocol ||
        generation.expected_threads() != expected_threads ||
        pending == pending_cta_barriers_.end() ||
        pending->generation != generation.generation()) {
      return detail::step_error(StepErrorCode::collective_pending_mismatch);
    }
  } else if (pending != pending_cta_barriers_.end()) {
    return detail::step_error(StepErrorCode::collective_pending_mismatch);
  }

  if (pending != pending_cta_barriers_.end()) {
    for (const auto& lane : prepared) {
      if (!lane.effect.cta_barrier) {
        continue;
      }
      if (std::any_of(pending->lanes.begin(), pending->lanes.end(),
                      [&lane](const auto& item) {
                        return item.thread == lane.thread;
                      })) {
        return detail::step_error(StepErrorCode::collective_duplicate_arrival,
                                  lane.thread->lane_id());
      }
    }
    for (const auto& item : pending->lanes) {
      if (&item.thread->warp() == &warp &&
          (item.pc != issue.pc || item.waits != request.waits ||
           item.successor !=
               std::get<common::ProgramCounter>(first->effect.control))) {
        return detail::step_error(StepErrorCode::collective_pending_mismatch);
      }
    }
  }

  const auto existing_arrivals =
      slot.active() ? slot.current().arrived_threads() : 0U;
  bool local_completion = true;
  for (const auto& thread : warp) {
    if (thread.exited()) {
      continue;
    }
    const bool arrives_now = std::any_of(
        prepared.begin(), prepared.end(), [&thread](const auto& lane) {
          return lane.thread == &thread && lane.effect.cta_barrier.has_value();
        });
    const bool arrived_before =
        pending != pending_cta_barriers_.end() &&
        find_record(*pending, thread, issue.pc) != pending->lanes.end();
    if (!arrives_now && !arrived_before) {
      local_completion = false;
      break;
    }
  }
  const auto arrival_threads =
      request.expected_threads
          ? warp.architectural_warp_size()
          : static_cast<std::uint32_t>(warp.thread_count());
  if (local_completion &&
      existing_arrivals + arrival_threads > expected_threads) {
    return detail::step_error(StepErrorCode::collective_pending_mismatch);
  }

  if (!slot.active()) {
    const auto& generation = slot.begin(expected_threads, request.protocol);
    pending = pending_cta_barriers_.emplace(
        pending_cta_barriers_.end(),
        PendingCtaBarrier{
            .cta = warp.cta().id(),
            .id = request.id,
            .generation = generation.generation(),
            .protocol = request.protocol,
            .expected_threads = expected_threads,
            .explicit_thread_count = request.expected_threads.has_value()});
  }

  for (const auto& lane : prepared) {
    if (!lane.effect.cta_barrier) {
      std::visit(
          [thread = lane.thread](const auto& control) {
            if constexpr (std::same_as<std::remove_cvref_t<decltype(control)>,
                                       common::ProgramCounter>) {
              thread->set_pc(control);
            } else {
              thread->mark_exited();
            }
          },
          lane.effect.control);
      continue;
    }
    const auto& effect = *lane.effect.cta_barrier;
    pending->lanes.push_back(PendingCtaBarrierLane{
        .thread = lane.thread,
        .pc = issue.pc,
        .successor = std::get<common::ProgramCounter>(lane.effect.control),
        .waits = effect.waits,
        .reduction_input = effect.reduction_input,
        .reduction_registers =
            effect.reduction_write
                ? std::optional{effect.reduction_write->registers}
                : std::nullopt,
        .reduction_destination =
            effect.reduction_write
                ? std::optional{effect.reduction_write->destination}
                : std::nullopt});
    lane.thread->mark_waiting(execution_model::WaitReason::CtaBarrier);
  }
  if (!local_completion) {
    return {};
  }

  auto& generation = slot.current();
  const auto warp_index = warp.index_in_cta();
  assert(!generation.warp_arrived(warp_index));
  const auto [live_threads, true_predicates] = reduction_inputs(*pending, warp);
  if (reduction) {
    generation.arrive_reduction_warp(warp_index, arrival_threads, live_threads,
                                     true_predicates);
  } else {
    generation.arrive_warp(warp_index, arrival_threads, request.waits);
  }
  if (!request.waits) {
    release_arrive_warp(*pending, warp);
  }
  if (!generation.complete()) {
    return {};
  }
  release_cta_barrier(*pending, slot, false, report);
  advance_completed_cta_generation(*pending, slot);
  if (pending->lanes.empty()) {
    pending_cta_barriers_.erase(pending);
  }
  return {};
}

void InstExecuteEngine::release_cta_barrier(
    PendingCtaBarrier& pending, execution_model::CtaBarrierSlot& slot,
    bool released_by_exit, StepReport& report) {
  const auto value =
      pending.protocol == execution_model::CtaBarrierProtocol::SyncArrive
          ? std::optional<common::RawValue>{}
          : std::optional{released_by_exit
                              ? exited_reduction_value(pending)
                              : completed_reduction_value(slot.current())};
  for (auto& lane : pending.lanes) {
    if (!lane.waits || lane.thread->exited() || lane.thread->trapped()) {
      continue;
    }
    if (!released_by_exit &&
        !slot.current().warp_arrived(lane.thread->warp().index_in_cta())) {
      continue;
    }
    if (value && lane.reduction_registers) {
      const auto width =
          lane.reduction_registers->declared_width(*lane.reduction_destination);
      if (!width) {
        report.faults.push_back(
            {lane.thread->lane_id(), width.error(), lane.thread->warp().id()});
        lane.thread->mark_trapped();
        continue;
      }
      if (*width != value->width()) {
        report.faults.push_back(
            {lane.thread->lane_id(),
             memory::RegisterError{
                 memory::RegisterErrorCode::width_mismatch,
                 {},
                 *lane.reduction_destination,
                 *width,
                 value->width(),
                 static_cast<std::size_t>(lane.reduction_destination->value())},
             lane.thread->warp().id()});
        lane.thread->mark_trapped();
        continue;
      }
      if (const auto written = lane.reduction_registers->write(
              *lane.reduction_destination, *value);
          !written) {
        report.faults.push_back({lane.thread->lane_id(), written.error(),
                                 lane.thread->warp().id()});
        lane.thread->mark_trapped();
        continue;
      }
    }
    lane.thread->set_pc(lane.successor);
    lane.thread->mark_ready();
  }
}

void InstExecuteEngine::advance_completed_cta_generation(
    PendingCtaBarrier& pending, execution_model::CtaBarrierSlot& slot) {
  const auto arrived_warps = slot.current().arrived_warps();
  std::erase_if(pending.lanes, [&arrived_warps](const auto& lane) {
    return arrived_warps.test(lane.thread->warp().index_in_cta());
  });
  slot.clear_completed();
  if (pending.lanes.empty()) {
    return;
  }
  const auto& generation =
      slot.begin(pending.expected_threads, pending.protocol);
  pending.generation = generation.generation();
}

void InstExecuteEngine::reconcile_exited_barriers(execution_model::Warp& warp,
                                                  StepReport& report) {
  auto& sync = warp.execution_state().sync;
  if (sync.active()) {
    const auto& rendezvous = sync.pending();
    bool releasable = true;
    for (std::uint32_t index = 0; index < warp.architectural_warp_size();
         ++index) {
      const execution_model::LaneId lane{index};
      if (rendezvous.participants().test(lane) &&
          !rendezvous.arrivals().test(lane) && !warp.thread(lane).exited()) {
        releasable = false;
        break;
      }
    }
    if (releasable) {
      for (std::uint32_t index = 0; index < warp.architectural_warp_size();
           ++index) {
        const execution_model::LaneId lane{index};
        if (!rendezvous.participants().test(lane) ||
            warp.thread(lane).exited() || warp.thread(lane).trapped()) {
          continue;
        }
        auto& thread = warp.thread(lane);
        thread.set_pc(rendezvous.successor());
        thread.mark_ready();
      }
      sync.clear_exited();
    }
  }

  for (auto pending = pending_cta_barriers_.begin();
       pending != pending_cta_barriers_.end();) {
    if (pending->cta != warp.cta().id()) {
      ++pending;
      continue;
    }
    auto& slot = warp.cta().execution_state().barriers.barrier(pending->id);
    if (!slot.active() || slot.current().generation() != pending->generation) {
      pending = pending_cta_barriers_.erase(pending);
      continue;
    }
    auto& generation = slot.current();
    for (auto& cta_warp : warp.cta()) {
      const auto record = first_record(*pending, cta_warp);
      if (record == pending->lanes.end() ||
          generation.warp_arrived(cta_warp.index_in_cta()) ||
          !warp_converged(*pending, cta_warp, record->pc)) {
        continue;
      }
      const auto arrival_threads =
          pending->explicit_thread_count
              ? cta_warp.architectural_warp_size()
              : static_cast<std::uint32_t>(cta_warp.thread_count());
      const auto [live_threads, true_predicates] =
          reduction_inputs(*pending, cta_warp);
      if (live_threads == 0 || generation.arrived_threads() + arrival_threads >
                                   pending->expected_threads) {
        continue;
      }
      if (pending->protocol ==
          execution_model::CtaBarrierProtocol::SyncArrive) {
        generation.arrive_warp(cta_warp.index_in_cta(), arrival_threads,
                               record->waits);
        if (!record->waits) {
          release_arrive_warp(*pending, cta_warp);
        }
      } else {
        generation.arrive_reduction_warp(cta_warp.index_in_cta(),
                                         arrival_threads, live_threads,
                                         true_predicates);
      }
    }
    if (generation.complete()) {
      release_cta_barrier(*pending, slot, false, report);
      advance_completed_cta_generation(*pending, slot);
      if (pending->lanes.empty()) {
        pending = pending_cta_barriers_.erase(pending);
      } else {
        ++pending;
      }
      continue;
    }
    if (!only_exited_arrivals_remain(*pending, warp.cta())) {
      ++pending;
      continue;
    }
    release_cta_barrier(*pending, slot, true, report);
    slot.clear_exited();
    pending = pending_cta_barriers_.erase(pending);
  }
}

}  // namespace ptxsim::inst_execute_engine
