#include <gtest/gtest.h>

#include <cstdint>
#include <type_traits>
#include <unordered_set>

#include <ptxsim/execution_model/execution_model.hpp>

namespace ptxsim::execution_model::test {
namespace {

/*
 * Architectural runtime topology nodes have stable identity.
 * Copying or moving them would invalidate parent/child navigation
 * and non-owning index tables.
 */
static_assert(!std::is_copy_constructible_v<Grid>);
static_assert(!std::is_copy_assignable_v<Grid>);
static_assert(!std::is_move_constructible_v<Grid>);
static_assert(!std::is_move_assignable_v<Grid>);

static_assert(!std::is_copy_constructible_v<CTA>);
static_assert(!std::is_copy_assignable_v<CTA>);
static_assert(!std::is_move_constructible_v<CTA>);
static_assert(!std::is_move_assignable_v<CTA>);

static_assert(!std::is_copy_constructible_v<Warp>);
static_assert(!std::is_copy_assignable_v<Warp>);
static_assert(!std::is_move_constructible_v<Warp>);
static_assert(!std::is_move_assignable_v<Warp>);

static_assert(!std::is_copy_constructible_v<Thread>);
static_assert(!std::is_copy_assignable_v<Thread>);
static_assert(!std::is_move_constructible_v<Thread>);
static_assert(!std::is_move_assignable_v<Thread>);

}  // namespace

TEST(GridTest, InvalidShapeThrows) {
  const GridShape invalid{
      .cta_dim = {1, 1, 1},
      .thread_dim = {0, 1, 1},
      .warp_size = 32,
  };

  EXPECT_THROW([[maybe_unused]] Grid grid(GridId{0}, invalid),
               std::invalid_argument);
}

TEST(GridTest, SingleThreadGrid) {
  Grid grid(GridId{7}, GridShape{
                           .cta_dim = {1, 1, 1},
                           .thread_dim = {1, 1, 1},
                           .warp_size = 32,
                       });

  EXPECT_EQ(grid.id(), GridId{7});

  EXPECT_EQ(grid.cta_count(), 1u);
  EXPECT_EQ(grid.warp_count(), 1u);
  EXPECT_EQ(grid.thread_count(), 1u);

  auto& cta = grid.cta(CtaId{GridId{7}, 0});
  EXPECT_EQ(cta.position(), (Dim3{0, 0, 0}));
  EXPECT_EQ(cta.thread_count(), 1u);

  auto& warp = grid.warp(WarpId{GridId{7}, 0});
  EXPECT_EQ(warp.thread_count(), 1u);
  EXPECT_FALSE(warp.full());

  auto& thread = grid.thread(ThreadId{GridId{7}, 0});

  EXPECT_EQ(thread.position(), (Dim3{0, 0, 0}));
  EXPECT_EQ(thread.lane_id(), LaneId{0});
}

TEST(GridTest, OneFullWarp) {
  Grid grid(GridId{0}, GridShape{
                           .cta_dim = {1, 1, 1},
                           .thread_dim = {32, 1, 1},
                           .warp_size = 32,
                       });

  EXPECT_EQ(grid.cta_count(), 1u);
  EXPECT_EQ(grid.warp_count(), 1u);
  EXPECT_EQ(grid.thread_count(), 32u);

  auto& warp = grid.warp(WarpId{GridId{0}, 0});

  EXPECT_TRUE(warp.full());
  EXPECT_EQ(warp.thread_count(), 32u);

  for (std::uint32_t lane = 0; lane < 32; ++lane) {
    const auto& thread = warp.thread(LaneId{lane});

    EXPECT_EQ(thread.lane_id(), LaneId{lane});
    EXPECT_EQ(thread.linear_index_in_cta(), lane);
    EXPECT_EQ(thread.position(), (Dim3{lane, 0, 0}));
  }
}

TEST(GridTest, PartialWarp) {
  Grid grid(GridId{0}, GridShape{
                           .cta_dim = {1, 1, 1},
                           .thread_dim = {33, 1, 1},
                           .warp_size = 32,
                       });

  EXPECT_EQ(grid.warp_count(), 2u);
  EXPECT_EQ(grid.thread_count(), 33u);

  auto& warp0 = grid.warp(WarpId{GridId{0}, 0});
  auto& warp1 = grid.warp(WarpId{GridId{0}, 1});

  EXPECT_TRUE(warp0.full());
  EXPECT_EQ(warp0.thread_count(), 32u);

  EXPECT_FALSE(warp1.full());
  EXPECT_EQ(warp1.thread_count(), 1u);

  const auto& last_thread = warp1.thread(LaneId{0});

  EXPECT_EQ(last_thread.linear_index_in_cta(), 32u);
  EXPECT_EQ(last_thread.position(), (Dim3{32, 0, 0}));
  EXPECT_EQ(last_thread.lane_id(), LaneId{0});
}

TEST(GridTest, MultiWarpLaneIdsResetPerWarp) {
  Grid grid(GridId{0}, GridShape{
                           .cta_dim = {1, 1, 1},
                           .thread_dim = {64, 1, 1},
                           .warp_size = 32,
                       });

  const auto& warp0 = grid.warp(WarpId{GridId{0}, 0});

  const auto& warp1 = grid.warp(WarpId{GridId{0}, 1});

  EXPECT_EQ(warp0.thread(LaneId{0}).linear_index_in_cta(), 0u);

  EXPECT_EQ(warp1.thread(LaneId{0}).linear_index_in_cta(), 32u);

  EXPECT_EQ(warp0.thread(LaneId{0}).lane_id(), LaneId{0});

  EXPECT_EQ(warp1.thread(LaneId{0}).lane_id(), LaneId{0});

  EXPECT_EQ(warp1.thread(LaneId{31}).lane_id(), LaneId{31});
}

TEST(GridTest, TwoDimensionalThreadCoordinates) {
  Grid grid(GridId{0}, GridShape{
                           .cta_dim = {1, 1, 1},
                           .thread_dim = {8, 4, 1},
                           .warp_size = 32,
                       });

  ASSERT_EQ(grid.thread_count(), 32u);

  for (std::uint32_t y = 0; y < 4; ++y) {
    for (std::uint32_t x = 0; x < 8; ++x) {
      const std::uint64_t linear = static_cast<std::uint64_t>(y) * 8 + x;

      const auto& thread = grid.thread(ThreadId{
          .grid = GridId{0},
          .value = linear,
      });

      EXPECT_EQ(thread.position(), (Dim3{x, y, 0}));
      EXPECT_EQ(thread.linear_index_in_cta(), linear);
    }
  }
}

TEST(GridTest, ThreeDimensionalThreadCoordinates) {
  Grid grid(GridId{4}, GridShape{
                           .cta_dim = {1, 1, 1},
                           .thread_dim = {4, 3, 2},
                           .warp_size = 32,
                       });

  ASSERT_EQ(grid.thread_count(), 24u);

  for (std::uint64_t linear = 0; linear < 24; ++linear) {
    const auto expected = delinearize(linear, Dim3{4, 3, 2});

    const auto& thread = grid.thread(ThreadId{
        .grid = GridId{4},
        .value = linear,
    });

    EXPECT_EQ(thread.position(), expected);
  }
}

TEST(GridTest, ThreeDimensionalCtaCoordinates) {
  Grid grid(GridId{2}, GridShape{
                           .cta_dim = {4, 3, 2},
                           .thread_dim = {1, 1, 1},
                           .warp_size = 32,
                       });

  ASSERT_EQ(grid.cta_count(), 24u);

  for (std::uint64_t linear = 0; linear < 24; ++linear) {
    const auto expected = delinearize(linear, Dim3{4, 3, 2});

    const auto& cta = grid.cta(CtaId{
        .grid = GridId{2},
        .value = linear,
    });

    EXPECT_EQ(cta.position(), expected);
  }
}

TEST(GridTest, ParentNavigationIsConsistent) {
  Grid grid(GridId{13}, GridShape{
                            .cta_dim = {2, 1, 1},
                            .thread_dim = {64, 1, 1},
                            .warp_size = 32,
                        });

  auto& thread = grid.thread(ThreadId{
      .grid = GridId{13},
      .value = 70,
  });

  auto& warp = thread.warp();
  auto& cta = thread.cta();

  EXPECT_EQ(&thread.grid(), &grid);
  EXPECT_EQ(&warp.grid(), &grid);
  EXPECT_EQ(&cta.grid(), &grid);

  EXPECT_EQ(&warp.cta(), &cta);
  EXPECT_EQ(&thread.warp(), &warp);
  EXPECT_EQ(&thread.cta(), &cta);
}

TEST(GridTest, ConstParentNavigationIsConsistent) {
  const Grid grid(GridId{13}, GridShape{
                                  .cta_dim = {1, 1, 1},
                                  .thread_dim = {32, 1, 1},
                                  .warp_size = 32,
                              });

  const auto& thread = grid.thread(ThreadId{
      .grid = GridId{13},
      .value = 0,
  });

  const auto& warp = thread.warp();
  const auto& cta = thread.cta();

  EXPECT_EQ(&thread.grid(), &grid);
  EXPECT_EQ(&warp.grid(), &grid);
  EXPECT_EQ(&cta.grid(), &grid);
}

TEST(GridTest, IDsAreUnique) {
  Grid grid(GridId{21}, GridShape{
                            .cta_dim = {3, 2, 1},
                            .thread_dim = {65, 1, 1},
                            .warp_size = 32,
                        });

  std::unordered_set<std::uint64_t> cta_ids;
  std::unordered_set<std::uint64_t> warp_ids;
  std::unordered_set<std::uint64_t> thread_ids;

  for (const auto& cta : grid) {
    EXPECT_TRUE(cta_ids.insert(cta.id().value).second);

    for (const auto& warp : cta) {
      EXPECT_TRUE(warp_ids.insert(warp.id().value).second);

      for (const auto& thread : warp) {
        EXPECT_TRUE(thread_ids.insert(thread.id().value).second);
      }
    }
  }

  EXPECT_EQ(cta_ids.size(), grid.cta_count());
  EXPECT_EQ(warp_ids.size(), grid.warp_count());
  EXPECT_EQ(thread_ids.size(), grid.thread_count());
}

TEST(GridTest, EveryNodeUsesCorrectGridId) {
  constexpr GridId grid_id{37};

  Grid grid(grid_id, GridShape{
                         .cta_dim = {2, 2, 1},
                         .thread_dim = {33, 1, 1},
                         .warp_size = 32,
                     });

  for (const auto& cta : grid) {
    EXPECT_EQ(cta.id().grid, grid_id);

    for (const auto& warp : cta) {
      EXPECT_EQ(warp.id().grid, grid_id);

      for (const auto& thread : warp) {
        EXPECT_EQ(thread.id().grid, grid_id);
      }
    }
  }
}

TEST(GridTest, FindFunctionsReturnCorrectObjects) {
  Grid grid(GridId{3}, GridShape{
                           .cta_dim = {2, 1, 1},
                           .thread_dim = {64, 1, 1},
                           .warp_size = 32,
                       });

  const CtaId cta_id{GridId{3}, 1};
  const WarpId warp_id{GridId{3}, 2};
  const ThreadId thread_id{GridId{3}, 64};

  ASSERT_NE(grid.find_cta(cta_id), nullptr);
  ASSERT_NE(grid.find_warp(warp_id), nullptr);
  ASSERT_NE(grid.find_thread(thread_id), nullptr);

  EXPECT_EQ(grid.find_cta(cta_id), &grid.cta(cta_id));

  EXPECT_EQ(grid.find_warp(warp_id), &grid.warp(warp_id));

  EXPECT_EQ(grid.find_thread(thread_id), &grid.thread(thread_id));
}

TEST(GridTest, ForeignGridIdsAreRejected) {
  Grid grid(GridId{1}, GridShape{
                           .cta_dim = {1, 1, 1},
                           .thread_dim = {32, 1, 1},
                           .warp_size = 32,
                       });

  EXPECT_EQ(grid.find_cta(CtaId{GridId{999}, 0}), nullptr);

  EXPECT_EQ(grid.find_warp(WarpId{GridId{999}, 0}), nullptr);

  EXPECT_EQ(grid.find_thread(ThreadId{GridId{999}, 0}), nullptr);
}

TEST(GridTest, OutOfRangeIdsAreRejected) {
  Grid grid(GridId{1}, GridShape{
                           .cta_dim = {1, 1, 1},
                           .thread_dim = {32, 1, 1},
                           .warp_size = 32,
                       });

  EXPECT_EQ(grid.find_cta(CtaId{GridId{1}, 1}), nullptr);

  EXPECT_EQ(grid.find_warp(WarpId{GridId{1}, 1}), nullptr);

  EXPECT_EQ(grid.find_thread(ThreadId{GridId{1}, 32}), nullptr);
}

/*
 * Address stability is an architectural invariant of execution_model:
 * index lookup and owner-tree traversal must always resolve to the same
 * runtime node.
 */
TEST(GridTest, NodeAddressesRemainStable) {
  Grid grid(GridId{0}, GridShape{
                           .cta_dim = {4, 1, 1},
                           .thread_dim = {64, 1, 1},
                           .warp_size = 32,
                       });

  const CtaId cta_id{GridId{0}, 2};
  const WarpId warp_id{GridId{0}, 5};
  const ThreadId thread_id{GridId{0}, 162};

  CTA* const cta = grid.find_cta(cta_id);
  Warp* const warp = grid.find_warp(warp_id);
  Thread* const thread = grid.find_thread(thread_id);

  ASSERT_NE(cta, nullptr);
  ASSERT_NE(warp, nullptr);
  ASSERT_NE(thread, nullptr);

  // Mutate legal runtime state; topology identity must not move.
  thread->set_pc(ProgramCounter{123});
  thread->mark_waiting(WaitReason::Other);
  thread->mark_ready();

  EXPECT_EQ(grid.find_cta(cta_id), cta);
  EXPECT_EQ(grid.find_warp(warp_id), warp);
  EXPECT_EQ(grid.find_thread(thread_id), thread);

  EXPECT_EQ(&thread->warp(), warp);
  EXPECT_EQ(&thread->cta(), cta);
  EXPECT_EQ(&thread->grid(), &grid);
}

TEST(GridTest, LargeTopologyIsInternallyConsistent) {
  constexpr GridId grid_id{42};

  const GridShape shape{
      .cta_dim = {3, 2, 2},      // 12 CTAs
      .thread_dim = {17, 3, 2},  // 102 threads / CTA
      .warp_size = 32,
  };

  Grid grid(grid_id, shape);

  ASSERT_EQ(grid.cta_count(), 12u);
  ASSERT_EQ(grid.thread_count(), 1224u);
  ASSERT_EQ(grid.warp_count(), 48u);

  std::size_t observed_ctas = 0;
  std::size_t observed_warps = 0;
  std::size_t observed_threads = 0;

  for (auto& cta : grid) {
    ++observed_ctas;

    EXPECT_EQ(&cta.grid(), &grid);
    EXPECT_EQ(cta.thread_count(), shape.threads_per_cta());

    std::size_t cta_threads = 0;

    for (auto& warp : cta) {
      ++observed_warps;

      EXPECT_EQ(&warp.cta(), &cta);
      EXPECT_EQ(&warp.grid(), &grid);

      for (auto& thread : warp) {
        ++observed_threads;
        ++cta_threads;

        EXPECT_EQ(&thread.warp(), &warp);
        EXPECT_EQ(&thread.cta(), &cta);
        EXPECT_EQ(&thread.grid(), &grid);

        EXPECT_EQ(linearize(thread.position(), cta.thread_shape()),
                  thread.linear_index_in_cta());

        EXPECT_EQ(thread.id().grid, grid_id);

        EXPECT_LT(thread.lane_id().value, shape.warp_size);
      }
    }

    EXPECT_EQ(cta_threads, shape.threads_per_cta());
  }

  EXPECT_EQ(observed_ctas, grid.cta_count());
  EXPECT_EQ(observed_warps, grid.warp_count());
  EXPECT_EQ(observed_threads, grid.thread_count());
}

}  // namespace ptxsim::execution_model::test
