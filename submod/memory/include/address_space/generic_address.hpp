#pragma once

#include <compare>
#include <cstdint>
#include <expected>
#include <optional>
#include <variant>

#include <ptxsim/memory/address_space/address_space_handle.hpp>
#include <ptxsim/memory/core/address.hpp>
#include <ptxsim/memory/core/state_space.hpp>

namespace ptxsim::memory {

/**
 * @brief A byte address in the fixed generic-address layout.
 */
struct GenericAddress {
  std::uint64_t value = 0;

  auto operator<=>(const GenericAddress&) const = default;
};

/**
 * @brief Fixed, non-configurable windows used to resolve generic addresses.
 */
struct GenericAddressLayout {
  static constexpr std::uint64_t window_size = std::uint64_t{1} << 60;
  static constexpr std::uint64_t global_base = 0;
  static constexpr std::uint64_t constant_base = window_size;
  static constexpr std::uint64_t entry_parameter_base = 2 * window_size;
  static constexpr std::uint64_t local_base = 3 * window_size;
  static constexpr std::uint64_t shared_base = 4 * window_size;
  static constexpr std::uint64_t unmapped_base = 5 * window_size;
};

/**
 * @brief Optional memory-owned handle bindings used by generic resolution.
 *
 * The context contains no manager or topology identity and does not validate
 * handle staleness or resource capacity.
 */
struct ExecutionAddressContext {
  std::optional<GlobalSpaceHandle> global;
  std::optional<ConstantSpaceHandle> constant;
  std::optional<EntryParameterHandle> entry_parameter;
  std::optional<LocalFrameHandle> local;
  std::optional<SharedSpaceHandle> shared;
};

using ResolvedResourceHandle =
    std::variant<GlobalSpaceHandle, ConstantSpaceHandle, EntryParameterHandle,
                 LocalFrameHandle, SharedSpaceHandle>;

/**
 * @brief A state-space handle and region-relative address selected by resolve.
 */
struct ResolvedAddress {
  StateSpace space;
  ResolvedResourceHandle resource;
  Address region_address;

  bool operator==(const ResolvedAddress&) const = default;
};

enum class AddressResolutionErrorCode : std::uint8_t {
  missing_binding,
  unmapped_address,
};

/**
 * @brief A generic address that could not be resolved to a bound resource.
 */
struct AddressResolutionError {
  AddressResolutionErrorCode code;
  GenericAddress address;
  std::optional<StateSpace> space;

  bool operator==(const AddressResolutionError&) const = default;
};

/**
 * @brief Purely resolve a generic address through the fixed layout.
 *
 * Staleness and capacity remain the selected address-space manager's concern.
 */
[[nodiscard]] auto resolve(GenericAddress address,
                           const ExecutionAddressContext& context)
    -> std::expected<ResolvedAddress, AddressResolutionError>;

}  // namespace ptxsim::memory
