#include "memory_preparation.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
#include <span>

namespace ptxsim::inst_execute_engine::detail {
namespace {

/** @brief Return the byte width selected by an ordinary memory type. */
auto type_size(exec_ir::DataType type) -> std::optional<std::size_t> {
  switch (type) {
    case exec_ir::DataType::b8:
    case exec_ir::DataType::u8:
    case exec_ir::DataType::s8:
      return 1;
    case exec_ir::DataType::b16:
    case exec_ir::DataType::u16:
    case exec_ir::DataType::s16:
      return 2;
    case exec_ir::DataType::b32:
    case exec_ir::DataType::u32:
    case exec_ir::DataType::s32:
    case exec_ir::DataType::f32:
      return 4;
    case exec_ir::DataType::b64:
    case exec_ir::DataType::u64:
    case exec_ir::DataType::s64:
    case exec_ir::DataType::f64:
      return 8;
    default:
      return std::nullopt;
  }
}

/** @brief Return whether a memory type requires signed extension on a load. */
auto signed_type(exec_ir::DataType type) -> bool {
  return type == exec_ir::DataType::s8 || type == exec_ir::DataType::s16 ||
         type == exec_ir::DataType::s32 || type == exec_ir::DataType::s64;
}

/** @brief Return the lane count represented by an optional vector selector. */
auto lane_count(std::optional<exec_ir::VectorArity> vector)
    -> std::optional<std::size_t> {
  if (!vector)
    return 1;
  switch (*vector) {
    case exec_ir::VectorArity::v2:
      return 2;
    case exec_ir::VectorArity::v4:
      return 4;
    case exec_ir::VectorArity::v8:
      return 8;
  }
  return std::nullopt;
}

/** @brief Return the byte capacity of an architectural raw register width. */
auto raw_size(common::RawWidth width) -> std::optional<std::size_t> {
  switch (width) {
    case common::RawWidth::b8:
      return 1;
    case common::RawWidth::b16:
      return 2;
    case common::RawWidth::b32:
      return 4;
    case common::RawWidth::b64:
      return 8;
    case common::RawWidth::b128:
      return 16;
    case common::RawWidth::pred:
      return std::nullopt;
  }
  return std::nullopt;
}

/** @brief Convert a failed memory-width check into the existing lane fault type. */
auto width_fault(common::RawWidth expected, common::RawWidth actual)
    -> std::unexpected<LaneFaultCause> {
  return std::unexpected(
      LaneFaultCause{common::RawValueError{expected, actual}});
}

/** @brief Decode up to eight PTX little-endian bytes into low-order bits. */
auto low_bits(std::span<const std::byte> bytes) -> std::uint64_t {
  std::uint64_t value = 0;
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    value |=
        static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[index]))
        << (index * 8U);
  }
  return value;
}

/** @brief Form a destination-width raw value from loaded bits and extension policy. */
auto loaded_value(std::span<const std::byte> bytes, bool sign_extend,
                  common::RawWidth destination)
    -> std::expected<common::RawValue, LaneFaultCause> {
  const auto capacity = raw_size(destination);
  if (!capacity || bytes.size() > *capacity)
    return width_fault(common::RawWidth::b64, destination);
  auto value = low_bits(bytes);
  const bool negative =
      sign_extend &&
      (value & (std::uint64_t{1} << (bytes.size() * 8U - 1U))) != 0U;
  if (negative && bytes.size() < 8)
    value |= ~std::uint64_t{0} << (bytes.size() * 8U);
  switch (destination) {
    case common::RawWidth::b8:
      return common::RawValue::b8(static_cast<std::uint8_t>(value));
    case common::RawWidth::b16:
      return common::RawValue::b16(static_cast<std::uint16_t>(value));
    case common::RawWidth::b32:
      return common::RawValue::b32(static_cast<std::uint32_t>(value));
    case common::RawWidth::b64:
      return common::RawValue::b64(value);
    case common::RawWidth::b128:
      return common::RawValue::b128(common::Bits128{
          value, negative ? std::numeric_limits<std::uint64_t>::max() : 0U});
    case common::RawWidth::pred:
      return width_fault(common::RawWidth::b8, destination);
  }
  return width_fault(common::RawWidth::b8, destination);
}

/** @brief Serialize the low bytes of one source register in PTX little-endian order. */
auto store_bytes(const common::RawValue& value, std::span<std::byte> bytes)
    -> std::expected<void, LaneFaultCause> {
  std::uint64_t low = 0;
  switch (value.width()) {
    case common::RawWidth::b8:
      low = *value.as_b8();
      break;
    case common::RawWidth::b16:
      low = *value.as_b16();
      break;
    case common::RawWidth::b32:
      low = *value.as_b32();
      break;
    case common::RawWidth::b64:
      low = *value.as_b64();
      break;
    case common::RawWidth::b128:
      low = value.as_b128()->low;
      break;
    case common::RawWidth::pred:
      return width_fault(common::RawWidth::b8, value.width());
  }
  const auto capacity = raw_size(value.width());
  if (!capacity || bytes.size() > *capacity)
    return width_fault(common::RawWidth::b64, value.width());
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    bytes[index] = std::byte{static_cast<std::uint8_t>(low >> (index * 8U))};
  }
  return {};
}

