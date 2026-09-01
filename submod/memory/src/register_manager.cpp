#include <ptxsim/memory/register/register_manager.hpp>

#include <atomic>
#include <limits>
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

auto valid_width(common::RawWidth width) noexcept -> bool {
  switch (width) {
    case common::RawWidth::pred:
    case common::RawWidth::b8:
    case common::RawWidth::b16:
    case common::RawWidth::b32:
    case common::RawWidth::b64:
    case common::RawWidth::b128:
      return true;
  }
  return false;
}

auto stale_error(std::size_t index) -> RegisterError {
  return {RegisterErrorCode::stale_frame,
          index,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          std::nullopt};
}

auto slot_error(std::size_t frame_index, common::RegisterSlot slot)
    -> RegisterError {
  return {RegisterErrorCode::slot_out_of_range,
          frame_index,
          slot,
          std::nullopt,
          std::nullopt,
          static_cast<std::size_t>(slot.value())};
}

}  // namespace

namespace detail {

struct RegisterManagerState {
  struct Slot {
    std::optional<RegisterFrame> frame;
    std::uint64_t generation = 1;
  };

  explicit RegisterManagerState(std::uint64_t token) : token(token) {}

  [[nodiscard]] auto find(const RegisterFrameHandle& handle)
      -> std::expected<RegisterFrame*, RegisterError> {
    if (handle.manager_token_ != token || handle.index_ >= frames.size()) {
      return std::unexpected(stale_error(handle.index_));
    }
    auto& slot = frames[handle.index_];
    if (!slot.frame || slot.generation != handle.generation_) {
      return std::unexpected(stale_error(handle.index_));
    }
    return &*slot.frame;
  }

  [[nodiscard]] auto find(const RegisterFrameHandle& handle) const
      -> std::expected<const RegisterFrame*, RegisterError> {
    if (handle.manager_token_ != token || handle.index_ >= frames.size()) {
      return std::unexpected(stale_error(handle.index_));
    }
    const auto& slot = frames[handle.index_];
    if (!slot.frame || slot.generation != handle.generation_) {
      return std::unexpected(stale_error(handle.index_));
    }
    return &*slot.frame;
  }

  std::uint64_t token;
  std::vector<Slot> frames;
  std::vector<std::size_t> free_indices;
};

}  // namespace detail

RegisterManager::RegisterManager()
    : state_(std::make_shared<detail::RegisterManagerState>(
          next_manager_token())) {}

RegisterManager::~RegisterManager() = default;

auto RegisterManager::create_frame(const RegisterFrameSpec& spec)
    -> std::expected<RegisterFrameHandle, RegisterError> {
  constexpr auto max_slot_count =
      std::uint64_t{std::numeric_limits<std::uint32_t>::max()} + 1;
  if constexpr (std::numeric_limits<std::size_t>::max() > max_slot_count) {
    if (spec.slot_widths.size() > static_cast<std::size_t>(max_slot_count)) {
      return std::unexpected(RegisterError{
          RegisterErrorCode::layout_size_not_representable, std::nullopt,
          std::nullopt, std::nullopt, std::nullopt, spec.slot_widths.size()});
    }
  }
  for (std::size_t index = 0; index < spec.slot_widths.size(); ++index) {
    if (!valid_width(spec.slot_widths[index])) {
      return std::unexpected(RegisterError{
          RegisterErrorCode::invalid_layout_width, std::nullopt, std::nullopt,
          std::nullopt, spec.slot_widths[index], index});
    }
  }

  std::size_t index;
  if (state_->free_indices.empty()) {
    index = state_->frames.size();
    state_->frames.emplace_back();
  } else {
    index = state_->free_indices.back();
    state_->free_indices.pop_back();
  }
  auto& slot = state_->frames[index];
  slot.frame = RegisterFrame{spec.slot_widths};
  return RegisterFrameHandle{state_->token, index, slot.generation};
}

auto RegisterManager::destroy_frame(RegisterFrameHandle handle)
    -> std::expected<void, RegisterError> {
  auto frame = state_->find(handle);
  if (!frame) {
    return std::unexpected(frame.error());
  }
  state_->frames[handle.index_].frame.reset();
  auto& generation = state_->frames[handle.index_].generation;
  if (generation != std::numeric_limits<std::uint64_t>::max()) {
    ++generation;
    state_->free_indices.push_back(handle.index_);
  }
  return {};
}

