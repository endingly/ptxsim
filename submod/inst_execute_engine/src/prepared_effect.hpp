#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>

#include <ptxsim/common/ids.hpp>
#include <ptxsim/common/raw_value.hpp>
#include <ptxsim/execution_model/cta_state.hpp>
#include <ptxsim/execution_model/thread.hpp>
#include <ptxsim/memory/address_space/address_space_manager.hpp>
#include <ptxsim/memory/register/register_view.hpp>

namespace ptxsim::inst_execute_engine::detail {

/** @brief One register write deferred until the scalar commit phase. */
struct PreparedWrite {
  /** Non-owning register frame view checked again when committed. */
  memory::RegisterView registers;
  /** Destination slot in @ref registers. */
  common::RegisterSlot destination;
  /** Fully evaluated value to write. */
  common::RawValue value;
};

/** @brief One lane's validated b32 membership value for a warp rendezvous. */
struct PreparedWarpSync {
  /** Raw 32-bit lane-membership bitmap read during preparation. */
  std::uint32_t membermask;
};

/** @brief One preflighted reduction destination retained until CTA release. */
struct PreparedBarrierReductionWrite {
  /** Stable non-owning frame view validated before the lane joins the barrier. */
  memory::RegisterView registers;
  /** Destination register to receive the completed CTA reduction value. */
  common::RegisterSlot destination;
};

/** @brief One lane's CTA-barrier arrival prepared without state mutation. */
struct PreparedCtaBarrier {
  /** Architectural CTA barrier resource selected by this lane. */
  execution_model::CtaBarrierId id;
  /** Explicit participating-thread count, or empty for the whole CTA. */
  std::optional<std::uint32_t> expected_threads;
  /** Active generation protocol selected by this PTX form. */
  execution_model::CtaBarrierProtocol protocol;
  /** Whether this form waits for global CTA-barrier completion. */
  bool waits;
  /** This lane's reduction input when @ref protocol is a reduction protocol. */
  std::optional<bool> reduction_input;
  /** Deferred destination for a waiting reduction form. */
  std::optional<PreparedBarrierReductionWrite> reduction_write;
};

/** @brief One validated scalar or vector store retained until the lane's commit turn. */
struct PreparedMemoryWrite {
  /** Target resource view selected during preparation. */
  memory::AddressSpaceView space;
  /** Byte offset within @ref space, not a generic virtual address. */
  memory::Address address;
  /** PTX little-endian bytes; only the first @ref size bytes are committed. */
  std::array<std::byte, 32> value;
  /** Number of initialized bytes in @ref value and in the pending access. */
  std::size_t size;
  /** Required alignment for the complete pending access. */
  std::size_t alignment;
};

/** @brief Control effect produced by a prepared scalar instruction. */
struct ExitControl {};

/** @brief Either a fallthrough/branch PC or an architectural thread exit. */
using PreparedControl = std::variant<common::ProgramCounter, ExitControl>;

/** @brief All side effects prepared for one lane before any scalar commit. */
struct PreparedEffect {
  /**
   * Register writes preflighted before any mutation, in architectural order.
   * At most eight entries are needed by the widest supported memory vector.
   */
  std::array<std::optional<PreparedWrite>, 8> writes{};
  /** Optional memory write validated during preparation. */
  std::optional<PreparedMemoryWrite> memory_write;
  /** Control state to apply after data effects commit. */
  PreparedControl control;
  /** Present only for the collective warp-sync instruction. */
  std::optional<PreparedWarpSync> warp_sync;
  /** Present only for an active CTA-barrier form. */
  std::optional<PreparedCtaBarrier> cta_barrier;
};

/** @brief Couples a prepared effect to the non-owning issued thread it affects. */
struct PreparedLane {
  /** Issued thread whose effects this record will commit. */
  execution_model::Thread* thread;
  /** Side effects produced without architectural mutation. */
  PreparedEffect effect;
};

}  // namespace ptxsim::inst_execute_engine::detail
