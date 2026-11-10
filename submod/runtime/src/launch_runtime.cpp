#include <ptxsim/runtime/runtime.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <exception>
#include <limits>
#include <new>
#include <set>
#include <stdexcept>
#include <ptxsim/exec_ir/exec_ir_types.hpp>

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
                   std::optional<StorageTarget> symbol = std::nullopt,
                   std::optional<RuntimeBindingError> runtime = std::nullopt,
                   std::optional<memory::AddressSpaceError> address =
                       std::nullopt) -> std::unexpected<StorageError> {
  return std::unexpected(StorageError{code, symbol, runtime, address});
}

/** @brief Supported allocation plan derived from a complete immutable declaration. */
struct AllocationDeclaration {
  /** @brief Owned storage identity, independent of frontend symbol numbering. */
  exec_ir::StorageId symbol{0};
  /** @brief Boundary spelling used only to match an explicit external binding. */
  std::string name;
  /** @brief Real materialized byte-address space. */
  exec_ir::StorageSpace space;
  /** @brief Canonical owner for function-local storage. */
  std::optional<common::FunctionId> owner_function;
  /** @brief Byte extent of one supported allocated object. */
  std::size_t extent;
  /** @brief Effective power-of-two byte alignment. */
  std::size_t alignment;
  /** @brief Actual initialization policy retained from the full declaration. */
  exec_ir::StorageInitialization initialization;
  /** @brief Complete byte image derived only for supported explicit constants. */
  std::vector<std::byte> initializer_bytes;
  /** @brief Dynamic shared allocation consumes launch-selected bytes. */
  bool dynamic_shared;
  /** @brief Scalar/vector/inner-array byte multiple for an unsized external. */
  std::size_t external_extent_multiple;
};

/** @brief Derive supported allocation inputs without discarding full IR metadata. */
[[nodiscard]] auto allocation_declarations(std::span<const exec_ir::StorageDeclaration> sources)
    -> std::expected<std::vector<AllocationDeclaration>, StorageError> try {
  std::vector<AllocationDeclaration> result;
  for (const auto& source : sources) {
    if (source.byte_extent &&
        (*source.byte_extent >= memory::GenericAddressLayout::window_size ||
         *source.byte_extent > std::numeric_limits<std::size_t>::max()))
      return storage_error(StorageErrorCode::invalid_symbol, source.id);
    const auto* type = std::get_if<exec_ir::DataType>(&source.element_type);
    if (!type || source.parameterized_count || source.managed || source.unified_id ||
        (source.vector_width != 1 && source.vector_width != 2 && source.vector_width != 4) ||
        !source.alignment || !std::has_single_bit(*source.alignment) ||
        *source.alignment > std::numeric_limits<std::size_t>::max() ||
        (source.space == exec_ir::StorageSpace::shared &&
         source.initialization == exec_ir::StorageInitialization::external && source.byte_extent) ||
        (source.byte_extent == 0 && source.initialization != exec_ir::StorageInitialization::explicit_values) ||
        (!source.byte_extent && source.initialization != exec_ir::StorageInitialization::external))
      return storage_error(StorageErrorCode::unsupported_declaration, source.id);
    const auto width = exec_ir::data_type_width(*type);
    if (!width || *width == common::RawWidth::pred)
      return storage_error(StorageErrorCode::unsupported_declaration, source.id);
    std::size_t scalar_size = 0;
    switch (*width) {
      case common::RawWidth::b8: scalar_size = 1; break;
      case common::RawWidth::b16: scalar_size = 2; break;
      case common::RawWidth::b32: scalar_size = 4; break;
      case common::RawWidth::b64: scalar_size = 8; break;
      case common::RawWidth::b128: scalar_size = 16; break;
      case common::RawWidth::pred: break;
    }
    std::size_t multiple = scalar_size * source.vector_width;
    for (const auto axis : source.array_extents) {
      if (!axis) continue;
      if (*axis == 0 || *axis > std::numeric_limits<std::size_t>::max() / multiple)
        return storage_error(StorageErrorCode::unsupported_declaration, source.id);
      multiple *= static_cast<std::size_t>(*axis);
    }
    AllocationDeclaration target{source.id, source.name, source.space, source.owner_function,
        static_cast<std::size_t>(source.byte_extent.value_or(0)), static_cast<std::size_t>(*source.alignment),
        source.initialization, {}, source.dynamic_shared,
        !source.byte_extent && !source.dynamic_shared ? multiple : 1};
    if (source.initialization == exec_ir::StorageInitialization::opaque_static)
      return storage_error(StorageErrorCode::unsupported_declaration, source.id);
    for (const auto& element : source.initializer)
      if (!std::holds_alternative<exec_ir::Constant128>(element.value))
        return storage_error(StorageErrorCode::unsupported_relocation, source.id);
    if (source.initialization == exec_ir::StorageInitialization::explicit_values) {
      target.initializer_bytes.assign(target.extent, std::byte{0});
      std::set<std::uint64_t> initialized;
      for (const auto& element : source.initializer) {
        const auto& constant = std::get<exec_ir::Constant128>(element.value);
        if (element.byte_offset % scalar_size != 0 || element.byte_offset > target.extent ||
            scalar_size > target.extent - element.byte_offset ||
            !initialized.insert(element.byte_offset).second ||
            (scalar_size <= 8 && constant.high_bits != 0) ||
            (scalar_size < 8 && (constant.bits >> (scalar_size * 8)) != 0))
          return storage_error(StorageErrorCode::invalid_symbol, source.id);
        for (std::size_t byte = 0; byte < scalar_size; ++byte) {
          const auto word = byte < 8 ? constant.bits : constant.high_bits;
          target.initializer_bytes[static_cast<std::size_t>(element.byte_offset) + byte] =
              std::byte{static_cast<unsigned char>(word >> (8 * (byte % 8)))};
        }
      }
    }
    result.push_back(std::move(target));
  }
  return result;
} catch (const std::bad_alloc&) {
  return storage_error(StorageErrorCode::address_space_failure);
} catch (const std::length_error&) {
  return storage_error(StorageErrorCode::address_space_failure);
}

