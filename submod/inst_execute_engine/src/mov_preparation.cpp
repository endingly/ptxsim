#include "mov_preparation.hpp"

#include <array>
#include <concepts>
#include <cstdint>
#include <limits>
#include <ranges>
#include <type_traits>
#include <utility>

#include <ptxsim/execution_model/cta.hpp>
#include <ptxsim/execution_model/grid.hpp>

namespace ptxsim::inst_execute_engine::detail {
namespace {

/** @brief Return the raw storage width represented by one movement type. */
auto movement_width(exec_ir::DataType type) noexcept
    -> std::optional<common::RawWidth> {
  switch (type) {
    case exec_ir::DataType::b16:
    case exec_ir::DataType::u16:
    case exec_ir::DataType::s16:
      return common::RawWidth::b16;
    case exec_ir::DataType::b32:
    case exec_ir::DataType::u32:
    case exec_ir::DataType::s32:
    case exec_ir::DataType::f32:
      return common::RawWidth::b32;
    case exec_ir::DataType::b64:
    case exec_ir::DataType::u64:
    case exec_ir::DataType::s64:
    case exec_ir::DataType::f64:
      return common::RawWidth::b64;
    case exec_ir::DataType::b128:
      return common::RawWidth::b128;
    default:
      return std::nullopt;
  }
}

/** @brief Return the bit count represented by a raw width. */
auto width_bits(common::RawWidth width) noexcept -> std::optional<std::uint32_t> {
  switch (width) {
    case common::RawWidth::b8:
      return 8U;
    case common::RawWidth::b16:
      return 16U;
    case common::RawWidth::b32:
      return 32U;
    case common::RawWidth::b64:
      return 64U;
    case common::RawWidth::b128:
      return 128U;
    case common::RawWidth::pred:
      return std::nullopt;
  }
  return std::nullopt;
}

/** @brief Construct the raw value for one supported non-predicate width. */
auto raw_bits(common::RawWidth width, std::uint64_t low, std::uint64_t high = 0)
    -> common::RawValue {
  switch (width) {
    case common::RawWidth::b8:
      return common::RawValue::b8(static_cast<std::uint8_t>(low));
    case common::RawWidth::b16:
      return common::RawValue::b16(static_cast<std::uint16_t>(low));
    case common::RawWidth::b32:
      return common::RawValue::b32(static_cast<std::uint32_t>(low));
    case common::RawWidth::b64:
      return common::RawValue::b64(low);
    case common::RawWidth::b128:
      return common::RawValue::b128(common::Bits128{low, high});
    case common::RawWidth::pred:
      return common::RawValue::pred(false);
  }
  return common::RawValue::pred(false);
}

/** @brief Extract the low 64 raw bits after exact width verification. */
auto low_bits(const common::RawValue& value, common::RawWidth expected)
    -> std::expected<std::uint64_t, LaneFaultCause> {
  if (value.width() != expected) {
    return std::unexpected(
        LaneFaultCause{common::RawValueError{expected, value.width()}});
  }
  switch (expected) {
    case common::RawWidth::b8:
      return *value.as_b8();
    case common::RawWidth::b16:
      return *value.as_b16();
    case common::RawWidth::b32:
      return *value.as_b32();
    case common::RawWidth::b64:
      return *value.as_b64();
    default:
      return std::unexpected(
          LaneFaultCause{common::RawValueError{expected, value.width()}});
  }
}

/** @brief Return the contiguous component slot while rejecting unsigned overflow. */
auto component_slot(common::RegisterSlot base, std::size_t component)
    -> std::optional<common::RegisterSlot> {
  if (component > std::numeric_limits<std::uint32_t>::max() - base.value()) {
    return std::nullopt;
  }
  return common::RegisterSlot{base.value() + static_cast<std::uint32_t>(component)};
}

/** @brief Describe an engine-unimplemented topology special-register reference. */
auto unsupported_special(const exec_ir::SpecialRegisterRef& source)
    -> std::unexpected<LaneFaultCause> {
  return std::unexpected(LaneFaultCause{UnsupportedSpecialRegister{
      .id = source.id, .component = source.component}});
}

/** @brief Resolve one x/y/z coordinate from a topology special-register identity. */
auto topology_component(const execution_model::Thread& thread,
                        common::SpecialRegisterId id, std::uint8_t component)
    -> std::expected<std::uint32_t, LaneFaultCause> {
  if (component > 2U) {
    return unsupported_special(exec_ir::SpecialRegisterRef{id, component});
  }
  const auto select = [component](common::Dim3 value) {
    return component == 0U ? value.x : component == 1U ? value.y : value.z;
  };
  if (id == exec_ir::kThreadIdSpecialRegister)
    return select(thread.position());
  if (id == exec_ir::kThreadCountSpecialRegister)
    return select(thread.cta().thread_shape());
  if (id == exec_ir::kCtaIdSpecialRegister)
    return select(thread.cta().position());
  if (id == exec_ir::kCtaCountSpecialRegister)
    return select(thread.grid().cta_shape());
  return unsupported_special(exec_ir::SpecialRegisterRef{id, component});
}

/** @brief Resolve a supported topology source using the instruction's exact width. */
auto topology_value(const execution_model::Thread& thread,
                    const exec_ir::SpecialRegisterRef& source,
                    common::RawWidth width)
    -> std::expected<common::RawValue, LaneFaultCause> {
  if (source.id == exec_ir::kGridIdSpecialRegister) {
    if (source.component || (width != common::RawWidth::b16 &&
                             width != common::RawWidth::b32 &&
                             width != common::RawWidth::b64)) {
      return unsupported_special(source);
    }
    return raw_bits(width, thread.grid().id().value);
  }
  if (source.id == exec_ir::kLaneMaskEqSpecialRegister ||
      source.id == exec_ir::kLaneMaskLeSpecialRegister ||
      source.id == exec_ir::kLaneMaskLtSpecialRegister ||
      source.id == exec_ir::kLaneMaskGeSpecialRegister ||
      source.id == exec_ir::kLaneMaskGtSpecialRegister) {
    if (source.component || width != common::RawWidth::b32 ||
        thread.lane_id().value >= 32U) {
      return unsupported_special(source);
    }
    const auto lane = thread.lane_id().value;
    const auto equal = std::uint32_t{1} << lane;
    const auto lower = equal - 1U;
    if (source.id == exec_ir::kLaneMaskEqSpecialRegister)
      return common::RawValue::b32(equal);
    if (source.id == exec_ir::kLaneMaskLtSpecialRegister)
      return common::RawValue::b32(lower);
    if (source.id == exec_ir::kLaneMaskLeSpecialRegister)
      return common::RawValue::b32(lower | equal);
    if (source.id == exec_ir::kLaneMaskGeSpecialRegister)
      return common::RawValue::b32(~lower);
    return common::RawValue::b32(~(lower | equal));
  }
  if (source.id == exec_ir::kWarpIdSpecialRegister) {
    if (source.component || width != common::RawWidth::b32) {
      return unsupported_special(source);
    }
    return common::RawValue::b32(thread.warp().index_in_cta());
  }
  std::uint32_t value = 0;
  if (source.id == exec_ir::kLaneIdSpecialRegister) {
    if (source.component || width != common::RawWidth::b32) {
      return unsupported_special(source);
    }
    value = thread.lane_id().value;
  } else {
    if (!source.component || width == common::RawWidth::b64 ||
        width == common::RawWidth::b128) {
      return unsupported_special(source);
    }
    const auto coordinate = topology_component(thread, source.id, *source.component);
    if (!coordinate)
      return std::unexpected(coordinate.error());
    value = *coordinate;
  }
  if (width == common::RawWidth::b16)
    return common::RawValue::b16(static_cast<std::uint16_t>(value));
  if (width == common::RawWidth::b32)
    return common::RawValue::b32(value);
  return unsupported_special(source);
}

/** @brief Read a scalar MOV source with no address or symbol fabrication. */
auto scalar_source(const memory::RegisterView& registers,
                   const execution_model::Thread& thread,
                   const exec_ir::MovSource& source, common::RawWidth width)
    -> std::expected<common::RawValue, LaneFaultCause> {
  if (const auto* slot = std::get_if<common::RegisterSlot>(&source)) {
    const auto value = registers.read(*slot);
    if (!value)
      return std::unexpected(LaneFaultCause{value.error()});
    if (value->width() != width) {
      return std::unexpected(
          LaneFaultCause{common::RawValueError{width, value->width()}});
    }
    return *value;
  }
  if (const auto* immediate = std::get_if<common::RawValue>(&source)) {
    if (immediate->width() != width) {
      return std::unexpected(
          LaneFaultCause{common::RawValueError{width, immediate->width()}});
    }
    return *immediate;
  }
  if (const auto* special = std::get_if<exec_ir::SpecialRegisterRef>(&source))
    return topology_value(thread, *special, width);
  if (const auto* address = std::get_if<exec_ir::Address>(&source)) {
    if (std::holds_alternative<exec_ir::SymbolRef>(address->base)) {
      return std::unexpected(LaneFaultCause{UnsupportedMovSource{}});
    }
    const auto numeric = numeric_address(registers, *address);
    if (!numeric)
      return std::unexpected(numeric.error());
    return raw_bits(width, *numeric);
  }
  return std::unexpected(LaneFaultCause{UnsupportedMovSource{}});
}

/** @brief Verify one scalar destination's exact raw storage width. */
auto valid_destination(const memory::RegisterView& registers,
                       common::RegisterSlot destination, common::RawWidth width)
    -> std::expected<void, LaneFaultCause> {
  const auto actual = registers.declared_width(destination);
  if (!actual)
    return std::unexpected(LaneFaultCause{actual.error()});
  if (*actual != width) {
    return std::unexpected(LaneFaultCause{common::RawValueError{width, *actual}});
  }
  return {};
}

/** @brief Return an exact component width for a 2- or 4-element aggregate. */
auto component_width(common::RawWidth aggregate, std::size_t count)
    -> std::optional<common::RawWidth> {
  const auto bits = width_bits(aggregate);
  if (!bits || count == 0U || *bits % count != 0U)
    return std::nullopt;
  switch (*bits / count) {
    case 8U:
      return common::RawWidth::b8;
    case 16U:
      return common::RawWidth::b16;
    case 32U:
      return common::RawWidth::b32;
    case 64U:
      return common::RawWidth::b64;
    default:
      return std::nullopt;
  }
}

/** @brief Stage a validated sequence of writes with one shared control successor. */
auto staged_writes(const memory::RegisterView& registers,
                   const std::array<std::optional<common::RegisterSlot>, 4>& destinations,
                   const std::array<common::RawValue, 4>& values,
                   std::size_t count, common::ProgramCounter successor)
    -> PreparedEffect {
  PreparedEffect effect{.memory_write = std::nullopt, .control = successor};
  for (std::size_t index = 0; index < count; ++index) {
    if (destinations[index]) {
      effect.writes[index] =
          PreparedWrite{registers, *destinations[index], values[index]};
    }
  }
  return effect;
}

}  // namespace

auto valid_movement_type(exec_ir::DataType type) noexcept -> bool {
  return movement_width(type).has_value();
}

auto valid_scalar_move_source(const exec_ir::MovSource& source,
                              exec_ir::DataType type) noexcept -> bool {
  const auto is_address_integer = [type] {
    return type == exec_ir::DataType::b32 || type == exec_ir::DataType::u32 ||
           type == exec_ir::DataType::s32 || type == exec_ir::DataType::b64 ||
           type == exec_ir::DataType::u64 || type == exec_ir::DataType::s64;
  };
  if (std::holds_alternative<exec_ir::Address>(source) ||
      std::holds_alternative<exec_ir::SymbolRef>(source)) {
    return is_address_integer();
  }
  if (std::holds_alternative<exec_ir::FunctionRef>(source)) {
    return type == exec_ir::DataType::u32 || type == exec_ir::DataType::s32 ||
           type == exec_ir::DataType::u64 || type == exec_ir::DataType::s64;
  }
  const auto* special = std::get_if<exec_ir::SpecialRegisterRef>(&source);
  if (special == nullptr)
    return true;
  const auto is_integer = [type](common::RawWidth width) {
    return (width == common::RawWidth::b16 &&
            (type == exec_ir::DataType::b16 || type == exec_ir::DataType::u16 ||
             type == exec_ir::DataType::s16)) ||
           (width == common::RawWidth::b32 &&
            (type == exec_ir::DataType::b32 || type == exec_ir::DataType::u32 ||
             type == exec_ir::DataType::s32)) ||
           (width == common::RawWidth::b64 &&
            (type == exec_ir::DataType::b64 || type == exec_ir::DataType::u64 ||
             type == exec_ir::DataType::s64));
  };
  if (special->id == exec_ir::kThreadIdSpecialRegister ||
      special->id == exec_ir::kThreadCountSpecialRegister ||
      special->id == exec_ir::kCtaIdSpecialRegister ||
      special->id == exec_ir::kCtaCountSpecialRegister) {
    return special->component && *special->component <= 2U &&
           (is_integer(common::RawWidth::b16) ||
            is_integer(common::RawWidth::b32));
  }
  if (special->id == exec_ir::kGridIdSpecialRegister) {
    return !special->component &&
           (is_integer(common::RawWidth::b16) ||
            is_integer(common::RawWidth::b32) ||
            is_integer(common::RawWidth::b64));
  }
  if (special->id == exec_ir::kLaneIdSpecialRegister ||
      special->id == exec_ir::kWarpIdSpecialRegister ||
      special->id == exec_ir::kLaneMaskEqSpecialRegister ||
      special->id == exec_ir::kLaneMaskLeSpecialRegister ||
      special->id == exec_ir::kLaneMaskLtSpecialRegister ||
      special->id == exec_ir::kLaneMaskGeSpecialRegister ||
      special->id == exec_ir::kLaneMaskGtSpecialRegister) {
    return !special->component && is_integer(common::RawWidth::b32);
  }
  return true;
}

auto valid_aggregate_move(const exec_ir::RegisterVector& registers,
                          exec_ir::DataType type,
                          bool source_vector) noexcept -> bool {
  const auto count = registers.elements.size();
  const auto is_bit_container = type == exec_ir::DataType::b16 ||
                                type == exec_ir::DataType::b32 ||
                                type == exec_ir::DataType::b64 ||
                                type == exec_ir::DataType::b128;
  if (!is_bit_container || (count != 2U && count != 4U) ||
      (type == exec_ir::DataType::b16 && count != 2U)) {
    return false;
  }
  return source_vector
             ? std::ranges::all_of(registers.elements,
                                   [](const auto& slot) { return slot.has_value(); })
             : std::ranges::any_of(registers.elements,
                                   [](const auto& slot) { return slot.has_value(); });
}

auto prepare_scalar_move(LaneResourceResolver& resolver, exec_ir::DataType type,
                         common::RegisterSlot destination,
                         const exec_ir::MovSource& source,
                         std::optional<common::ProgramCounter> successor)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  const auto width = movement_width(type);
  if (!width)
    return std::unexpected(LaneFaultCause{UnsupportedMovType{type}});
  const auto registers = resolver.resolve();
  if (!registers)
    return std::unexpected(registers.error());
  const auto value = scalar_source(registers->get(), resolver.thread(), source, *width);
  if (!value)
    return std::unexpected(value.error());
  const auto valid = valid_destination(registers->get(), destination, *width);
  if (!valid)
    return std::unexpected(valid.error());
  return staged_writes(registers->get(), {destination, std::nullopt, std::nullopt,
                                           std::nullopt},
                       {*value, raw_bits(common::RawWidth::b8, 0),
                        raw_bits(common::RawWidth::b8, 0), raw_bits(common::RawWidth::b8, 0)},
                       1U, *successor);
}

