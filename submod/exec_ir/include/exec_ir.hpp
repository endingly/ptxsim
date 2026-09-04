#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <ptxsim/exec_ir/exec_ir.gen.hpp>
#include <ptxsim/exec_ir/exec_ir_types.hpp>

namespace ptxsim::exec_ir {

/** @brief Function-local instruction range and register layout. */
struct FunctionLayout {
  /** @brief Dense function identity used by code locations. */
  common::FunctionId id;
  /** @brief First instruction index in the owning program instruction vector. */
  std::size_t begin;
  /** @brief Number of instructions belonging to this function. */
  std::uint32_t instruction_count;
  /** @brief Declared width for each function-local register slot. */
  std::vector<common::RawWidth> register_widths;
};

/**
 * @brief Input records used to construct a self-consistent executable container.
 *
 * Construction validates ownership and layout invariants, not whether a
 * particular executor supports every contained instruction.
 */
struct ProgramDefinition {
  /** @brief Flat instruction records, owned by the definition. */
  std::vector<Instruction> instructions;
  /** @brief Dense function layouts covering every instruction exactly once. */
  std::vector<FunctionLayout> functions;
};

/** @brief Reasons an executable-program operation can fail. */
enum class ProgramErrorCode : std::uint8_t {
  function_id_not_dense,
  function_count_not_representable,
  invalid_layout,
  invalid_layout_range,
  invalid_register_width,
  function_not_found,
  pc_out_of_range,
  no_fallthrough,
};

/** @brief Context attached to a failed executable-program operation. */
struct ProgramError {
  /** @brief Stable category of the rejected invariant. */
  ProgramErrorCode code;
  /** @brief Function containing the invalid record when applicable. */
  std::optional<common::FunctionId> function;
  /** @brief Function-relative instruction location when applicable. */
  std::optional<common::ProgramCounter> pc;
  /** @brief Observed invalid width when applicable. */
  std::optional<common::RawWidth> actual;

  constexpr bool operator==(const ProgramError&) const noexcept = default;
};

/**
 * @brief Report an instruction's control-flow classification for an executor.
 *
 * Executors call this only after selecting a supported instruction form.
 */
[[nodiscard]] constexpr auto may_fallthrough(
    const Instruction& instruction) noexcept -> bool {
  switch (op(instruction)) {
    case Op::bra:
    case Op::exit:
      return execution_predicate(instruction).has_value();
    default:
      return true;
  }
}

/**
 * @brief Validated, owning execution-program container.
 *
 * A valid container can include declaration-only instructions that a particular
 * executor does not support.
 */
class ExecutableProgram final {
 public:
  ExecutableProgram(const ExecutableProgram&) = default;
  ExecutableProgram& operator=(const ExecutableProgram&) = default;
  ExecutableProgram(ExecutableProgram&&) noexcept = default;
  ExecutableProgram& operator=(ExecutableProgram&&) noexcept = default;
  ~ExecutableProgram() = default;

  /** @brief Validate container and layout invariants, then take ownership. */
  [[nodiscard]] static auto create(ProgramDefinition definition)
      -> std::expected<ExecutableProgram, ProgramError>;

  /**
   * @brief Return the validated layout for one dense function identity.
   *
   * The returned reference remains valid for this program's lifetime and
   * exposes the register frame layout required by an executor.
   */
  [[nodiscard]] auto function_layout(common::FunctionId function) const
      -> std::expected<std::reference_wrapper<const FunctionLayout>,
                       ProgramError>;
  /** @brief Return the instruction at one validated function-relative location. */
  [[nodiscard]] auto fetch(common::CodeLocation location) const
      -> std::expected<std::reference_wrapper<const Instruction>, ProgramError>;
  /** @brief Translate a function-relative location to the owned flat index. */
  [[nodiscard]] auto flat_offset(common::CodeLocation location) const
      -> std::expected<std::size_t, ProgramError>;
  /** @brief Return the next location within the same function. */
  [[nodiscard]] auto fallthrough(common::CodeLocation location) const
      -> std::expected<common::CodeLocation, ProgramError>;

 private:
  friend auto to_string(const ExecutableProgram& program) -> std::string;

  /** @brief Take ownership after the factory has established all invariants. */
  explicit ExecutableProgram(ProgramDefinition definition) noexcept;

  /** @brief Flat instruction storage owned for the program lifetime. */
  std::vector<Instruction> instructions_;
  /** @brief Dense layouts corresponding to the owned instruction storage. */
  std::vector<FunctionLayout> functions_;
};

/** @brief Format the validated execution records for diagnostics. */
[[nodiscard]] auto to_string(const ExecutableProgram& program) -> std::string;

}  // namespace ptxsim::exec_ir
