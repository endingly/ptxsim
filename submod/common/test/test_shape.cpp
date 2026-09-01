#include <gtest/gtest.h>

#include <ptxsim/common/shape.hpp>

namespace ptxsim::common::test {

TEST(Dim3Test, Volume) {
  EXPECT_EQ((Dim3{1, 1, 1}.volume()), 1u);
  EXPECT_EQ((Dim3{32, 1, 1}.volume()), 32u);
  EXPECT_EQ((Dim3{8, 4, 2}.volume()), 64u);
}

TEST(Dim3Test, Linearize1D) {
  const Dim3 shape{32, 1, 1};

  EXPECT_EQ(linearize(Dim3{0, 0, 0}, shape), 0u);
  EXPECT_EQ(linearize(Dim3{1, 0, 0}, shape), 1u);
  EXPECT_EQ(linearize(Dim3{31, 0, 0}, shape), 31u);
}

TEST(Dim3Test, Linearize2D) {
  const Dim3 shape{8, 4, 1};

  EXPECT_EQ(linearize(Dim3{0, 0, 0}, shape), 0u);
  EXPECT_EQ(linearize(Dim3{7, 0, 0}, shape), 7u);
  EXPECT_EQ(linearize(Dim3{0, 1, 0}, shape), 8u);
  EXPECT_EQ(linearize(Dim3{3, 2, 0}, shape), 19u);
  EXPECT_EQ(linearize(Dim3{7, 3, 0}, shape), 31u);
}

TEST(Dim3Test, Linearize3D) {
  const Dim3 shape{4, 3, 2};

  EXPECT_EQ(linearize(Dim3{0, 0, 0}, shape), 0u);
  EXPECT_EQ(linearize(Dim3{3, 0, 0}, shape), 3u);
  EXPECT_EQ(linearize(Dim3{0, 1, 0}, shape), 4u);
  EXPECT_EQ(linearize(Dim3{0, 0, 1}, shape), 12u);
  EXPECT_EQ(linearize(Dim3{3, 2, 1}, shape), 23u);
}

TEST(Dim3Test, DelinearizeIsInverse) {
  const Dim3 shape{7, 5, 3};

  for (std::uint64_t i = 0; i < shape.volume(); ++i) {
    const auto coord = delinearize(i, shape);

    EXPECT_LT(coord.x, shape.x);
    EXPECT_LT(coord.y, shape.y);
    EXPECT_LT(coord.z, shape.z);

    EXPECT_EQ(linearize(coord, shape), i);
  }
}

TEST(GridShapeTest, Counts) {
  const GridShape shape{
      .cta_dim = {4, 2, 1},
      .thread_dim = {8, 4, 1},
      .warp_size = 32,
  };

  EXPECT_TRUE(shape.valid());

  EXPECT_EQ(shape.cta_count(), 8u);
  EXPECT_EQ(shape.threads_per_cta(), 32u);
  EXPECT_EQ(shape.warps_per_cta(), 1u);
  EXPECT_EQ(shape.thread_count(), 256u);
  EXPECT_EQ(shape.warp_count(), 8u);
}

TEST(GridShapeTest, PartialWarpCountRoundsUp) {
  const GridShape shape{
      .cta_dim = {2, 1, 1},
      .thread_dim = {33, 1, 1},
      .warp_size = 32,
  };

  EXPECT_EQ(shape.threads_per_cta(), 33u);
  EXPECT_EQ(shape.warps_per_cta(), 2u);
  EXPECT_EQ(shape.warp_count(), 4u);
}

TEST(GridShapeTest, ZeroDimensionIsInvalid) {
  EXPECT_FALSE((GridShape{
                    .cta_dim = {0, 1, 1},
                    .thread_dim = {32, 1, 1},
                    .warp_size = 32,
                })
                   .valid());

  EXPECT_FALSE((GridShape{
                    .cta_dim = {1, 1, 1},
                    .thread_dim = {0, 1, 1},
                    .warp_size = 32,
                })
                   .valid());

  EXPECT_FALSE((GridShape{
                    .cta_dim = {1, 1, 1},
                    .thread_dim = {32, 1, 1},
                    .warp_size = 0,
                })
                   .valid());
}

}  // namespace ptxsim::common::test