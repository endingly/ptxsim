#pragma once

#include <cstddef>
#include <expected>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include <ptxsim/memory/address_space/address_space_error.hpp>
#include <ptxsim/memory/address_space/address_space_handle.hpp>
#include <ptxsim/memory/address_space/constant_space.hpp>
#include <ptxsim/memory/address_space/global_space.hpp>
#include <ptxsim/memory/address_space/local_space.hpp>
#include <ptxsim/memory/address_space/parameter_space.hpp>
#include <ptxsim/memory/address_space/shared_space.hpp>
#include <ptxsim/memory/core/memory_region.hpp>
#include <ptxsim/memory/sync/mbarrier_state.hpp>

namespace ptxsim::memory {

class ConstAddressSpaceView;

class MBarrierView final {
 public:
  [[nodiscard]] auto init(Address address,
                          std::uint32_t expected_arrivals) const
      -> std::expected<void, MBarrierError>;
  [[nodiscard]] auto invalidate(Address address) const
      -> std::expected<void, MBarrierError>;
  [[nodiscard]] auto arrive(Address address, std::uint32_t count = 1) const
      -> std::expected<MBarrierToken, MBarrierError>;
  [[nodiscard]] auto expect_tx(Address address, std::uint32_t count) const
      -> std::expected<void, MBarrierError>;
  [[nodiscard]] auto complete_tx(Address address, std::uint32_t count) const
      -> std::expected<void, MBarrierError>;
  [[nodiscard]] auto test_wait(Address address, MBarrierToken token) const
      -> std::expected<bool, MBarrierError>;
  [[nodiscard]] auto snapshot(Address address) const
      -> std::expected<MBarrierSnapshot, MBarrierError>;

 private:
  MBarrierView(std::weak_ptr<detail::AddressSpaceManagerState> state,
               detail::AddressSpaceLocator locator) noexcept
      : state_(std::move(state)), locator_(locator) {}

  std::weak_ptr<detail::AddressSpaceManagerState> state_;
  detail::AddressSpaceLocator locator_{};

  friend class AddressSpaceManager;
};

class AddressSpaceView final {
 public:
  [[nodiscard]] auto size() const
      -> std::expected<std::size_t, AddressSpaceError>;
  [[nodiscard]] auto access() const
      -> std::expected<RegionAccess, AddressSpaceError>;
  [[nodiscard]] auto is_initialized(Address address, std::size_t size) const
      -> std::expected<bool, AddressSpaceError>;
  /**
   * @brief Validate a pending write while retaining the view's stale-handle
   *        checks and storage-error wrapping.
   *
   * This is side-effect free and has the same write-precondition result as
   * `write` for the supplied byte count and alignment.
   */
  [[nodiscard]] auto validate_write(Address address, std::size_t size,
                                    std::size_t alignment = 1) const
      -> std::expected<void, AddressSpaceError>;
  [[nodiscard]] auto read(
      Address address, std::span<std::byte> destination,
      std::size_t alignment = 1,
      ReadRequirement requirement = ReadRequirement::RequireInitialized) const
      -> std::expected<void, AddressSpaceError>;
  [[nodiscard]] auto write(Address address, std::span<const std::byte> source,
                           std::size_t alignment = 1)
      -> std::expected<void, AddressSpaceError>;
  [[nodiscard]] auto initialize(Address address,
                                std::span<const std::byte> source)
      -> std::expected<void, AddressSpaceError>;
  [[nodiscard]] auto snapshot(
      Address address, std::size_t size,
      ReadRequirement requirement = ReadRequirement::IgnoreInitialization) const
      -> std::expected<std::vector<std::byte>, AddressSpaceError>;

  [[nodiscard]] operator ConstAddressSpaceView() const;

 private:
  AddressSpaceView(std::weak_ptr<detail::AddressSpaceManagerState> state,
                   detail::AddressSpaceLocator locator) noexcept
      : state_(std::move(state)), locator_(locator) {}

  std::weak_ptr<detail::AddressSpaceManagerState> state_;
  detail::AddressSpaceLocator locator_{};

  friend class AddressSpaceManager;
  friend class ConstAddressSpaceView;
};

class ConstAddressSpaceView final {
 public:
  [[nodiscard]] auto size() const
      -> std::expected<std::size_t, AddressSpaceError>;
  [[nodiscard]] auto access() const
      -> std::expected<RegionAccess, AddressSpaceError>;
  [[nodiscard]] auto is_initialized(Address address, std::size_t size) const
      -> std::expected<bool, AddressSpaceError>;
  [[nodiscard]] auto read(
      Address address, std::span<std::byte> destination,
      std::size_t alignment = 1,
      ReadRequirement requirement = ReadRequirement::RequireInitialized) const
      -> std::expected<void, AddressSpaceError>;
  [[nodiscard]] auto snapshot(
      Address address, std::size_t size,
      ReadRequirement requirement = ReadRequirement::IgnoreInitialization) const
      -> std::expected<std::vector<std::byte>, AddressSpaceError>;

