#include "resource_resolution.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
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

namespace {

/** @brief Decode a b32/b64 address register or b64 immediate address base. */
auto address_base(const memory::RegisterView& registers,
                  const exec_ir::Address& address)
    -> std::expected<std::uint64_t, LaneFaultCause> {
  if (const auto* slot = std::get_if<common::RegisterSlot>(&address.base)) {
    const auto value = registers.read(*slot);
    if (!value)
      return std::unexpected(LaneFaultCause{value.error()});
    if (const auto b32 = value->as_b32(); b32)
      return *b32;
    if (const auto b64 = value->as_b64(); b64)
      return *b64;
    return std::unexpected(LaneFaultCause{
        common::RawValueError{common::RawWidth::b64, value->width()}});
  }
  const auto* immediate = std::get_if<common::RawValue>(&address.base);
  if (immediate == nullptr) {
    return std::unexpected(LaneFaultCause{
        common::RawValueError{common::RawWidth::b64, common::RawWidth::b32}});
  }
  const auto value = immediate->as_b64();
  if (!value)
    return std::unexpected(LaneFaultCause{value.error()});
  return *value;
}

/** @brief Apply one raw b64 signed-direction byte offset without wraparound. */
auto apply_offset(std::uint64_t base,
                  const std::optional<exec_ir::AddressOffset>& offset)
    -> std::expected<std::uint64_t, LaneFaultCause> {
  if (!offset)
    return base;
  const auto magnitude = offset->value.as_b64();
  if (!magnitude)
    return std::unexpected(LaneFaultCause{magnitude.error()});
  if (offset->subtract) {
    if (*magnitude > base)
      return std::unexpected(LaneFaultCause{memory::AddressResolutionError{
          memory::AddressResolutionErrorCode::unmapped_address,
          memory::GenericAddress{base}, std::nullopt}});
    return base - *magnitude;
  }
  if (*magnitude > std::numeric_limits<std::uint64_t>::max() - base) {
    return std::unexpected(LaneFaultCause{memory::AddressResolutionError{
        memory::AddressResolutionErrorCode::unmapped_address,
        memory::GenericAddress{base}, std::nullopt}});
  }
  return base + *magnitude;
}

}  // namespace

auto numeric_address(const memory::RegisterView& registers,
                     const exec_ir::Address& address)
    -> std::expected<std::uint64_t, LaneFaultCause> {
  const auto base = address_base(registers, address);
  if (!base)
    return std::unexpected(base.error());
  return apply_offset(*base, address.offset);
}

auto LaneResourceResolver::resolve_memory(exec_ir::AddressSpace space,
                                          const exec_ir::Address& address,
                                          std::size_t size)
    -> std::expected<std::pair<memory::AddressSpaceView, memory::Address>,
                     LaneFaultCause> {
  const auto registers = resolve();
  if (!registers) {
    return std::unexpected(registers.error());
  }
  const auto value = numeric_address(registers->get(), address);
  if (!value)
    return std::unexpected(value.error());
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
      if (size == 32 && !std::holds_alternative<memory::GlobalSpaceHandle>(
                            resolved->resource)) {
        return std::unexpected(LaneFaultCause{memory::AddressResolutionError{
            memory::AddressResolutionErrorCode::unmapped_address,
            memory::GenericAddress{*value}, resolved->space}});
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
    case exec_ir::AddressSpace::const_: {
      const auto handle = runtime_.constant();
      if (!handle)
        return std::unexpected(LaneFaultCause{handle.error()});
      const auto view = runtime_.address_spaces().view(*handle);
      if (!view)
        return std::unexpected(LaneFaultCause{view.error()});
      return std::pair{*view, memory::Address{*value}};
    }
    case exec_ir::AddressSpace::local: {
      const auto handle = runtime_.local_frame(thread_.id(), function_);
      if (!handle)
        return std::unexpected(LaneFaultCause{handle.error()});
      const auto view = runtime_.address_spaces().view(*handle);
      if (!view)
        return std::unexpected(LaneFaultCause{view.error()});
      return std::pair{*view, memory::Address{*value}};
    }
    case exec_ir::AddressSpace::shared: {
      const auto handle = runtime_.shared(thread_.cta().id());
      if (!handle)
        return std::unexpected(LaneFaultCause{handle.error()});
      const auto view = runtime_.address_spaces().view(*handle);
      if (!view)
        return std::unexpected(LaneFaultCause{view.error()});
      return std::pair{*view, memory::Address{*value}};
    }
    case exec_ir::AddressSpace::param:
    case exec_ir::AddressSpace::param_entry: {
      const auto handle = runtime_.entry_parameter();
      if (!handle)
        return std::unexpected(LaneFaultCause{handle.error()});
      const auto view = runtime_.address_spaces().view(*handle);
      if (!view)
        return std::unexpected(LaneFaultCause{view.error()});
      return std::pair{*view, memory::Address{*value}};
    }
    case exec_ir::AddressSpace::param_func:
      break;
  }
  return std::unexpected(LaneFaultCause{memory::AddressResolutionError{
      memory::AddressResolutionErrorCode::unmapped_address,
      memory::GenericAddress{*value}, std::nullopt}});
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

}  // namespace ptxsim::inst_execute_engine::detail
