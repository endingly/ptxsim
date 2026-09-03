#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include <ptxsim/common/ids.hpp>
#include <ptxsim/common/raw_value.hpp>

namespace ptxsim::exec_ir {

enum class Op : std::uint8_t {
  mov,
  add,
  ld,
  st,
  bra,
  exit,
};

enum class DataType : std::uint8_t {
  b32,
  u32,
};

/**
 * @brief Address interpretation selected by the scalar memory instructions.
 *
 * `generic` is resolved through the lane's runtime address context; `global`
 * treats the b64 address operand as an offset in the bound global region.
 */
enum class AddressSpace : std::uint8_t {
  generic,
  global,
};

struct Predicate {
  common::RegisterSlot source;
  bool negated = false;

  constexpr bool operator==(const Predicate&) const noexcept = default;
};

struct Move {
  DataType type;
  common::RegisterSlot destination;
  common::RegisterSlot source;

  constexpr bool operator==(const Move&) const noexcept = default;
};

using B32Operand = std::variant<common::RegisterSlot, common::RawValue>;

struct Add {
  DataType type;
  common::RegisterSlot destination;
  B32Operand lhs;
  B32Operand rhs;

  bool operator==(const Add&) const noexcept = default;
};

/** @brief Fully-bound four-byte scalar load operation. */
struct Load {
  /** PTX scalar type; executable validation currently accepts only `u32`. */
  DataType type;
  /** Determines whether `address` is generic or global-region-relative. */
  AddressSpace space;
  /** b32 register slot that receives the little-endian loaded value. */
  common::RegisterSlot destination;
  /** b64 register slot holding the byte address with no embedded offset. */
  common::RegisterSlot address;

  constexpr bool operator==(const Load&) const noexcept = default;
};

/** @brief Fully-bound four-byte scalar store operation. */
struct Store {
  /** PTX scalar type; executable validation currently accepts only `u32`. */
  DataType type;
  /** Determines whether `address` is generic or global-region-relative. */
  AddressSpace space;
  /** b64 register slot holding the byte address with no embedded offset. */
  common::RegisterSlot address;
  /** b32 register slot copied to memory in little-endian byte order. */
  common::RegisterSlot source;

  constexpr bool operator==(const Store&) const noexcept = default;
};

struct Branch {
  common::ProgramCounter target;

  constexpr bool operator==(const Branch&) const noexcept = default;
};

struct Exit {
  constexpr bool operator==(const Exit&) const noexcept = default;
};

using Operation = std::variant<Move, Add, Load, Store, Branch, Exit>;

namespace detail {

template <typename T>
concept OperationAlternative = std::same_as<std::remove_cvref_t<T>, Move> ||
                               std::same_as<std::remove_cvref_t<T>, Add> ||
                               std::same_as<std::remove_cvref_t<T>, Load> ||
                               std::same_as<std::remove_cvref_t<T>, Store> ||
                               std::same_as<std::remove_cvref_t<T>, Branch> ||
                               std::same_as<std::remove_cvref_t<T>, Exit>;

}  // namespace detail

[[nodiscard]] constexpr auto op(const Operation& operation) noexcept -> Op {
  return std::visit(
      []<detail::OperationAlternative T>(const T&) constexpr -> Op {
        if constexpr (std::same_as<T, Move>) {
          return Op::mov;
        } else if constexpr (std::same_as<T, Add>) {
          return Op::add;
        } else if constexpr (std::same_as<T, Load>) {
          return Op::ld;
        } else if constexpr (std::same_as<T, Store>) {
          return Op::st;
        } else if constexpr (std::same_as<T, Branch>) {
          return Op::bra;
        } else {
          static_assert(std::same_as<T, Exit>);
          return Op::exit;
        }
      },
      operation);
}

struct Instruction {
  std::optional<Predicate> predicate;
  Operation operation;

  bool operator==(const Instruction&) const noexcept = default;
};

[[nodiscard]] constexpr auto may_fallthrough(
    const Instruction& instruction) noexcept -> bool {
  return instruction.predicate.has_value() ||
         (!std::holds_alternative<Branch>(instruction.operation) &&
          !std::holds_alternative<Exit>(instruction.operation));
}

struct FunctionLayout {
  common::FunctionId id;
  std::size_t begin;
  std::uint32_t instruction_count;
  std::vector<common::RawWidth> register_widths;
};

struct ProgramDefinition {
  std::vector<Instruction> instructions;
  std::vector<FunctionLayout> functions;
};

enum class ProgramErrorCode : std::uint8_t {
  function_id_not_dense,
  function_count_not_representable,
  invalid_layout,
  invalid_layout_range,
  invalid_register_width,
  function_not_found,
  pc_out_of_range,
  no_fallthrough,
  operand_slot_out_of_range,
  operand_width_mismatch,
  immediate_width_mismatch,
  unsupported_instruction,
  branch_target_out_of_range,
};

struct ProgramError {
  ProgramErrorCode code;
  std::optional<common::FunctionId> function;
  std::optional<common::ProgramCounter> pc;
  std::optional<common::RegisterSlot> slot;
  std::optional<common::RawWidth> expected;
  std::optional<common::RawWidth> actual;

  constexpr bool operator==(const ProgramError&) const noexcept = default;
};

class ExecutableProgram final {
 public:
  ExecutableProgram(const ExecutableProgram&) = default;
  ExecutableProgram& operator=(const ExecutableProgram&) = default;
  ExecutableProgram(ExecutableProgram&&) noexcept = default;
  ExecutableProgram& operator=(ExecutableProgram&&) noexcept = default;
  ~ExecutableProgram() = default;

  [[nodiscard]] static auto create(ProgramDefinition definition)
      -> std::expected<ExecutableProgram, ProgramError>;

  [[nodiscard]] auto fetch(common::CodeLocation location) const
      -> std::expected<std::reference_wrapper<const Instruction>, ProgramError>;
  [[nodiscard]] auto flat_offset(common::CodeLocation location) const
      -> std::expected<std::size_t, ProgramError>;
  [[nodiscard]] auto fallthrough(common::CodeLocation location) const
      -> std::expected<common::CodeLocation, ProgramError>;

 private:
  friend auto to_string(const ExecutableProgram& program) -> std::string;

  explicit ExecutableProgram(ProgramDefinition definition) noexcept;

  std::vector<Instruction> instructions_;
  std::vector<FunctionLayout> functions_;
};

[[nodiscard]] auto to_string(const ExecutableProgram& program) -> std::string;

}  // namespace ptxsim::exec_ir
