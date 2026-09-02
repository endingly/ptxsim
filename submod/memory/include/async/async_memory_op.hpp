#pragma once

#include <cstddef>
#include <variant>

#include <ptxsim/memory/address_space/address_space_manager.hpp>
#include <ptxsim/memory/core/address.hpp>

namespace ptxsim::memory {

/**
 * @brief Snapshot-based byte copy between two resolved address-space views.
 *
 * Source bytes must be initialized. The destination enforces its normal
 * runtime write permission. Snapshotting the source before writing preserves
 * overlap safety.
 */
struct CopyOp {
  ConstAddressSpaceView source;
  Address source_offset;
  AddressSpaceView destination;
  Address destination_offset;
  std::size_t size;
};

/**
 * @brief Descriptor for one asynchronously progressed memory operation.
 */
using AsyncMemoryOp = std::variant<CopyOp>;

}  // namespace ptxsim::memory
