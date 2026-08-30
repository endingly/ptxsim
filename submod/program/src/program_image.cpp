#include <ptxsim/program/program_image.hpp>

#include <charconv>
#include <concepts>
#include <limits>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace ptxsim::program {
namespace {

[[nodiscard]] auto error(
    ProgramErrorCode code,
    std::optional<common::ProgramCounter> pc = std::nullopt,
    std::optional<std::size_t> index = std::nullopt,
    std::optional<exec_ir::InstructionErrorCode> instruction_error =
        std::nullopt,
    std::optional<common::FunctionId> function = std::nullopt) -> ProgramError {
  return {code, function, pc, index, instruction_error};
}

[[nodiscard]] auto pc_for(std::size_t index) -> common::ProgramCounter {
  return common::ProgramCounter{static_cast<std::uint32_t>(index)};
}

[[nodiscard]] auto width_for(exec_ir::AddressWidth width) -> common::RawWidth {
  return width == exec_ir::AddressWidth::bits32 ? common::RawWidth::b32
                                                : common::RawWidth::b64;
}

[[nodiscard]] auto width_name(common::RawWidth width) -> std::string_view {
  switch (width) {
    case common::RawWidth::pred:
      return "pred";
    case common::RawWidth::b8:
      return "b8";
    case common::RawWidth::b16:
      return "b16";
    case common::RawWidth::b32:
      return "b32";
    case common::RawWidth::b64:
      return "b64";
    case common::RawWidth::b128:
      return "b128";
  }
  return "invalid";
}

[[nodiscard]] constexpr auto is_valid_width(common::RawWidth width) -> bool {
  switch (width) {
    case common::RawWidth::pred:
    case common::RawWidth::b8:
    case common::RawWidth::b16:
    case common::RawWidth::b32:
    case common::RawWidth::b64:
    case common::RawWidth::b128:
      return true;
  }
  return false;
}

[[nodiscard]] auto verify_data(const ProgramImageData& data)
    -> std::expected<void, ProgramError> {
  constexpr auto kMaxPc = std::numeric_limits<std::uint32_t>::max();
  if (data.instructions.size() > kMaxPc) {
    return std::unexpected(
        error(ProgramErrorCode::instruction_count_not_representable));
  }

  const auto instruction_count =
      static_cast<std::uint32_t>(data.instructions.size());
  if (instruction_count == 0 && !data.functions.empty()) {
    return std::unexpected(
        error(ProgramErrorCode::empty_program_has_functions));
  }
  for (std::size_t index = 0; index < data.functions.size(); ++index) {
    const auto& function = data.functions[index];
    if (function.id.value() != index) {
      return std::unexpected(error(ProgramErrorCode::function_id_not_canonical,
                                   std::nullopt, index));
    }
    for (std::size_t register_index = 0;
         register_index < function.registers.size(); ++register_index) {
      if (function.registers[register_index].slot.value() != register_index) {
        return std::unexpected(
            error(ProgramErrorCode::register_slot_not_canonical, std::nullopt,
                  register_index, std::nullopt, function.id));
      }
      if (!is_valid_width(function.registers[register_index].width)) {
        return std::unexpected(error(ProgramErrorCode::invalid_register_width,
                                     std::nullopt, register_index, std::nullopt,
                                     function.id));
      }
    }
    if (function.begin_pc.value() > function.end_pc.value() ||
        function.end_pc.value() > instruction_count) {
      return std::unexpected(error(ProgramErrorCode::invalid_function_range,
                                   function.begin_pc, index, std::nullopt,
                                   function.id));
    }
    if ((index == 0 && function.begin_pc.value() != 0) ||
        (index > 0 && function.begin_pc != data.functions[index - 1].end_pc)) {
      return std::unexpected(
          error(ProgramErrorCode::function_ranges_not_partition,
                function.begin_pc, index, std::nullopt, function.id));
    }
  }
  if (instruction_count != 0 && data.functions.empty()) {
    return std::unexpected(
        error(ProgramErrorCode::function_ranges_not_partition));
  }
  if (!data.functions.empty() &&
      data.functions.back().end_pc.value() != instruction_count) {
    return std::unexpected(
        error(ProgramErrorCode::function_ranges_not_partition,
              data.functions.back().end_pc, data.functions.size() - 1,
              std::nullopt, data.functions.back().id));
  }
  for (std::size_t index = 0; index < data.symbols.size(); ++index) {
    if (data.symbols[index].id.value() != index) {
      return std::unexpected(error(ProgramErrorCode::symbol_id_not_canonical,
                                   std::nullopt, index));
    }
  }
  for (std::size_t index = 0; index < data.source_locations.size(); ++index) {
    if (data.source_locations[index].id.value() != index) {
      return std::unexpected(
          error(ProgramErrorCode::source_location_id_not_canonical,
                std::nullopt, index));
    }
  }
  for (std::size_t index = 0; index < data.entry_points.size(); ++index) {
    if (data.entry_points[index].value() >= data.functions.size()) {
      return std::unexpected(
          error(ProgramErrorCode::invalid_entry_function, std::nullopt, index));
    }
  }
  if (data.source_locations_by_pc.size() != data.instructions.size()) {
    return std::unexpected(error(ProgramErrorCode::source_map_size_mismatch));
  }
  for (std::size_t index = 0; index < data.source_locations_by_pc.size();
       ++index) {
    const auto source = data.source_locations_by_pc[index];
    if (source && source->value() >= data.source_locations.size()) {
      return std::unexpected(error(ProgramErrorCode::invalid_source_location,
                                   pc_for(index), index));
    }
  }

  std::size_t function_index = 0;
  for (std::size_t index = 0; index < data.instructions.size(); ++index) {
    while (index >= data.functions[function_index].end_pc.value()) {
      ++function_index;
    }
    const auto& function = data.functions[function_index];
    const auto pc = pc_for(index);
    const auto& instruction = data.instructions[index];
    if (const auto valid = exec_ir::validate(instruction); !valid) {
      return std::unexpected(error(ProgramErrorCode::invalid_instruction, pc,
                                   index, valid.error().code, function.id));
    }

    const auto check_register = [&](const exec_ir::RegisterOperand& operand)
        -> std::expected<void, ProgramError> {
      if (operand.slot.value() >= function.registers.size()) {
        return std::unexpected(error(ProgramErrorCode::register_slot_not_found,
                                     pc, operand.slot.value(), std::nullopt,
                                     function.id));
      }
      if (function.registers[operand.slot.value()].width != operand.width) {
        return std::unexpected(error(ProgramErrorCode::register_width_mismatch,
                                     pc, operand.slot.value(), std::nullopt,
                                     function.id));
      }
      return {};
    };
    const auto check_guard =
        [&](const std::optional<exec_ir::PredicateGuard>& guard)
        -> std::expected<void, ProgramError> {
      if (!guard) {
        return {};
      }
      return check_register({guard->predicate, common::RawWidth::pred});
    };
    const auto check_value = [&](const exec_ir::ValueOperand& operand)
        -> std::expected<void, ProgramError> {
      if (const auto* register_operand =
              std::get_if<exec_ir::RegisterOperand>(&operand)) {
        return check_register(*register_operand);
      }
      return {};
    };
    const auto check_address = [&](const exec_ir::AddressOperand& operand)
        -> std::expected<void, ProgramError> {
      if (const auto* register_slot =
              std::get_if<common::RegisterSlot>(&operand.base)) {
        return check_register({*register_slot, width_for(operand.width)});
      }
      if (std::get<common::SymbolId>(operand.base).value() >=
          data.symbols.size()) {
        return std::unexpected(
            error(ProgramErrorCode::invalid_symbol, pc,
                  std::get<common::SymbolId>(operand.base).value(),
                  std::nullopt, function.id));
      }
      return {};
    };
    const auto check_branch = [&](const exec_ir::BranchTarget& target)
        -> std::expected<void, ProgramError> {
      if (target.pc.value() >= instruction_count) {
        return std::unexpected(
            error(ProgramErrorCode::branch_target_out_of_range, pc,
                  target.pc.value(), std::nullopt, function.id));
      }
      if (target.pc.value() < function.begin_pc.value() ||
          target.pc.value() >= function.end_pc.value()) {
        return std::unexpected(error(ProgramErrorCode::branch_crosses_function,
                                     pc, target.pc.value(), std::nullopt,
                                     function.id));
      }
      return {};
    };
    const auto checked = std::visit(
        [&](const auto& record) -> std::expected<void, ProgramError> {
          if constexpr (std::same_as<std::decay_t<decltype(record)>,
                                     exec_ir::MovInst>) {
            if (auto value = check_register(record.dest); !value)
              return value;
            if (auto value = check_value(record.src); !value)
              return value;
            return check_guard(record.guard);
          } else if constexpr (std::same_as<std::decay_t<decltype(record)>,
                                            exec_ir::IntegerBinaryInst> ||
                               std::same_as<std::decay_t<decltype(record)>,
                                            exec_ir::IntegerMulInst> ||
                               std::same_as<std::decay_t<decltype(record)>,
                                            exec_ir::BitInst>) {
            if (auto value = check_register(record.dest); !value)
              return value;
            if (auto value = check_value(record.lhs); !value)
              return value;
            if (auto value = check_value(record.rhs); !value)
              return value;
            return check_guard(record.guard);
          } else if constexpr (std::same_as<std::decay_t<decltype(record)>,
                                            exec_ir::BranchInst>) {
            if (auto value = check_branch(record.target); !value)
              return value;
            return check_guard(record.guard);
          } else if constexpr (std::same_as<std::decay_t<decltype(record)>,
                                            exec_ir::LoadInst>) {
            if (auto value = check_register(record.dest); !value)
              return value;
            if (auto value = check_address(record.address); !value)
              return value;
            return check_guard(record.guard);
          } else {
            if (auto value = check_address(record.address); !value)
              return value;
            if (auto value = check_register(record.src); !value)
              return value;
            return check_guard(record.guard);
          }
        },
        instruction);
    if (!checked) {
      return checked;
    }
  }
  return {};
}

void append_number(std::string& output, std::uint32_t value) {
  char digits[std::numeric_limits<std::uint32_t>::digits10 + 1];
  const auto [end, ignored] =
      std::to_chars(digits, digits + sizeof(digits), value);
  (void)ignored;
  output.append(digits, end);
}

void append_escaped(std::string& output, std::string_view value) {
  output.push_back('"');
  constexpr char kHex[] = "0123456789abcdef";
  for (const unsigned char character : value) {
    switch (character) {
      case '\\':
        output += "\\\\";
        break;
      case '"':
        output += "\\\"";
        break;
      case '\n':
        output += "\\n";
        break;
      case '\r':
        output += "\\r";
        break;
      case '\t':
        output += "\\t";
        break;
      default:
        if (character < 0x20U || character == 0x7fU) {
          output += "\\x";
          output.push_back(kHex[character >> 4U]);
          output.push_back(kHex[character & 0x0fU]);
        } else {
          output.push_back(static_cast<char>(character));
        }
    }
  }
  output.push_back('"');
}

}  // namespace