 private:
  ConstAddressSpaceView(std::weak_ptr<detail::AddressSpaceManagerState> state,
                        detail::AddressSpaceLocator locator) noexcept
      : state_(std::move(state)), locator_(locator) {}

  std::weak_ptr<detail::AddressSpaceManagerState> state_;
  detail::AddressSpaceLocator locator_{};

  friend class AddressSpaceManager;
  friend class AddressSpaceView;
};

class AddressSpaceManager final {
 public:
  AddressSpaceManager();
  ~AddressSpaceManager();

  AddressSpaceManager(const AddressSpaceManager&) = delete;
  AddressSpaceManager& operator=(const AddressSpaceManager&) = delete;
  AddressSpaceManager(AddressSpaceManager&&) = delete;
  AddressSpaceManager& operator=(AddressSpaceManager&&) = delete;

  [[nodiscard]] auto create_global(GlobalSpaceSpec spec) -> GlobalSpaceHandle;
  [[nodiscard]] auto create_constant(ConstantSpaceSpec spec)
      -> ConstantSpaceHandle;
  [[nodiscard]] auto create_local_frame(LocalFrameSpec spec)
      -> LocalFrameHandle;
  [[nodiscard]] auto create_entry_parameter(EntryParameterSpec spec)
      -> EntryParameterHandle;
  [[nodiscard]] auto create_function_parameter(FunctionParameterSpec spec)
      -> FunctionParameterHandle;
  [[nodiscard]] auto create_shared(SharedSpaceSpec spec) -> SharedSpaceHandle;

  [[nodiscard]] auto destroy(GlobalSpaceHandle handle)
      -> std::expected<void, AddressSpaceError>;
  [[nodiscard]] auto destroy(ConstantSpaceHandle handle)
      -> std::expected<void, AddressSpaceError>;
  [[nodiscard]] auto destroy(LocalFrameHandle handle)
      -> std::expected<void, AddressSpaceError>;
  [[nodiscard]] auto destroy(EntryParameterHandle handle)
      -> std::expected<void, AddressSpaceError>;
  [[nodiscard]] auto destroy(FunctionParameterHandle handle)
      -> std::expected<void, AddressSpaceError>;
  [[nodiscard]] auto destroy(SharedSpaceHandle handle)
      -> std::expected<void, AddressSpaceError>;

  [[nodiscard]] auto view(GlobalSpaceHandle handle)
      -> std::expected<AddressSpaceView, AddressSpaceError>;
  [[nodiscard]] auto view(ConstantSpaceHandle handle)
      -> std::expected<AddressSpaceView, AddressSpaceError>;
  [[nodiscard]] auto view(LocalFrameHandle handle)
      -> std::expected<AddressSpaceView, AddressSpaceError>;
  [[nodiscard]] auto view(EntryParameterHandle handle)
      -> std::expected<AddressSpaceView, AddressSpaceError>;
  [[nodiscard]] auto view(FunctionParameterHandle handle)
      -> std::expected<AddressSpaceView, AddressSpaceError>;
  [[nodiscard]] auto view(SharedSpaceHandle handle)
      -> std::expected<AddressSpaceView, AddressSpaceError>;

  [[nodiscard]] auto view(GlobalSpaceHandle handle) const
      -> std::expected<ConstAddressSpaceView, AddressSpaceError>;
  [[nodiscard]] auto view(ConstantSpaceHandle handle) const
      -> std::expected<ConstAddressSpaceView, AddressSpaceError>;
  [[nodiscard]] auto view(LocalFrameHandle handle) const
      -> std::expected<ConstAddressSpaceView, AddressSpaceError>;
  [[nodiscard]] auto view(EntryParameterHandle handle) const
      -> std::expected<ConstAddressSpaceView, AddressSpaceError>;
  [[nodiscard]] auto view(FunctionParameterHandle handle) const
      -> std::expected<ConstAddressSpaceView, AddressSpaceError>;
  [[nodiscard]] auto view(SharedSpaceHandle handle) const
      -> std::expected<ConstAddressSpaceView, AddressSpaceError>;

  [[nodiscard]] auto mbarriers(SharedSpaceHandle handle)
      -> std::expected<MBarrierView, AddressSpaceError>;

  [[nodiscard]] auto allocate(GlobalSpaceHandle handle, std::size_t size,
                              std::size_t alignment = 1)
      -> std::expected<AddressRange, AddressSpaceError>;
  [[nodiscard]] auto allocate(ConstantSpaceHandle handle, std::size_t size,
                              std::size_t alignment = 1)
      -> std::expected<AddressRange, AddressSpaceError>;

 private:
  std::shared_ptr<detail::AddressSpaceManagerState> state_;
};

}  // namespace ptxsim::memory
