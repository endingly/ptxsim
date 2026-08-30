#pragma once

#include <charconv>
#include <compare>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace ptxsim::common {
namespace detail {

template <typename Tag>
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
struct ThreadIdTag {
  static constexpr std::string_view prefix = "thread";
};
struct CtaIdTag {
  static constexpr std::string_view prefix = "cta";
};
struct WarpIdTag {
  static constexpr std::string_view prefix = "warp";
};
struct LaneIdTag {
  static constexpr std::string_view prefix = "lane";
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
using ThreadId = detail::Id<detail::ThreadIdTag>;
using CtaId = detail::Id<detail::CtaIdTag>;
using WarpId = detail::Id<detail::WarpIdTag>;
using LaneId = detail::Id<detail::LaneIdTag>;
using SpecialRegisterId = detail::Id<detail::SpecialRegisterIdTag>;

template <typename Tag>
[[nodiscard]] inline auto to_string(detail::Id<Tag> id) -> std::string {
  char digits[std::numeric_limits<std::uint32_t>::digits10 + 1];
  const auto [end, error] =
      std::to_chars(digits, digits + sizeof(digits), id.value());
  (void)error;
  std::string result{Tag::prefix};
  result.push_back(':');
  result.append(digits, end);
  return result;
}

}  // namespace ptxsim::common