ProgramImage::ProgramImage(ProgramImageData&& data) noexcept
    : data_(std::move(data)) {}

auto ProgramImage::create(ProgramImageData data)
    -> std::expected<ProgramImage, ProgramError> {
  if (const auto valid = verify_data(data); !valid) {
    return std::unexpected(valid.error());
  }
  return ProgramImage{std::move(data)};
}

auto ProgramImage::instructions() const noexcept
    -> std::span<const exec_ir::Instruction> {
  return data_.instructions;
}
auto ProgramImage::functions() const noexcept
    -> std::span<const FunctionRecord> {
  return data_.functions;
}
auto ProgramImage::symbols() const noexcept -> std::span<const SymbolRecord> {
  return data_.symbols;
}
auto ProgramImage::entry_points() const noexcept
    -> std::span<const common::FunctionId> {
  return data_.entry_points;
}
auto ProgramImage::source_locations() const noexcept
    -> std::span<const SourceLocationRecord> {
  return data_.source_locations;
}
auto ProgramImage::source_locations_by_pc() const noexcept
    -> std::span<const std::optional<common::SourceLocationId>> {
  return data_.source_locations_by_pc;
}

auto verify(const ProgramImage& image) -> std::expected<void, ProgramError> {
  return verify_data(image.data_);
}

