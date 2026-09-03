#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <ptxsim/execution_model/cta.hpp>
#include <ptxsim/execution_model/cta_state.hpp>
#include <ptxsim/execution_model/execution_model.hpp>

namespace ptxsim::execution_model::test {
namespace {

// -----------------------------------------------------------------------------
// Compile-time topology contract
// -----------------------------------------------------------------------------

static_assert(!std::is_copy_constructible_v<CTA>);
static_assert(!std::is_copy_assignable_v<CTA>);
static_assert(!std::is_move_constructible_v<CTA>);
static_assert(!std::is_move_assignable_v<CTA>);
}  // namespace
// -----------------------------------------------------------------------------
// Basic CTA topology
// -----------------------------------------------------------------------------

TEST(CtaTest, BasicTopologyIsConstructedCorrectly) {
  constexpr GridId grid_id{0};

  const GridShape shape{
      .cta_dim = {2, 1, 1},
      .thread_dim = {64, 1, 1},
      .warp_size = 32,
  };

  Grid grid(grid_id, shape);

  ASSERT_EQ(grid.cta_count(), 2u);

  const auto& cta0 = grid.cta(CtaId{grid_id, 0});

  const auto& cta1 = grid.cta(CtaId{grid_id, 1});

  EXPECT_EQ(cta0.id(), (CtaId{grid_id, 0}));
  EXPECT_EQ(cta1.id(), (CtaId{grid_id, 1}));

  EXPECT_EQ(cta0.position(), (Dim3{0, 0, 0}));
  EXPECT_EQ(cta1.position(), (Dim3{1, 0, 0}));

  EXPECT_EQ(cta0.thread_shape(), (Dim3{64, 1, 1}));
  EXPECT_EQ(cta0.thread_count(), 64u);

  EXPECT_EQ(cta0.warp_size(), 32u);
  EXPECT_EQ(cta0.warp_count(), 2u);
}

TEST(CtaTest, MultiDimensionalPositionIsPreserved) {
  constexpr GridId grid_id{3};

  const GridShape shape{
      .cta_dim = {4, 3, 2},
      .thread_dim = {8, 4, 1},
      .warp_size = 32,
  };

  Grid grid(grid_id, shape);

  for (std::uint64_t linear = 0; linear < shape.cta_count(); ++linear) {
    const auto expected = delinearize(linear, shape.cta_dim);

    const auto& cta = grid.cta(CtaId{
        .grid = grid_id,
        .value = linear,
    });

    EXPECT_EQ(cta.position(), expected);
  }
}

TEST(CtaTest, ParentGridNavigationIsCorrect) {
  constexpr GridId grid_id{17};

  const GridShape shape{
      .cta_dim = {2, 1, 1},
      .thread_dim = {32, 1, 1},
      .warp_size = 32,
  };

  Grid grid(grid_id, shape);

  auto& cta = grid.cta(CtaId{grid_id, 1});

  EXPECT_EQ(&cta.grid(), &grid);

  for (auto& warp : cta) {
    EXPECT_EQ(&warp.cta(), &cta);
    EXPECT_EQ(&warp.grid(), &grid);

    for (auto& thread : warp) {
      EXPECT_EQ(&thread.cta(), &cta);
      EXPECT_EQ(&thread.grid(), &grid);
    }
  }
}

TEST(CtaTest, OwnsExpectedNumberOfWarps) {
  constexpr GridId grid_id{0};

  const GridShape shape{
      .cta_dim = {1, 1, 1},
      .thread_dim = {96, 1, 1},
      .warp_size = 32,
  };

  Grid grid(grid_id, shape);

  const auto& cta = grid.cta(CtaId{grid_id, 0});

  ASSERT_EQ(cta.warp_count(), 3u);

  EXPECT_EQ(cta.warp(0).index_in_cta(), 0u);
  EXPECT_EQ(cta.warp(1).index_in_cta(), 1u);
  EXPECT_EQ(cta.warp(2).index_in_cta(), 2u);
}

TEST(CtaTest, PartialFinalWarpIsOwnedNormally) {
  constexpr GridId grid_id{0};

  const GridShape shape{
      .cta_dim = {1, 1, 1},
      .thread_dim = {35, 1, 1},
      .warp_size = 32,
  };

  Grid grid(grid_id, shape);

  const auto& cta = grid.cta(CtaId{grid_id, 0});

  ASSERT_EQ(cta.warp_count(), 2u);

  EXPECT_EQ(cta.warp(0).thread_count(), 32u);
  EXPECT_TRUE(cta.warp(0).full());

  EXPECT_EQ(cta.warp(1).thread_count(), 3u);
  EXPECT_FALSE(cta.warp(1).full());
}

// -----------------------------------------------------------------------------
// Derived CTA execution state
// -----------------------------------------------------------------------------

TEST(CtaTest, AllThreadsAreInitiallyLive) {
  constexpr GridId grid_id{0};

  const GridShape shape{
      .cta_dim = {1, 1, 1},
      .thread_dim = {35, 1, 1},
      .warp_size = 32,
  };

  Grid grid(grid_id, shape);

  const auto& cta = grid.cta(CtaId{grid_id, 0});

  EXPECT_EQ(cta.live_thread_count(), 35u);
  EXPECT_FALSE(cta.completed());
  EXPECT_FALSE(cta.trapped());
}

TEST(CtaTest, LiveThreadCountIsDerivedFromThreadState) {
  constexpr GridId grid_id{0};

  const GridShape shape{
      .cta_dim = {1, 1, 1},
      .thread_dim = {8, 1, 1},
      .warp_size = 32,
  };

  Grid grid(grid_id, shape);

  auto& cta = grid.cta(CtaId{grid_id, 0});

  ASSERT_EQ(cta.live_thread_count(), 8u);

  cta.warp(0).thread(LaneId{1}).mark_exited();

  EXPECT_EQ(cta.live_thread_count(), 7u);

  cta.warp(0).thread(LaneId{5}).mark_exited();

  EXPECT_EQ(cta.live_thread_count(), 6u);
}

TEST(CtaTest, WaitingThreadsRemainLive) {
  constexpr GridId grid_id{0};

  const GridShape shape{
      .cta_dim = {1, 1, 1},
      .thread_dim = {8, 1, 1},
      .warp_size = 32,
  };

  Grid grid(grid_id, shape);

  auto& cta = grid.cta(CtaId{grid_id, 0});

  cta.warp(0).thread(LaneId{2}).mark_waiting(WaitReason::CtaBarrier);

  cta.warp(0).thread(LaneId{6}).mark_waiting(WaitReason::CtaBarrier);

  EXPECT_EQ(cta.live_thread_count(), 8u);
  EXPECT_FALSE(cta.completed());
}

TEST(CtaTest, CompletedIsDerivedFromAllChildThreads) {
  constexpr GridId grid_id{0};

  const GridShape shape{
      .cta_dim = {1, 1, 1},
      .thread_dim = {5, 1, 1},
      .warp_size = 32,
  };

  Grid grid(grid_id, shape);

  auto& cta = grid.cta(CtaId{grid_id, 0});

  ASSERT_FALSE(cta.completed());

  for (auto& warp : cta) {
    for (auto& thread : warp) {
      thread.mark_exited();
    }
  }

  EXPECT_TRUE(cta.completed());
  EXPECT_EQ(cta.live_thread_count(), 0u);
}

TEST(CtaTest, OneLiveThreadPreventsCompletion) {
  constexpr GridId grid_id{0};

  const GridShape shape{
      .cta_dim = {1, 1, 1},
      .thread_dim = {8, 1, 1},
      .warp_size = 32,
  };

  Grid grid(grid_id, shape);

  auto& cta = grid.cta(CtaId{grid_id, 0});

  for (std::uint32_t lane = 0; lane < 7; ++lane) {
    cta.warp(0).thread(LaneId{lane}).mark_exited();
  }

  EXPECT_EQ(cta.live_thread_count(), 1u);
  EXPECT_FALSE(cta.completed());
}

TEST(CtaTest, TrapStateIsDerivedFromChildThread) {
  constexpr GridId grid_id{0};

  const GridShape shape{
      .cta_dim = {1, 1, 1},
      .thread_dim = {8, 1, 1},
      .warp_size = 32,
  };

  Grid grid(grid_id, shape);

  auto& cta = grid.cta(CtaId{grid_id, 0});

  EXPECT_FALSE(cta.trapped());

  cta.warp(0).thread(LaneId{3}).mark_trapped();

  EXPECT_TRUE(cta.trapped());
}

// -----------------------------------------------------------------------------
// CTA barrier resource topology
// -----------------------------------------------------------------------------

TEST(CtaBarrierStateTest, ProvidesSixteenIndependentBarrierResources) {
  constexpr std::size_t warp_count = 4;

  CtaBarrierState state{warp_count};

  EXPECT_EQ(state.warp_count(), warp_count);

  for (std::uint32_t id = 0; id < kCtaBarrierCount; ++id) {
    auto& barrier = state.barrier(CtaBarrierId{id});

    EXPECT_FALSE(barrier.active());
    EXPECT_EQ(barrier.next_generation(), 0u);
  }
}

TEST(CtaBarrierStateTest, BarrierSlotsAreIndependent) {
  CtaBarrierState state{4};

  auto& barrier0 = state.barrier(CtaBarrierId{0});

  auto& barrier15 = state.barrier(CtaBarrierId{15});

  barrier0.begin(32, CtaBarrierProtocol::SyncArrive);

  EXPECT_TRUE(barrier0.active());
  EXPECT_FALSE(barrier15.active());

  barrier15.begin(64, CtaBarrierProtocol::SyncArrive);

  EXPECT_TRUE(barrier0.active());
  EXPECT_TRUE(barrier15.active());

  EXPECT_EQ(barrier0.current().expected_threads(), 32u);

  EXPECT_EQ(barrier15.current().expected_threads(), 64u);
}

// -----------------------------------------------------------------------------
// Basic CTA barrier generation
// -----------------------------------------------------------------------------

TEST(CtaBarrierTest, NewGenerationStartsEmpty) {
  CtaBarrierState state{4};

  auto& slot = state.barrier(CtaBarrierId{3});

  auto& generation = slot.begin(128, CtaBarrierProtocol::SyncArrive);

  EXPECT_TRUE(slot.active());

  EXPECT_EQ(generation.generation(), 0u);
  EXPECT_EQ(generation.expected_threads(), 128u);
  EXPECT_EQ(generation.arrived_threads(), 0u);

  EXPECT_EQ(generation.arrived_warps().count(), 0u);
  EXPECT_EQ(generation.waiting_warps().count(), 0u);

  EXPECT_FALSE(generation.complete());
}

TEST(CtaBarrierTest, WarpArrivalContributesThreadCount) {
  CtaBarrierState state{4};

  auto& generation =
      state.barrier(CtaBarrierId{0}).begin(64, CtaBarrierProtocol::SyncArrive);

  generation.arrive_warp(0, 32, true);

  EXPECT_EQ(generation.arrived_threads(), 32u);
  EXPECT_TRUE(generation.warp_arrived(0));
  EXPECT_TRUE(generation.warp_waiting(0));

  EXPECT_FALSE(generation.complete());

  generation.arrive_warp(1, 32, true);

  EXPECT_EQ(generation.arrived_threads(), 64u);
  EXPECT_TRUE(generation.complete());
}

TEST(CtaBarrierTest, PartialWarpCanContributeActualThreadCount) {
  CtaBarrierState state{2};

  auto& generation =
      state.barrier(CtaBarrierId{0}).begin(35, CtaBarrierProtocol::SyncArrive);

  generation.arrive_warp(0, 32, true);

  EXPECT_EQ(generation.arrived_threads(), 32u);
  EXPECT_FALSE(generation.complete());

  generation.arrive_warp(1, 3, true);

  EXPECT_EQ(generation.arrived_threads(), 35u);
  EXPECT_TRUE(generation.complete());
}

// -----------------------------------------------------------------------------
// sync / arrive mixed protocol
// -----------------------------------------------------------------------------

TEST(CtaBarrierTest, SyncAndArriveMayShareOneGeneration) {
  CtaBarrierState state{2};

  auto& generation =
      state.barrier(CtaBarrierId{5}).begin(64, CtaBarrierProtocol::SyncArrive);

  // Producer-like warp executes barrier.arrive:
  // it contributes arrival but does not wait.
  generation.arrive_warp(0, 32, false);

  EXPECT_TRUE(generation.warp_arrived(0));
  EXPECT_FALSE(generation.warp_waiting(0));

  EXPECT_FALSE(generation.complete());

  // Consumer-like warp executes barrier.sync:
  // it contributes arrival and waits for completion.
  generation.arrive_warp(1, 32, true);

  EXPECT_TRUE(generation.warp_arrived(1));
  EXPECT_TRUE(generation.warp_waiting(1));

  EXPECT_TRUE(generation.complete());

  EXPECT_EQ(generation.arrived_warps().count(), 2u);

  EXPECT_EQ(generation.waiting_warps().count(), 1u);
}

// -----------------------------------------------------------------------------
// Barrier generations and reuse
// -----------------------------------------------------------------------------

TEST(CtaBarrierTest, BarrierCanBeReusedAcrossGenerations) {
  CtaBarrierState state{2};

  auto& slot = state.barrier(CtaBarrierId{1});

  {
    auto& generation = slot.begin(64, CtaBarrierProtocol::SyncArrive);

    EXPECT_EQ(generation.generation(), 0u);

    generation.arrive_warp(0, 32, true);
    generation.arrive_warp(1, 32, true);

    ASSERT_TRUE(generation.complete());

    slot.clear_completed();
  }

  EXPECT_FALSE(slot.active());
  EXPECT_EQ(slot.next_generation(), 1u);

  {
    auto& generation = slot.begin(64, CtaBarrierProtocol::SyncArrive);

    EXPECT_EQ(generation.generation(), 1u);

    generation.arrive_warp(0, 32, true);
    generation.arrive_warp(1, 32, true);

    ASSERT_TRUE(generation.complete());

    slot.clear_completed();
  }

  EXPECT_FALSE(slot.active());
  EXPECT_EQ(slot.next_generation(), 2u);
}

TEST(CtaBarrierTest, ResetDoesNotRewindGenerationNumber) {
  CtaBarrierState state{2};

  auto& slot = state.barrier(CtaBarrierId{0});

  slot.begin(64, CtaBarrierProtocol::SyncArrive);

  ASSERT_TRUE(slot.active());
  EXPECT_EQ(slot.next_generation(), 1u);

  slot.reset();

  EXPECT_FALSE(slot.active());
  EXPECT_EQ(slot.next_generation(), 1u);

  auto& second = slot.begin(64, CtaBarrierProtocol::SyncArrive);

  EXPECT_EQ(second.generation(), 1u);
}

TEST(CtaBarrierStateTest, ResetActiveClearsEveryPendingBarrier) {
  CtaBarrierState state{4};

  state.barrier(CtaBarrierId{0}).begin(32, CtaBarrierProtocol::SyncArrive);

  state.barrier(CtaBarrierId{7}).begin(64, CtaBarrierProtocol::SyncArrive);

  state.barrier(CtaBarrierId{15}).begin(128, CtaBarrierProtocol::SyncArrive);

  ASSERT_TRUE(state.barrier(CtaBarrierId{0}).active());

  ASSERT_TRUE(state.barrier(CtaBarrierId{7}).active());

  ASSERT_TRUE(state.barrier(CtaBarrierId{15}).active());

  state.reset_active();

  EXPECT_FALSE(state.barrier(CtaBarrierId{0}).active());

  EXPECT_FALSE(state.barrier(CtaBarrierId{7}).active());

  EXPECT_FALSE(state.barrier(CtaBarrierId{15}).active());

  // Generation allocation is monotonic across reset.
  EXPECT_EQ(state.barrier(CtaBarrierId{0}).next_generation(), 1u);

  EXPECT_EQ(state.barrier(CtaBarrierId{7}).next_generation(), 1u);

  EXPECT_EQ(state.barrier(CtaBarrierId{15}).next_generation(), 1u);
}

// -----------------------------------------------------------------------------
// Reduction barriers
// -----------------------------------------------------------------------------

TEST(CtaBarrierReductionTest, ReduceAndReturnsTrueWhenAllPredicatesAreTrue) {
  CtaBarrierState state{2};

  auto& generation =
      state.barrier(CtaBarrierId{2}).begin(64, CtaBarrierProtocol::ReduceAnd);

  generation.arrive_reduction_warp(0, 32, 32);

  generation.arrive_reduction_warp(1, 32, 32);

  ASSERT_TRUE(generation.complete());

  EXPECT_TRUE(generation.predicate_result());
}

TEST(CtaBarrierReductionTest, ReduceAndReturnsFalseWhenAnyPredicateIsFalse) {
  CtaBarrierState state{2};

  auto& generation =
      state.barrier(CtaBarrierId{2}).begin(64, CtaBarrierProtocol::ReduceAnd);

  generation.arrive_reduction_warp(0, 32, 32);

  generation.arrive_reduction_warp(1, 32, 31);

  ASSERT_TRUE(generation.complete());

  EXPECT_FALSE(generation.predicate_result());
}

TEST(CtaBarrierReductionTest, ReduceOrReturnsFalseWhenAllPredicatesAreFalse) {
  CtaBarrierState state{2};

  auto& generation =
      state.barrier(CtaBarrierId{2}).begin(64, CtaBarrierProtocol::ReduceOr);

  generation.arrive_reduction_warp(0, 32, 0);

  generation.arrive_reduction_warp(1, 32, 0);

  ASSERT_TRUE(generation.complete());

  EXPECT_FALSE(generation.predicate_result());
}

TEST(CtaBarrierReductionTest, ReduceOrReturnsTrueWhenAnyPredicateIsTrue) {
  CtaBarrierState state{2};

  auto& generation =
      state.barrier(CtaBarrierId{2}).begin(64, CtaBarrierProtocol::ReduceOr);

  generation.arrive_reduction_warp(0, 32, 0);

  generation.arrive_reduction_warp(1, 32, 1);

  ASSERT_TRUE(generation.complete());

  EXPECT_TRUE(generation.predicate_result());
}

TEST(CtaBarrierReductionTest, ReducePopcAccumulatesTruePredicates) {
  CtaBarrierState state{3};

  auto& generation =
      state.barrier(CtaBarrierId{4}).begin(96, CtaBarrierProtocol::ReducePopc);

  generation.arrive_reduction_warp(0, 32, 5);

  generation.arrive_reduction_warp(1, 32, 11);

  generation.arrive_reduction_warp(2, 32, 17);

  ASSERT_TRUE(generation.complete());

  EXPECT_EQ(generation.popc_result(), 33u);
}

TEST(CtaBarrierReductionTest, ReductionWarpsAreWaitingWarps) {
  CtaBarrierState state{2};

  auto& generation =
      state.barrier(CtaBarrierId{3}).begin(64, CtaBarrierProtocol::ReducePopc);

  generation.arrive_reduction_warp(0, 32, 10);

  EXPECT_TRUE(generation.warp_arrived(0));
  EXPECT_TRUE(generation.warp_waiting(0));

  generation.arrive_reduction_warp(1, 32, 20);

  EXPECT_TRUE(generation.warp_arrived(1));
  EXPECT_TRUE(generation.warp_waiting(1));

  EXPECT_TRUE(generation.complete());
}

// -----------------------------------------------------------------------------
// CTA execution-state integration
// -----------------------------------------------------------------------------

TEST(CtaTest, CtaOwnsBarrierExecutionState) {
  constexpr GridId grid_id{0};

  const GridShape shape{
      .cta_dim = {1, 1, 1},
      .thread_dim = {64, 1, 1},
      .warp_size = 32,
  };

  Grid grid(grid_id, shape);

  auto& cta = grid.cta(CtaId{grid_id, 0});

  auto& barriers = cta.execution_state().barriers;

  EXPECT_EQ(barriers.warp_count(), 2u);

  auto& generation = barriers.barrier(CtaBarrierId{0})
                         .begin(64, CtaBarrierProtocol::SyncArrive);

  generation.arrive_warp(0, 32, true);

  EXPECT_FALSE(generation.complete());

  generation.arrive_warp(1, 32, true);

  EXPECT_TRUE(generation.complete());
}

TEST(CtaTest, EachCtaOwnsIndependentBarrierState) {
  constexpr GridId grid_id{0};

  const GridShape shape{
      .cta_dim = {2, 1, 1},
      .thread_dim = {64, 1, 1},
      .warp_size = 32,
  };

  Grid grid(grid_id, shape);

  auto& cta0 = grid.cta(CtaId{grid_id, 0});

  auto& cta1 = grid.cta(CtaId{grid_id, 1});

  auto& barrier0 = cta0.execution_state().barriers.barrier(CtaBarrierId{0});

  auto& barrier1 = cta1.execution_state().barriers.barrier(CtaBarrierId{0});

  barrier0.begin(64, CtaBarrierProtocol::SyncArrive);

  EXPECT_TRUE(barrier0.active());
  EXPECT_FALSE(barrier1.active());
}

// -----------------------------------------------------------------------------
// Address-stability invariant
// -----------------------------------------------------------------------------

TEST(CtaTest, TopologyAddressesRemainStableAcrossRuntimeMutation) {
  constexpr GridId grid_id{0};

  const GridShape shape{
      .cta_dim = {2, 1, 1},
      .thread_dim = {64, 1, 1},
      .warp_size = 32,
  };

  Grid grid(grid_id, shape);

  const CtaId cta_id{
      .grid = grid_id,
      .value = 1,
  };

  CTA* const cta = grid.find_cta(cta_id);

  ASSERT_NE(cta, nullptr);

  Warp* const warp = &cta->warp(0);

  Thread* const thread = &warp->thread(LaneId{7});

  // Mutate Thread runtime state.
  thread->set_pc(ProgramCounter{100});
  thread->mark_waiting(WaitReason::CtaBarrier);
  thread->mark_ready();

  // Mutate CTA runtime state.
  auto& generation = cta->execution_state()
                         .barriers.barrier(CtaBarrierId{0})
                         .begin(64, CtaBarrierProtocol::SyncArrive);

  generation.arrive_warp(0, 32, true);

  generation.arrive_warp(1, 32, true);

  ASSERT_TRUE(generation.complete());

  cta->execution_state().barriers.barrier(CtaBarrierId{0}).clear_completed();

  // Runtime mutation must never affect topology-node identity.
  EXPECT_EQ(grid.find_cta(cta_id), cta);

  EXPECT_EQ(&cta->warp(0), warp);

  EXPECT_EQ(&warp->thread(LaneId{7}), thread);

  EXPECT_EQ(&thread->cta(), cta);

  EXPECT_EQ(&thread->grid(), &grid);
}

}  // namespace ptxsim::execution_model::test
