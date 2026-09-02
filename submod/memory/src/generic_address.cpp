#include <ptxsim/memory/address_space/generic_address.hpp>

namespace ptxsim::memory {

auto resolve(GenericAddress address, const ExecutionAddressContext& context)
    -> std::expected<ResolvedAddress, AddressResolutionError> {
  if (address.value < GenericAddressLayout::constant_base) {
    if (!context.global) {
      return std::unexpected(
          AddressResolutionError{AddressResolutionErrorCode::missing_binding,
                                 address, StateSpace::Global});
    }
    return ResolvedAddress{StateSpace::Global, *context.global,
                           Address{address.value}};
  }

  if (address.value < GenericAddressLayout::entry_parameter_base) {
    if (!context.constant) {
      return std::unexpected(
          AddressResolutionError{AddressResolutionErrorCode::missing_binding,
                                 address, StateSpace::Constant});
    }
    return ResolvedAddress{
        StateSpace::Constant, *context.constant,
        Address{address.value - GenericAddressLayout::constant_base}};
  }

  if (address.value < GenericAddressLayout::local_base) {
    if (!context.entry_parameter) {
      return std::unexpected(
          AddressResolutionError{AddressResolutionErrorCode::missing_binding,
                                 address, StateSpace::Parameter});
    }
    return ResolvedAddress{
        StateSpace::Parameter, *context.entry_parameter,
        Address{address.value - GenericAddressLayout::entry_parameter_base}};
  }

  if (address.value < GenericAddressLayout::shared_base) {
    if (!context.local) {
      return std::unexpected(
          AddressResolutionError{AddressResolutionErrorCode::missing_binding,
                                 address, StateSpace::Local});
    }
    return ResolvedAddress{
        StateSpace::Local, *context.local,
        Address{address.value - GenericAddressLayout::local_base}};
  }

  if (address.value < GenericAddressLayout::unmapped_base) {
    if (!context.shared) {
      return std::unexpected(
          AddressResolutionError{AddressResolutionErrorCode::missing_binding,
                                 address, StateSpace::Shared});
    }
    return ResolvedAddress{
        StateSpace::Shared, *context.shared,
        Address{address.value - GenericAddressLayout::shared_base}};
  }

  return std::unexpected(AddressResolutionError{
      AddressResolutionErrorCode::unmapped_address, address, std::nullopt});
}

}  // namespace ptxsim::memory
