#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include <ptxsim/memory/core/address.hpp>
#include <ptxsim/memory/core/state_space.hpp>

namespace ptxsim::memory {

/**
 * @brief Architectural kind of a memory access.
 */
enum class AccessKind : std::uint8_t {
  Load,
  Store,
  Atomic,
  Reduction,
  Prefetch,
};

/**
 * @brief PTX memory-ordering semantics associated with an operation.
 *
 * Volatile and MMIO are represented here because they define distinct strong
 * operation behaviors in the PTX memory consistency model.
 */
enum class MemorySemantic : std::uint8_t {
  Weak,
  Relaxed,
  Acquire,
  Release,
  AcquireRelease,
  Volatile,
  Mmio,
};

/**
 * @brief Visibility scope of a strong PTX memory operation.
 *
 * Warp is intentionally absent: PTX memory consistency scopes begin at CTA
 * scope.
 */
enum class MemoryScope : std::uint8_t {
  Cta,
  Cluster,
  Gpu,
  System,
};

/**
 * @brief Memory proxy through which an operation is performed.
 *
 * This enum captures proxy identity rather than memory location.
 *
 * Additional PTX proxies may be introduced without changing MemoryRegion.
 */
enum class MemoryProxy : std::uint8_t {
  Generic,
  Async,
  TensorMap,
  Alias,
};

/**
 * @brief Architectural description of one PTX memory operation.
 *
 * AccessDescriptor intentionally does not contain:
 *
 * - ThreadId;
 * - CtaId;
 * - GridId;
 * - resolved MemoryRegion pointer.
 *
 * Those belong to the address-space / execution-context binding layer.
 */
struct AccessDescriptor {
  StateSpace space = StateSpace::Global;

  AccessKind kind = AccessKind::Load;

  MemorySemantic semantic = MemorySemantic::Weak;

  /**
   * @brief Scope associated with strong memory semantics.
   *
   * Weak operations normally leave this unset. Instruction lowering or
   * semantics should resolve PTX defaults before the descriptor reaches the
   * memory-model implementation when a scope is required.
   */
  std::optional<MemoryScope> scope;

  MemoryProxy proxy = MemoryProxy::Generic;

  /**
   * @brief Access size in bytes.
   */
  std::size_t size = 0;

  /**
   * @brief Required byte alignment.
   *
   * Alignment must be a non-zero power of two.
   */
  std::size_t alignment = 1;

  /**
   * @brief Whether completion is decoupled from instruction issue.
   *
   * The actual pending-operation state belongs to AsyncMemoryEngine.
   */
  bool asynchronous = false;
};

/**
 * @brief Perform structural validation independent of any actual address.
 */
[[nodiscard]]
constexpr bool valid(const AccessDescriptor& access) noexcept {
  if (access.size == 0) {
    return false;
  }

  if (!is_power_of_two(access.alignment)) {
    return false;
  }

  return true;
}

}  // namespace ptxsim::memory