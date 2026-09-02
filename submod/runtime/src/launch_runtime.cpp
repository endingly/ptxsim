#include <ptxsim/runtime/runtime.hpp>

namespace ptxsim::runtime {
namespace {

auto error(RuntimeBindingErrorCode code, RuntimeResourceKind resource)
    -> RuntimeBindingError {
  return {code, resource};
}

template <typename Handle>
auto missing(const std::optional<Handle>& handle, RuntimeResourceKind resource)
    -> std::expected<Handle, RuntimeBindingError> {
  if (!handle) {
    return std::unexpected(error(RuntimeBindingErrorCode::missing_binding,
                                 resource));
  }
  return *handle;
}

template <typename Key, typename Handle>
auto lookup(const std::map<Key, Handle>& bindings, const Key& key,
            RuntimeResourceKind resource)
    -> std::expected<Handle, RuntimeBindingError> {
  const auto found = bindings.find(key);
  if (found == bindings.end()) {
    return std::unexpected(error(RuntimeBindingErrorCode::missing_binding,
                                 resource));
  }
  return found->second;
}

}  // namespace

LaunchRuntime::LaunchRuntime(execution_model::GridId id,
                             execution_model::GridShape shape)
    : grid_(id, shape) {}

auto LaunchRuntime::grid() noexcept -> execution_model::Grid& { return grid_; }
auto LaunchRuntime::grid() const noexcept -> const execution_model::Grid& {
  return grid_;
}
auto LaunchRuntime::registers() noexcept -> memory::RegisterManager& {
  return registers_;
}
auto LaunchRuntime::registers() const noexcept
    -> const memory::RegisterManager& {
  return registers_;
}
auto LaunchRuntime::address_spaces() noexcept -> memory::AddressSpaceManager& {
  return address_spaces_;
}
auto LaunchRuntime::address_spaces() const noexcept
    -> const memory::AddressSpaceManager& {
  return address_spaces_;
}
auto LaunchRuntime::tensor_memory() noexcept -> memory::TensorMemoryManager& {
  return tensor_memory_;
}
auto LaunchRuntime::tensor_memory() const noexcept
    -> const memory::TensorMemoryManager& {
  return tensor_memory_;
}
auto LaunchRuntime::async_memory() noexcept -> memory::AsyncMemoryEngine& {
  return async_memory_;
}
auto LaunchRuntime::async_memory() const noexcept
    -> const memory::AsyncMemoryEngine& {
  return async_memory_;
}

auto LaunchRuntime::bind_global(memory::GlobalSpaceHandle handle)
    -> std::expected<void, RuntimeBindingError> {
  if (!address_spaces_.view(handle)) {
    return std::unexpected(error(RuntimeBindingErrorCode::invalid_resource,
                                 RuntimeResourceKind::global));
  }
  if (global_) {
    return std::unexpected(error(RuntimeBindingErrorCode::duplicate_binding,
                                 RuntimeResourceKind::global));
  }
  global_ = handle;
  return {};
}

auto LaunchRuntime::bind_constant(memory::ConstantSpaceHandle handle)
    -> std::expected<void, RuntimeBindingError> {
  if (!address_spaces_.view(handle)) {
    return std::unexpected(error(RuntimeBindingErrorCode::invalid_resource,
                                 RuntimeResourceKind::constant));
  }
  if (constant_) {
    return std::unexpected(error(RuntimeBindingErrorCode::duplicate_binding,
                                 RuntimeResourceKind::constant));
  }
  constant_ = handle;
  return {};
}

auto LaunchRuntime::bind_entry_parameter(memory::EntryParameterHandle handle)
    -> std::expected<void, RuntimeBindingError> {
  if (!address_spaces_.view(handle)) {
    return std::unexpected(error(RuntimeBindingErrorCode::invalid_resource,
                                 RuntimeResourceKind::entry_parameter));
  }
  if (entry_parameter_) {
    return std::unexpected(error(RuntimeBindingErrorCode::duplicate_binding,
                                 RuntimeResourceKind::entry_parameter));
  }
  entry_parameter_ = handle;
  return {};
}

auto LaunchRuntime::bind_shared(execution_model::CtaId cta,
                                memory::SharedSpaceHandle handle)
    -> std::expected<void, RuntimeBindingError> {
  if (!grid_.find_cta(cta)) {
    return std::unexpected(error(RuntimeBindingErrorCode::foreign_topology,
                                 RuntimeResourceKind::shared));
  }
  if (!address_spaces_.view(handle)) {
    return std::unexpected(error(RuntimeBindingErrorCode::invalid_resource,
                                 RuntimeResourceKind::shared));
  }
  if (shared_.contains(cta)) {
    return std::unexpected(error(RuntimeBindingErrorCode::duplicate_binding,
                                 RuntimeResourceKind::shared));
  }
  shared_.emplace(cta, handle);
  return {};
}

auto LaunchRuntime::bind_tensor_memory(
    execution_model::CtaId cta, memory::TensorMemorySpaceHandle handle)
    -> std::expected<void, RuntimeBindingError> {
  if (!grid_.find_cta(cta)) {
    return std::unexpected(error(RuntimeBindingErrorCode::foreign_topology,
                                 RuntimeResourceKind::tensor_memory));
  }
  if (!tensor_memory_.allocation_permitted(handle)) {
    return std::unexpected(error(RuntimeBindingErrorCode::invalid_resource,
                                 RuntimeResourceKind::tensor_memory));
  }
  if (tensor_spaces_.contains(cta)) {
    return std::unexpected(error(RuntimeBindingErrorCode::duplicate_binding,
                                 RuntimeResourceKind::tensor_memory));
  }
  tensor_spaces_.emplace(cta, handle);
  return {};
}

auto LaunchRuntime::bind_register_frame(
    execution_model::ThreadId thread, common::FunctionId function,
    memory::RegisterFrameHandle handle)
    -> std::expected<void, RuntimeBindingError> {
  if (!grid_.find_thread(thread)) {
    return std::unexpected(error(RuntimeBindingErrorCode::foreign_topology,
                                 RuntimeResourceKind::register_frame));
  }
  if (!registers_.view(handle)) {
    return std::unexpected(error(RuntimeBindingErrorCode::invalid_resource,
                                 RuntimeResourceKind::register_frame));
  }
  const ThreadFunction key{thread, function};
  if (register_frames_.contains(key)) {
    return std::unexpected(error(RuntimeBindingErrorCode::duplicate_binding,
                                 RuntimeResourceKind::register_frame));
  }
  register_frames_.emplace(key, handle);
  return {};
}

auto LaunchRuntime::bind_local_frame(
    execution_model::ThreadId thread, common::FunctionId function,
    memory::LocalFrameHandle handle)
    -> std::expected<void, RuntimeBindingError> {
  if (!grid_.find_thread(thread)) {
    return std::unexpected(error(RuntimeBindingErrorCode::foreign_topology,
                                 RuntimeResourceKind::local_frame));
  }
  if (!address_spaces_.view(handle)) {
    return std::unexpected(error(RuntimeBindingErrorCode::invalid_resource,
                                 RuntimeResourceKind::local_frame));
  }
  const ThreadFunction key{thread, function};
  if (local_frames_.contains(key)) {
    return std::unexpected(error(RuntimeBindingErrorCode::duplicate_binding,
                                 RuntimeResourceKind::local_frame));
  }
  local_frames_.emplace(key, handle);
  return {};
}

auto LaunchRuntime::global() const
    -> std::expected<memory::GlobalSpaceHandle, RuntimeBindingError> {
  return missing(global_, RuntimeResourceKind::global);
}
auto LaunchRuntime::constant() const
    -> std::expected<memory::ConstantSpaceHandle, RuntimeBindingError> {
  return missing(constant_, RuntimeResourceKind::constant);
}
auto LaunchRuntime::entry_parameter() const
    -> std::expected<memory::EntryParameterHandle, RuntimeBindingError> {
  return missing(entry_parameter_, RuntimeResourceKind::entry_parameter);
}
auto LaunchRuntime::shared(execution_model::CtaId cta) const
    -> std::expected<memory::SharedSpaceHandle, RuntimeBindingError> {
  if (!grid_.find_cta(cta)) {
    return std::unexpected(error(RuntimeBindingErrorCode::foreign_topology,
                                 RuntimeResourceKind::shared));
  }
  return lookup(shared_, cta, RuntimeResourceKind::shared);
}
auto LaunchRuntime::tensor_memory_space(execution_model::CtaId cta) const
    -> std::expected<memory::TensorMemorySpaceHandle, RuntimeBindingError> {
  if (!grid_.find_cta(cta)) {
    return std::unexpected(error(RuntimeBindingErrorCode::foreign_topology,
                                 RuntimeResourceKind::tensor_memory));
  }
  return lookup(tensor_spaces_, cta, RuntimeResourceKind::tensor_memory);
}
auto LaunchRuntime::register_frame(execution_model::ThreadId thread,
                                   common::FunctionId function) const
    -> std::expected<memory::RegisterFrameHandle, RuntimeBindingError> {
  if (!grid_.find_thread(thread)) {
    return std::unexpected(error(RuntimeBindingErrorCode::foreign_topology,
                                 RuntimeResourceKind::register_frame));
  }
  return lookup(register_frames_, ThreadFunction{thread, function},
                RuntimeResourceKind::register_frame);
}
auto LaunchRuntime::local_frame(execution_model::ThreadId thread,
                                common::FunctionId function) const
    -> std::expected<memory::LocalFrameHandle, RuntimeBindingError> {
  if (!grid_.find_thread(thread)) {
    return std::unexpected(error(RuntimeBindingErrorCode::foreign_topology,
                                 RuntimeResourceKind::local_frame));
  }
  return lookup(local_frames_, ThreadFunction{thread, function},
                RuntimeResourceKind::local_frame);
}

auto LaunchRuntime::address_context(execution_model::ThreadId thread,
                                    common::FunctionId function) const
    -> std::expected<memory::ExecutionAddressContext, RuntimeBindingError> {
  const auto* topology_thread = grid_.find_thread(thread);
  if (!topology_thread) {
    return std::unexpected(error(RuntimeBindingErrorCode::foreign_topology,
                                 RuntimeResourceKind::local_frame));
  }
  memory::ExecutionAddressContext context{
      .global = global_,
      .constant = constant_,
      .entry_parameter = entry_parameter_,
      .local = std::nullopt,
      .shared = std::nullopt,
  };
  if (const auto local = local_frames_.find({thread, function});
      local != local_frames_.end()) {
    context.local = local->second;
  }
  if (const auto shared = shared_.find(topology_thread->cta().id());
      shared != shared_.end()) {
    context.shared = shared->second;
  }
  return context;
}

}  // namespace ptxsim::runtime
