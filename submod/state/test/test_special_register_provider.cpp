#include <gtest/gtest.h>

#include <cstdint>
#include <expected>
#include <type_traits>

#include <ptxsim/state/special_register_provider.hpp>

namespace ptxsim::state::test {
namespace {

class MockProvider final : public SpecialRegisterProvider {
 public:
  [[nodiscard]] auto read(common::SpecialRegisterId register_id,
                          const ThreadState& thread) const
      -> std::expected<common::RawValue, SpecialRegisterError> override {
    if (register_id == common::SpecialRegisterId{0}) {
      return common::RawValue::b32(thread.thread_id().value());
    }
    return std::unexpected(SpecialRegisterError{
        SpecialRegisterErrorCode::unsupported_register, register_id});
  }
};

static_assert(std::is_abstract_v<SpecialRegisterProvider>);
static_assert(std::has_virtual_destructor_v<SpecialRegisterProvider>);

TEST(SpecialRegisterProvider, MockUsesThreadStateThroughBaseReference) {
  const auto thread =
      ThreadState::create(common::ThreadId{12}, common::FunctionId{0},
                          common::ProgramCounter{0}, {});
  ASSERT_TRUE(thread);
  const MockProvider mock;
  const SpecialRegisterProvider& provider = mock;

  const auto value = provider.read(common::SpecialRegisterId{0}, *thread);
  ASSERT_TRUE(value);
  EXPECT_EQ(*value, common::RawValue::b32(std::uint32_t{12}));

  const auto unsupported = provider.read(common::SpecialRegisterId{1}, *thread);
  ASSERT_FALSE(unsupported);
  EXPECT_EQ(unsupported.error().code,
            SpecialRegisterErrorCode::unsupported_register);
  EXPECT_EQ(unsupported.error().register_id, common::SpecialRegisterId{1});
}

}  // namespace
}  // namespace ptxsim::state::test
