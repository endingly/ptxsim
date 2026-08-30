#pragma once

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include <ptxsim/exec_ir/operand.hpp>

namespace ptxsim::exec_ir {

struct PredicateGuard {
  common::RegisterSlot predicate;
  bool negated;

  constexpr bool operator==(const PredicateGuard&) const noexcept = default;
};

enum class IntegerBinaryOp {
  add,
  sub,
};

enum class IntegerSignedness {
  signed_,
  unsigned_,
};

enum class ProductPart {
  low,
  high,
  wide,
};

enum class BitOp {
  and_,
  or_,
  xor_,
};

enum class MemorySpace {
  global,
  constant,
};

struct MovInst {
  RegisterOperand dest;
  ValueOperand src;
  std::optional<PredicateGuard> guard;

  constexpr bool operator==(const MovInst&) const noexcept = default;
};

struct IntegerBinaryInst {
  IntegerBinaryOp op;
  IntegerSignedness signedness;
  RegisterOperand dest;
  ValueOperand lhs;
  ValueOperand rhs;
  std::optional<PredicateGuard> guard;

  constexpr bool operator==(const IntegerBinaryInst&) const noexcept = default;
};

struct IntegerMulInst {
  ProductPart part;
  IntegerSignedness signedness;
  RegisterOperand dest;
  ValueOperand lhs;
  ValueOperand rhs;
  std::optional<PredicateGuard> guard;

  constexpr bool operator==(const IntegerMulInst&) const noexcept = default;
};

struct BitInst {
  BitOp op;
  RegisterOperand dest;
  ValueOperand lhs;
  ValueOperand rhs;
  std::optional<PredicateGuard> guard;

  constexpr bool operator==(const BitInst&) const noexcept = default;
};

struct BranchInst {
  BranchTarget target;
  std::optional<PredicateGuard> guard;

  constexpr bool operator==(const BranchInst&) const noexcept = default;
};

struct LoadInst {
  MemorySpace space;
  RegisterOperand dest;
  AddressOperand address;
  std::optional<PredicateGuard> guard;

  constexpr bool operator==(const LoadInst&) const noexcept = default;
};

struct StoreInst {
  MemorySpace space;
  AddressOperand address;
  RegisterOperand src;
  std::optional<PredicateGuard> guard;

  constexpr bool operator==(const StoreInst&) const noexcept = default;
};

using Instruction = std::variant<MovInst, IntegerBinaryInst, IntegerMulInst,
                                 BitInst, BranchInst, LoadInst, StoreInst>;

enum class InstructionErrorCode {
  invalid_control,
  width_mismatch,
  unsupported_width,
  invalid_mul_result_relation,
  read_only_store,
};

struct InstructionError {
  InstructionErrorCode code;

