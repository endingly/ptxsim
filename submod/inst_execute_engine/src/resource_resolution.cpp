#include "resource_resolution.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

#include <ptxsim/memory/address_space/generic_address.hpp>

namespace ptxsim::inst_execute_engine::detail {

LaneResourceResolver::LaneResourceResolver(runtime::LaunchRuntime& runtime,
                                           execution_model::Thread& thread,
                                           common::FunctionId function) noexcept
    : runtime_(runtime), thread_(thread), function_(function) {}

auto LaneResourceResolver::resolve()
    -> std::expected<std::reference_wrapper<const memory::RegisterView>,
                     LaneFaultCause> {
  if (!registers_) {
    const auto frame = runtime_.register_frame(thread_.id(), function_);
    if (!frame) {
      return std::unexpected(LaneFaultCause{frame.error()});
    }
    const auto view = runtime_.registers().view(*frame);
    if (!view) {
      return std::unexpected(LaneFaultCause{view.error()});
    }
    registers_ = *view;
  }
  return std::cref(*registers_);
}

auto LaneResourceResolver::thread() const noexcept
    -> const execution_model::Thread& {
  return thread_;
}

auto LaneResourceResolver::resolve_memory(exec_ir::AddressSpace space,
                                          common::RegisterSlot address)
    -> std::expected<std::pair<memory::AddressSpaceView, memory::Address>,
                     LaneFaultCause> {
  const auto registers = resolve();
  if (!registers) {
    return std::unexpected(registers.error());
  }
  const auto raw = registers->get().read(address);
  if (!raw) {
    return std::unexpected(LaneFaultCause{raw.error()});
  }
  const auto value = raw->as_b64();
  if (!value) {
    return std::unexpected(LaneFaultCause{value.error()});
  }
  switch (space) {
    case exec_ir::AddressSpace::global: {
      const auto global = runtime_.global();
      if (!global) {
        return std::unexpected(LaneFaultCause{global.error()});
      }
      const auto view = runtime_.address_spaces().view(*global);
      if (!view) {
        return std::unexpected(LaneFaultCause{view.error()});
      }
      return std::pair{*view, memory::Address{*value}};
    }
    case exec_ir::AddressSpace::generic: {
      const auto context = runtime_.address_context(thread_.id(), function_);
      if (!context) {
        return std::unexpected(LaneFaultCause{context.error()});
      }
      const auto resolved =
          memory::resolve(memory::GenericAddress{*value}, *context);
      if (!resolved) {
        return std::unexpected(LaneFaultCause{resolved.error()});
      }
      const auto view = std::visit(
          [this]<memory::AddressSpaceHandleType Handle>(const Handle& handle)
              -> std::expected<memory::AddressSpaceView,
                               memory::AddressSpaceError> {
            return runtime_.address_spaces().view(handle);
          },
          resolved->resource);
      if (!view) {
        return std::unexpected(LaneFaultCause{view.error()});
      }
      return std::pair{*view, resolved->region_address};
    }
  }
  return std::unexpected(LaneFaultCause{memory::AddressResolutionError{
      memory::AddressResolutionErrorCode::unmapped_address,
      memory::GenericAddress{*value}, std::nullopt}});
}

auto LaneResourceResolver::resolve_entry_parameter(std::uint64_t address)
    -> std::expected<std::pair<memory::AddressSpaceView, memory::Address>,
                     LaneFaultCause> {
  const auto parameter = runtime_.entry_parameter();
  if (!parameter) {
    return std::unexpected(LaneFaultCause{parameter.error()});
  }
  const auto view = runtime_.address_spaces().view(*parameter);
  if (!view) {
    return std::unexpected(LaneFaultCause{view.error()});
  }
  return std::pair{*view, memory::Address{address}};
}

auto b32_operand(const memory::RegisterView& registers,
                 const exec_ir::B32Operand& operand)
    -> std::expected<std::uint32_t, LaneFaultCause> {
  if (const auto* immediate = std::get_if<common::RawValue>(&operand)) {
    if (const auto value = immediate->as_b32(); value) {
      return *value;
    } else {
      return std::unexpected(LaneFaultCause{value.error()});
    }
  }
  const auto value = registers.read(std::get<common::RegisterSlot>(operand));
  if (!value) {
    return std::unexpected(LaneFaultCause{value.error()});
  }
  if (const auto b32 = value->as_b32(); b32) {
    return *b32;
  } else {
    return std::unexpected(LaneFaultCause{b32.error()});
  }
}

auto register_address(const exec_ir::Address& address)
    -> std::optional<common::RegisterSlot> {
  if (address.offset) {
    return std::nullopt;
  }
  if (const auto* slot = std::get_if<common::RegisterSlot>(&address.base)) {
    return *slot;
  }
  return std::nullopt;
}

auto entry_parameter_address(const exec_ir::Address& address)
    -> std::expected<std::uint64_t, LaneFaultCause> {
  if (address.offset) {
    return std::unexpected(LaneFaultCause{
        common::RawValueError{common::RawWidth::b64, common::RawWidth::b32}});
  }
  const auto* immediate = std::get_if<common::RawValue>(&address.base);
  if (immediate == nullptr) {
    return std::unexpected(LaneFaultCause{
        common::RawValueError{common::RawWidth::b64, common::RawWidth::b32}});
  }
  const auto value = immediate->as_b64();
  if (!value) {
    return std::unexpected(LaneFaultCause{value.error()});
  }
  return *value;
}

auto b32_bytes(std::uint32_t value) -> std::array<std::byte, 4> {
  return {std::byte{static_cast<std::uint8_t>(value)},
          std::byte{static_cast<std::uint8_t>(value >> 8U)},
          std::byte{static_cast<std::uint8_t>(value >> 16U)},
          std::byte{static_cast<std::uint8_t>(value >> 24U)}};
}

auto bytes_b32(const std::array<std::byte, 4>& value) -> std::uint32_t {
  return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(value[0])) |
         (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(value[1]))
          << 8U) |
         (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(value[2]))
          << 16U) |
         (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(value[3]))
          << 24U);
}

}  // namespace ptxsim::inst_execute_engine::detail