auto prepare_pack_move(LaneResourceResolver& resolver, exec_ir::DataType type,
                       common::RegisterSlot destination,
                       const exec_ir::RegisterVector& source,
                       std::optional<common::ProgramCounter> successor)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  const auto aggregate = movement_width(type);
  if (!aggregate || (source.elements.size() != 2U && source.elements.size() != 4U))
    return std::unexpected(LaneFaultCause{UnsupportedMovType{type}});
  const auto component = component_width(*aggregate, source.elements.size());
  if (!component)
    return std::unexpected(LaneFaultCause{UnsupportedMovType{type}});
  const auto registers = resolver.resolve();
  if (!registers)
    return std::unexpected(registers.error());
  std::uint64_t low = 0;
  std::uint64_t high = 0;
  const auto bits = *width_bits(*component);
  for (std::size_t index = 0; index < source.elements.size(); ++index) {
    if (!source.elements[index])
      return std::unexpected(LaneFaultCause{InvalidMovVector{}});
    const auto raw = registers->get().read(*source.elements[index]);
    if (!raw)
      return std::unexpected(LaneFaultCause{raw.error()});
    const auto value = low_bits(*raw, *component);
    if (!value)
      return std::unexpected(value.error());
    if (index * bits < 64U)
      low |= *value << (index * bits);
    else
      high |= *value << (index * bits - 64U);
  }
  const auto valid = valid_destination(registers->get(), destination, *aggregate);
  if (!valid)
    return std::unexpected(valid.error());
  const auto packed = raw_bits(*aggregate, low, high);
  return staged_writes(registers->get(), {destination, std::nullopt, std::nullopt,
                                           std::nullopt},
                       {packed, raw_bits(common::RawWidth::b8, 0),
                        raw_bits(common::RawWidth::b8, 0), raw_bits(common::RawWidth::b8, 0)},
                       1U, *successor);
}