  constexpr bool operator==(const InstructionError&) const noexcept = default;
};

namespace detail {

[[nodiscard]] constexpr auto is_move_width(common::RawWidth width) noexcept
    -> bool {
  return width == common::RawWidth::pred || width == common::RawWidth::b16 ||
         width == common::RawWidth::b32 || width == common::RawWidth::b64;
}

[[nodiscard]] constexpr auto is_integer_width(common::RawWidth width) noexcept
    -> bool {
  return width == common::RawWidth::b32 || width == common::RawWidth::b64;
}

[[nodiscard]] constexpr auto is_valid(IntegerBinaryOp op) noexcept -> bool {
  return op == IntegerBinaryOp::add || op == IntegerBinaryOp::sub;
}

[[nodiscard]] constexpr auto is_valid(IntegerSignedness signedness) noexcept
    -> bool {
  return signedness == IntegerSignedness::signed_ ||
         signedness == IntegerSignedness::unsigned_;
}

[[nodiscard]] constexpr auto is_valid(ProductPart part) noexcept -> bool {
  return part == ProductPart::low || part == ProductPart::high ||
         part == ProductPart::wide;
}

[[nodiscard]] constexpr auto is_valid(BitOp op) noexcept -> bool {
  return op == BitOp::and_ || op == BitOp::or_ || op == BitOp::xor_;
}

[[nodiscard]] constexpr auto is_valid(MemorySpace space) noexcept -> bool {
  return space == MemorySpace::global || space == MemorySpace::constant;
}

[[nodiscard]] inline auto width_error(InstructionErrorCode code)
    -> std::expected<void, InstructionError> {
  return std::unexpected(InstructionError{code});
}

inline void append_guard(std::string& output,
                         const std::optional<PredicateGuard>& guard) {
  if (!guard) {
    return;
  }
  output += guard->negated ? " @!" : " @";
  output += common::to_string(guard->predicate);
}

[[nodiscard]] constexpr auto binary_op_name(IntegerBinaryOp op) noexcept
    -> std::string_view {
  switch (op) {
    case IntegerBinaryOp::add:
      return "add";
    case IntegerBinaryOp::sub:
      return "sub";
  }
  return "invalid";
}

[[nodiscard]] constexpr auto signedness_name(
    IntegerSignedness signedness) noexcept -> std::string_view {
  switch (signedness) {
    case IntegerSignedness::signed_:
      return "s";
    case IntegerSignedness::unsigned_:
      return "u";
  }
  return "invalid";
}

[[nodiscard]] constexpr auto product_part_name(ProductPart part) noexcept
    -> std::string_view {
  switch (part) {
    case ProductPart::low:
      return "lo";
    case ProductPart::high:
      return "hi";
    case ProductPart::wide:
      return "wide";
  }
  return "invalid";
}

[[nodiscard]] constexpr auto bit_op_name(BitOp op) noexcept
    -> std::string_view {
  switch (op) {
    case BitOp::and_:
      return "and";
    case BitOp::or_:
      return "or";
    case BitOp::xor_:
      return "xor";
  }
  return "invalid";
}

[[nodiscard]] constexpr auto memory_space_name(MemorySpace space) noexcept
    -> std::string_view {
  switch (space) {
    case MemorySpace::global:
      return "global";
    case MemorySpace::constant:
      return "constant";
  }
  return "invalid";
}

inline void append_binary_operands(std::string& output,
                                   const RegisterOperand& dest,
                                   const ValueOperand& lhs,
                                   const ValueOperand& rhs) {
  output.push_back(' ');
  output += to_string(dest);
  output.push_back(',');
  output += to_string(lhs);
  output.push_back(',');
  output += to_string(rhs);
}

}  // namespace detail

[[nodiscard]] inline auto validate(const MovInst& instruction)
    -> std::expected<void, InstructionError> {
  if (instruction.dest.width != width(instruction.src)) {
    return detail::width_error(InstructionErrorCode::width_mismatch);
  }
  if (!detail::is_move_width(instruction.dest.width)) {
    return detail::width_error(InstructionErrorCode::unsupported_width);
  }
  return {};
}

[[nodiscard]] inline auto validate(const IntegerBinaryInst& instruction)
    -> std::expected<void, InstructionError> {
  if (!detail::is_valid(instruction.op) ||
      !detail::is_valid(instruction.signedness)) {
    return detail::width_error(InstructionErrorCode::invalid_control);
  }
  if (instruction.dest.width != width(instruction.lhs) ||
      instruction.dest.width != width(instruction.rhs)) {
    return detail::width_error(InstructionErrorCode::width_mismatch);
  }
  if (!detail::is_integer_width(instruction.dest.width)) {
    return detail::width_error(InstructionErrorCode::unsupported_width);
  }
  return {};
}

[[nodiscard]] inline auto validate(const IntegerMulInst& instruction)
    -> std::expected<void, InstructionError> {
  if (!detail::is_valid(instruction.part) ||
      !detail::is_valid(instruction.signedness)) {
    return detail::width_error(InstructionErrorCode::invalid_control);
  }
  if (width(instruction.lhs) != common::RawWidth::b32 ||
      width(instruction.rhs) != common::RawWidth::b32) {
    return detail::width_error(InstructionErrorCode::unsupported_width);
  }
  switch (instruction.part) {
    case ProductPart::wide:
      if (instruction.dest.width == common::RawWidth::b64) {
        return {};
      }
      break;
    case ProductPart::low:
    case ProductPart::high:
      if (instruction.dest.width == common::RawWidth::b32 &&
          instruction.signedness == IntegerSignedness::unsigned_) {
        return {};
      }
      break;
  }
  return detail::width_error(InstructionErrorCode::invalid_mul_result_relation);
}

[[nodiscard]] inline auto validate(const BitInst& instruction)
    -> std::expected<void, InstructionError> {
  if (!detail::is_valid(instruction.op)) {
    return detail::width_error(InstructionErrorCode::invalid_control);
  }
  if (instruction.dest.width != width(instruction.lhs) ||
      instruction.dest.width != width(instruction.rhs)) {
    return detail::width_error(InstructionErrorCode::width_mismatch);
  }
  if (instruction.dest.width != common::RawWidth::b32) {
    return detail::width_error(InstructionErrorCode::unsupported_width);
  }
  return {};
}

[[nodiscard]] inline auto validate(const BranchInst&)
    -> std::expected<void, InstructionError> {
  return {};
}

[[nodiscard]] inline auto validate(const LoadInst& instruction)
    -> std::expected<void, InstructionError> {
  if (!detail::is_valid(instruction.space)) {
    return detail::width_error(InstructionErrorCode::invalid_control);
  }
  if (instruction.dest.width != common::RawWidth::b32 ||
      instruction.address.width != AddressWidth::bits64) {
    return detail::width_error(InstructionErrorCode::unsupported_width);
  }
  return {};
}

[[nodiscard]] inline auto validate(const StoreInst& instruction)
    -> std::expected<void, InstructionError> {
  if (!detail::is_valid(instruction.space)) {
    return detail::width_error(InstructionErrorCode::invalid_control);
  }
  if (instruction.space == MemorySpace::constant) {
    return detail::width_error(InstructionErrorCode::read_only_store);
  }
  if (instruction.src.width != common::RawWidth::b32 ||
      instruction.address.width != AddressWidth::bits64) {
    return detail::width_error(InstructionErrorCode::unsupported_width);
  }
  return {};
}

[[nodiscard]] inline auto validate(const Instruction& instruction)
    -> std::expected<void, InstructionError> {
  return std::visit([](const auto& record) { return validate(record); },
                    instruction);
}

[[nodiscard]] inline auto to_string(const MovInst& instruction) -> std::string {
  std::string output{"mov:"};
  output += detail::raw_width_name(instruction.dest.width);
  detail::append_guard(output, instruction.guard);
  output.push_back(' ');
  output += to_string(instruction.dest);
  output.push_back(',');
  output += to_string(instruction.src);
  return output;
}

[[nodiscard]] inline auto to_string(const IntegerBinaryInst& instruction)
    -> std::string {
  std::string output{detail::binary_op_name(instruction.op)};
  output.push_back(':');
  output += detail::signedness_name(instruction.signedness);
  output.push_back(':');
  output += detail::raw_width_name(instruction.dest.width);
  detail::append_guard(output, instruction.guard);
  detail::append_binary_operands(output, instruction.dest, instruction.lhs,
                                 instruction.rhs);
  return output;
}

[[nodiscard]] inline auto to_string(const IntegerMulInst& instruction)
    -> std::string {
  std::string output{"mul:"};
  output += detail::product_part_name(instruction.part);
  output.push_back(':');
  output += detail::signedness_name(instruction.signedness);
  output.push_back(':');
  output += detail::raw_width_name(instruction.dest.width);
  detail::append_guard(output, instruction.guard);
  detail::append_binary_operands(output, instruction.dest, instruction.lhs,
                                 instruction.rhs);
  return output;
}

[[nodiscard]] inline auto to_string(const BitInst& instruction) -> std::string {
  std::string output{detail::bit_op_name(instruction.op)};
  output += ":b32";
  detail::append_guard(output, instruction.guard);
  detail::append_binary_operands(output, instruction.dest, instruction.lhs,
                                 instruction.rhs);
  return output;
}

[[nodiscard]] inline auto to_string(const BranchInst& instruction)
    -> std::string {
  std::string output{"bra"};
  detail::append_guard(output, instruction.guard);
  output.push_back(' ');
  output += to_string(instruction.target);
  return output;
}

[[nodiscard]] inline auto to_string(const LoadInst& instruction)
    -> std::string {
  std::string output{"ld:"};
  output += detail::memory_space_name(instruction.space);
  output.push_back(':');
  output += detail::raw_width_name(instruction.dest.width);
  detail::append_guard(output, instruction.guard);
  output.push_back(' ');
  output += to_string(instruction.dest);
  output.push_back(',');
  output += to_string(instruction.address);
  return output;
}

[[nodiscard]] inline auto to_string(const StoreInst& instruction)
    -> std::string {
  std::string output{"st:"};
  output += detail::memory_space_name(instruction.space);
  output.push_back(':');
  output += detail::raw_width_name(instruction.src.width);
  detail::append_guard(output, instruction.guard);
  output.push_back(' ');
  output += to_string(instruction.address);
  output.push_back(',');
  output += to_string(instruction.src);
  return output;
}

[[nodiscard]] inline auto to_string(const Instruction& instruction)
    -> std::string {
  return std::visit([](const auto& record) { return to_string(record); },
                    instruction);
}

}  // namespace ptxsim::exec_ir
