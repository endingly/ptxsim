#include <gtest/gtest.h>

#include <cstdint>
#include <optional>

#include <ptxsim/exec_ir/exec_ir_types.hpp>

namespace ptxsim::exec_ir::test {
namespace {
/** @brief Make a real typed scalar carrier without reading runtime state. */
[[nodiscard]] auto scalar_register() -> RegisterRef {
  return {.slot = common::RegisterSlot{0}, .declared_type = DataType::b32};
}
/** @brief Attach one selected video representation to a valid scalar carrier. */
[[nodiscard]] auto video(VideoSelector selector) -> VideoOperand {
  return {ScalarOperand{scalar_register()}, std::move(selector), false};
}

TEST(OwnedOperandShape, DistinguishesScalarAndOrdinaryVectorDeclarations) {
  auto value = scalar_register();
  EXPECT_TRUE(valid_operand(value));
  for (const auto width : {std::uint8_t{2}, std::uint8_t{4}}) {
    value.vector_width = width;
    EXPECT_TRUE(valid_operand(value));
  }
  for (const auto width : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{3}, std::uint8_t{8}}) {
    value.vector_width = width;
    EXPECT_FALSE(valid_operand(value));
  }
  RegisterVector operand;
  operand.elements.assign(8, scalar_register());
  EXPECT_TRUE(valid_operand(operand));
}

TEST(OwnedOperandShape, PreservesCrossSourceVideoSwizzleUpperBounds) {
  EXPECT_TRUE(valid_operand(video(VideoHalfSwizzle{{3, 0}})));
  EXPECT_TRUE(valid_operand(video(VideoByteSwizzle{{7, 6, 0, 7}})));
  EXPECT_FALSE(valid_operand(video(VideoHalfSwizzle{{4, 0}})));
  EXPECT_FALSE(valid_operand(video(VideoHalfSwizzle{{255, 0}})));
  EXPECT_FALSE(valid_operand(video(VideoByteSwizzle{{8, 0, 0, 0}})));
  EXPECT_FALSE(valid_operand(video(VideoByteSwizzle{{255, 0, 0, 0}})));
}

TEST(OwnedOperandShape, RequiresNonemptyCanonicalActiveVideoMasks) {
  EXPECT_TRUE(valid_operand(video(VideoHalfMask{{1, 0}, 2})));
  EXPECT_TRUE(valid_operand(video(VideoHalfMask{{0, 255}, 1})));
  EXPECT_TRUE(valid_operand(video(VideoByteMask{{3, 2, 1, 0}, 4})));
  EXPECT_TRUE(valid_operand(video(VideoByteMask{{3, 1, 255, 255}, 2})));
  EXPECT_FALSE(valid_operand(video(VideoHalfMask{{0, 0}, 0})));
  EXPECT_FALSE(valid_operand(video(VideoByteMask{{0, 0, 0, 0}, 0})));
  EXPECT_FALSE(valid_operand(video(VideoHalfMask{{255, 0}, 1})));
  EXPECT_FALSE(valid_operand(video(VideoByteMask{{4, 0, 0, 0}, 1})));
  EXPECT_FALSE(valid_operand(video(VideoByteMask{{255, 0, 0, 0}, 1})));
  EXPECT_FALSE(valid_operand(video(VideoHalfMask{{0, 1}, 2})));
  EXPECT_FALSE(valid_operand(video(VideoByteMask{{3, 3, 0, 0}, 2})));
  EXPECT_FALSE(valid_operand(video(VideoByteMask{{1, 3, 0, 0}, 2})));
}

}  // namespace
TEST(OwnedOperandShape, PredicateOutputChecksDoNotBanSourceNegationOrSinks) {
  const RegisterRef slot{.slot=common::RegisterSlot{999},.register_class=RegisterClass::Predicate,.declared_type=DataType::pred};
  const Predicate normal{slot,false}, inverted{slot,true};
  EXPECT_TRUE(valid_operand(inverted));
  EXPECT_TRUE(valid_predicate_destination(normal)); EXPECT_FALSE(valid_predicate_destination(inverted));
  EXPECT_TRUE(valid_predicate_destination(PredicateOrSink{}));
  EXPECT_FALSE(valid_predicate_destination(PredicateOrSink{inverted}));
  EXPECT_FALSE(valid_predicate_destination(PredicatePair{normal,inverted}));
  EXPECT_TRUE(valid_predicate_destination(PredicatePairOrSink{normal,std::nullopt}));
  EXPECT_FALSE(valid_predicate_destination(PredicatePairOrSink{std::nullopt,inverted}));
  EXPECT_FALSE(valid_predicate_destination(ShflDestination{std::nullopt,inverted}));
  EXPECT_FALSE(valid_predicate_destination(TextureResult{RegisterVector{},DataType::u32,inverted}));
}
TEST(OwnedOperandShape, AddressOffsetsRetainSigned64RepresentationWithoutIsaRangeChecks) {
  Address value{scalar_register()};
  value.offset=AddressOffset{AddressOffsetOperator::Add,Immediate{4,DataType::s64,false,4}};
  EXPECT_TRUE(valid_operand(value));
  value.offset->value.type=DataType::b64; EXPECT_FALSE(valid_operand(value));
  value.offset->value.type=DataType::s64;
  value.offset->value.integer_source_bits=5; EXPECT_FALSE(valid_operand(value));
  value.offset->value=Immediate{std::uint64_t{1}<<63,DataType::s64,false,std::uint64_t{1}<<63};
  EXPECT_TRUE(valid_operand(value));
}

TEST(OwnedOperandShape, RejectsUnnormalizedImmediateUseBitsAndProvenance) {
  EXPECT_FALSE(valid_operand(Immediate{0x100,DataType::u8}));
  EXPECT_FALSE(valid_operand(Immediate{0x10000,DataType::u16}));
  EXPECT_FALSE(valid_operand(Immediate{std::uint64_t{1}<<32,DataType::f32}));
  EXPECT_FALSE(valid_operand(Immediate{1,DataType::s16,false,2}));
  EXPECT_FALSE(valid_operand(Immediate{1,DataType::s64,true,1}));
  EXPECT_TRUE(valid_operand(Immediate{0xff,DataType::u8,true,UINT64_MAX}));
  EXPECT_TRUE(valid_operand(Immediate{UINT64_MAX,DataType::u64,false,UINT64_MAX}));
  EXPECT_TRUE(valid_operand(Immediate{0x80000000,DataType::f32}));
  EXPECT_TRUE(valid_operand(Immediate{UINT64_MAX,DataType::b128,true,UINT64_MAX}));
}
TEST(OwnedOperandShape, EveryImmediateDomainPreservesItsPhysicalContainerBits) {
  for(std::uint16_t index=0;index<50;++index) {
    const auto type=static_cast<DataType>(index);
    const auto width=data_type_width(type); ASSERT_TRUE(width);
    std::uint64_t mask=UINT64_MAX;
    switch(*width) {
      case common::RawWidth::pred: mask=1; break;
      case common::RawWidth::b8: mask=0xff; break;
      case common::RawWidth::b16: mask=0xffff; break;
      case common::RawWidth::b32: mask=0xffffffff; break;
      case common::RawWidth::b64: case common::RawWidth::b128: break;
    }
    EXPECT_TRUE(valid_operand(Immediate{mask,type,false,mask}));
    if(mask!=UINT64_MAX) EXPECT_FALSE(valid_operand(Immediate{mask+1,type}));
  }
}
}  // namespace ptxsim::exec_ir::test