auto prepare_unpack_move(LaneResourceResolver& resolver, exec_ir::DataType type,
                         const exec_ir::RegisterVector& destination,
                         common::RegisterSlot source,
                         std::optional<common::ProgramCounter> successor)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  const auto aggregate = movement_width(type);
  if (!aggregate || (destination.elements.size() != 2U && destination.elements.size() != 4U))
    return std::unexpected(LaneFaultCause{UnsupportedMovType{type}});
  const auto component = component_width(*aggregate, destination.elements.size());
  if (!component)
    return std::unexpected(LaneFaultCause{UnsupportedMovType{type}});
  const auto registers = resolver.resolve();
  if (!registers)
    return std::unexpected(registers.error());
  const auto aggregate_raw = registers->get().read(source);
  if (!aggregate_raw)
    return std::unexpected(LaneFaultCause{aggregate_raw.error()});
  if (aggregate_raw->width() != *aggregate) {
    return std::unexpected(LaneFaultCause{
        common::RawValueError{*aggregate, aggregate_raw->width()}});
  }
  const auto aggregate_bits = *aggregate == common::RawWidth::b128
                                  ? *aggregate_raw->as_b128()
                                  : common::Bits128{*low_bits(*aggregate_raw, *aggregate), 0};
  const auto bits = *width_bits(*component);
  std::array<std::optional<common::RegisterSlot>, 4> slots{};
  std::array<common::RawValue, 4> values{
      raw_bits(common::RawWidth::b8, 0), raw_bits(common::RawWidth::b8, 0),
      raw_bits(common::RawWidth::b8, 0), raw_bits(common::RawWidth::b8, 0)};
  for (std::size_t index = 0; index < destination.elements.size(); ++index) {
    slots[index] = destination.elements[index];
    const auto piece = index * bits < 64U
                           ? (aggregate_bits.low >> (index * bits))
                           : (aggregate_bits.high >> (index * bits - 64U));
    values[index] = raw_bits(*component, piece);
    if (slots[index]) {
      const auto valid = valid_destination(registers->get(), *slots[index], *component);
      if (!valid)
        return std::unexpected(valid.error());
    }
  }
  return staged_writes(registers->get(), slots, values, destination.elements.size(),
                       *successor);
}

