#pragma once

#include <cstdint>
#include <variant>

#include <ptxsim/common/ids.hpp>
#include <ptxsim/common/raw_value.hpp>

namespace ptxsim::exec_ir {

/** @brief Scalar data widths accepted by the currently executable forms. */
enum class DataType : std::uint8_t {
  b32,
  u32,
};

/**
 * @brief Address interpretation selected by scalar memory instructions.
 *
 * `generic` is resolved through the lane runtime context; `global` treats a
 * b64 address operand as an offset in the bound global region.
 */
enum class AddressSpace : std::uint8_t {
  generic,
  global,
};

/** @brief Memory-order selector retained from resolved memory instructions. */
enum class MemoryConsistency : std::uint8_t {
  omitted,
  weak,
};

/** @brief Visibility scope retained from resolved memory instructions. */
enum class MemoryScope : std::uint8_t {
  none,
};

/** @brief Cache hint retained from resolved memory instructions. */
enum class CacheOperator : std::uint8_t {
  unspecified,
};

/** @brief A fully-bound predicate register and its optional inversion. */
struct Predicate {
  /** @brief Predicate register read before execution. */
  common::RegisterSlot source;
  /** @brief Whether the predicate result is inverted. */
  bool negated = false;

  /** @brief Compare the predicate source and inversion. */
  constexpr bool operator==(const Predicate&) const noexcept = default;
};

/** @brief A b32 source represented by a register slot or an immediate value. */
using B32Operand = std::variant<common::RegisterSlot, common::RawValue>;

}  // namespace ptxsim::exec_ir