auto RegisterManager::view(RegisterFrameHandle handle)
    -> std::expected<RegisterView, RegisterError> {
  if (auto frame = state_->find(handle); !frame) {
    return std::unexpected(frame.error());
  }
  return RegisterView{state_, handle};
}

auto RegisterManager::view(RegisterFrameHandle handle) const
    -> std::expected<ConstRegisterView, RegisterError> {
  if (auto frame = state_->find(handle); !frame) {
    return std::unexpected(frame.error());
  }
  return ConstRegisterView{state_, handle};
}

auto RegisterView::read(common::RegisterSlot slot) const
    -> std::expected<common::RawValue, RegisterError> {
  auto state = state_.lock();
  if (!state) {
    return std::unexpected(stale_error(handle_.index_));
  }
  auto frame = state->find(handle_);
  if (!frame) {
    return std::unexpected(frame.error());
  }
  if (slot.value() >= (*frame)->values_.size()) {
    return std::unexpected(slot_error(handle_.index_, slot));
  }
  const auto& value = (*frame)->values_[slot.value()];
  if (!value) {
    return std::unexpected(
        RegisterError{RegisterErrorCode::uninitialized_read, handle_.index_,
                      slot, (*frame)->slot_widths_[slot.value()], std::nullopt,
                      static_cast<std::size_t>(slot.value())});
  }
  return *value;
}

auto RegisterView::write(common::RegisterSlot slot, common::RawValue value)
    -> std::expected<void, RegisterError> {
  auto state = state_.lock();
  if (!state) {
    return std::unexpected(stale_error(handle_.index_));
  }
  auto frame = state->find(handle_);
  if (!frame) {
    return std::unexpected(frame.error());
  }
  if (slot.value() >= (*frame)->values_.size()) {
    return std::unexpected(slot_error(handle_.index_, slot));
  }
  if ((*frame)->slot_widths_[slot.value()] != value.width()) {
    return std::unexpected(
        RegisterError{RegisterErrorCode::width_mismatch, handle_.index_, slot,
                      (*frame)->slot_widths_[slot.value()], value.width(),
                      static_cast<std::size_t>(slot.value())});
  }
  (*frame)->values_[slot.value()] = std::move(value);
  return {};
}

auto RegisterView::initialized(common::RegisterSlot slot) const
    -> std::expected<bool, RegisterError> {
  auto state = state_.lock();
  if (!state) {
    return std::unexpected(stale_error(handle_.index_));
  }
  auto frame = state->find(handle_);
  if (!frame) {
    return std::unexpected(frame.error());
  }
  if (slot.value() >= (*frame)->values_.size()) {
    return std::unexpected(slot_error(handle_.index_, slot));
  }
  return (*frame)->values_[slot.value()].has_value();
}

auto RegisterView::declared_width(common::RegisterSlot slot) const
    -> std::expected<common::RawWidth, RegisterError> {
  auto state = state_.lock();
  if (!state) {
    return std::unexpected(stale_error(handle_.index_));
  }
  auto frame = state->find(handle_);
  if (!frame) {
    return std::unexpected(frame.error());
  }
  if (slot.value() >= (*frame)->slot_widths_.size()) {
    return std::unexpected(slot_error(handle_.index_, slot));
  }
  return (*frame)->slot_widths_[slot.value()];
}

auto RegisterView::slot_count() const
    -> std::expected<std::size_t, RegisterError> {
  auto state = state_.lock();
  if (!state) {
    return std::unexpected(stale_error(handle_.index_));
  }
  auto frame = state->find(handle_);
  if (!frame) {
    return std::unexpected(frame.error());
  }
  return (*frame)->slot_count();
}

RegisterView::operator ConstRegisterView() const {
  return ConstRegisterView{state_, handle_};
}

auto ConstRegisterView::read(common::RegisterSlot slot) const
    -> std::expected<common::RawValue, RegisterError> {
  return RegisterView{state_, handle_}.read(slot);
}

auto ConstRegisterView::initialized(common::RegisterSlot slot) const
    -> std::expected<bool, RegisterError> {
  return RegisterView{state_, handle_}.initialized(slot);
}

auto ConstRegisterView::declared_width(common::RegisterSlot slot) const
    -> std::expected<common::RawWidth, RegisterError> {
  return RegisterView{state_, handle_}.declared_width(slot);
}

auto ConstRegisterView::slot_count() const
    -> std::expected<std::size_t, RegisterError> {
  return RegisterView{state_, handle_}.slot_count();
}

}  // namespace ptxsim::memory
