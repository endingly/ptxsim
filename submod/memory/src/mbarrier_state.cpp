#include <ptxsim/memory/sync/mbarrier_state.hpp>

#include <limits>

namespace ptxsim::memory {
namespace {

auto error(MBarrierErrorCode code, Address address) -> MBarrierError {
  return {code, address, std::nullopt, std::nullopt};
}

}  // namespace

auto MBarrierState::finish_update(Address address,
                                  std::uint32_t pending_arrivals,
                                  std::int64_t transaction_count)
    -> std::expected<void, MBarrierError> {
  if (pending_arrivals == 0 && transaction_count == 0) {
    if (phase_ == std::numeric_limits<std::uint64_t>::max()) {
      return std::unexpected(error(MBarrierErrorCode::phase_overflow, address));
    }
    pending_arrivals_ = expected_arrivals_;
    transaction_count_ = 0;
    ++phase_;
    return {};
  }

  pending_arrivals_ = pending_arrivals;
  transaction_count_ = transaction_count;
  return {};
}

auto MBarrierState::arrive(Address address, std::uint32_t count)
    -> std::expected<MBarrierToken, MBarrierError> {
  if (count == 0 || count > max_count) {
    return std::unexpected(
        error(MBarrierErrorCode::invalid_arrival_count, address));
  }
  if (count > pending_arrivals_) {
    return std::unexpected(
        error(MBarrierErrorCode::arrival_underflow, address));
  }
  const auto token = MBarrierToken{manager_token_, incarnation_, phase_};
  auto update =
      finish_update(address, pending_arrivals_ - count, transaction_count_);
  if (!update) {
    return std::unexpected(update.error());
  }
  return token;
}

auto MBarrierState::expect_tx(Address address, std::uint32_t count)
    -> std::expected<void, MBarrierError> {
  const auto amount = static_cast<std::int64_t>(count);
  const auto maximum = static_cast<std::int64_t>(max_count);
  if (transaction_count_ > maximum - amount) {
    return std::unexpected(
        error(MBarrierErrorCode::transaction_overflow, address));
  }
  return finish_update(address, pending_arrivals_, transaction_count_ + amount);
}

auto MBarrierState::complete_tx(Address address, std::uint32_t count)
    -> std::expected<void, MBarrierError> {
  const auto amount = static_cast<std::int64_t>(count);
  const auto maximum = static_cast<std::int64_t>(max_count);
  if (transaction_count_ < -maximum + amount) {
    return std::unexpected(
        error(MBarrierErrorCode::transaction_overflow, address));
  }
  return finish_update(address, pending_arrivals_, transaction_count_ - amount);
}

auto MBarrierState::test_wait(Address address, MBarrierToken token) const
    -> std::expected<bool, MBarrierError> {
  if (token.manager_token_ != manager_token_ ||
      token.incarnation_ != incarnation_) {
    return std::unexpected(error(MBarrierErrorCode::invalid_token, address));
  }
  return token.phase_ != phase_;
}

auto MBarrierState::snapshot() const noexcept -> MBarrierSnapshot {
  return {expected_arrivals_, pending_arrivals_, transaction_count_, phase_};
}

}  // namespace ptxsim::memory
