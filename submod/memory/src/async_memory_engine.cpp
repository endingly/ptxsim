#include <ptxsim/memory/async/async_memory_engine.hpp>

#include <atomic>
#include <utility>

namespace ptxsim::memory {
namespace {

auto next_engine_token() noexcept -> std::uint64_t {
  static std::atomic<std::uint64_t> next{1};
  auto token = next.fetch_add(1, std::memory_order_relaxed);
  return token == 0 ? next.fetch_add(1, std::memory_order_relaxed) : token;
}

auto execute(CopyOp& operation) -> std::expected<void, AsyncMemoryError> {
  auto bytes = operation.source.snapshot(
      operation.source_offset, operation.size,
      ReadRequirement::RequireInitialized);
  if (!bytes) {
    return std::unexpected(AsyncMemoryError{
        AsyncMemoryErrorCode::source_failure, std::move(bytes.error())});
  }

  auto written = operation.destination.write(operation.destination_offset,
                                             *bytes);
  if (!written) {
    return std::unexpected(AsyncMemoryError{
        AsyncMemoryErrorCode::destination_failure,
        std::move(written.error())});
  }
  return {};
}

auto execute(AsyncMemoryOp& operation)
    -> std::expected<void, AsyncMemoryError> {
  return std::visit([](auto& value) { return execute(value); }, operation);
}

auto stale_error() -> AsyncMemoryError {
  return {AsyncMemoryErrorCode::stale_handle, std::nullopt};
}

}  // namespace

AsyncMemoryEngine::AsyncMemoryEngine() : token_(next_engine_token()) {}

auto AsyncMemoryEngine::issue(AsyncMemoryOp operation) -> AsyncMemoryHandle {
  const auto index = records_.size();
  records_.push_back(Record{std::move(operation), AsyncMemoryStatus::pending,
                            std::nullopt});
  return AsyncMemoryHandle{token_, index};
}

auto AsyncMemoryEngine::progress() -> std::optional<AsyncMemoryHandle> {
  if (next_pending_ == records_.size()) {
    return std::nullopt;
  }

  const auto index = next_pending_++;
  auto& record = records_[index];
  auto execution = execute(record.operation);
  if (execution) {
    record.status = AsyncMemoryStatus::completed;
  } else {
    record.status = AsyncMemoryStatus::failed;
    record.error = std::move(execution.error());
  }
  return AsyncMemoryHandle{token_, index};
}

auto AsyncMemoryEngine::find(AsyncMemoryHandle handle) const
    -> std::expected<const Record*, AsyncMemoryError> {
  if (handle.engine_token_ != token_ ||
      handle.record_index_ >= records_.size()) {
    return std::unexpected(stale_error());
  }
  return &records_[handle.record_index_];
}

auto AsyncMemoryEngine::status(AsyncMemoryHandle handle) const
    -> std::expected<AsyncMemoryStatus, AsyncMemoryError> {
  auto record = find(handle);
  if (!record) {
    return std::unexpected(record.error());
  }
  return (*record)->status;
}

auto AsyncMemoryEngine::result(AsyncMemoryHandle handle) const
    -> std::expected<void, AsyncMemoryError> {
  auto record = find(handle);
  if (!record) {
    return std::unexpected(record.error());
  }
  if ((*record)->status == AsyncMemoryStatus::pending) {
    return std::unexpected(AsyncMemoryError{
        AsyncMemoryErrorCode::pending_operation, std::nullopt});
  }
  if ((*record)->status == AsyncMemoryStatus::failed) {
    return std::unexpected(*(*record)->error);
  }
  return {};
}

}  // namespace ptxsim::memory
