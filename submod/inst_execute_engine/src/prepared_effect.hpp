#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>

#include <ptxsim/common/ids.hpp>
#include <ptxsim/common/raw_value.hpp>
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

/** @brief One validated scalar store retained until the lane's commit turn. */
struct PreparedMemoryWrite {
  /** Target resource view selected during preparation. */
  memory::AddressSpaceView space;
  /** Byte offset within @ref space, not a generic virtual address. */
  memory::Address address;
  /** Four bytes serialized in PTX little-endian order. */
  std::array<std::byte, 4> value;
};

/** @brief Control effect produced by a prepared scalar instruction. */
struct ExitControl {};

/** @brief Either a fallthrough/branch PC or an architectural thread exit. */
using PreparedControl = std::variant<common::ProgramCounter, ExitControl>;

/** @brief All side effects prepared for one lane before any scalar commit. */
struct PreparedEffect {
  /** Optional register write applied before a pending memory write. */
  std::optional<PreparedWrite> write;
  /**
   * Optional second register write preflighted with @ref write before either
   * mutation; when both destinations alias, this write deterministically wins.
   */
  std::optional<PreparedWrite> second_write;
  /** Optional memory write validated during preparation. */
  std::optional<PreparedMemoryWrite> memory_write;
  /** Control state to apply after data effects commit. */
  PreparedControl control;
  /** Present only for the collective warp-sync instruction. */
  std::optional<PreparedWarpSync> warp_sync;
};

/** @brief Couples a prepared effect to the non-owning issued thread it affects. */
struct PreparedLane {
  /** Issued thread whose effects this record will commit. */
  execution_model::Thread* thread;
  /** Side effects produced without architectural mutation. */
  PreparedEffect effect;
};

}  // namespace ptxsim::inst_execute_engine::detail
