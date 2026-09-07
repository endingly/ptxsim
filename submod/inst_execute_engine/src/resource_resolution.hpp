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

  /** @brief Bind a b64 address register to a region-relative memory view. */
  auto resolve_memory(exec_ir::AddressSpace space, common::RegisterSlot address)
      -> std::expected<std::pair<memory::AddressSpaceView, memory::Address>,
                       LaneFaultCause>;

  /** @brief Bind an entry-parameter byte offset to its memory view. */
  auto resolve_entry_parameter(std::uint64_t address)
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

/** @brief Read a b32 operand from an immediate or one register slot. */
auto b32_operand(const memory::RegisterView& registers,
                 const exec_ir::B32Operand& operand)
    -> std::expected<std::uint32_t, LaneFaultCause>;

/** @brief Read the register-backed address supported by scalar memory forms. */
auto register_address(const exec_ir::Address& address)
    -> std::optional<common::RegisterSlot>;

/** @brief Read the b64 immediate accepted as an entry-parameter byte offset. */
auto entry_parameter_address(const exec_ir::Address& address)
    -> std::expected<std::uint64_t, LaneFaultCause>;

/** @brief Serialize one u32 into four PTX little-endian bytes. */
auto b32_bytes(std::uint32_t value) -> std::array<std::byte, 4>;

/** @brief Reconstruct one u32 from four PTX little-endian bytes. */
auto bytes_b32(const std::array<std::byte, 4>& value) -> std::uint32_t;

}  // namespace ptxsim::inst_execute_engine::detail
