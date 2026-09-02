#pragma once

#include <cstdint>
#include <expected>
#include <map>
#include <optional>
#include <utility>

#include <ptxsim/common/ids.hpp>
#include <ptxsim/execution_model/grid.hpp>
#include <ptxsim/execution_model/ids.hpp>
#include <ptxsim/memory/memory.hpp>

namespace ptxsim::runtime {

enum class RuntimeResourceKind : std::uint8_t {
  global,
  constant,
  entry_parameter,
  shared,
  tensor_memory,
  register_frame,
  local_frame,
};

enum class RuntimeBindingErrorCode : std::uint8_t {
  foreign_topology,
  invalid_resource,
  duplicate_binding,
  missing_binding,
};

struct RuntimeBindingError {
  RuntimeBindingErrorCode code;
  RuntimeResourceKind resource;

  constexpr bool operator==(const RuntimeBindingError&) const noexcept =
      default;
};

/**
 * @brief Owns one launch topology, its memory managers, and their bindings.
 *
 * Storage stays in the managers. This class only binds manager-owned handles
 * to the execution-model identities of one grid.
 */
class LaunchRuntime final {
 public:
  LaunchRuntime(execution_model::GridId id, execution_model::GridShape shape);

  LaunchRuntime(const LaunchRuntime&) = delete;
  LaunchRuntime& operator=(const LaunchRuntime&) = delete;
  LaunchRuntime(LaunchRuntime&&) = delete;
  LaunchRuntime& operator=(LaunchRuntime&&) = delete;

  [[nodiscard]] auto grid() noexcept -> execution_model::Grid&;
  [[nodiscard]] auto grid() const noexcept -> const execution_model::Grid&;
  [[nodiscard]] auto registers() noexcept -> memory::RegisterManager&;
  [[nodiscard]] auto registers() const noexcept
      -> const memory::RegisterManager&;
  [[nodiscard]] auto address_spaces() noexcept -> memory::AddressSpaceManager&;
  [[nodiscard]] auto address_spaces() const noexcept
      -> const memory::AddressSpaceManager&;
  [[nodiscard]] auto tensor_memory() noexcept -> memory::TensorMemoryManager&;
  [[nodiscard]] auto tensor_memory() const noexcept
      -> const memory::TensorMemoryManager&;
  [[nodiscard]] auto async_memory() noexcept -> memory::AsyncMemoryEngine&;
  [[nodiscard]] auto async_memory() const noexcept
      -> const memory::AsyncMemoryEngine&;

  [[nodiscard]] auto bind_global(memory::GlobalSpaceHandle handle)
      -> std::expected<void, RuntimeBindingError>;
  [[nodiscard]] auto bind_constant(memory::ConstantSpaceHandle handle)
      -> std::expected<void, RuntimeBindingError>;
  [[nodiscard]] auto bind_entry_parameter(memory::EntryParameterHandle handle)
      -> std::expected<void, RuntimeBindingError>;
  [[nodiscard]] auto bind_shared(execution_model::CtaId cta,
                                 memory::SharedSpaceHandle handle)
      -> std::expected<void, RuntimeBindingError>;
  [[nodiscard]] auto bind_tensor_memory(execution_model::CtaId cta,
                                        memory::TensorMemorySpaceHandle handle)
      -> std::expected<void, RuntimeBindingError>;
  [[nodiscard]] auto bind_register_frame(execution_model::ThreadId thread,
                                         common::FunctionId function,
                                         memory::RegisterFrameHandle handle)
      -> std::expected<void, RuntimeBindingError>;
  [[nodiscard]] auto bind_local_frame(execution_model::ThreadId thread,
                                      common::FunctionId function,
                                      memory::LocalFrameHandle handle)
      -> std::expected<void, RuntimeBindingError>;

  [[nodiscard]] auto global() const
      -> std::expected<memory::GlobalSpaceHandle, RuntimeBindingError>;
  [[nodiscard]] auto constant() const
      -> std::expected<memory::ConstantSpaceHandle, RuntimeBindingError>;
  [[nodiscard]] auto entry_parameter() const
      -> std::expected<memory::EntryParameterHandle, RuntimeBindingError>;
  [[nodiscard]] auto shared(execution_model::CtaId cta) const
      -> std::expected<memory::SharedSpaceHandle, RuntimeBindingError>;
  [[nodiscard]] auto tensor_memory_space(execution_model::CtaId cta) const
      -> std::expected<memory::TensorMemorySpaceHandle, RuntimeBindingError>;
  [[nodiscard]] auto register_frame(execution_model::ThreadId thread,
                                    common::FunctionId function) const
      -> std::expected<memory::RegisterFrameHandle, RuntimeBindingError>;
  [[nodiscard]] auto local_frame(execution_model::ThreadId thread,
                                 common::FunctionId function) const
      -> std::expected<memory::LocalFrameHandle, RuntimeBindingError>;

  [[nodiscard]] auto address_context(execution_model::ThreadId thread,
                                     common::FunctionId function) const
      -> std::expected<memory::ExecutionAddressContext, RuntimeBindingError>;

 private:
  using ThreadFunction =
      std::pair<execution_model::ThreadId, common::FunctionId>;

  execution_model::Grid grid_;
  memory::RegisterManager registers_;
  memory::AddressSpaceManager address_spaces_;
  memory::TensorMemoryManager tensor_memory_;
  memory::AsyncMemoryEngine async_memory_;

  std::optional<memory::GlobalSpaceHandle> global_;
  std::optional<memory::ConstantSpaceHandle> constant_;
  std::optional<memory::EntryParameterHandle> entry_parameter_;
  std::map<execution_model::CtaId, memory::SharedSpaceHandle> shared_;
  std::map<execution_model::CtaId, memory::TensorMemorySpaceHandle>
      tensor_spaces_;
  std::map<ThreadFunction, memory::RegisterFrameHandle> register_frames_;
  std::map<ThreadFunction, memory::LocalFrameHandle> local_frames_;
};

}  // namespace ptxsim::runtime
