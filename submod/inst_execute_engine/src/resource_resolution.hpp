#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <utility>

#include <ptxsim/common/ids.hpp>
#include <ptxsim/exec_ir/exec_ir.hpp>
#include <ptxsim/execution_model/thread.hpp>
#include <ptxsim/inst_execute_engine/step_outcome.hpp>
#include <ptxsim/memory/address_space/address_space_manager.hpp>
#include <ptxsim/memory/register/register_view.hpp>
#include <ptxsim/runtime/runtime.hpp>

namespace ptxsim::inst_execute_engine::detail {

/** @brief Lazily resolves one lane's register and memory resources. */
class LaneResourceResolver final {
 public:
  /** @brief Associate resolution with one thread and function register frame. */
  LaneResourceResolver(runtime::LaunchRuntime& runtime,
                       execution_model::Thread& thread,
                       common::FunctionId function) noexcept;

  /** @brief Return the lane's cached register view or its binding fault. */
  auto resolve()
      -> std::expected<std::reference_wrapper<const memory::RegisterView>,
                       LaneFaultCause>;

  /** @brief Return the thread whose topology supplies special-register values. */
  [[nodiscard]] auto thread() const noexcept -> const execution_model::Thread&;

  /** @brief Resolve a numeric PTX address in one bound memory state space. */
  auto resolve_memory(exec_ir::AddressSpace space,
                      const exec_ir::Address& address, std::size_t size)
      -> std::expected<std::pair<memory::AddressSpaceView, memory::Address>,
                       LaneFaultCause>;

 private:
  /** Launch resource owner borrowed for this execute call. */
  runtime::LaunchRuntime& runtime_;
  /** Thread whose bindings select register and address resources. */
  execution_model::Thread& thread_;
  /** Function layout identity used for the thread's register frame. */
  common::FunctionId function_;
  /** Cached non-owning view populated after a successful register-frame bind. */
  std::optional<memory::RegisterView> registers_;
};

/** @brief Decode and offset an address without resolving an address-space resource. */
auto numeric_address(const memory::RegisterView& registers,
                     const exec_ir::Address& address)
    -> std::expected<std::uint64_t, LaneFaultCause>;

/** @brief Read a b32 operand from an immediate or one register slot. */
auto b32_operand(const memory::RegisterView& registers,
                 const exec_ir::B32Operand& operand)
    -> std::expected<std::uint32_t, LaneFaultCause>;

}  // namespace ptxsim::inst_execute_engine::detail
