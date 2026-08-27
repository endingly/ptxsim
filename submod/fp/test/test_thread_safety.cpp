#include <ptxsim/fp/environment.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <thread>

namespace ptxsim::fp::test {

TEST(ThreadSafety, DifferentRoundingModesAreIsolated) {
  const Environment environment;
  std::atomic<bool> start{false};
  std::atomic<bool> failed{false};
  auto worker = [&](RoundingMode mode, std::uint32_t expected) {
    while (!start.load(std::memory_order_acquire)) {}
    for (int index = 0; index != 20000; ++index) {
      const auto result =
          environment.add(Fp32{0x3F800000u}, Fp32{0x33800000u}, {mode});
      if (result.value.bits != expected ||
          !result.flags.contains(ExceptionFlag::Inexact))
        failed.store(true, std::memory_order_relaxed);
    }
  };
  std::thread positive(worker, RoundingMode::TowardPositive, 0x3F800001u);
  std::thread negative(worker, RoundingMode::TowardNegative, 0x3F800000u);
  start.store(true, std::memory_order_release);
  positive.join();
  negative.join();
  EXPECT_FALSE(failed.load());
}

}  // namespace ptxsim::fp::test
