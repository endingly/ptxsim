#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>

namespace ptxsim::memory {

namespace detail {
struct AddressSpaceManagerState;

enum class AddressSpaceKind : std::uint8_t {
  global,
  constant,
  local,
  entry_parameter,
  function_parameter,
  shared,
};

struct AddressSpaceLocator {
  std::uint64_t manager_token;
  std::size_t index;
  std::uint64_t generation;
  AddressSpaceKind kind;
};
}  // namespace detail

struct GlobalSpaceTag;
struct ConstantSpaceTag;
struct LocalFrameTag;
struct EntryParameterTag;
struct FunctionParameterTag;
struct SharedSpaceTag;

template <typename Tag>
concept AddressSpaceTag =
    std::same_as<Tag, GlobalSpaceTag> || std::same_as<Tag, ConstantSpaceTag> ||
    std::same_as<Tag, LocalFrameTag> || std::same_as<Tag, EntryParameterTag> ||
    std::same_as<Tag, FunctionParameterTag> ||
    std::same_as<Tag, SharedSpaceTag>;

template <AddressSpaceTag Tag>
class AddressSpaceHandle final {
 public:
  constexpr bool operator==(const AddressSpaceHandle&) const noexcept = default;

 private:
  constexpr AddressSpaceHandle(std::uint64_t manager_token, std::size_t index,
                               std::uint64_t generation) noexcept
      : manager_token_(manager_token), index_(index), generation_(generation) {}

  std::uint64_t manager_token_ = 0;
  std::size_t index_ = 0;
  std::uint64_t generation_ = 0;

  friend struct detail::AddressSpaceManagerState;
  friend class AddressSpaceManager;
};

using GlobalSpaceHandle = AddressSpaceHandle<GlobalSpaceTag>;
using ConstantSpaceHandle = AddressSpaceHandle<ConstantSpaceTag>;
using LocalFrameHandle = AddressSpaceHandle<LocalFrameTag>;
using EntryParameterHandle = AddressSpaceHandle<EntryParameterTag>;
using FunctionParameterHandle = AddressSpaceHandle<FunctionParameterTag>;
using SharedSpaceHandle = AddressSpaceHandle<SharedSpaceTag>;

template <typename Handle>
concept AddressSpaceHandleType =
    std::same_as<Handle, GlobalSpaceHandle> ||
    std::same_as<Handle, ConstantSpaceHandle> ||
    std::same_as<Handle, LocalFrameHandle> ||
    std::same_as<Handle, EntryParameterHandle> ||
    std::same_as<Handle, FunctionParameterHandle> ||
    std::same_as<Handle, SharedSpaceHandle>;

}  // namespace ptxsim::memory
