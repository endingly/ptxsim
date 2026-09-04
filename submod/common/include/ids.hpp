#pragma once

#include <charconv>
#include <compare>
#include <concepts>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace ptxsim::common {
namespace detail {

template <typename T>
concept HasPrefixMember = requires {
  { T::prefix } -> std::convertible_to<std::string_view>;
} && std::same_as<decltype(T::prefix), const std::string_view>;

template <HasPrefixMember Tag>
class Id {
 public:
  explicit constexpr Id(std::uint32_t value) noexcept : value_(value) {}
  [[nodiscard]] constexpr auto value() const noexcept -> std::uint32_t {
    return value_;
  }
  constexpr auto operator<=>(const Id&) const noexcept = default;

 private:
  std::uint32_t value_;
};

struct ProgramCounterTag {
  static constexpr std::string_view prefix = "pc";
};
struct FunctionIdTag {
  static constexpr std::string_view prefix = "function";
};
struct RegisterSlotTag {
  static constexpr std::string_view prefix = "register";
};
struct SymbolIdTag {
  static constexpr std::string_view prefix = "symbol";
};
struct LabelIdTag {
  static constexpr std::string_view prefix = "label";
};
struct SourceLocationIdTag {
  static constexpr std::string_view prefix = "source";
};
struct SpecialRegisterIdTag {
  static constexpr std::string_view prefix = "special-register";
};

}  // namespace detail

using ProgramCounter = detail::Id<detail::ProgramCounterTag>;
using FunctionId = detail::Id<detail::FunctionIdTag>;
using RegisterSlot = detail::Id<detail::RegisterSlotTag>;
using SymbolId = detail::Id<detail::SymbolIdTag>;
using LabelId = detail::Id<detail::LabelIdTag>;
using SourceLocationId = detail::Id<detail::SourceLocationIdTag>;
using SpecialRegisterId = detail::Id<detail::SpecialRegisterIdTag>;

struct CodeLocation {
  FunctionId function;
  ProgramCounter pc;

  constexpr auto operator<=>(const CodeLocation&) const noexcept = default;
};

template <typename Tag>
[[nodiscard]] inline auto to_string(detail::Id<Tag> id) -> std::string {
  constexpr std::size_t max_digits =
      std::numeric_limits<std::uint32_t>::digits10 + 1;
  char digits[max_digits];
  const auto [end, ec] = std::to_chars(digits, digits + max_digits, id.value());
  (void)ec;

  static_assert(Tag::prefix.size() >= 0,
                "Tag::prefix must be a static constexpr std::string_view");

  std::string result;
  result.reserve(Tag::prefix.size() + 1 + max_digits);
  result.append(Tag::prefix);
  result.push_back(':');
  result.append(digits, end);
  return result;
}

}  // namespace ptxsim::common
