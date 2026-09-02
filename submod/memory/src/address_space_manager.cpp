#include <ptxsim/memory/address_space/address_space_manager.hpp>

#include <atomic>
#include <limits>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace ptxsim::memory {
namespace {

auto next_manager_token() noexcept -> std::uint64_t {
  static std::atomic<std::uint64_t> next{1};
  auto token = next.fetch_add(1, std::memory_order_relaxed);
  return token == 0 ? next.fetch_add(1, std::memory_order_relaxed) : token;
}

auto stale_error(std::size_t index) -> AddressSpaceError {
  return {AddressSpaceErrorCode::stale_resource, std::nullopt, index};
}

auto memory_error(AddressSpaceErrorCode code, std::size_t index,
                  MemoryError error) -> AddressSpaceError {
  return {code, std::move(error), index};
}

}  // namespace

namespace detail {

struct AddressSpaceManagerState {
  struct Resource {
    Resource(AddressSpaceKind kind, std::size_t size, RegionAccess access)
        : kind(kind), region(size, access) {}

    Resource(const Resource&) = delete;
    Resource(Resource&&) noexcept = default;

    AddressSpaceKind kind;
    MemoryRegion region;
    std::size_t next_free = 0;
    std::map<std::uint64_t, MBarrierState> mbarriers;
  };

  struct Slot {
    std::optional<Resource> resource;
    std::uint64_t generation = 1;
  };

  explicit AddressSpaceManagerState(std::uint64_t token) : token(token) {}

  template <AddressSpaceHandleType Handle>
  [[nodiscard]] auto create(AddressSpaceKind kind, std::size_t size,
                            RegionAccess access) -> Handle {
    Resource resource{kind, size, access};
    if (!free_indices.empty()) {
      const auto index = free_indices.back();
      auto& slot = slots[index];
      slot.resource.emplace(std::move(resource));
      free_indices.pop_back();
      return Handle{token, index, slot.generation};
    }

    Slot slot;
    slot.resource.emplace(std::move(resource));
    slots.push_back(std::move(slot));
    return Handle{token, slots.size() - 1, slots.back().generation};
  }

  template <AddressSpaceHandleType Handle>
  [[nodiscard]] auto locator(const Handle& handle, AddressSpaceKind kind) const
      -> AddressSpaceLocator {
    return {handle.manager_token_, handle.index_, handle.generation_, kind};
  }

  [[nodiscard]] auto find(const AddressSpaceLocator& locator)
      -> std::expected<Resource*, AddressSpaceError> {
    if (locator.manager_token != token || locator.index >= slots.size()) {
      return std::unexpected(stale_error(locator.index));
    }
    auto& slot = slots[locator.index];
    if (!slot.resource || slot.generation != locator.generation ||
        slot.resource->kind != locator.kind) {
      return std::unexpected(stale_error(locator.index));
    }
    return &*slot.resource;
  }

  [[nodiscard]] auto destroy(const AddressSpaceLocator& locator)
      -> std::expected<void, AddressSpaceError> {
    auto resource = find(locator);
    if (!resource) {
      return std::unexpected(resource.error());
    }
    slots[locator.index].resource.reset();
    auto& generation = slots[locator.index].generation;
    if (generation != std::numeric_limits<std::uint64_t>::max()) {
      ++generation;
      free_indices.push_back(locator.index);
    }
    return {};
  }

  [[nodiscard]] auto allocate(const AddressSpaceLocator& locator,
                              std::size_t size, std::size_t alignment)
      -> std::expected<AddressRange, AddressSpaceError> {
    auto resource = find(locator);
    if (!resource) {
      return std::unexpected(resource.error());
    }
    auto& value = **resource;
    if (!is_power_of_two(alignment)) {
      return std::unexpected(
          memory_error(AddressSpaceErrorCode::allocation_failure, locator.index,
                       {MemoryErrorCode::InvalidAlignment,
                        Address{value.next_free}, size, alignment}));
    }
    if (value.next_free >
        std::numeric_limits<std::size_t>::max() - (alignment - 1)) {
      return std::unexpected(
          memory_error(AddressSpaceErrorCode::allocation_failure, locator.index,
                       {MemoryErrorCode::OutOfBounds, Address{value.next_free},
                        size, alignment}));
    }
    const auto begin = (value.next_free + alignment - 1) & ~(alignment - 1);
    if (begin > value.region.size() || size > value.region.size() - begin) {
      return std::unexpected(memory_error(
          AddressSpaceErrorCode::allocation_failure, locator.index,
          {MemoryErrorCode::OutOfBounds, Address{begin}, size, alignment}));
    }
    if (size == 0) {
      return AddressRange{Address{begin}, 0};
    }
    value.next_free = begin + size;
    return AddressRange{Address{begin}, size};
  }