/** @brief Resolve and validate one memory access shape before resource mutation. */
auto access_shape(exec_ir::DataType type,
                  std::optional<exec_ir::VectorArity> vector)
    -> std::expected<std::pair<std::size_t, std::size_t>, LaneFaultCause> {
  const auto width = type_size(type);
  const auto lanes = lane_count(vector);
  if (!width || !lanes || *width > 8 || *lanes > 8 ||
      (*lanes == 8 && *width != 4) || *width * *lanes > 32) {
    return width_fault(common::RawWidth::b64, common::RawWidth::pred);
  }
  return std::pair{*width, *lanes};
}

/** @brief Resolve one address and read the complete memory span before staging writes. */
auto read_access(LaneResourceResolver& resolver,
                 const exec_ir::Address& address, exec_ir::AddressSpace space,
                 std::span<std::byte> bytes, std::size_t alignment)
    -> std::expected<void, LaneFaultCause> {
  if (bytes.size() == 32 && space != exec_ir::AddressSpace::generic &&
      space != exec_ir::AddressSpace::global) {
    return width_fault(common::RawWidth::b64, common::RawWidth::pred);
  }
  const auto memory = resolver.resolve_memory(space, address, bytes.size());
  if (!memory)
    return std::unexpected(memory.error());
  if (const auto read = memory->first.read(memory->second, bytes, alignment);
      !read) {
    return std::unexpected(LaneFaultCause{read.error()});
  }
  return {};
}

/** @brief Validate and retain one complete store span for deferred commit. */
auto stage_store(LaneResourceResolver& resolver,
                 const exec_ir::Address& address, exec_ir::AddressSpace space,
                 std::array<std::byte, 32> bytes, std::size_t size,
                 common::ProgramCounter successor)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  if (size == 32 && space != exec_ir::AddressSpace::generic &&
      space != exec_ir::AddressSpace::global) {
    return width_fault(common::RawWidth::b64, common::RawWidth::pred);
  }
  const auto memory = resolver.resolve_memory(space, address, size);
  if (!memory)
    return std::unexpected(memory.error());
  if (const auto valid =
          memory->first.validate_write(memory->second, size, size);
      !valid) {
    return std::unexpected(LaneFaultCause{valid.error()});
  }
  return PreparedEffect{
      .memory_write =
          PreparedMemoryWrite{memory->first, memory->second, bytes, size, size},
      .control = successor};
}

}  // namespace

auto valid_memory_shape(exec_ir::DataType type,
                        std::optional<exec_ir::VectorArity> vector,
                        exec_ir::AddressSpace space) noexcept -> bool {
  const auto shape = access_shape(type, vector);
  if (!shape)
    return false;
  const std::size_t total = shape->first * shape->second;
  return total != 32 || space == exec_ir::AddressSpace::generic ||
         space == exec_ir::AddressSpace::global;
}

auto valid_memory_vector_shape(exec_ir::DataType type,
                               exec_ir::VectorArity vector,
                               exec_ir::AddressSpace space,
                               const exec_ir::RegisterVector& registers,
                               bool load) noexcept -> bool {
  const auto shape = access_shape(type, vector);
  if (!shape || registers.elements.size() != shape->second ||
      !valid_memory_shape(type, vector, space)) {
    return false;
  }
  const bool has_sink = std::ranges::any_of(
      registers.elements, [](const auto& slot) { return !slot; });
  return !has_sink || (load && shape->first * shape->second == 32);
}

auto prepare_memory_load(LaneResourceResolver& resolver,
                         const exec_ir::Address& address,
                         common::RegisterSlot destination,
                         exec_ir::DataType type, exec_ir::AddressSpace space,
                         std::optional<exec_ir::VectorArity> vector,
                         common::ProgramCounter successor)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  if (vector)
    return width_fault(common::RawWidth::b64, common::RawWidth::pred);
  const auto shape = access_shape(type, vector);
  if (!shape)
    return std::unexpected(shape.error());
  const auto registers = resolver.resolve();
  if (!registers)
    return std::unexpected(registers.error());
  const auto width = registers->get().declared_width(destination);
  if (!width)
    return std::unexpected(LaneFaultCause{width.error()});
  if (!raw_size(*width) || *raw_size(*width) < shape->first)
    return width_fault(common::RawWidth::b64, *width);
  std::array<std::byte, 32> bytes{};
  if (const auto read =
          read_access(resolver, address, space,
                      std::span{bytes}.first(shape->first), shape->first);
      !read)
    return std::unexpected(read.error());
  const auto value = loaded_value(std::span{bytes}.first(shape->first),
                                  signed_type(type), *width);
  if (!value)
    return std::unexpected(value.error());
  return PreparedEffect{
      .writes = {PreparedWrite{registers->get(), destination, *value}},
      .control = successor};
}