/** @brief Return the next offset satisfying a declaration's byte alignment. */
auto aligned_end(std::size_t begin,
                 const AllocationDeclaration& declaration)
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
  const auto full_declarations = program.storage_declarations();
  if (storage_layout_) {
    return storage_error(std::ranges::equal(*storage_layout_, full_declarations)
                             ? StorageErrorCode::already_prepared
                             : StorageErrorCode::different_program);
  }
  decltype(parameter_offsets_) parameter_offsets;
  for (const auto& function : program.functions()) {
    if (!function.is_entry || !function.body) continue;
    for (const auto& parameter : function.body->entry_parameters)
      parameter_offsets.emplace(parameter.parameter, std::pair{function.id, parameter.offset});
  }
  if (full_declarations.empty()) {
    parameter_offsets_ = std::move(parameter_offsets);
    return {};
  }
  const auto allocation = allocation_declarations(full_declarations);
  if (!allocation) return std::unexpected(allocation.error());
  const auto& declarations = *allocation;

  std::set<std::string> declaration_names;
  for (const auto& declaration : declarations)
    if (declaration.initialization == exec_ir::StorageInitialization::external &&
        !declaration.dynamic_shared && !declaration_names.insert(declaration.name).second)
      return storage_error(StorageErrorCode::ambiguous_external_binding, declaration.symbol);

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
  std::vector<const AllocationDeclaration*> shared_declarations;
  std::map<common::FunctionId, std::vector<const AllocationDeclaration*>>
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
        const AllocationDeclaration segment{
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
        if (const auto reserved = address_spaces_.grow(handle, global_end);
            !reserved)
          return storage_error(StorageErrorCode::address_space_failure,
                               std::nullopt, std::nullopt, reserved.error());
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
        if (const auto reserved = address_spaces_.grow(handle, constant_end);
            !reserved)
          return storage_error(StorageErrorCode::address_space_failure,
                               std::nullopt, std::nullopt, reserved.error());
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
        exec_ir::StorageInitialization::explicit_values) {
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
        if (const auto reserved = address_spaces_.grow(handle, shared_size);
            !reserved)
          return storage_error(StorageErrorCode::address_space_failure,
                               std::nullopt, std::nullopt, reserved.error());
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
        if (const auto reserved = address_spaces_.grow(handle, local_size);
            !reserved)
          return storage_error(StorageErrorCode::address_space_failure,
                               std::nullopt, std::nullopt, reserved.error());
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
  parameter_offsets_ = std::move(parameter_offsets);
  storage_layout_.emplace(full_declarations.begin(), full_declarations.end());
  return {};
}

