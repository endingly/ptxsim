#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <ptxsim/common/ids.hpp>
#include <ptxsim/common/raw_value.hpp>
#include <ptxsim/exec_ir/instruction.hpp>

namespace ptxsim::program {

struct RegisterLayout {
  common::RegisterSlot slot;
  common::RawWidth width;

  constexpr bool operator==(const RegisterLayout&) const noexcept = default;
};

struct FunctionRecord {
  common::FunctionId id;
  std::string name;
  common::ProgramCounter begin_pc;
  common::ProgramCounter end_pc;
  std::vector<RegisterLayout> registers;
};

struct SymbolRecord {
  common::SymbolId id;
  std::string name;
};

struct SourceLocationRecord {
  common::SourceLocationId id;
  std::string file;
  std::uint32_t line;
  std::uint32_t column;
};

struct ProgramImageData {
  std::vector<exec_ir::Instruction> instructions;
  std::vector<FunctionRecord> functions;
  std::vector<SymbolRecord> symbols;
  std::vector<common::FunctionId> entry_points;
  std::vector<SourceLocationRecord> source_locations;
  std::vector<std::optional<common::SourceLocationId>> source_locations_by_pc;
};

enum class ProgramErrorCode {
  instruction_count_not_representable,
  empty_program_has_functions,
  invalid_instruction,
  function_id_not_canonical,
  invalid_function_range,
  function_ranges_not_partition,
  register_slot_not_canonical,
  invalid_register_width,
  symbol_id_not_canonical,
  source_location_id_not_canonical,
  invalid_entry_function,
  source_map_size_mismatch,
  invalid_source_location,
  register_slot_not_found,
  register_width_mismatch,
  invalid_symbol,
  branch_target_out_of_range,
  branch_crosses_function,
};

struct ProgramError {
  ProgramErrorCode code;
  std::optional<common::FunctionId> function;
  std::optional<common::ProgramCounter> pc;
  std::optional<std::size_t> index;
  std::optional<exec_ir::InstructionErrorCode> instruction_error;

  constexpr bool operator==(const ProgramError&) const noexcept = default;
};

[[nodiscard]] auto to_string(ProgramErrorCode code) -> std::string;
[[nodiscard]] auto to_string(const ProgramError& error) -> std::string;

class ProgramImage;
[[nodiscard]] auto verify(const ProgramImageData& data)
    -> std::expected<void, ProgramError>;
[[nodiscard]] auto verify(const ProgramImage& image)
    -> std::expected<void, ProgramError>;

class ProgramImage {
 public:
  ProgramImage(const ProgramImage&) = default;
  ProgramImage(ProgramImage&&) noexcept = default;
  auto operator=(const ProgramImage&) -> ProgramImage& = delete;
  auto operator=(ProgramImage&&) -> ProgramImage& = delete;

  [[nodiscard]] static auto create(ProgramImageData data)
      -> std::expected<ProgramImage, ProgramError>;

  [[nodiscard]] auto instructions() const noexcept
      -> std::span<const exec_ir::Instruction>;
  [[nodiscard]] auto functions() const noexcept
      -> std::span<const FunctionRecord>;
  [[nodiscard]] auto symbols() const noexcept -> std::span<const SymbolRecord>;
  [[nodiscard]] auto entry_points() const noexcept
      -> std::span<const common::FunctionId>;
  [[nodiscard]] auto source_locations() const noexcept
      -> std::span<const SourceLocationRecord>;
  [[nodiscard]] auto source_locations_by_pc() const noexcept
      -> std::span<const std::optional<common::SourceLocationId>>;

 private:
  friend auto verify(const ProgramImage& image)
      -> std::expected<void, ProgramError>;

  explicit ProgramImage(ProgramImageData&& data) noexcept;

  ProgramImageData data_;
};

[[nodiscard]] auto dump(const ProgramImage& image) -> std::string;

}  // namespace ptxsim::program
