#include <gtest/gtest.h>

#include <cstdint>
#include <type_traits>

#include "ptxsim/execution_model/execution_model.hpp"

namespace ptxsim::execution_model::test {
namespace {

static_assert(!std::is_copy_constructible_v<Warp>);
static_assert(!std::is_copy_assignable_v<Warp>);
static_assert(!std::is_move_constructible_v<Warp>);
static_assert(!std::is_move_assignable_v<Warp>);
}  // namespace
// -----------------------------------------------------------------------------
// Basic warp topology
// -----------------------------------------------------------------------------

TEST(WarpTest, FullWarpHasExpectedTopology) {
  constexpr GridId grid_id{0};

  const GridShape shape{
      .cta_dim = {1, 1, 1},
      .thread_dim = {32, 1, 1},
      .warp_size = 32,
  };

  Grid grid(grid_id, shape);

  ASSERT_EQ(grid.warp_count(), 1u);

  auto& warp = grid.warp(WarpId{grid_id, 0});

  EXPECT_EQ(warp.id(), (WarpId{grid_id, 0}));
  EXPECT_EQ(warp.index_in_cta(), 0u);
  EXPECT_EQ(warp.architectural_warp_size(), 32u);
  EXPECT_EQ(warp.thread_count(), 32u);
  EXPECT_TRUE(warp.full());
}

TEST(WarpTest, PartialWarpContainsOnlyRealThreads) {
  constexpr GridId grid_id{0};

  const GridShape shape{
      .cta_dim = {1, 1, 1},
      .thread_dim = {35, 1, 1},
      .warp_size = 32,
  };

  Grid grid(grid_id, shape);

  ASSERT_EQ(grid.warp_count(), 2u);

  const auto& warp0 = grid.warp(WarpId{grid_id, 0});
  const auto& warp1 = grid.warp(WarpId{grid_id, 1});

  EXPECT_EQ(warp0.thread_count(), 32u);
  EXPECT_TRUE(warp0.full());

  EXPECT_EQ(warp1.thread_count(), 3u);
  EXPECT_FALSE(warp1.full());

  EXPECT_EQ(warp1.architectural_warp_size(), 32u);
}

// -----------------------------------------------------------------------------
// Valid mask
// -----------------------------------------------------------------------------

TEST(WarpTest, FullWarpValidMaskContainsAllLanes) {
  constexpr GridId grid_id{0};

  const GridShape shape{
      .cta_dim = {1, 1, 1},
      .thread_dim = {32, 1, 1},
      .warp_size = 32,
  };

  Grid grid(grid_id, shape);

  const auto& warp = grid.warp(WarpId{grid_id, 0});
  const auto& mask = warp.valid_mask();

  ASSERT_EQ(mask.size(), 32u);
  EXPECT_EQ(mask.count(), 32u);

  for (std::uint32_t lane = 0; lane < 32; ++lane) {
    EXPECT_TRUE(mask.test(LaneId{lane}));
  }
}

TEST(WarpTest, PartialWarpValidMaskContainsOnlyExistingThreads) {
  constexpr GridId grid_id{0};

  const GridShape shape{
      .cta_dim = {1, 1, 1},
      .thread_dim = {35, 1, 1},
      .warp_size = 32,
  };

  Grid grid(grid_id, shape);

  const auto& warp = grid.warp(WarpId{grid_id, 1});
  const auto& mask = warp.valid_mask();

  ASSERT_EQ(mask.size(), 32u);
  EXPECT_EQ(mask.count(), 3u);

  EXPECT_TRUE(mask.test(LaneId{0}));
  EXPECT_TRUE(mask.test(LaneId{1}));
  EXPECT_TRUE(mask.test(LaneId{2}));

  for (std::uint32_t lane = 3; lane < 32; ++lane) {
    EXPECT_FALSE(mask.test(LaneId{lane}));
  }
}

// -----------------------------------------------------------------------------
// Thread ownership and lane mapping
// -----------------------------------------------------------------------------

TEST(WarpTest, ThreadLaneIdsAreLocalToWarp) {
  constexpr GridId grid_id{0};

  const GridShape shape{
      .cta_dim = {1, 1, 1},
      .thread_dim = {64, 1, 1},
      .warp_size = 32,
  };

  Grid grid(grid_id, shape);

  const auto& warp0 = grid.warp(WarpId{grid_id, 0});
  const auto& warp1 = grid.warp(WarpId{grid_id, 1});

  EXPECT_EQ(warp0.thread(LaneId{0}).lane_id(), LaneId{0});
  EXPECT_EQ(warp0.thread(LaneId{31}).lane_id(), LaneId{31});

  EXPECT_EQ(warp1.thread(LaneId{0}).lane_id(), LaneId{0});
  EXPECT_EQ(warp1.thread(LaneId{31}).lane_id(), LaneId{31});

  EXPECT_EQ(warp0.thread(LaneId{0}).linear_index_in_cta(), 0u);

  EXPECT_EQ(warp1.thread(LaneId{0}).linear_index_in_cta(), 32u);
}

TEST(WarpTest, ThreadParentNavigationReturnsOwningWarp) {
  constexpr GridId grid_id{5};

  const GridShape shape{
      .cta_dim = {1, 1, 1},
      .thread_dim = {32, 1, 1},
      .warp_size = 32,
  };

  Grid grid(grid_id, shape);

  auto& warp = grid.warp(WarpId{grid_id, 0});
  auto& thread = warp.thread(LaneId{7});

  EXPECT_EQ(&thread.warp(), &warp);
  EXPECT_EQ(&thread.cta(), &warp.cta());
  EXPECT_EQ(&thread.grid(), &grid);
}

TEST(WarpTest, IterationVisitsEveryOwnedThreadExactlyOnce) {
  constexpr GridId grid_id{0};

  const GridShape shape{
      .cta_dim = {1, 1, 1},
      .thread_dim = {19, 1, 1},
      .warp_size = 32,
  };

  Grid grid(grid_id, shape);

  const auto& warp = grid.warp(WarpId{grid_id, 0});

  std::size_t count = 0;

  for (const auto& thread : warp) {
    EXPECT_EQ(thread.lane_id(), LaneId{static_cast<std::uint32_t>(count)});

    ++count;
  }

  EXPECT_EQ(count, 19u);
}

// -----------------------------------------------------------------------------
// Derived execution masks
// -----------------------------------------------------------------------------

TEST(WarpTest, AllThreadsAreInitiallyReady) {
  constexpr GridId grid_id{0};

  const GridShape shape{
      .cta_dim = {1, 1, 1},
      .thread_dim = {8, 1, 1},
      .warp_size = 32,
  };

  Grid grid(grid_id, shape);

  const auto& warp = grid.warp(WarpId{grid_id, 0});

  const auto ready = warp.ready_mask();
  const auto waiting = warp.waiting_mask();
  const auto exited = warp.exited_mask();

  EXPECT_EQ(ready.count(), 8u);
  EXPECT_EQ(waiting.count(), 0u);
  EXPECT_EQ(exited.count(), 0u);

  EXPECT_EQ(ready, warp.valid_mask());
}

TEST(WarpTest, DerivedMasksFollowThreadState) {
  constexpr GridId grid_id{0};

  const GridShape shape{
      .cta_dim = {1, 1, 1},
      .thread_dim = {8, 1, 1},
      .warp_size = 32,
  };

  Grid grid(grid_id, shape);

  auto& warp = grid.warp(WarpId{grid_id, 0});

  warp.thread(LaneId{1}).mark_waiting();
  warp.thread(LaneId{3}).mark_waiting();

  warp.thread(LaneId{5}).mark_exited();

  const auto ready = warp.ready_mask();
  const auto waiting = warp.waiting_mask();
  const auto exited = warp.exited_mask();

  EXPECT_EQ(ready.count(), 5u);
  EXPECT_EQ(waiting.count(), 2u);
  EXPECT_EQ(exited.count(), 1u);

  EXPECT_TRUE(waiting.test(LaneId{1}));
  EXPECT_TRUE(waiting.test(LaneId{3}));

  EXPECT_TRUE(exited.test(LaneId{5}));

  EXPECT_FALSE(ready.test(LaneId{1}));
  EXPECT_FALSE(ready.test(LaneId{3}));
  EXPECT_FALSE(ready.test(LaneId{5}));

  EXPECT_TRUE(ready.test(LaneId{0}));
  EXPECT_TRUE(ready.test(LaneId{2}));
  EXPECT_TRUE(ready.test(LaneId{4}));
  EXPECT_TRUE(ready.test(LaneId{6}));
  EXPECT_TRUE(ready.test(LaneId{7}));
}

TEST(WarpTest, ReadyMaskNeverIncludesInvalidPartialWarpLanes) {
  constexpr GridId grid_id{0};

  const GridShape shape{
      .cta_dim = {1, 1, 1},
      .thread_dim = {35, 1, 1},
      .warp_size = 32,
  };

  Grid grid(grid_id, shape);

  const auto& warp = grid.warp(WarpId{grid_id, 1});
  const auto ready = warp.ready_mask();

  EXPECT_EQ(ready.size(), 32u);
  EXPECT_EQ(ready.count(), 3u);

  EXPECT_TRUE(ready.test(LaneId{0}));
  EXPECT_TRUE(ready.test(LaneId{1}));
  EXPECT_TRUE(ready.test(LaneId{2}));

  for (std::uint32_t lane = 3; lane < 32; ++lane) {
    EXPECT_FALSE(ready.test(LaneId{lane}));
  }
}

// -----------------------------------------------------------------------------
// WarpIssueGroup
// -----------------------------------------------------------------------------

TEST(WarpIssueGroupTest, RepresentsPcAndLaneSet) {
  LaneMask lanes{32};

  lanes.set(LaneId{0});
  lanes.set(LaneId{3});
  lanes.set(LaneId{7});

  WarpIssueGroup group{
      .pc = ProgramCounter{42},
      .lanes = lanes,
  };

  EXPECT_EQ(group.pc, ProgramCounter{42});
  EXPECT_EQ(group.size(), 3u);
  EXPECT_FALSE(group.empty());

  EXPECT_TRUE(group.lanes.test(LaneId{0}));
  EXPECT_TRUE(group.lanes.test(LaneId{3}));
  EXPECT_TRUE(group.lanes.test(LaneId{7}));
}

TEST(WarpIssueGroupTest, EmptyMaskProducesEmptyIssueGroup) {
  WarpIssueGroup group{
      .pc = ProgramCounter{13},
      .lanes = LaneMask{32},
  };

  EXPECT_TRUE(group.empty());
  EXPECT_EQ(group.size(), 0u);
}

// -----------------------------------------------------------------------------
// Warp rendezvous
// -----------------------------------------------------------------------------

TEST(WarpRendezvousTest, StartsWithNoArrivals) {
  LaneMask participants{32};

  participants.set(LaneId{0});
  participants.set(LaneId{1});
  participants.set(LaneId{4});

  WarpRendezvous rendezvous{
      ProgramCounter{100},
      7,
      participants,
  };

  EXPECT_EQ(rendezvous.pc(), ProgramCounter{100});
  EXPECT_EQ(rendezvous.generation(), 7u);

  EXPECT_EQ(rendezvous.participants().count(), 3u);
  EXPECT_EQ(rendezvous.arrivals().count(), 0u);

  EXPECT_FALSE(rendezvous.complete());
}

TEST(WarpRendezvousTest, SingleLaneArrivalIsRecorded) {
  LaneMask participants{32};

  participants.set(LaneId{0});
  participants.set(LaneId{1});

  WarpRendezvous rendezvous{
      ProgramCounter{17},
      0,
      participants,
  };

  rendezvous.arrive(LaneId{0});

  EXPECT_TRUE(rendezvous.has_arrived(LaneId{0}));
  EXPECT_FALSE(rendezvous.has_arrived(LaneId{1}));

  EXPECT_FALSE(rendezvous.complete());
}

TEST(WarpRendezvousTest, CompletesWhenAllParticipantsArrive) {
  LaneMask participants{32};

  participants.set(LaneId{0});
  participants.set(LaneId{2});
  participants.set(LaneId{5});

  WarpRendezvous rendezvous{
      ProgramCounter{17},
      0,
      participants,
  };

  rendezvous.arrive(LaneId{0});
  rendezvous.arrive(LaneId{2});

  EXPECT_FALSE(rendezvous.complete());

  rendezvous.arrive(LaneId{5});

  EXPECT_TRUE(rendezvous.complete());
}

TEST(WarpRendezvousTest, MultipleLanesMayArriveAsMask) {
  LaneMask participants{32};

  participants.set(LaneId{1});
  participants.set(LaneId{2});
  participants.set(LaneId{3});
  participants.set(LaneId{4});

  WarpRendezvous rendezvous{
      ProgramCounter{77},
      2,
      participants,
  };

  LaneMask first_group{32};
  first_group.set(LaneId{1});
  first_group.set(LaneId{3});

  rendezvous.arrive(first_group);

  EXPECT_EQ(rendezvous.arrivals().count(), 2u);
  EXPECT_FALSE(rendezvous.complete());

  LaneMask second_group{32};
  second_group.set(LaneId{2});
  second_group.set(LaneId{4});

  rendezvous.arrive(second_group);

  EXPECT_EQ(rendezvous.arrivals().count(), 4u);
  EXPECT_TRUE(rendezvous.complete());
}

// -----------------------------------------------------------------------------
// WarpSyncState
// -----------------------------------------------------------------------------

TEST(WarpSyncStateTest, InitiallyHasNoPendingRendezvous) {
  WarpSyncState state;

  EXPECT_FALSE(state.active());
  EXPECT_EQ(state.next_generation(), 0u);
}

TEST(WarpSyncStateTest, BeginCreatesPendingRendezvous) {
  WarpSyncState state;

  LaneMask participants{32};
  participants.set(LaneId{0});
  participants.set(LaneId{1});

  auto& rendezvous = state.begin(ProgramCounter{10}, participants);

  EXPECT_TRUE(state.active());

  EXPECT_EQ(rendezvous.pc(), ProgramCounter{10});
  EXPECT_EQ(rendezvous.generation(), 0u);

  EXPECT_EQ(state.pending().pc(), ProgramCounter{10});

  EXPECT_EQ(state.next_generation(), 1u);
}

TEST(WarpSyncStateTest, CompletedRendezvousCanBeCleared) {
  WarpSyncState state;

  LaneMask participants{32};
  participants.set(LaneId{0});
  participants.set(LaneId{1});

  auto& rendezvous = state.begin(ProgramCounter{10}, participants);

  rendezvous.arrive(LaneId{0});
  rendezvous.arrive(LaneId{1});

  ASSERT_TRUE(rendezvous.complete());

  state.clear_completed();

  EXPECT_FALSE(state.active());
}

TEST(WarpSyncStateTest, GenerationIncreasesAcrossRendezvous) {
  WarpSyncState state;

  {
    LaneMask participants{32};
    participants.set(LaneId{0});

    auto& rendezvous = state.begin(ProgramCounter{20}, participants);

    EXPECT_EQ(rendezvous.generation(), 0u);

    rendezvous.arrive(LaneId{0});
    state.clear_completed();
  }

  {
    LaneMask participants{32};
    participants.set(LaneId{0});

    auto& rendezvous = state.begin(ProgramCounter{20}, participants);

    EXPECT_EQ(rendezvous.generation(), 1u);

    rendezvous.arrive(LaneId{0});
    state.clear_completed();
  }

  EXPECT_EQ(state.next_generation(), 2u);
}

TEST(WarpSyncStateTest, ResetDiscardsPendingRendezvous) {
  WarpSyncState state;

  LaneMask participants{32};
  participants.set(LaneId{0});
  participants.set(LaneId{1});

  state.begin(ProgramCounter{10}, participants);

  ASSERT_TRUE(state.active());

  state.reset();

  EXPECT_FALSE(state.active());

  // Resetting the current rendezvous does not rewind generation numbering.
  EXPECT_EQ(state.next_generation(), 1u);
}

// -----------------------------------------------------------------------------
// WarpExecutionState integration
// -----------------------------------------------------------------------------

TEST(WarpTest, WarpOwnsPersistentExecutionState) {
  constexpr GridId grid_id{0};

  const GridShape shape{
      .cta_dim = {1, 1, 1},
      .thread_dim = {32, 1, 1},
      .warp_size = 32,
  };

  Grid grid(grid_id, shape);

  auto& warp = grid.warp(WarpId{grid_id, 0});
  auto& state = warp.execution_state();

  EXPECT_FALSE(state.sync.active());

  LaneMask participants{32};
  participants.set(LaneId{0});
  participants.set(LaneId{1});

  auto& rendezvous = state.sync.begin(ProgramCounter{25}, participants);

  rendezvous.arrive(LaneId{0});

  EXPECT_TRUE(warp.execution_state().sync.active());

  EXPECT_EQ(warp.execution_state().sync.pending().arrivals().count(), 1u);
}

// -----------------------------------------------------------------------------
// Address stability
// -----------------------------------------------------------------------------

TEST(WarpTest, WarpAndThreadAddressesRemainStableDuringRuntimeMutation) {
  constexpr GridId grid_id{0};

  const GridShape shape{
      .cta_dim = {2, 1, 1},
      .thread_dim = {64, 1, 1},
      .warp_size = 32,
  };

  Grid grid(grid_id, shape);

  constexpr WarpId warp_id{
      .grid = grid_id,
      .value = 2,
  };

  auto* const warp = grid.find_warp(warp_id);

  ASSERT_NE(warp, nullptr);

  auto* const thread = &warp->thread(LaneId{7});

  thread->set_pc(ProgramCounter{100});
  thread->mark_waiting();
  thread->mark_ready();

  LaneMask participants{32};
  participants.set(LaneId{7});

  auto& rendezvous =
      warp->execution_state().sync.begin(ProgramCounter{100}, participants);

  rendezvous.arrive(LaneId{7});

  ASSERT_TRUE(rendezvous.complete());

  warp->execution_state().sync.clear_completed();

  EXPECT_EQ(grid.find_warp(warp_id), warp);
  EXPECT_EQ(&warp->thread(LaneId{7}), thread);

  EXPECT_EQ(&thread->warp(), warp);
}

}  // namespace ptxsim::execution_model::test