auto LaunchRuntime::resolve_symbol_address(
    exec_ir::SymbolRef symbol, execution_model::ThreadId thread,
    common::FunctionId function, exec_ir::AddressSpace requested) const
    -> std::expected<std::uint64_t, StorageError> {
  const auto offset = resolve_symbol_offset(symbol, thread, function);
  if (!offset) return std::unexpected(offset.error());
  if (std::holds_alternative<exec_ir::ParameterId>(symbol.target)) {
    if (requested == exec_ir::AddressSpace::generic)
      return memory::GenericAddressLayout::entry_parameter_base + *offset;
    if (requested == exec_ir::AddressSpace::param || requested == exec_ir::AddressSpace::param_entry)
      return *offset;
    return storage_error(StorageErrorCode::incompatible_address_space, symbol.target);
  }
  const auto id = std::get<exec_ir::StorageId>(symbol.target);
  const auto found = std::find_if(storage_layout_->begin(), storage_layout_->end(),
      [id](const auto& value) { return value.id == id; });
  if (requested == exec_ir::AddressSpace::generic) {
    switch (found->space) {
      case exec_ir::StorageSpace::global: return *offset;
      case exec_ir::StorageSpace::constant: return memory::GenericAddressLayout::constant_base + *offset;
      case exec_ir::StorageSpace::shared: return memory::GenericAddressLayout::shared_base + *offset;
      case exec_ir::StorageSpace::local: return memory::GenericAddressLayout::local_base + *offset;
    }
  }
  if (!matching_space(found->space, requested))
    return storage_error(StorageErrorCode::incompatible_address_space, symbol.target);
  return *offset;
}

auto LaunchRuntime::resolve_symbol_offset(exec_ir::SymbolRef symbol,
                                          execution_model::ThreadId thread,
                                          common::FunctionId function) const
    -> std::expected<std::uint64_t, StorageError> {
  const auto* topology_thread = grid_.find_thread(thread);
  if (!topology_thread || symbol.member)
    return storage_error(StorageErrorCode::invalid_symbol, symbol.target);
  if (const auto* id = std::get_if<exec_ir::ParameterId>(&symbol.target)) {
    const auto found = parameter_offsets_.find(*id);
    if (found == parameter_offsets_.end() || found->second.first != function)
      return storage_error(StorageErrorCode::invalid_symbol, symbol.target);
    return static_cast<std::uint64_t>(found->second.second);
  }
  if (!storage_layout_)
    return storage_error(StorageErrorCode::invalid_symbol, symbol.target);
  const auto id = std::get<exec_ir::StorageId>(symbol.target);
  const auto found = std::find_if(storage_layout_->begin(), storage_layout_->end(),
      [id](const auto& value) { return value.id == id; });
  if (found == storage_layout_->end() ||
      (found->owner_function && *found->owner_function != function))
    return storage_error(StorageErrorCode::invalid_symbol, symbol.target);
  if (found->space == exec_ir::StorageSpace::shared) {
    const auto offset = shared_storage_offsets_.find({topology_thread->cta().id(), id.value()});
    if (offset == shared_storage_offsets_.end())
      return storage_error(StorageErrorCode::invalid_symbol, symbol.target);
    return static_cast<std::uint64_t>(offset->second);
  }
  if (found->space == exec_ir::StorageSpace::local) {
    const auto offset = local_storage_offsets_.find({{thread, function}, id.value()});
    if (offset == local_storage_offsets_.end())
      return storage_error(StorageErrorCode::invalid_symbol, symbol.target);
    return static_cast<std::uint64_t>(offset->second);
  }
  const auto offset = storage_offsets_.find(id.value());
  if (offset == storage_offsets_.end())
    return storage_error(StorageErrorCode::invalid_symbol, symbol.target);
  return static_cast<std::uint64_t>(offset->second);
}

}  // namespace ptxsim::runtime
