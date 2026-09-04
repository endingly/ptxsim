#pragma once

#include <concepts>
#include <string>
#include <string_view>
#include <variant>

#include <ptxsim/exec_ir/exec_ir_types.hpp>

namespace ptxsim::exec_ir::detail {

/** @brief Format common owned IDs and raw values through their established text. */
template <typename T>
  requires requires(const T& value) { common::to_string(value); }
[[nodiscard]] inline auto diagnostic_value(const T& value) -> std::string {
  return common::to_string(value);
}

/** @brief Format execution-IR leaves through their stable diagnostic text. */
template <typename T>
  requires(!requires(const T& value) { common::to_string(value); }) &&
          requires(const T& value) {
            {
              ::ptxsim::exec_ir::to_string(value)
            } -> std::convertible_to<std::string_view>;
          }
[[nodiscard]] inline auto diagnostic_value(const T& value) -> std::string {
  return std::string{::ptxsim::exec_ir::to_string(value)};
}

/** @brief Format a sum alternative by visiting its currently held value. */
template <typename... T>
  requires(requires(const T& item) {
    { diagnostic_value(item) } -> std::same_as<std::string>;
  } && ...)
[[nodiscard]] inline auto diagnostic_value(const std::variant<T...>& value)
    -> std::string {
  return std::visit([](const auto& item) { return diagnostic_value(item); },
                    value);
}

/** @brief Append one bound operand, separating it from preceding operands. */
template <typename T>
  requires requires(const T& item) {
    { diagnostic_value(item) } -> std::same_as<std::string>;
  }
inline void append_operand(std::string& output, const T& value) {
  if (!output.empty())
    output += ", ";
  output += diagnostic_value(value);
}

}  // namespace ptxsim::exec_ir::detail
