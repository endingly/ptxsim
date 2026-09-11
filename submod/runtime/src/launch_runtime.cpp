#include <ptxsim/runtime/runtime.hpp>

#include <algorithm>
#include <array>
#include <exception>
#include <limits>
#include <set>

namespace ptxsim::runtime {

static_assert(exec_ir::kStorageAddressSpaceSize ==
              memory::GenericAddressLayout::window_size);
namespace {

auto error(RuntimeBindingErrorCode code, RuntimeResourceKind resource)
    -> RuntimeBindingError {
  return {code, resource};
}

template <typename Handle>
auto missing(const std::optional<Handle>& handle, RuntimeResourceKind resource)
    -> std::expected<Handle, RuntimeBindingError> {
  if (!handle) {
    return std::unexpected(
        error(RuntimeBindingErrorCode::missing_binding, resource));
  }
  return *handle;
}

template <typename Key, typename Handle>
auto lookup(const std::map<Key, Handle>& bindings, const Key& key,
            RuntimeResourceKind resource)
    -> std::expected<Handle, RuntimeBindingError> {
  const auto found = bindings.find(key);
  if (found == bindings.end()) {
    return std::unexpected(
        error(RuntimeBindingErrorCode::missing_binding, resource));
  }
  return found->second;
}

/** @brief Construct a storage diagnostic retaining any available resource cause. */
auto storage_error(StorageErrorCode code,
                   std::optional<common::SymbolId> symbol = std::nullopt,
                   std::optional<RuntimeBindingError> runtime = std::nullopt,
                   std::optional<memory::AddressSpaceError> address =
                       std::nullopt) -> std::unexpected<StorageError> {
  return std::unexpected(StorageError{code, symbol, runtime, address});
}

/** @brief Return the next offset satisfying a declaration's byte alignment. */
auto aligned_end(std::size_t begin,
                 const exec_ir::StorageDeclaration& declaration)
    -> std::optional<std::size_t> {
  if (begin >
      std::numeric_limits<std::size_t>::max() - (declaration.alignment - 1U)) {
    return std::nullopt;
  }
  const auto offset =
      (begin + declaration.alignment - 1U) & ~(declaration.alignment - 1U);
  if (declaration.extent > std::numeric_limits<std::size_t>::max() - offset) {
    return std::nullopt;
  }
  return offset + declaration.extent;
}

/** @brief Map an executable declaration space to a direct instruction qualifier. */
auto matching_space(exec_ir::StorageSpace storage,
                    exec_ir::AddressSpace requested) -> bool {
  switch (storage) {
    case exec_ir::StorageSpace::global:
      return requested == exec_ir::AddressSpace::global;
    case exec_ir::StorageSpace::constant:
      return requested == exec_ir::AddressSpace::const_;
    case exec_ir::StorageSpace::shared:
      return requested == exec_ir::AddressSpace::shared;
    case exec_ir::StorageSpace::local:
      return requested == exec_ir::AddressSpace::local;
  }
  return false;
}

}  // namespace

LaunchRuntime::LaunchRuntime(execution_model::GridId id,
                             execution_model::GridShape shape)
    : grid_(id, shape) {}

auto LaunchRuntime::grid() noexcept -> execution_model::Grid& {
  return grid_;
}
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

auto LaunchRuntime::bind_tensor_memory(execution_model::CtaId cta,
                                       memory::TensorMemorySpaceHandle handle)
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

auto LaunchRuntime::bind_register_frame(execution_model::ThreadId thread,
                                        common::FunctionId function,
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

auto LaunchRuntime::bind_local_frame(execution_model::ThreadId thread,
                                     common::FunctionId function,
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

auto LaunchRuntime::prepare_storage(const exec_ir::ExecutableProgram& program,
                                    const StorageLaunchOptions& options)
    -> std::expected<void, StorageError> {
  const auto& declarations = program.storage_declarations();
  if (declarations.empty()) {
    return {};
  }
  if (storage_layout_) {
    return storage_error(*storage_layout_ == declarations
                             ? StorageErrorCode::already_prepared
                             : StorageErrorCode::different_program);
  }

  std::set<std::string> external_names;
  for (const auto& external : options.externals) {
    if (!external_names.insert(external.name).second) {
      return storage_error(StorageErrorCode::duplicate_external_binding);
    }
  }

  std::map<std::uint32_t, std::size_t> offsets;
  std::set<std::string> consumed_externals;
  std::size_t global_end = 0;
  std::size_t constant_end = 0;
  for (const auto& declaration : declarations) {
    if (declaration.initialization ==
        exec_ir::StorageInitialization::external) {
      if (declaration.dynamic_shared) {
        offsets.emplace(declaration.symbol.value(), 0U);
        continue;
      }
      const auto count =
          std::count_if(options.externals.begin(), options.externals.end(),
                        [&declaration](const auto& value) {
                          return value.name == declaration.name;
                        });
      if (count != 1) {
        return storage_error(count == 0
                                 ? StorageErrorCode::missing_external_binding
                                 : StorageErrorCode::ambiguous_external_binding,
                             declaration.symbol);
      }
      const auto found =
          std::find_if(options.externals.begin(), options.externals.end(),
                       [&declaration](const auto& value) {
                         return value.name == declaration.name;
                       });
      consumed_externals.insert(found->name);
      if (found->extent == 0U ||
          found->extent % declaration.external_extent_multiple != 0U ||
          (declaration.extent != 0U && found->extent != declaration.extent) ||
          found->offset % declaration.alignment != 0U ||
          found->offset >
              std::numeric_limits<std::size_t>::max() - found->extent) {
        return storage_error(StorageErrorCode::invalid_external_binding,
                             declaration.symbol);
      }
      if (found->offset + found->extent >=
          memory::GenericAddressLayout::window_size) {
        return storage_error(StorageErrorCode::invalid_external_binding,
                             declaration.symbol);
      }
      if (declaration.space == exec_ir::StorageSpace::global) {
        if (!global_)
          return storage_error(StorageErrorCode::missing_external_binding,
                               declaration.symbol);
        const auto view = address_spaces_.view(*global_);
        if (!view)
          return storage_error(StorageErrorCode::address_space_failure,
                               declaration.symbol, std::nullopt, view.error());
        const auto size = view->size();
        if (!size || found->offset + found->extent > *size)
          return storage_error(StorageErrorCode::invalid_external_binding,
                               declaration.symbol);
      } else if (declaration.space == exec_ir::StorageSpace::constant) {
        if (!constant_)
          return storage_error(StorageErrorCode::missing_external_binding,
                               declaration.symbol);
        const auto view = address_spaces_.view(*constant_);
        if (!view)
          return storage_error(StorageErrorCode::address_space_failure,
                               declaration.symbol, std::nullopt, view.error());
        const auto size = view->size();
        if (!size || found->offset + found->extent > *size)
          return storage_error(StorageErrorCode::invalid_external_binding,
                               declaration.symbol);
      }
      offsets.emplace(declaration.symbol.value(), found->offset);
      continue;
    }
    std::size_t* end = nullptr;
    if (declaration.space == exec_ir::StorageSpace::global)
      end = &global_end;
    if (declaration.space == exec_ir::StorageSpace::constant)
      end = &constant_end;
    if (end != nullptr) {
      const auto grown = aligned_end(*end, declaration);
      if (!grown)
        return storage_error(StorageErrorCode::invalid_symbol,
                             declaration.symbol);
      offsets.emplace(declaration.symbol.value(), *grown - declaration.extent);
      *end = *grown;
    }
  }
  for (const auto& external : options.externals) {
    if (!consumed_externals.contains(external.name)) {
      return storage_error(StorageErrorCode::invalid_external_binding);
    }
  }

  // Existing caller resources define the append base.  Recompute only the
  // launch-owned global/constant offsets after their live sizes are known.
  const auto plan_module_space =
      [&](exec_ir::StorageSpace space,
          std::size_t& required) -> std::expected<void, StorageError> {
    std::size_t begin = 0;
    if (space == exec_ir::StorageSpace::global && global_) {
      const auto view = address_spaces_.view(*global_);
      if (!view)
        return storage_error(StorageErrorCode::address_space_failure,
                             std::nullopt, std::nullopt, view.error());
      begin = *view->size();
    }
    if (space == exec_ir::StorageSpace::constant && constant_) {
      const auto view = address_spaces_.view(*constant_);
      if (!view)
        return storage_error(StorageErrorCode::address_space_failure,
                             std::nullopt, std::nullopt, view.error());
      begin = *view->size();
    }
    required = begin;
    for (const auto& declaration : declarations) {
      if (declaration.space != space ||
          declaration.initialization ==
              exec_ir::StorageInitialization::external)
        continue;
      const auto grown = aligned_end(required, declaration);
      if (!grown)
        return storage_error(StorageErrorCode::invalid_symbol,
                             declaration.symbol);
      offsets[declaration.symbol.value()] = *grown - declaration.extent;
      required = *grown;
    }
    return {};
  };
  if (auto planned =
          plan_module_space(exec_ir::StorageSpace::global, global_end);
      !planned)
    return std::unexpected(planned.error());
  if (auto planned =
          plan_module_space(exec_ir::StorageSpace::constant, constant_end);
      !planned)
    return std::unexpected(planned.error());
  if (global_end >= memory::GenericAddressLayout::window_size ||
      constant_end >= memory::GenericAddressLayout::window_size) {
    return storage_error(StorageErrorCode::invalid_symbol);
  }

  decltype(shared_storage_offsets_) shared_offsets;
  decltype(local_storage_offsets_) local_offsets;
  std::map<execution_model::CtaId, std::size_t> shared_sizes;
  std::map<ThreadFunction, std::size_t> local_sizes;
  // The same declaration may have a different offset in each prebound region.
  // Dynamic shared declarations alias one segment following all static data.
  std::vector<const exec_ir::StorageDeclaration*> shared_declarations;
  std::map<common::FunctionId, std::vector<const exec_ir::StorageDeclaration*>>
      local_declarations;
  for (const auto& declaration : declarations) {
    if (declaration.space == exec_ir::StorageSpace::shared)
      shared_declarations.push_back(&declaration);
    if (declaration.space == exec_ir::StorageSpace::local &&
        declaration.owner_function)
      local_declarations[*declaration.owner_function].push_back(&declaration);
  }
  for (const auto& cta : grid_) {
    if (!shared_declarations.empty()) {
      std::size_t shared_size = 0;
      const auto existing = shared_.find(cta.id());
      if (existing != shared_.end()) {
        const auto view = address_spaces_.view(existing->second);
        if (!view)
          return storage_error(StorageErrorCode::address_space_failure,
                               std::nullopt, std::nullopt, view.error());
        shared_size = *view->size();
      }
      std::size_t dynamic_alignment = 1;
      bool has_dynamic = false;
      for (const auto* declaration : shared_declarations) {
        if (declaration->dynamic_shared) {
          has_dynamic = true;
          dynamic_alignment =
              std::max(dynamic_alignment, declaration->alignment);
          continue;
        }
        const auto end = aligned_end(shared_size, *declaration);
        if (!end)
          return storage_error(StorageErrorCode::invalid_symbol,
                               declaration->symbol);
        shared_offsets[{cta.id(), declaration->symbol.value()}] =
            *end - declaration->extent;
        shared_size = *end;
      }
      if (has_dynamic) {
        const exec_ir::StorageDeclaration segment{
            .symbol = shared_declarations.front()->symbol,
            .extent = options.dynamic_shared_bytes,
            .alignment = dynamic_alignment};
        const auto end = aligned_end(shared_size, segment);
        if (!end)
          return storage_error(StorageErrorCode::invalid_symbol);
        for (const auto* declaration : shared_declarations)
          if (declaration->dynamic_shared)
            shared_offsets[{cta.id(), declaration->symbol.value()}] =
                *end - options.dynamic_shared_bytes;
        shared_size = *end;
      }
      if (shared_size >= memory::GenericAddressLayout::window_size)
        return storage_error(StorageErrorCode::invalid_symbol);
      shared_sizes.emplace(cta.id(), shared_size);
    }
    for (const auto& warp : cta)
      for (const auto& thread : warp) {
        for (const auto& [function, function_declarations] :
             local_declarations) {
          std::size_t local_size = 0;
          const auto existing = local_frames_.find({thread.id(), function});
          if (existing != local_frames_.end()) {
            const auto view = address_spaces_.view(existing->second);
            if (!view)
              return storage_error(StorageErrorCode::address_space_failure,
                                   std::nullopt, std::nullopt, view.error());
            local_size = *view->size();
          }
          for (const auto* declaration : function_declarations) {
            const auto end = aligned_end(local_size, *declaration);
            if (!end)
              return storage_error(StorageErrorCode::invalid_symbol,
                                   declaration->symbol);
            local_offsets[{{thread.id(), function},
                           declaration->symbol.value()}] =
                *end - declaration->extent;
            local_size = *end;
          }
          if (local_size >= memory::GenericAddressLayout::window_size)
            return storage_error(StorageErrorCode::invalid_symbol);
          local_sizes.emplace(ThreadFunction{thread.id(), function},
                              local_size);
        }
      }
  }

  const auto ensure_global = [&]() -> std::expected<void, StorageError> {
    if (global_end == 0U)
      return {};
    if (!global_) {
      try {
        const auto handle =
            address_spaces_.create_global({.capacity = global_end});
        if (const auto bound = bind_global(handle); !bound)
          return storage_error(StorageErrorCode::runtime_binding_failure,
                               std::nullopt, bound.error());
      } catch (const std::exception&) {
        return storage_error(StorageErrorCode::address_space_failure);
      }
      return {};
    }
    if (const auto grown = address_spaces_.grow(*global_, global_end); !grown)
      return storage_error(StorageErrorCode::address_space_failure,
                           std::nullopt, std::nullopt, grown.error());
    return {};
  };
  const auto ensure_constant = [&]() -> std::expected<void, StorageError> {
    if (constant_end == 0U)
      return {};
    if (!constant_) {
      try {
        const auto handle =
            address_spaces_.create_constant({.capacity = constant_end});
        if (const auto bound = bind_constant(handle); !bound)
          return storage_error(StorageErrorCode::runtime_binding_failure,
                               std::nullopt, bound.error());
      } catch (const std::exception&) {
        return storage_error(StorageErrorCode::address_space_failure);
      }
      return {};
    }
    if (const auto grown = address_spaces_.grow(*constant_, constant_end);
        !grown)
      return storage_error(StorageErrorCode::address_space_failure,
                           std::nullopt, std::nullopt, grown.error());
    return {};
  };
  if (auto ready = ensure_global(); !ready)
    return std::unexpected(ready.error());
  if (auto ready = ensure_constant(); !ready)
    return std::unexpected(ready.error());

  for (const auto& declaration : declarations) {
    if (declaration.initialization == exec_ir::StorageInitialization::external)
      continue;
    if (declaration.space != exec_ir::StorageSpace::global &&
        declaration.space != exec_ir::StorageSpace::constant)
      continue;
    auto handle = declaration.space == exec_ir::StorageSpace::global
                      ? address_spaces_.view(*global_)
                      : address_spaces_.view(*constant_);
    if (!handle)
      return storage_error(StorageErrorCode::address_space_failure,
                           declaration.symbol, std::nullopt, handle.error());
    const auto begin = offsets.at(declaration.symbol.value());
    if (declaration.initialization ==
        exec_ir::StorageInitialization::explicit_bytes) {
      if (const auto initialized = handle->initialize(
              memory::Address{begin}, declaration.initializer_bytes);
          !initialized)
        return storage_error(StorageErrorCode::address_space_failure,
                             declaration.symbol, std::nullopt,
                             initialized.error());
    } else {
      static constexpr std::array<std::byte, 4096> zero{};
      std::size_t initialized = 0;
      while (initialized != declaration.extent) {
        const auto count =
            std::min(zero.size(), declaration.extent - initialized);
        if (const auto result = handle->initialize(
                memory::Address{begin + initialized},
                std::span<const std::byte>{zero}.first(count));
            !result)
          return storage_error(StorageErrorCode::address_space_failure,
                               declaration.symbol, std::nullopt,
                               result.error());
        initialized += count;
      }
    }
  }

  for (const auto& [cta_id, shared_size] : shared_sizes) {
    const auto existing = shared_.find(cta_id);
    if (existing == shared_.end()) {
      try {
        const auto handle =
            address_spaces_.create_shared({.size = shared_size});
        if (const auto bound = bind_shared(cta_id, handle); !bound)
          return storage_error(StorageErrorCode::runtime_binding_failure,
                               std::nullopt, bound.error());
      } catch (const std::bad_alloc&) {
        return storage_error(StorageErrorCode::address_space_failure);
      } catch (const std::length_error&) {
        return storage_error(StorageErrorCode::address_space_failure);
      }
    } else if (const auto grown =
                   address_spaces_.grow(existing->second, shared_size);
               !grown) {
      return storage_error(StorageErrorCode::address_space_failure,
                           std::nullopt, std::nullopt, grown.error());
    }
  }
  for (const auto& [owner, local_size] : local_sizes) {
    const auto& [thread_id, function] = owner;
    const auto existing = local_frames_.find(owner);
    if (existing == local_frames_.end()) {
      try {
        const auto handle =
            address_spaces_.create_local_frame({.size = local_size});
        if (const auto bound = bind_local_frame(thread_id, function, handle);
            !bound)
          return storage_error(StorageErrorCode::runtime_binding_failure,
                               std::nullopt, bound.error());
      } catch (const std::bad_alloc&) {
        return storage_error(StorageErrorCode::address_space_failure);
      } catch (const std::length_error&) {
        return storage_error(StorageErrorCode::address_space_failure);
      }
    } else if (const auto grown =
                   address_spaces_.grow(existing->second, local_size);
               !grown) {
      return storage_error(StorageErrorCode::address_space_failure,
                           std::nullopt, std::nullopt, grown.error());
    }
  }
  shared_storage_offsets_ = std::move(shared_offsets);
  local_storage_offsets_ = std::move(local_offsets);
  storage_offsets_ = std::move(offsets);
  storage_layout_ = declarations;
  return {};
}

auto LaunchRuntime::resolve_symbol_address(
    exec_ir::SymbolRef symbol, execution_model::ThreadId thread,
    common::FunctionId function, exec_ir::AddressSpace requested) const
    -> std::expected<std::uint64_t, StorageError> {
  if (!storage_layout_ || !grid_.find_thread(thread))
    return storage_error(StorageErrorCode::invalid_symbol, symbol.id);
  const auto found = std::find_if(
      storage_layout_->begin(), storage_layout_->end(),
      [&symbol](const auto& value) { return value.symbol == symbol.id; });
  if (found == storage_layout_->end())
    return storage_error(StorageErrorCode::invalid_symbol, symbol.id);
  if (found->owner_function && *found->owner_function != function)
    return storage_error(StorageErrorCode::invalid_symbol, symbol.id);
  std::optional<std::size_t> offset;
  if (found->space == exec_ir::StorageSpace::shared) {
    const auto* topology_thread = grid_.find_thread(thread);
    if (!topology_thread)
      return storage_error(StorageErrorCode::invalid_symbol, symbol.id);
    const auto local = shared_storage_offsets_.find(
        {topology_thread->cta().id(), symbol.id.value()});
    if (local != shared_storage_offsets_.end())
      offset = local->second;
  } else if (found->space == exec_ir::StorageSpace::local) {
    const auto local =
        local_storage_offsets_.find({{thread, function}, symbol.id.value()});
    if (local != local_storage_offsets_.end())
      offset = local->second;
  } else if (const auto global = storage_offsets_.find(symbol.id.value());
             global != storage_offsets_.end()) {
    offset = global->second;
  }
  if (!offset)
    return storage_error(StorageErrorCode::invalid_symbol, symbol.id);
  if (requested == exec_ir::AddressSpace::generic) {
    switch (found->space) {
      case exec_ir::StorageSpace::global:
        return static_cast<std::uint64_t>(*offset);
      case exec_ir::StorageSpace::constant:
        return memory::GenericAddressLayout::constant_base + *offset;
      case exec_ir::StorageSpace::shared:
        return memory::GenericAddressLayout::shared_base + *offset;
      case exec_ir::StorageSpace::local:
        return memory::GenericAddressLayout::local_base + *offset;
    }
  }
  if (!matching_space(found->space, requested))
    return storage_error(StorageErrorCode::incompatible_address_space,
                         symbol.id);
  (void)thread;
  return static_cast<std::uint64_t>(*offset);
}

auto LaunchRuntime::resolve_symbol_offset(exec_ir::SymbolRef symbol,
                                          execution_model::ThreadId thread,
                                          common::FunctionId function) const
    -> std::expected<std::uint64_t, StorageError> {
  if (!storage_layout_ || !grid_.find_thread(thread))
    return storage_error(StorageErrorCode::invalid_symbol, symbol.id);
  const auto found = std::find_if(
      storage_layout_->begin(), storage_layout_->end(),
      [&symbol](const auto& value) { return value.symbol == symbol.id; });
  if (found == storage_layout_->end() ||
      (found->owner_function && *found->owner_function != function))
    return storage_error(StorageErrorCode::invalid_symbol, symbol.id);
  if (found->space == exec_ir::StorageSpace::shared) {
    const auto* topology_thread = grid_.find_thread(thread);
    if (!topology_thread)
      return storage_error(StorageErrorCode::invalid_symbol, symbol.id);
    const auto offset = shared_storage_offsets_.find(
        {topology_thread->cta().id(), symbol.id.value()});
    if (offset == shared_storage_offsets_.end())
      return storage_error(StorageErrorCode::invalid_symbol, symbol.id);
    return static_cast<std::uint64_t>(offset->second);
  }
  if (found->space == exec_ir::StorageSpace::local) {
    const auto offset =
        local_storage_offsets_.find({{thread, function}, symbol.id.value()});
    if (offset == local_storage_offsets_.end())
      return storage_error(StorageErrorCode::invalid_symbol, symbol.id);
    return static_cast<std::uint64_t>(offset->second);
  }
  const auto offset = storage_offsets_.find(symbol.id.value());
  if (offset == storage_offsets_.end())
    return storage_error(StorageErrorCode::invalid_symbol, symbol.id);
  return static_cast<std::uint64_t>(offset->second);
}

}  // namespace ptxsim::runtime
