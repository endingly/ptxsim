#pragma once

#include <charconv>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <variant>

#include <ptxsim/common/ids.hpp>
#include <ptxsim/common/raw_value.hpp>

namespace ptxsim::exec_ir {

struct RegisterOperand {
  common::RegisterSlot slot;
  common::RawWidth width;

  constexpr bool operator==(const RegisterOperand&) const noexcept = default;
};

struct ImmediateOperand {
  common::RawValue value;

  constexpr bool operator==(const ImmediateOperand&) const noexcept = default;
};

struct SpecialRegisterOperand {
  common::SpecialRegisterId id;
  common::RawWidth width;

  constexpr bool operator==(const SpecialRegisterOperand&) const noexcept =
      default;
};

enum class AddressWidth {
  bits32,
  bits64,
};

struct AddressOperand {
  std::variant<common::RegisterSlot, common::SymbolId> base;
  AddressWidth width;
  std::int64_t byte_offset;

  constexpr bool operator==(const AddressOperand&) const noexcept = default;
};

struct BranchTarget {
  common::ProgramCounter pc;

  constexpr bool operator==(const BranchTarget&) const noexcept = default;
};

struct FunctionTarget {
  common::FunctionId function;

  constexpr bool operator==(const FunctionTarget&) const noexcept = default;
};

using ValueOperand =
    std::variant<RegisterOperand, ImmediateOperand, SpecialRegisterOperand>;

namespace detail {

[[nodiscard]] constexpr auto raw_width_name(common::RawWidth width) noexcept
    -> std::string_view {
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
  return {};
}

[[nodiscard]] constexpr auto address_width_name(AddressWidth width) noexcept
    -> std::string_view {
  switch (width) {
    case AddressWidth::bits32:
      return "b32";
    case AddressWidth::bits64:
      return "b64";
  }
  return {};
}

inline void append_offset(std::string& output, std::int64_t offset) {
  if (offset >= 0) {
    output.push_back('+');
  }
  char digits[std::numeric_limits<std::int64_t>::digits10 + 2];
  const auto [end, error] =
      std::to_chars(digits, digits + sizeof(digits), offset);
  (void)error;
  output.append(digits, end);
}

}  // namespace detail

[[nodiscard]] inline auto to_string(const RegisterOperand& operand)
    -> std::string {
  std::string output = common::to_string(operand.slot);
  output.push_back(':');
  output += detail::raw_width_name(operand.width);
  return output;
}

[[nodiscard]] inline auto to_string(const ImmediateOperand& operand)
    -> std::string {
  std::string output{"immediate:"};
  output += common::to_string(operand.value);
  return output;
}

[[nodiscard]] inline auto to_string(const SpecialRegisterOperand& operand)
    -> std::string {
  std::string output = common::to_string(operand.id);
  output.push_back(':');
  output += detail::raw_width_name(operand.width);
  return output;
}

[[nodiscard]] inline auto to_string(const AddressOperand& operand)
    -> std::string {
  std::string output{"address:"};
  output += detail::address_width_name(operand.width);
  output.push_back(':');
  if (const auto* base = std::get_if<common::RegisterSlot>(&operand.base)) {
    output += common::to_string(*base);
  } else {
    output += common::to_string(std::get<common::SymbolId>(operand.base));
  }
  output.push_back(':');
  detail::append_offset(output, operand.byte_offset);
  return output;
}

[[nodiscard]] inline auto to_string(const BranchTarget& target) -> std::string {
  return "branch:" + common::to_string(target.pc);
}

[[nodiscard]] inline auto to_string(const FunctionTarget& target)
    -> std::string {
  return "function-target:" + common::to_string(target.function);
}

[[nodiscard]] inline auto to_string(const ValueOperand& operand)
    -> std::string {
  return std::visit([](const auto& value) { return to_string(value); },
                    operand);
}

}  // namespace ptxsim::exec_ir