auto prepare_vector_special_move(
    LaneResourceResolver& resolver, exec_ir::VectorArity vector,
    exec_ir::DataType type, exec_ir::VectorRegisterRef destination,
    exec_ir::VectorSpecialRegisterRef source,
    std::optional<common::ProgramCounter> successor)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  if (vector != exec_ir::VectorArity::v4 || type != exec_ir::DataType::u32) {
    return std::unexpected(LaneFaultCause{UnsupportedMovType{type}});
  }
  const auto registers = resolver.resolve();
  if (!registers)
    return std::unexpected(registers.error());
  std::array<std::optional<common::RegisterSlot>, 4> slots{};
  std::array<common::RawValue, 4> values{
      common::RawValue::b32(0U), common::RawValue::b32(0U),
      common::RawValue::b32(0U), common::RawValue::b32(0U)};
  for (std::size_t index = 0; index < slots.size(); ++index) {
    slots[index] = component_slot(destination.register_slot, index);
    if (!slots[index])
      return std::unexpected(LaneFaultCause{InvalidMovVector{}});
    const auto valid = valid_destination(registers->get(), *slots[index], common::RawWidth::b32);
    if (!valid)
      return std::unexpected(valid.error());
    if (index < 3U) {
      const auto coordinate = topology_component(resolver.thread(), source.id,
                                                 static_cast<std::uint8_t>(index));
      if (!coordinate)
        return std::unexpected(coordinate.error());
      values[index] = common::RawValue::b32(*coordinate);
    }
  }
  return staged_writes(registers->get(), slots, values, slots.size(), *successor);
}

