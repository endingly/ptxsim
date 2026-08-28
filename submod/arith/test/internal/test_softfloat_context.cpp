#include <gtest/gtest.h>

#include "detail/softfloat_context.hpp"

#include <atomic>
#include <stdexcept>
#include <thread>
#include <vector>

extern "C" {
#include <softfloat/softfloat.h>
}

namespace ptxsim::arith::test {
namespace {

using detail::RoundingMode;
using detail::SoftFloatContext;

TEST(SoftFloatState, NestedGuardsRestoreEveryThreadLocalField) {
  const auto initial_rounding = softfloat_roundingMode;
  const auto initial_tininess = softfloat_detectTininess;
  const auto initial_flags = softfloat_exceptionFlags;

  softfloat_roundingMode = softfloat_round_minMag;
  softfloat_detectTininess = softfloat_tininess_beforeRounding;
  softfloat_exceptionFlags = softfloat_flag_overflow;
  {
    SoftFloatContext outer{RoundingMode::TowardPositive};
    EXPECT_EQ(softfloat_roundingMode, softfloat_round_max);
    EXPECT_EQ(softfloat_detectTininess, softfloat_tininess_afterRounding);
    EXPECT_EQ(softfloat_exceptionFlags, 0);
    softfloat_exceptionFlags = softfloat_flag_invalid;
    {
      SoftFloatContext inner{RoundingMode::TowardNegative};
      EXPECT_EQ(softfloat_roundingMode, softfloat_round_min);
      EXPECT_EQ(softfloat_exceptionFlags, 0);
      softfloat_exceptionFlags = softfloat_flag_underflow;
      EXPECT_TRUE(inner.flags().contains(ExceptionFlag::Underflow));
    }
    EXPECT_EQ(softfloat_roundingMode, softfloat_round_max);
    EXPECT_EQ(softfloat_detectTininess, softfloat_tininess_afterRounding);
    EXPECT_EQ(softfloat_exceptionFlags, softfloat_flag_invalid);
    EXPECT_TRUE(outer.flags().contains(ExceptionFlag::Invalid));
  }
  EXPECT_EQ(softfloat_roundingMode, softfloat_round_minMag);
  EXPECT_EQ(softfloat_detectTininess, softfloat_tininess_beforeRounding);
  EXPECT_EQ(softfloat_exceptionFlags, softfloat_flag_overflow);

  softfloat_roundingMode = initial_rounding;
  softfloat_detectTininess = initial_tininess;
  softfloat_exceptionFlags = initial_flags;
}

TEST(SoftFloatState, ExceptionUnwindingRestoresState) {
  const auto initial_rounding = softfloat_roundingMode;
  const auto initial_tininess = softfloat_detectTininess;
  const auto initial_flags = softfloat_exceptionFlags;
  EXPECT_THROW(
      {
        SoftFloatContext guard{RoundingMode::TowardPositive};
        softfloat_exceptionFlags = softfloat_flag_inexact;
        throw std::runtime_error{"test unwind"};
      },
      std::runtime_error);
  EXPECT_EQ(softfloat_roundingMode, initial_rounding);
  EXPECT_EQ(softfloat_detectTininess, initial_tininess);
  EXPECT_EQ(softfloat_exceptionFlags, initial_flags);
}

TEST(SoftFloatState, NestedGuardsRemainIsolatedUnderThreadContention) {
  std::atomic<bool> failed{false};
  std::vector<std::thread> threads;
  for (unsigned index = 0; index != 8; ++index) {
    threads.emplace_back([&, index] {
      const auto baseline_rounding = index % 2 == 0 ? softfloat_round_near_even
                                                    : softfloat_round_minMag;
      const auto baseline_flags = index % 2 == 0 ? softfloat_flag_overflow
                                                 : softfloat_flag_invalid;
      softfloat_roundingMode = baseline_rounding;
      softfloat_detectTininess = softfloat_tininess_beforeRounding;
      softfloat_exceptionFlags = baseline_flags;
      for (unsigned iteration = 0; iteration != 2000; ++iteration) {
        {
          SoftFloatContext outer{RoundingMode::TowardPositive};
          softfloat_exceptionFlags = softfloat_flag_inexact;
          {
            SoftFloatContext inner{RoundingMode::TowardNegative};
            if (softfloat_roundingMode != softfloat_round_min ||
                softfloat_exceptionFlags != 0)
              failed.store(true, std::memory_order_relaxed);
          }
          if (softfloat_roundingMode != softfloat_round_max ||
              softfloat_exceptionFlags != softfloat_flag_inexact)
            failed.store(true, std::memory_order_relaxed);
        }
        if (softfloat_roundingMode != baseline_rounding ||
            softfloat_detectTininess != softfloat_tininess_beforeRounding ||
            softfloat_exceptionFlags != baseline_flags)
          failed.store(true, std::memory_order_relaxed);
      }
    });
  }
  for (auto& thread : threads)
    thread.join();
  EXPECT_FALSE(failed.load(std::memory_order_relaxed));
}

}  // namespace
}  // namespace ptxsim::arith::test
