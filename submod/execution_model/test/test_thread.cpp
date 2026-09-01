#include <gtest/gtest.h>

#include <cstdint>

#include <ptxsim/execution_model/execution_model.hpp>

namespace ptxsim::execution_model::test {
namespace {

class FakeEngine {
 public:
  struct Result {
    ThreadId thread;
    ProgramCounter old_pc;
  };

  Result step(Thread& thread) {
    const auto old_pc = thread.pc();

    thread.set_pc(ProgramCounter{
        old_pc.value + 1,
    });

    ++calls_;

    return Result{
        .thread = thread.id(),
        .old_pc = old_pc,
    };
  }

  [[nodiscard]]
  std::uint32_t calls() const noexcept {
    return calls_;
  }

 private:
  std::uint32_t calls_ = 0;
};
}  // namespace

TEST(ThreadTest, InitialExecutionState) {
  Grid grid(GridId{0}, GridShape{
                           .cta_dim = {1, 1, 1},
                           .thread_dim = {1, 1, 1},
                           .warp_size = 32,
                       });

  const auto& thread = grid.thread(ThreadId{GridId{0}, 0});

  EXPECT_EQ(thread.pc(), ProgramCounter{0});

  EXPECT_EQ(thread.status(), ThreadStatus::Ready);

  EXPECT_TRUE(thread.ready());
  EXPECT_FALSE(thread.waiting());
  EXPECT_FALSE(thread.exited());
  EXPECT_FALSE(thread.trapped());
}

TEST(ThreadTest, ProgramCounterCanBeUpdated) {
  Grid grid(GridId{0}, GridShape{
                           .cta_dim = {1, 1, 1},
                           .thread_dim = {1, 1, 1},
                           .warp_size = 32,
                       });

  auto& thread = grid.thread(ThreadId{GridId{0}, 0});

  thread.set_pc(ProgramCounter{42});

  EXPECT_EQ(thread.pc(), ProgramCounter{42});
}

TEST(ThreadTest, StatusTransitions) {
  Grid grid(GridId{0}, GridShape{
                           .cta_dim = {1, 1, 1},
                           .thread_dim = {1, 1, 1},
                           .warp_size = 32,
                       });

  auto& thread = grid.thread(ThreadId{GridId{0}, 0});

  ASSERT_TRUE(thread.ready());

  thread.mark_waiting();

  EXPECT_TRUE(thread.waiting());
  EXPECT_FALSE(thread.ready());

  thread.mark_ready();

  EXPECT_TRUE(thread.ready());
  EXPECT_FALSE(thread.waiting());

  thread.mark_trapped();

  EXPECT_TRUE(thread.trapped());
  EXPECT_FALSE(thread.ready());

  thread.mark_exited();

  EXPECT_TRUE(thread.exited());
  EXPECT_FALSE(thread.trapped());
}

TEST(ThreadTest, StepIsThinEngineFacade) {
  Grid grid(GridId{9}, GridShape{
                           .cta_dim = {1, 1, 1},
                           .thread_dim = {1, 1, 1},
                           .warp_size = 32,
                       });

  auto& thread = grid.thread(ThreadId{GridId{9}, 0});

  thread.set_pc(ProgramCounter{17});

  FakeEngine engine;

  const auto result = thread.step(engine);

  EXPECT_EQ(engine.calls(), 1u);

  EXPECT_EQ(result.thread, thread.id());

  EXPECT_EQ(result.old_pc, ProgramCounter{17});

  EXPECT_EQ(thread.pc(), ProgramCounter{18});
}

TEST(ThreadTest, StepDoesNotRequireExecutionModelToKnowEngineType) {
  struct AnotherEngine {
    int step(Thread&) { return 1234; }
  };

  Grid grid(GridId{0}, GridShape{
                           .cta_dim = {1, 1, 1},
                           .thread_dim = {1, 1, 1},
                           .warp_size = 32,
                       });

  auto& thread = grid.thread(ThreadId{GridId{0}, 0});

  AnotherEngine engine;

  EXPECT_EQ(thread.step(engine), 1234);
}

}  // namespace ptxsim::execution_model::test