auto prepare_predicate_move(LaneResourceResolver& resolver,
                            exec_ir::Predicate destination,
                            const exec_ir::PredicateSource& source,
                            std::optional<common::ProgramCounter> successor)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  const auto registers = resolver.resolve();
  if (!registers)
    return std::unexpected(registers.error());
  if (destination.negated)
    return std::unexpected(LaneFaultCause{InvalidMovPredicate{}});
  bool value = false;
  if (const auto* predicate = std::get_if<exec_ir::Predicate>(&source)) {
    const auto raw = registers->get().read(predicate->source);
    if (!raw)
      return std::unexpected(LaneFaultCause{raw.error()});
    const auto decoded = raw->as_pred();
    if (!decoded)
      return std::unexpected(LaneFaultCause{decoded.error()});
    value = predicate->negated ? !*decoded : *decoded;
  } else {
    return unsupported_special(std::get<exec_ir::SpecialRegisterRef>(source));
  }
  const auto valid = valid_destination(registers->get(), destination.source,
                                       common::RawWidth::pred);
  if (!valid)
    return std::unexpected(valid.error());
  return staged_writes(registers->get(), {destination.source, std::nullopt,
                                           std::nullopt, std::nullopt},
                       {common::RawValue::pred(value), raw_bits(common::RawWidth::b8, 0),
                        raw_bits(common::RawWidth::b8, 0), raw_bits(common::RawWidth::b8, 0)},
                       1U, *successor);
}

}  // namespace ptxsim::inst_execute_engine::detail
