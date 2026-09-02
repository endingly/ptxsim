#pragma once

#include <compare>
#include <cstdint>

namespace ptxsim::execution_model {

struct GridId {
  std::uint64_t value = 0;

  auto operator<=>(const GridId&) const = default;
};

template <typename Tag>
struct GridScopedId {
  GridId grid{};
  std::uint64_t value = 0;

  auto operator<=>(const GridScopedId&) const = default;
};

struct CtaIdTag;
struct WarpIdTag;
struct ThreadIdTag;

using CtaId = GridScopedId<CtaIdTag>;
using WarpId = GridScopedId<WarpIdTag>;
using ThreadId = GridScopedId<ThreadIdTag>;

struct LaneId {
  std::uint32_t value = 0;

  auto operator<=>(const LaneId&) const = default;
};

}  // namespace ptxsim::execution_model