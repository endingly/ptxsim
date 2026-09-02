#pragma once

#include <cstdint>

namespace ptxsim::common {

struct Dim3 {
  std::uint32_t x = 1;
  std::uint32_t y = 1;
  std::uint32_t z = 1;

  [[nodiscard]]
  constexpr std::uint64_t volume() const noexcept {
    return static_cast<std::uint64_t>(x) * static_cast<std::uint64_t>(y) *
           static_cast<std::uint64_t>(z);
  }

  bool operator<=>(const Dim3&) const = default;
};

[[nodiscard]]
constexpr std::uint64_t linearize(Dim3 index, Dim3 shape) noexcept {
  return static_cast<std::uint64_t>(index.z) * shape.y * shape.x +
         static_cast<std::uint64_t>(index.y) * shape.x + index.x;
}

[[nodiscard]]
constexpr Dim3 delinearize(std::uint64_t linear, Dim3 shape) noexcept {
  Dim3 result{};

  result.x = static_cast<std::uint32_t>(linear % shape.x);
  linear /= shape.x;

  result.y = static_cast<std::uint32_t>(linear % shape.y);
  linear /= shape.y;

  result.z = static_cast<std::uint32_t>(linear);

  return result;
}

struct GridShape {
  // Number of CTAs in x/y/z.
  Dim3 cta_dim{};

  // Number of threads per CTA in x/y/z.
  Dim3 thread_dim{};

  // Architectural warp size used by this launch.
  std::uint32_t warp_size = 32;

  [[nodiscard]]
  constexpr bool valid() const noexcept {
    return cta_dim.x != 0 && cta_dim.y != 0 && cta_dim.z != 0 &&
           thread_dim.x != 0 && thread_dim.y != 0 && thread_dim.z != 0 &&
           warp_size != 0;
  }

  [[nodiscard]]
  constexpr std::uint64_t cta_count() const noexcept {
    return cta_dim.volume();
  }

  [[nodiscard]]
  constexpr std::uint64_t threads_per_cta() const noexcept {
    return thread_dim.volume();
  }

  [[nodiscard]]
  constexpr std::uint64_t warps_per_cta() const noexcept {
    return (threads_per_cta() + warp_size - 1) / warp_size;
  }

  [[nodiscard]]
  constexpr std::uint64_t thread_count() const noexcept {
    return cta_count() * threads_per_cta();
  }

  [[nodiscard]]
  constexpr std::uint64_t warp_count() const noexcept {
    return cta_count() * warps_per_cta();
  }
};

}  // namespace ptxsim::execution_model