  std::uint64_t token;
  std::uint64_t next_mbarrier_incarnation = 1;
  std::vector<Slot> slots;
  std::vector<std::size_t> free_indices;
};

}  // namespace detail

namespace {

struct ResourceLease {
  std::shared_ptr<detail::AddressSpaceManagerState> state;
  detail::AddressSpaceManagerState::Resource* resource;

  [[nodiscard]] auto operator->() const noexcept
      -> detail::AddressSpaceManagerState::Resource* {
    return resource;
  }
};

auto resource(std::weak_ptr<detail::AddressSpaceManagerState> state,
              detail::AddressSpaceLocator locator)
    -> std::expected<ResourceLease, AddressSpaceError> {
  auto locked = state.lock();
  if (!locked) {
    return std::unexpected(stale_error(locator.index));
  }
  auto value = locked->find(locator);
  if (!value) {
    return std::unexpected(value.error());
  }
  return ResourceLease{std::move(locked), *value};
}

auto storage_result(std::expected<void, MemoryError> result, std::size_t index)
    -> std::expected<void, AddressSpaceError> {
  if (!result) {
    return std::unexpected(memory_error(AddressSpaceErrorCode::storage_failure,
                                        index, result.error()));
  }
  return {};
}

auto storage_result(std::expected<std::vector<std::byte>, MemoryError> result,
                    std::size_t index)
    -> std::expected<std::vector<std::byte>, AddressSpaceError> {
  if (!result) {
    return std::unexpected(memory_error(AddressSpaceErrorCode::storage_failure,
                                        index, result.error()));
  }
  return std::move(*result);
}

auto mbarrier_resource(std::weak_ptr<detail::AddressSpaceManagerState> state,
                       detail::AddressSpaceLocator locator)
    -> std::expected<ResourceLease, MBarrierError> {
  auto value = resource(std::move(state), locator);
  if (!value) {
    return std::unexpected(MBarrierError{MBarrierErrorCode::stale_shared_space,
                                         {},
                                         std::nullopt,
                                         locator.index});
  }
  return std::move(*value);
}

auto validate_mbarrier_address(const ResourceLease& resource, Address address,
                               std::size_t index)
    -> std::expected<void, MBarrierError> {
  auto valid = resource->region.validate(address, sizeof(std::uint64_t), 8);
  if (!valid) {
    return std::unexpected(MBarrierError{MBarrierErrorCode::invalid_address,
                                         address, valid.error(), index});
  }
  return {};
}

}  // namespace

AddressSpaceManager::AddressSpaceManager()
    : state_(std::make_shared<detail::AddressSpaceManagerState>(
          next_manager_token())) {}

AddressSpaceManager::~AddressSpaceManager() = default;

auto AddressSpaceManager::create_global(GlobalSpaceSpec spec)
    -> GlobalSpaceHandle {
  return state_->create<GlobalSpaceHandle>(
      detail::AddressSpaceKind::global, spec.capacity, RegionAccess::ReadWrite);
}

auto AddressSpaceManager::create_constant(ConstantSpaceSpec spec)
    -> ConstantSpaceHandle {
  return state_->create<ConstantSpaceHandle>(detail::AddressSpaceKind::constant,
                                             spec.capacity,
                                             RegionAccess::ReadOnly);
}

auto AddressSpaceManager::create_local_frame(LocalFrameSpec spec)
    -> LocalFrameHandle {
  return state_->create<LocalFrameHandle>(detail::AddressSpaceKind::local,
                                          spec.size, RegionAccess::ReadWrite);
}

auto AddressSpaceManager::create_entry_parameter(EntryParameterSpec spec)
    -> EntryParameterHandle {
  return state_->create<EntryParameterHandle>(
      detail::AddressSpaceKind::entry_parameter, spec.size,
      RegionAccess::ReadOnly);
}

