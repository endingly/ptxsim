#include "barrier_preparation.hpp"

#include <cstdint>

namespace ptxsim::inst_execute_engine::detail {
namespace {

/** @brief Read one scalar operand as exact b32 bits. */
auto barrier_operand(LaneResourceResolver& resolver,
                     const exec_ir::ScalarOperand& operand)
    -> std::expected<std::uint32_t, LaneFaultCause> {
  if (const auto* immediate = std::get_if<common::RawValue>(&operand)) {
    const auto value = immediate->as_b32();
    if (!value) {
      return std::unexpected(LaneFaultCause{value.error()});
    }
    return *value;
  }
  const auto registers = resolver.resolve();
  if (!registers) {
    return std::unexpected(registers.error());
  }
  return b32_operand(registers->get(), operand);
}

/** @brief Read one predicate operand, including its PTX logical inversion. */
auto barrier_predicate(LaneResourceResolver& resolver,
                       const exec_ir::Predicate& predicate)
    -> std::expected<bool, LaneFaultCause> {
  const auto registers = resolver.resolve();
  if (!registers) {
    return std::unexpected(registers.error());
  }
  const auto raw = registers->get().read(predicate.source);
  if (!raw) {
    return std::unexpected(LaneFaultCause{raw.error()});
  }
  const auto value = raw->as_pred();
  if (!value) {
    return std::unexpected(LaneFaultCause{value.error()});
  }
  return *value != predicate.negated;
}

/** @brief Validate a deferred reduction destination and retain its live view. */
auto barrier_destination(LaneResourceResolver& resolver,
                         common::RegisterSlot destination,
                         common::RawWidth expected)
    -> std::expected<PreparedBarrierReductionWrite, LaneFaultCause> {
  const auto registers = resolver.resolve();
  if (!registers) {
    return std::unexpected(registers.error());
  }
  const auto width = registers->get().declared_width(destination);
  if (!width) {
    return std::unexpected(LaneFaultCause{width.error()});
  }
  if (*width != expected) {
    return std::unexpected(
        LaneFaultCause{common::RawValueError{expected, *width}});
  }
  return PreparedBarrierReductionWrite{registers->get(), destination};
}

}  // namespace

auto prepare_bar_warp_sync(LaneResourceResolver& resolver,
                           const exec_ir::ScalarOperand& membermask,
                           common::ProgramCounter successor)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  const auto bits = barrier_operand(resolver, membermask);
  if (!bits) {
    return std::unexpected(bits.error());
  }
  return PreparedEffect{.memory_write = std::nullopt,
                        .control = successor,
                        .warp_sync = PreparedWarpSync{*bits}};
}

auto prepare_cta_barrier(
    LaneResourceResolver& resolver,
    execution_model::CtaBarrierProtocol protocol, bool waits,
    const exec_ir::ScalarOperand& barrier,
    std::optional<exec_ir::ScalarOperand> thread_count,
    std::optional<exec_ir::Predicate> reduction_input,
    std::optional<common::RegisterSlot> reduction_destination,
    common::ProgramCounter successor)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  const auto id = barrier_operand(resolver, barrier);
  if (!id) {
    return std::unexpected(id.error());
  }
  std::optional<std::uint32_t> expected_threads;
  if (thread_count) {
    const auto count = barrier_operand(resolver, *thread_count);
    if (!count) {
      return std::unexpected(count.error());
    }
    expected_threads = *count;
  }

  std::optional<bool> input;
  std::optional<PreparedBarrierReductionWrite> destination;
  if (protocol != execution_model::CtaBarrierProtocol::SyncArrive) {
    if (!reduction_input || !reduction_destination) {
      return std::unexpected(LaneFaultCause{common::RawValueError{
          common::RawWidth::pred, common::RawWidth::b32}});
    }
    const auto value = barrier_predicate(resolver, *reduction_input);
    if (!value) {
      return std::unexpected(value.error());
    }
    input = *value;
    const auto width =
        protocol == execution_model::CtaBarrierProtocol::ReducePopc
            ? common::RawWidth::b32
            : common::RawWidth::pred;
    const auto write =
        barrier_destination(resolver, *reduction_destination, width);
    if (!write) {
      return std::unexpected(write.error());
    }
    destination = *write;
  }

  return PreparedEffect{.memory_write = std::nullopt,
                        .control = successor,
                        .cta_barrier = PreparedCtaBarrier{
                            .id = execution_model::CtaBarrierId{*id},
                            .expected_threads = expected_threads,
                            .protocol = protocol,
                            .waits = waits,
                            .reduction_input = input,
                            .reduction_write = destination}};
}

}  // namespace ptxsim::inst_execute_engine::detail
