#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <ptxsim/exec_ir/instruction_base.gen.hpp>
#include <ptxsim/exec_ir/program_records.hpp>

namespace ptxsim::exec_ir {

/** @brief Query whether a supported form can require a same-body successor.
 *  Call only after execution capability has admitted the selected exact form.
 */
[[nodiscard]] auto may_fallthrough(const Instruction&) noexcept -> bool;

/** @brief Caller-owned mutable construction graph, independently frozen by create. */
struct ProgramDefinition {
  /** @brief Actual final instruction inputs; aliases remain caller-owned after create. */
  std::vector<std::unique_ptr<Instruction>> instructions;
  /** @brief Dense canonical functions, including body-less callable declarations. */
  std::vector<FunctionRecord> functions;
  /** @brief Distinct declaration scopes and their full callable contracts. */
  std::vector<FunctionDeclaration> function_declarations;
  /** @brief Every full storage declaration, without imposing runtime allocation support. */
  std::vector<StorageDeclaration> storage_declarations;
  /** @brief Module and entry opaque resource declarations. */
  std::vector<ResourceDeclaration> resources;
  /** @brief Full scalar parameter declaration table. */
  std::vector<ParameterDeclaration> parameters;
  /** @brief Indirect-call prototype metadata. */
  std::vector<PrototypeRecord> prototypes;
  /** @brief Ordered canonical callable target sets. */
  std::vector<CallTargetSetRecord> call_target_sets;
  /** @brief Ordered function-local branch target sets. */
  std::vector<BranchTargetSetRecord> branch_target_sets;
  /** @brief Sparse callable storage tables and complete ABI contracts. */
  std::vector<CallTableRecord> call_tables;
  /** @brief Owned alias boundary names and their canonical callable targets. */
  std::vector<FunctionAlias> aliases;
  /** @brief Checked source interpretation profiles, independent of source buffers. */
  std::vector<SourceProfile> source_profiles;
};

/** @brief Structural container or static-location error, separate from execution support. */
enum class ProgramErrorCode : std::uint8_t {
  function_id_not_dense,
  function_count_not_representable,
  declaration_id_not_dense,
  metadata_id_not_dense,
  invalid_layout,
  invalid_layout_range,
  invalid_register_width,
  invalid_entry_parameter_layout,
  invalid_storage_declaration,
  invalid_resource_declaration,
  invalid_parameter_declaration,
  invalid_callable_metadata,
  invalid_declaration_reference,
  null_instruction,
  invalid_instruction_shape,
  function_not_found,
  nonexecutable_function,
  pc_out_of_range,
  no_fallthrough,
  empty_program_handle
};
/** @brief Typed context attached to one failed structural operation. */
struct ProgramError {
  /** @brief Rejected structural operation or invariant. */
  ProgramErrorCode code;
  /** @brief Canonical function containing the failure when applicable. */
  std::optional<common::FunctionId> function;
  /** @brief Function-local instruction position when applicable. */
  std::optional<common::ProgramCounter> pc;
  /** @brief Observed invalid physical register width when applicable. */
  std::optional<common::RawWidth> actual;
  /** @brief Storage declaration containing the failure when applicable. */
  std::optional<StorageId> storage;
  /** @brief Compare structural category and typed diagnostic context. */
  bool operator==(const ProgramError&) const = default;
};

/** @brief Shared immutable executable snapshot with no mutable input aliases. */
class ExecutableProgram final {
 public:
  /** @brief Share the same independently frozen immutable storage. */
  ExecutableProgram(const ExecutableProgram&) = default;
  /** @brief Share another frozen program without cloning its instructions. */
  ExecutableProgram& operator=(const ExecutableProgram&) = default;
  /** @brief Transfer a frozen program handle. */
  ExecutableProgram(ExecutableProgram&&) noexcept = default;
  /** @brief Transfer a frozen program handle. */
  ExecutableProgram& operator=(ExecutableProgram&&) noexcept = default;
  /** @brief Release this handle; other copies retain the immutable snapshot. */
  ~ExecutableProgram() = default;

  /** @brief Validate and deep-copy all inputs into fresh actual const storage. */
  [[nodiscard]] static auto create(const ProgramDefinition&)
      -> std::expected<ExecutableProgram, ProgramError>;
  /** @brief Query a canonical callable, including prototypes without a body. */
  [[nodiscard]] auto function_record(common::FunctionId) const
      -> std::expected<std::reference_wrapper<const FunctionRecord>, ProgramError>;
  /** @brief Query a real body; declarations report nonexecutable_function. */
  [[nodiscard]] auto function_layout(common::FunctionId) const
      -> std::expected<std::reference_wrapper<const FunctionLayout>, ProgramError>;
  /** @brief Fetch one owned final object at a function-local executable location. */
  [[nodiscard]] auto fetch(common::CodeLocation) const
      -> std::expected<std::reference_wrapper<const Instruction>, ProgramError>;
  /** @brief Resolve a valid local instruction position to its flat storage index. */
  [[nodiscard]] auto flat_offset(common::CodeLocation) const
      -> std::expected<std::size_t, ProgramError>;
  /** @brief Return a successor only inside the same actual function body. */
  [[nodiscard]] auto fallthrough(common::CodeLocation) const
      -> std::expected<common::CodeLocation, ProgramError>;
  /** @brief Borrow only const canonical callable records for this snapshot's lifetime. */
  [[nodiscard]] auto functions() const noexcept -> std::span<const FunctionRecord>;
  /** @brief Borrow only const specific declaration records. */
  [[nodiscard]] auto function_declarations() const noexcept -> std::span<const FunctionDeclaration>;
  /** @brief Borrow only const full storage declarations. */
  [[nodiscard]] auto storage_declarations() const noexcept -> std::span<const StorageDeclaration>;
  /** @brief Borrow only const typed resource declarations. */
  [[nodiscard]] auto resources() const noexcept -> std::span<const ResourceDeclaration>;
  /** @brief Borrow only const full scalar parameter declarations. */
  [[nodiscard]] auto parameters() const noexcept -> std::span<const ParameterDeclaration>;
  /** @brief Borrow only const indirect-call prototype metadata. */
  [[nodiscard]] auto prototypes() const noexcept -> std::span<const PrototypeRecord>;
  /** @brief Borrow only const ordered callable target sets. */
  [[nodiscard]] auto call_target_sets() const noexcept -> std::span<const CallTargetSetRecord>;
  /** @brief Borrow only const ordered local branch target sets. */
  [[nodiscard]] auto branch_target_sets() const noexcept -> std::span<const BranchTargetSetRecord>;
  /** @brief Borrow only const sparse callable storage tables. */
  [[nodiscard]] auto call_tables() const noexcept -> std::span<const CallTableRecord>;
  /** @brief Borrow only const owned external/API alias names. */
  [[nodiscard]] auto aliases() const noexcept -> std::span<const FunctionAlias>;
  /** @brief Borrow only const checked source interpretation profiles. */
  [[nodiscard]] auto source_profiles() const noexcept -> std::span<const SourceProfile>;

 private:
  /** @brief Actual const object that owns all independently copied value graphs. */
  struct Storage;
  /** @brief Publish only a factory-created actual const storage object. */
  explicit ExecutableProgram(std::shared_ptr<const Storage>) noexcept;
  /** @brief Shared immutable snapshot; no API exposes its mutable ownership. */
  std::shared_ptr<const Storage> storage_;
};

/** @brief Print frozen owned instructions and their canonical function locations. */
[[nodiscard]] auto to_string(const ExecutableProgram&) -> std::string;

}  // namespace ptxsim::exec_ir