auto AddressSpaceManager::create_function_parameter(FunctionParameterSpec spec)
    -> FunctionParameterHandle {
  return state_->create<FunctionParameterHandle>(
      detail::AddressSpaceKind::function_parameter, spec.size,
      RegionAccess::ReadWrite);
}

auto AddressSpaceManager::create_shared(SharedSpaceSpec spec)
    -> SharedSpaceHandle {
  return state_->create<SharedSpaceHandle>(detail::AddressSpaceKind::shared,
                                           spec.size, RegionAccess::ReadWrite);
}

auto AddressSpaceManager::destroy(GlobalSpaceHandle handle)
    -> std::expected<void, AddressSpaceError> {
  return state_->destroy(
      state_->locator(handle, detail::AddressSpaceKind::global));
}

auto AddressSpaceManager::destroy(ConstantSpaceHandle handle)
    -> std::expected<void, AddressSpaceError> {
  return state_->destroy(
      state_->locator(handle, detail::AddressSpaceKind::constant));
}

auto AddressSpaceManager::destroy(LocalFrameHandle handle)
    -> std::expected<void, AddressSpaceError> {
  return state_->destroy(
      state_->locator(handle, detail::AddressSpaceKind::local));
}

auto AddressSpaceManager::destroy(EntryParameterHandle handle)
    -> std::expected<void, AddressSpaceError> {
  return state_->destroy(
      state_->locator(handle, detail::AddressSpaceKind::entry_parameter));
}

auto AddressSpaceManager::destroy(FunctionParameterHandle handle)
    -> std::expected<void, AddressSpaceError> {
  return state_->destroy(
      state_->locator(handle, detail::AddressSpaceKind::function_parameter));
}

auto AddressSpaceManager::destroy(SharedSpaceHandle handle)
    -> std::expected<void, AddressSpaceError> {
  return state_->destroy(
      state_->locator(handle, detail::AddressSpaceKind::shared));
}

auto AddressSpaceManager::view(GlobalSpaceHandle handle)
    -> std::expected<AddressSpaceView, AddressSpaceError> {
  const auto locator =
      state_->locator(handle, detail::AddressSpaceKind::global);
  if (auto result = state_->find(locator); !result) {
    return std::unexpected(result.error());
  }
  return AddressSpaceView{state_, locator};
}

auto AddressSpaceManager::view(ConstantSpaceHandle handle)
    -> std::expected<AddressSpaceView, AddressSpaceError> {
  const auto locator =
      state_->locator(handle, detail::AddressSpaceKind::constant);
  if (auto result = state_->find(locator); !result) {
    return std::unexpected(result.error());
  }
  return AddressSpaceView{state_, locator};
}

auto AddressSpaceManager::view(LocalFrameHandle handle)
    -> std::expected<AddressSpaceView, AddressSpaceError> {
  const auto locator = state_->locator(handle, detail::AddressSpaceKind::local);
  if (auto result = state_->find(locator); !result) {
    return std::unexpected(result.error());
  }
  return AddressSpaceView{state_, locator};
}

auto AddressSpaceManager::view(EntryParameterHandle handle)
    -> std::expected<AddressSpaceView, AddressSpaceError> {
  const auto locator =
      state_->locator(handle, detail::AddressSpaceKind::entry_parameter);
  if (auto result = state_->find(locator); !result) {
    return std::unexpected(result.error());
  }
  return AddressSpaceView{state_, locator};
}

auto AddressSpaceManager::view(FunctionParameterHandle handle)
    -> std::expected<AddressSpaceView, AddressSpaceError> {
  const auto locator =
      state_->locator(handle, detail::AddressSpaceKind::function_parameter);
  if (auto result = state_->find(locator); !result) {
    return std::unexpected(result.error());
  }
  return AddressSpaceView{state_, locator};
}

auto AddressSpaceManager::view(SharedSpaceHandle handle)
    -> std::expected<AddressSpaceView, AddressSpaceError> {
  const auto locator =
      state_->locator(handle, detail::AddressSpaceKind::shared);
  if (auto result = state_->find(locator); !result) {
    return std::unexpected(result.error());
  }
  return AddressSpaceView{state_, locator};
}

