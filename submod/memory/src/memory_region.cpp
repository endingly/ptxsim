#include <ptxsim/memory/core/memory_region.hpp>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <utility>

namespace ptxsim::memory {

MemoryRegion::MemoryRegion(std::size_t size, RegionAccess access)
    : bytes_(size, std::byte{0}), initialized_(size, false), access_(access) {}

bool MemoryRegion::contains(Address address,
                            std::size_t requested_size) const noexcept {
  /*
   * Check the starting address before converting it to size_t.
   *
   * This keeps the implementation valid even when architectural addresses are
   * wider than the host container index type.
   */
  if (address.value > static_cast<std::uint64_t>(bytes_.size())) {
    return false;
  }

  const auto begin = static_cast<std::size_t>(address.value);

  /*
   * Avoid begin + requested_size overflow by subtracting instead.
   */
  return requested_size <= bytes_.size() - begin;
}

std::expected<void, MemoryError> MemoryRegion::validate_range(
    Address address, std::size_t requested_size) const noexcept {
  if (!contains(address, requested_size)) {
    return std::unexpected(MemoryError{
        .code = MemoryErrorCode::OutOfBounds,
        .address = address,
        .size = requested_size,
    });
  }

  return {};
}

std::expected<void, MemoryError> MemoryRegion::validate_alignment(
    Address address, std::size_t alignment,
    std::size_t access_size) const noexcept {
  if (!is_power_of_two(alignment)) {
    return std::unexpected(MemoryError{
        .code = MemoryErrorCode::InvalidAlignment,
        .address = address,
        .size = access_size,
        .required_alignment = alignment,
    });
  }

  if (!is_aligned(address, alignment)) {
    return std::unexpected(MemoryError{
        .code = MemoryErrorCode::Misaligned,
        .address = address,
        .size = access_size,
        .required_alignment = alignment,
    });
  }

  return {};
}

std::expected<void, MemoryError> MemoryRegion::validate(
    Address address, std::size_t requested_size,
    std::size_t alignment) const noexcept {
  if (auto result = validate_alignment(address, alignment, requested_size);
      !result) {
    return result;
  }

  return validate_range(address, requested_size);
}

std::expected<void, MemoryError> MemoryRegion::validate_write(
    Address address, std::size_t requested_size,
    std::size_t alignment) const noexcept {
  if (access_ == RegionAccess::ReadOnly) {
    return std::unexpected(MemoryError{
        .code = MemoryErrorCode::WriteToReadOnlyRegion,
        .address = address,
        .size = requested_size,
        .required_alignment = alignment,
    });
  }
  return validate(address, requested_size, alignment);
}

bool MemoryRegion::is_initialized(Address address,
                                  std::size_t requested_size) const noexcept {
  if (!contains(address, requested_size)) {
    return false;
  }

  const auto begin = offset(address);

  for (std::size_t i = 0; i < requested_size; ++i) {
    if (!initialized_[begin + i]) {
      return false;
    }
  }

  return true;
}

std::expected<void, MemoryError> MemoryRegion::initialize(
    Address address, std::span<const std::byte> data) noexcept {
  if (auto result = validate_range(address, data.size()); !result) {
    return result;
  }

  const auto begin = offset(address);

  if (!data.empty()) {
    std::memcpy(bytes_.data() + begin, data.data(), data.size());
  }

  for (std::size_t i = 0; i < data.size(); ++i) {
    initialized_[begin + i] = true;
  }

  return {};
}

void MemoryRegion::reset_uninitialized(std::byte fill_value) noexcept {
  std::fill(bytes_.begin(), bytes_.end(), fill_value);

  std::fill(initialized_.begin(), initialized_.end(), false);
}

void MemoryRegion::fill_initialized(std::byte value) noexcept {
  std::fill(bytes_.begin(), bytes_.end(), value);

  std::fill(initialized_.begin(), initialized_.end(), true);
}

std::expected<void, MemoryError> MemoryRegion::read(
    Address address, std::span<std::byte> destination, std::size_t alignment,
    ReadRequirement requirement) const noexcept {
  if (auto result = validate(address, destination.size(), alignment); !result) {
    return result;
  }

  if (requirement == ReadRequirement::RequireInitialized &&
      !is_initialized(address, destination.size())) {
    return std::unexpected(MemoryError{
        .code = MemoryErrorCode::UninitializedRead,
        .address = address,
        .size = destination.size(),
        .required_alignment = alignment,
    });
  }

  const auto begin = offset(address);

  if (!destination.empty()) {
    std::memcpy(destination.data(), bytes_.data() + begin, destination.size());
  }

  return {};
}

std::expected<void, MemoryError> MemoryRegion::write(
    Address address, std::span<const std::byte> source,
    std::size_t alignment) noexcept {
  if (auto result = validate_write(address, source.size(), alignment);
      !result) {
    return result;
  }

  const auto begin = offset(address);

  if (!source.empty()) {
    std::memcpy(bytes_.data() + begin, source.data(), source.size());
  }

  for (std::size_t i = 0; i < source.size(); ++i) {
    initialized_[begin + i] = true;
  }

  return {};
}

std::expected<std::vector<std::byte>, MemoryError> MemoryRegion::snapshot(
    Address address, std::size_t requested_size,
    ReadRequirement requirement) const {
  std::vector<std::byte> result(requested_size);

  auto read_result = read(address, result, 1, requirement);

  if (!read_result) {
    return std::unexpected(read_result.error());
  }

  return result;
}

}  // namespace ptxsim::memory