auto prepare_memory_load(LaneResourceResolver& resolver,
                         const exec_ir::Address& address,
                         const exec_ir::RegisterVector& destination,
                         exec_ir::DataType type, exec_ir::AddressSpace space,
                         std::optional<exec_ir::VectorArity> vector,
                         common::ProgramCounter successor)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  const auto shape = access_shape(type, vector);
  if (!shape || !vector || destination.elements.size() != shape->second)
    return width_fault(common::RawWidth::b64, common::RawWidth::pred);
  const std::size_t total = shape->first * shape->second;
  const auto registers = resolver.resolve();
  if (!registers)
    return std::unexpected(registers.error());
  PreparedEffect effect{.control = successor};
  for (std::size_t index = 0; index < shape->second; ++index) {
    if (!destination.elements[index]) {
      if (total != 32)
        return width_fault(common::RawWidth::b64, common::RawWidth::pred);
      continue;
    }
    const auto width =
        registers->get().declared_width(*destination.elements[index]);
    if (!width)
      return std::unexpected(LaneFaultCause{width.error()});
    if (!raw_size(*width) || *raw_size(*width) < shape->first)
      return width_fault(common::RawWidth::b64, *width);
  }
  std::array<std::byte, 32> bytes{};
  if (const auto read = read_access(resolver, address, space,
                                    std::span{bytes}.first(total), total);
      !read)
    return std::unexpected(read.error());
  for (std::size_t index = 0; index < shape->second; ++index) {
    if (!destination.elements[index])
      continue;
    const auto width =
        *registers->get().declared_width(*destination.elements[index]);
    const auto value = loaded_value(
        std::span{bytes}.subspan(index * shape->first, shape->first),
        signed_type(type), width);
    if (!value)
      return std::unexpected(value.error());
    effect.writes[index] =
        PreparedWrite{registers->get(), *destination.elements[index], *value};
  }
  return effect;
}

auto prepare_memory_store(LaneResourceResolver& resolver,
                          const exec_ir::Address& address,
                          common::RegisterSlot source, exec_ir::DataType type,
                          exec_ir::AddressSpace space,
                          std::optional<exec_ir::VectorArity> vector,
                          common::ProgramCounter successor)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  if (vector)
    return width_fault(common::RawWidth::b64, common::RawWidth::pred);
  const auto shape = access_shape(type, vector);
  if (!shape)
    return std::unexpected(shape.error());
  const auto registers = resolver.resolve();
  if (!registers)
    return std::unexpected(registers.error());
  const auto value = registers->get().read(source);
  if (!value)
    return std::unexpected(LaneFaultCause{value.error()});
  std::array<std::byte, 32> bytes{};
  if (const auto encoded =
          store_bytes(*value, std::span{bytes}.first(shape->first));
      !encoded)
    return std::unexpected(encoded.error());
  return stage_store(resolver, address, space, bytes, shape->first, successor);
}

auto prepare_memory_store(LaneResourceResolver& resolver,
                          const exec_ir::Address& address,
                          const exec_ir::RegisterVector& source,
                          exec_ir::DataType type, exec_ir::AddressSpace space,
                          std::optional<exec_ir::VectorArity> vector,
                          common::ProgramCounter successor)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  const auto shape = access_shape(type, vector);
  if (!shape || !vector || source.elements.size() != shape->second)
    return width_fault(common::RawWidth::b64, common::RawWidth::pred);
  const auto registers = resolver.resolve();
  if (!registers)
    return std::unexpected(registers.error());
  std::array<std::byte, 32> bytes{};
  for (std::size_t index = 0; index < shape->second; ++index) {
    if (!source.elements[index])
      return width_fault(common::RawWidth::b64, common::RawWidth::pred);
    const auto value = registers->get().read(*source.elements[index]);
    if (!value)
      return std::unexpected(LaneFaultCause{value.error()});
    if (const auto encoded = store_bytes(
            *value,
            std::span{bytes}.subspan(index * shape->first, shape->first));
        !encoded)
      return std::unexpected(encoded.error());
  }
  return stage_store(resolver, address, space, bytes,
                     shape->first * shape->second, successor);
}

}  // namespace ptxsim::inst_execute_engine::detail