auto AddressSpaceManager::view(GlobalSpaceHandle handle) const
    -> std::expected<ConstAddressSpaceView, AddressSpaceError> {
  const auto locator =
      state_->locator(handle, detail::AddressSpaceKind::global);
  if (auto result = state_->find(locator); !result) {
    return std::unexpected(result.error());
  }
  return ConstAddressSpaceView{state_, locator};
}

auto AddressSpaceManager::view(ConstantSpaceHandle handle) const
    -> std::expected<ConstAddressSpaceView, AddressSpaceError> {
  const auto locator =
      state_->locator(handle, detail::AddressSpaceKind::constant);
  if (auto result = state_->find(locator); !result) {
    return std::unexpected(result.error());
  }
  return ConstAddressSpaceView{state_, locator};
}

auto AddressSpaceManager::view(LocalFrameHandle handle) const
    -> std::expected<ConstAddressSpaceView, AddressSpaceError> {
  const auto locator = state_->locator(handle, detail::AddressSpaceKind::local);
  if (auto result = state_->find(locator); !result) {
    return std::unexpected(result.error());
  }
  return ConstAddressSpaceView{state_, locator};
}

auto AddressSpaceManager::view(EntryParameterHandle handle) const
    -> std::expected<ConstAddressSpaceView, AddressSpaceError> {
  const auto locator =
      state_->locator(handle, detail::AddressSpaceKind::entry_parameter);
  if (auto result = state_->find(locator); !result) {
    return std::unexpected(result.error());
  }
  return ConstAddressSpaceView{state_, locator};
}

auto AddressSpaceManager::view(FunctionParameterHandle handle) const
    -> std::expected<ConstAddressSpaceView, AddressSpaceError> {
  const auto locator =
      state_->locator(handle, detail::AddressSpaceKind::function_parameter);
  if (auto result = state_->find(locator); !result) {
    return std::unexpected(result.error());
  }
  return ConstAddressSpaceView{state_, locator};
}

auto AddressSpaceManager::view(SharedSpaceHandle handle) const
    -> std::expected<ConstAddressSpaceView, AddressSpaceError> {
  const auto locator =
      state_->locator(handle, detail::AddressSpaceKind::shared);
  if (auto result = state_->find(locator); !result) {
    return std::unexpected(result.error());
  }
  return ConstAddressSpaceView{state_, locator};
}

auto AddressSpaceManager::mbarriers(SharedSpaceHandle handle)
    -> std::expected<MBarrierView, AddressSpaceError> {
  const auto locator =
      state_->locator(handle, detail::AddressSpaceKind::shared);
  if (auto result = state_->find(locator); !result) {
    return std::unexpected(result.error());
  }
  return MBarrierView{state_, locator};
}

auto AddressSpaceManager::allocate(GlobalSpaceHandle handle, std::size_t size,
                                   std::size_t alignment)
    -> std::expected<AddressRange, AddressSpaceError> {
  const auto locator =
      state_->locator(handle, detail::AddressSpaceKind::global);
  return state_->allocate(locator, size, alignment);
}

auto AddressSpaceManager::allocate(ConstantSpaceHandle handle, std::size_t size,
                                   std::size_t alignment)
    -> std::expected<AddressRange, AddressSpaceError> {
  const auto locator =
      state_->locator(handle, detail::AddressSpaceKind::constant);
  return state_->allocate(locator, size, alignment);
}

auto AddressSpaceView::size() const
    -> std::expected<std::size_t, AddressSpaceError> {
  auto value = resource(state_, locator_);
  if (!value) {
    return std::unexpected(value.error());
  }
  return (*value)->region.size();
}

auto AddressSpaceView::access() const
    -> std::expected<RegionAccess, AddressSpaceError> {
  auto value = resource(state_, locator_);
  if (!value) {
    return std::unexpected(value.error());
  }
  return (*value)->region.access();
}

auto AddressSpaceView::is_initialized(Address address, std::size_t size) const
    -> std::expected<bool, AddressSpaceError> {
  auto value = resource(state_, locator_);
  if (!value) {
    return std::unexpected(value.error());
  }
  if (auto valid = (*value)->region.validate(address, size); !valid) {
    return std::unexpected(memory_error(AddressSpaceErrorCode::storage_failure,
                                        locator_.index, valid.error()));
  }
  return (*value)->region.is_initialized(address, size);
}

