#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

#include <ptxsim/memory/core/address.hpp>
#include <ptxsim/memory/core/memory_error.hpp>

namespace ptxsim::memory {

/**
 * @brief Write permission of a byte-addressable memory region.
 */
enum class RegionAccess : std::uint8_t {
  ReadOnly,
  ReadWrite,
};

/**
 * @brief Policy controlling reads from bytes that have not been initialized.
 */
enum class ReadRequirement : std::uint8_t {
  /**
   * Reading any uninitialized byte produces MemoryError::UninitializedRead.
   */
  RequireInitialized,

  /**
   * Initialization tracking is ignored for this read.
   *
   * Intended primarily for debugging, snapshots, or PTX operations whose
   * undefined-value handling is implemented at a higher layer.
   */
  IgnoreInitialization,
};

/**
 * @brief Dense byte-addressable backing storage.
 *
 * MemoryRegion provides the primitive storage implementation used by
 * byte-addressable PTX state spaces such as global, constant, local,
 * parameter, and shared memory.
 *
 * MemoryRegion deliberately has no knowledge of execution topology or PTX
 * memory consistency semantics.
 *
 * Responsibilities:
 *
 * - byte storage;
 * - bounds validation;
 * - alignment validation;
 * - read/write permissions;
 * - initialized-byte tracking.
 *
 * Non-responsibilities:
 *
 * - state-space address resolution;
 * - generic addressing;
 * - memory ordering;
 * - scope/proxy semantics;
 * - asynchronous completion;
 * - register storage;
 * - Tensor Memory.
 */
class MemoryRegion final {
 public:
  MemoryRegion() = default;

  explicit MemoryRegion(std::size_t size,
                        RegionAccess access = RegionAccess::ReadWrite);

  MemoryRegion(const MemoryRegion&) = delete;
  MemoryRegion& operator=(const MemoryRegion&) = delete;

  MemoryRegion(MemoryRegion&&) noexcept = default;
  MemoryRegion& operator=(MemoryRegion&&) noexcept = default;

  ~MemoryRegion() = default;

  // -------------------------------------------------------------------------
  // Region metadata
  // -------------------------------------------------------------------------

  [[nodiscard]]
  std::size_t size() const noexcept {
    return bytes_.size();
  }

  [[nodiscard]]
  bool empty() const noexcept {
    return bytes_.empty();
  }

  [[nodiscard]]
  RegionAccess access() const noexcept {
    return access_;
  }

  /**
   * @brief Return whether the byte range lies completely within this region.
   */
  [[nodiscard]]
  bool contains(Address address, std::size_t size) const noexcept;

  /**
   * @brief Validate range and alignment without performing an access.
   */
  [[nodiscard]]
  std::expected<void, MemoryError> validate(
      Address address, std::size_t size,
      std::size_t alignment = 1) const noexcept;

  /**
   * @brief Check runtime write permission, alignment, and range together.
   *
   * Returns the same error as `write` would for these preconditions without
   * changing bytes or initialization state.
   */
  [[nodiscard]]
  std::expected<void, MemoryError> validate_write(
      Address address, std::size_t size,
      std::size_t alignment = 1) const noexcept;

  // -------------------------------------------------------------------------
  // Initialization state
  // -------------------------------------------------------------------------

  /**
   * @brief Return whether every byte in the requested range has been
   * initialized.
   *
   * Returns false for an out-of-bounds range.
   */
  [[nodiscard]]
  bool is_initialized(Address address, std::size_t size) const noexcept;

  /**
   * @brief Initialize bytes regardless of runtime write permission.
   *
   * This operation is intended for program loading, kernel parameter setup,
   * and other simulator/runtime initialization paths.
   *
   * In particular, it allows initialization of a read-only Constant region
   * without exposing runtime write permission to PTX instructions.
   */
  [[nodiscard]]
  std::expected<void, MemoryError> initialize(
      Address address, std::span<const std::byte> data) noexcept;

  /**
   * @brief Mark the entire region uninitialized and reset backing bytes.
   */
  void reset_uninitialized(std::byte fill_value = std::byte{0}) noexcept;

  /**
   * @brief Fill the entire region and mark every byte initialized.
   */
  void fill_initialized(std::byte value) noexcept;

  /**
   * @brief Zero-fill the entire region and mark every byte initialized.
   */
  void zero_initialize() noexcept { fill_initialized(std::byte{0}); }

  // -------------------------------------------------------------------------
  // Runtime access
  // -------------------------------------------------------------------------

  /**
   * @brief Read bytes from the region.
   *
   * @param address Byte offset into this MemoryRegion.
   * @param destination Destination host buffer.
   * @param alignment Required architectural alignment.
   * @param requirement Whether initialized-byte tracking is enforced.
   */
  [[nodiscard]]
  std::expected<void, MemoryError> read(
      Address address, std::span<std::byte> destination,
      std::size_t alignment = 1,
      ReadRequirement requirement =
          ReadRequirement::RequireInitialized) const noexcept;

  /**
   * @brief Write bytes to the region.
   *
   * Successful writes mark all affected bytes initialized.
   */
  [[nodiscard]]
  std::expected<void, MemoryError> write(Address address,
                                         std::span<const std::byte> source,
                                         std::size_t alignment = 1) noexcept;

  /**
   * @brief Copy a range into an owning byte vector.
   *
   * Primarily useful for debugger/snapshot infrastructure.
   */
  [[nodiscard]]
  std::expected<std::vector<std::byte>, MemoryError> snapshot(
      Address address, std::size_t size,
      ReadRequirement requirement =
          ReadRequirement::IgnoreInitialization) const;

 private:
  [[nodiscard]]
  std::expected<void, MemoryError> validate_range(
      Address address, std::size_t size) const noexcept;

  [[nodiscard]]
  std::expected<void, MemoryError> validate_alignment(
      Address address, std::size_t alignment,
      std::size_t access_size) const noexcept;

  [[nodiscard]]
  std::size_t offset(Address address) const noexcept {
    return static_cast<std::size_t>(address.value);
  }

  std::vector<std::byte> bytes_;

  /**
   * vector<bool> is intentional here: initialization state is one bit per
   * byte and does not participate in the architectural storage layout.
   */
  std::vector<bool> initialized_;

  RegionAccess access_ = RegionAccess::ReadWrite;
};

}  // namespace ptxsim::memory