auto dump(const ProgramImage& image) -> std::string {
  std::string output{"program-image\nfunctions:\n"};
  for (const auto& function : image.functions()) {
    output += "  ";
    output += common::to_string(function.id);
    output.push_back(' ');
    append_escaped(output, function.name);
    output += " [";
    output += common::to_string(function.begin_pc);
    output.push_back(',');
    output += common::to_string(function.end_pc);
    output += ") registers:";
    for (const auto& register_layout : function.registers) {
      output += " ";
      output += common::to_string(register_layout.slot);
      output.push_back(':');
      output += width_name(register_layout.width);
    }
    output.push_back('\n');
  }
  output += "symbols:\n";
  for (const auto& symbol : image.symbols()) {
    output += "  ";
    output += common::to_string(symbol.id);
    output.push_back(' ');
    append_escaped(output, symbol.name);
    output.push_back('\n');
  }
  output += "entries:";
  for (const auto entry : image.entry_points()) {
    output.push_back(' ');
    output += common::to_string(entry);
  }
  output += "\nsource-locations:\n";
  for (const auto& source : image.source_locations()) {
    output += "  ";
    output += common::to_string(source.id);
    output.push_back(' ');
    append_escaped(output, source.file);
    output.push_back(':');
    append_number(output, source.line);
    output.push_back(':');
    append_number(output, source.column);
    output.push_back('\n');
  }
  output += "source-map:\n";
  const auto source_map = image.source_locations_by_pc();
  for (std::size_t index = 0; index < source_map.size(); ++index) {
    output += "  ";
    output += common::to_string(pc_for(index));
    output += " -> ";
    output +=
        source_map[index] ? common::to_string(*source_map[index]) : "none";
    output.push_back('\n');
  }
  output += "instructions:\n";
  const auto instructions = image.instructions();
  for (std::size_t index = 0; index < instructions.size(); ++index) {
    output += "  ";
    output += common::to_string(pc_for(index));
    output += " ";
    output += exec_ir::to_string(instructions[index]);
    output.push_back('\n');
  }
  return output;
}

}  // namespace ptxsim::program