auto AddressSpaceView::read(Address address, std::span<std::byte> destination,
                            std::size_t alignment,
                            ReadRequirement requirement) const
    -> std::expected<void, AddressSpaceError> {
  auto value = resource(state_, locator_);
  if (!value) {
    return std::unexpected(value.error());
  }
  return storage_result(
      (*value)->region.read(address, destination, alignment, requirement),
      locator_.index);
}

auto AddressSpaceView::write(Address address, std::span<const std::byte> source,
                             std::size_t alignment)
    -> std::expected<void, AddressSpaceError> {
  auto value = resource(state_, locator_);
  if (!value) {
    return std::unexpected(value.error());
  }
  return storage_result((*value)->region.write(address, source, alignment),
                        locator_.index);
}

auto AddressSpaceView::initialize(Address address,
                                  std::span<const std::byte> source)
    -> std::expected<void, AddressSpaceError> {
  auto value = resource(state_, locator_);
  if (!value) {
    return std::unexpected(value.error());
  }
  return storage_result((*value)->region.initialize(address, source),
                        locator_.index);
}

auto AddressSpaceView::snapshot(Address address, std::size_t size,
                                ReadRequirement requirement) const
    -> std::expected<std::vector<std::byte>, AddressSpaceError> {
  auto value = resource(state_, locator_);
  if (!value) {
    return std::unexpected(value.error());
  }
  return storage_result((*value)->region.snapshot(address, size, requirement),
                        locator_.index);
}

AddressSpaceView::operator ConstAddressSpaceView() const {
  return ConstAddressSpaceView{state_, locator_};
}

auto ConstAddressSpaceView::size() const
    -> std::expected<std::size_t, AddressSpaceError> {
  return AddressSpaceView{state_, locator_}.size();
}

auto ConstAddressSpaceView::access() const
    -> std::expected<RegionAccess, AddressSpaceError> {
  return AddressSpaceView{state_, locator_}.access();
}

auto ConstAddressSpaceView::is_initialized(Address address,
                                           std::size_t size) const
    -> std::expected<bool, AddressSpaceError> {
  return AddressSpaceView{state_, locator_}.is_initialized(address, size);
}

auto ConstAddressSpaceView::read(Address address,
                                 std::span<std::byte> destination,
                                 std::size_t alignment,
                                 ReadRequirement requirement) const
    -> std::expected<void, AddressSpaceError> {
  return AddressSpaceView{state_, locator_}.read(address, destination,
                                                 alignment, requirement);
}

auto ConstAddressSpaceView::snapshot(Address address, std::size_t size,
                                     ReadRequirement requirement) const
    -> std::expected<std::vector<std::byte>, AddressSpaceError> {
  return AddressSpaceView{state_, locator_}.snapshot(address, size,
                                                     requirement);
}

auto MBarrierView::init(Address address, std::uint32_t expected_arrivals) const
    -> std::expected<void, MBarrierError> {
  auto value = mbarrier_resource(state_, locator_);
  if (!value) {
    return std::unexpected(value.error());
  }
  if (auto valid = validate_mbarrier_address(*value, address, locator_.index);
      !valid) {
    return std::unexpected(valid.error());
  }
  if (expected_arrivals == 0 || expected_arrivals > MBarrierState::max_count) {
    return std::unexpected(
        MBarrierError{MBarrierErrorCode::invalid_arrival_count, address,
                      std::nullopt, locator_.index});
  }
  if ((*value)->mbarriers.contains(address.value)) {
    return std::unexpected(
        MBarrierError{MBarrierErrorCode::duplicate_initialization, address,
                      std::nullopt, locator_.index});
  }
  if (value->state->next_mbarrier_incarnation == 0) {
    return std::unexpected(
        MBarrierError{MBarrierErrorCode::incarnation_exhausted, address,
                      std::nullopt, locator_.index});
  }
  const auto incarnation = value->state->next_mbarrier_incarnation;
  (*value)->mbarriers.emplace(
      address.value,
      MBarrierState{expected_arrivals, value->state->token, incarnation});
  if (incarnation == std::numeric_limits<std::uint64_t>::max()) {
    value->state->next_mbarrier_incarnation = 0;
  } else {
    ++value->state->next_mbarrier_incarnation;
  }
  return {};
}

