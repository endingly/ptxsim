#include <ptxsim/fp/validation.hpp>

#include <gtest/gtest.h>

namespace ptxsim::fp::test {

TEST(ValidationFormats, GenericPoliciesCoverEveryFormat) {
  using namespace validation;
  EXPECT_TRUE(bit_exact(Bf16{0x3F80u}, Bf16{0x3F80u}));
  EXPECT_EQ(ulp_distance(Bf16{0x3F80u}, Bf16{0x3F81u}), 1);
  EXPECT_TRUE(within_ulp(Tf32{0x3F800000u}, Tf32{0x3F802000u}, 1u));
  EXPECT_TRUE(same_float_class(Fp8E4M3{0x7Fu}, Fp8E4M3{0xFFu}));
  EXPECT_EQ(ulp_distance(Fp8E5M2{0x3Cu}, Fp8E5M2{0x3Du}), 1);
  EXPECT_EQ(ulp_distance(Fp4E2M1{0x02u}, Fp4E2M1{0x03u}), 1);
  EXPECT_EQ(ulp_distance(Fp4E2M1{}, Fp4E2M1{0x08u}), 0);
}

}  // namespace ptxsim::fp::test