auto MBarrierView::invalidate(Address address) const
    -> std::expected<void, MBarrierError> {
  auto value = mbarrier_resource(state_, locator_);
  if (!value) {
    return std::unexpected(value.error());
  }
  if (auto valid = validate_mbarrier_address(*value, address, locator_.index);
      !valid) {
    return std::unexpected(valid.error());
  }
  if ((*value)->mbarriers.erase(address.value) == 0) {
    return std::unexpected(MBarrierError{MBarrierErrorCode::invalid_barrier,
                                         address, std::nullopt,
                                         locator_.index});
  }
  return {};
}

auto MBarrierView::arrive(Address address, std::uint32_t count) const
    -> std::expected<MBarrierToken, MBarrierError> {
  auto value = mbarrier_resource(state_, locator_);
  if (!value) {
    return std::unexpected(value.error());
  }
  if (auto valid = validate_mbarrier_address(*value, address, locator_.index);
      !valid) {
    return std::unexpected(valid.error());
  }
  const auto barrier = (*value)->mbarriers.find(address.value);
  if (barrier == (*value)->mbarriers.end()) {
    return std::unexpected(MBarrierError{MBarrierErrorCode::invalid_barrier,
                                         address, std::nullopt,
                                         locator_.index});
  }
  return barrier->second.arrive(address, count);
}

auto MBarrierView::expect_tx(Address address, std::uint32_t count) const
    -> std::expected<void, MBarrierError> {
  auto value = mbarrier_resource(state_, locator_);
  if (!value) {
    return std::unexpected(value.error());
  }
  if (auto valid = validate_mbarrier_address(*value, address, locator_.index);
      !valid) {
    return std::unexpected(valid.error());
  }
  const auto barrier = (*value)->mbarriers.find(address.value);
  if (barrier == (*value)->mbarriers.end()) {
    return std::unexpected(MBarrierError{MBarrierErrorCode::invalid_barrier,
                                         address, std::nullopt,
                                         locator_.index});
  }
  return barrier->second.expect_tx(address, count);
}

auto MBarrierView::complete_tx(Address address, std::uint32_t count) const
    -> std::expected<void, MBarrierError> {
  auto value = mbarrier_resource(state_, locator_);
  if (!value) {
    return std::unexpected(value.error());
  }
  if (auto valid = validate_mbarrier_address(*value, address, locator_.index);
      !valid) {
    return std::unexpected(valid.error());
  }
  const auto barrier = (*value)->mbarriers.find(address.value);
  if (barrier == (*value)->mbarriers.end()) {
    return std::unexpected(MBarrierError{MBarrierErrorCode::invalid_barrier,
                                         address, std::nullopt,
                                         locator_.index});
  }
  return barrier->second.complete_tx(address, count);
}

auto MBarrierView::test_wait(Address address, MBarrierToken token) const
    -> std::expected<bool, MBarrierError> {
  auto value = mbarrier_resource(state_, locator_);
  if (!value) {
    return std::unexpected(value.error());
  }
  if (auto valid = validate_mbarrier_address(*value, address, locator_.index);
      !valid) {
    return std::unexpected(valid.error());
  }
  const auto barrier = (*value)->mbarriers.find(address.value);
  if (barrier == (*value)->mbarriers.end()) {
    return std::unexpected(MBarrierError{MBarrierErrorCode::invalid_barrier,
                                         address, std::nullopt,
                                         locator_.index});
  }
  return barrier->second.test_wait(address, token);
}

auto MBarrierView::snapshot(Address address) const
    -> std::expected<MBarrierSnapshot, MBarrierError> {
  auto value = mbarrier_resource(state_, locator_);
  if (!value) {
    return std::unexpected(value.error());
  }
  if (auto valid = validate_mbarrier_address(*value, address, locator_.index);
      !valid) {
    return std::unexpected(valid.error());
  }
  const auto barrier = (*value)->mbarriers.find(address.value);
  if (barrier == (*value)->mbarriers.end()) {
    return std::unexpected(MBarrierError{MBarrierErrorCode::invalid_barrier,
                                         address, std::nullopt,
                                         locator_.index});
  }
  return barrier->second.snapshot();
}

}  // namespace ptxsim::memory
