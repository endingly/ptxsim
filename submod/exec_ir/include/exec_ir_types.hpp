#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <ptxsim/common/ids.hpp>
#include <ptxsim/common/raw_value.hpp>

namespace ptxsim::exec_ir {

/** @brief PTX scalar type selected by a generated instruction form. */
enum class DataType : std::uint8_t {
  pred,
  b8,
  b16,
  b32,
  b64,
  b128,
  u8,
  u16,
  u32,
  u64,
  s8,
  s16,
  s32,
  s64,
  f16,
  f16x2,
  f32,
  f32x2,
  f64,
  bf16,
  bf16x2,
  u8x4,
  u16x2,
  s8x4,
  s16x2
};
/** @brief Rounding direction selected by a floating-point form. */
enum class RoundingMode : std::uint8_t { rn, rz, rm, rp, rzi };
/** @brief Comparison relation selected by compare forms. */
enum class ComparisonOperator : std::uint8_t { eq, lt, ge };
/** @brief Boolean relation selected by predicate-combining forms. */
enum class BooleanOperator : std::uint8_t { and_, or_, xor_ };
/** @brief Cache policy retained from a memory instruction. */
enum class CacheOperator : std::uint8_t {
  ca,
  cg,
  cs,
  cv,
  lu,
  unspecified,
  wb,
  wt
};
/** @brief Eviction policy retained from a memory instruction. */
enum class EvictionPriority : std::uint8_t {
  evict_first,
  evict_last,
  evict_normal,
  no_allocate
};
/** @brief Memory consistency qualifier retained from a memory instruction. */
enum class MemoryConsistency : std::uint8_t {
  acq_rel,
  acquire,
  omitted,
  relaxed,
  release,
  volatile_,
  weak
};
/** @brief Visibility scope retained from a memory or synchronization instruction. */
enum class MemoryScope : std::uint8_t { cluster, cta, gpu, none, sys };
/** @brief Vector arity selected by a vector form. */
enum class VectorArity : std::uint8_t { v2, v4, v8 };
/** @brief Address state space retained from an explicit memory form. */
enum class AddressSpace : std::uint8_t {
  const_,
  generic,
  global,
  local,
  param,
  param_entry,
  param_func,
  shared
};
/** @brief Mbarrier phase qualifier. */
enum class MbarrierPhaseType : std::uint8_t { conditional, primary };
/** @brief Mbarrier layout qualifier. */
enum class MbarrierLayout : std::uint8_t { v0, v1 };
/** @brief Async proxy endpoint qualifier. */
enum class AsyncProxyKind : std::uint8_t {
  async,
  async_global,
  async_shared_cluster,
  async_shared_cta
};
/** @brief Async proxy source/destination pair qualifier. */
enum class ProxyKindPair : std::uint8_t { async_generic, tensormap_generic };

/** @brief Format a scalar type using its stable PTX spelling. */
[[nodiscard]] auto to_string(DataType value) noexcept -> std::string_view;
/** @brief Format a rounding direction using its stable PTX spelling. */
[[nodiscard]] auto to_string(RoundingMode value) noexcept -> std::string_view;
/** @brief Format a comparison relation using its stable PTX spelling. */
[[nodiscard]] auto to_string(ComparisonOperator value) noexcept
    -> std::string_view;
/** @brief Format a boolean relation using its stable PTX spelling. */
[[nodiscard]] auto to_string(BooleanOperator value) noexcept
    -> std::string_view;
/** @brief Format a cache policy using its stable PTX spelling. */
[[nodiscard]] auto to_string(CacheOperator value) noexcept -> std::string_view;
/** @brief Format an eviction policy using its stable PTX spelling. */
[[nodiscard]] auto to_string(EvictionPriority value) noexcept
    -> std::string_view;
/** @brief Format a memory-consistency qualifier using its stable PTX spelling. */
[[nodiscard]] auto to_string(MemoryConsistency value) noexcept
    -> std::string_view;
/** @brief Format a memory scope using its stable PTX spelling. */
[[nodiscard]] auto to_string(MemoryScope value) noexcept -> std::string_view;
/** @brief Format a vector arity using its stable PTX spelling. */
[[nodiscard]] auto to_string(VectorArity value) noexcept -> std::string_view;
/** @brief Format an address space using its stable PTX spelling. */
[[nodiscard]] auto to_string(AddressSpace value) noexcept -> std::string_view;
/** @brief Format an mbarrier phase using its stable PTX spelling. */
[[nodiscard]] auto to_string(MbarrierPhaseType value) noexcept
    -> std::string_view;
/** @brief Format an mbarrier layout using its stable PTX spelling. */
[[nodiscard]] auto to_string(MbarrierLayout value) noexcept
    -> std::string_view;
/** @brief Format an async proxy endpoint using its stable PTX spelling. */
[[nodiscard]] auto to_string(AsyncProxyKind value) noexcept -> std::string_view;
/** @brief Format an async proxy pair using its stable PTX spelling. */
[[nodiscard]] auto to_string(ProxyKindPair value) noexcept -> std::string_view;

/** @brief A predicate register read with an optional logical inversion. */
struct Predicate {
  /** @brief Function-local predicate register slot. */
  common::RegisterSlot source;
  /** @brief Whether the register value is inverted before use. */
  bool negated = false;
  /** @brief Compare predicate source and inversion. */
  constexpr bool operator==(const Predicate&) const noexcept = default;
};

/** @brief A scalar operand that is either a register slot or typed raw bits. */
using ScalarOperand = std::variant<common::RegisterSlot, common::RawValue>;
/** @brief Compatibility spelling for existing b32 execution helpers. */
using B32Operand = ScalarOperand;

/** @brief A scalar destination that may be the PTX underscore sink. */
struct RegisterOrSink {
  /** @brief Present only when the destination writes a register. */
  std::optional<common::RegisterSlot> register_slot;
  /** @brief Compare sink selection. */
  constexpr bool operator==(const RegisterOrSink&) const noexcept = default;
};

/** @brief Stable reference to one special register and optional vector component. */
struct SpecialRegisterRef {
  /** @brief Stable special-register identity owned by the program representation. */
  common::SpecialRegisterId id;
  /** @brief Optional x/y/z component encoded as zero-based component index. */
  std::optional<std::uint8_t> component;
  /** @brief Compare special-register identity and component. */
  constexpr bool operator==(const SpecialRegisterRef&) const noexcept = default;
};
/** @brief A predicate source that can be a predicate register or special register. */
using PredicateSource = std::variant<Predicate, SpecialRegisterRef>;
/** @brief A declared vector-register base. */
struct VectorRegisterRef {
  /** @brief Slot identifying the declared vector-register base. */
  common::RegisterSlot register_slot;
  /** @brief Compare vector-register base slots. */
  constexpr bool operator==(const VectorRegisterRef&) const noexcept = default;
};
/** @brief A vector-capable special-register base. */
struct VectorSpecialRegisterRef {
  /** @brief Stable special-register base identity. */
  common::SpecialRegisterId id;
  /** @brief Compare vector special-register identities. */
  constexpr bool operator==(const VectorSpecialRegisterRef&) const noexcept =
      default;
};
/** @brief A brace-packed register vector whose empty elements are sinks. */
struct RegisterVector {
  /** @brief Register elements in source order; empty elements are sinks. */
  std::vector<std::optional<common::RegisterSlot>> elements;
  /** @brief Compare vector elements and sink positions. */
  bool operator==(const RegisterVector&) const noexcept = default;
};
/** @brief Pair of predicate values used by multi-result forms. */
struct PredicatePair {
  /** @brief First predicate value. */
  Predicate first;
  /** @brief Second predicate value. */
  Predicate second;
  /** @brief Compare both predicate values. */
  constexpr bool operator==(const PredicatePair&) const noexcept = default;
};
/** @brief Compound data/predicate destination used by shuffle-like forms. */
struct ShflDestination {
  /** @brief Optional data destination. */
  std::optional<common::RegisterSlot> data;
  /** @brief Optional predicate destination. */
  std::optional<Predicate> predicate;
  /** @brief Compare optional data and predicate destinations. */
  constexpr bool operator==(const ShflDestination&) const noexcept = default;
};

/** @brief Bound data-symbol reference used by symbol-bearing instruction forms. */
struct SymbolRef {
  /** @brief Program-stable symbol identity. */
  common::SymbolId id;
  /** @brief Compare stable symbol identities. */
  constexpr bool operator==(const SymbolRef&) const noexcept = default;
};
/** @brief Bound function reference used by call and address-taking forms. */
struct FunctionRef {
  /** @brief Program-stable function identity. */
  common::FunctionId id;
  /** @brief Compare stable function identities. */
  constexpr bool operator==(const FunctionRef&) const noexcept = default;
};
/** @brief One signed byte offset applied to an address base. */
struct AddressOffset {
  /** @brief Whether the raw immediate offset is subtracted from the base. */
  bool subtract = false;
  /** @brief Typed immediate offset bits. */
  common::RawValue value;
  /** @brief Compare offset direction and raw value. */
  constexpr bool operator==(const AddressOffset&) const noexcept = default;
};
/** @brief A fully bound address base and optional PTX byte offset. */
struct Address {
  /** @brief Register, immediate, or symbol producing the base address. */
  std::variant<common::RegisterSlot, common::RawValue, SymbolRef> base;
  /** @brief Optional byte offset included in the resolved execution address. */
  std::optional<AddressOffset> offset;
  /** @brief Construct an address whose base is one function-local register slot. */
  constexpr Address(common::RegisterSlot slot) : base(slot) {}
  /** @brief Construct an address from a fully bound base and optional byte offset. */
  constexpr Address(
      std::variant<common::RegisterSlot, common::RawValue, SymbolRef> value,
      std::optional<AddressOffset> value_offset = std::nullopt)
      : base(std::move(value)), offset(std::move(value_offset)) {}
  /** @brief Compare address base and optional offset. */
  constexpr bool operator==(const Address&) const noexcept = default;
};
/** @brief Scalar mov source retaining all resolved source categories. */
using MovSource =
    std::variant<common::RegisterSlot, common::RawValue, SpecialRegisterRef,
                 FunctionRef, SymbolRef, Address>;
/** @brief Bound indirect-call register or metadata reference. */
using IndirectCallee = std::variant<common::RegisterSlot, SymbolRef>;
/** @brief Bound .branchtargets declaration. */
struct BranchTargetSet {
  /** @brief Program-stable declaration identity. */
  common::SymbolId id;
  /** @brief Compare target-set declaration identities. */
  constexpr bool operator==(const BranchTargetSet&) const noexcept = default;
};
/** @brief Bound call return parameter. */
struct CallParameterRef {
  /** @brief Program-stable parameter symbol identity. */
  common::SymbolId id;
  /** @brief Optional element index for parameterized declarations. */
  std::optional<std::uint32_t> index;
  /** @brief Compare parameter identity and optional element index. */
  constexpr bool operator==(const CallParameterRef&) const noexcept = default;
};
/** @brief One direct-call argument after source binding. */
using CallArgument = std::variant<CallParameterRef, common::RawValue>;
/** @brief Ordered direct-call argument group. */
struct CallArguments {
  /** @brief Arguments in source order. */
  std::vector<CallArgument> values;
  /** @brief Compare call arguments in source order. */
  bool operator==(const CallArguments&) const noexcept = default;
};
/** @brief Mbarrier state token, with an empty value representing the sink. */
struct MbarrierStateToken {
  /** @brief State-token register when the result is retained. */
  std::optional<common::RegisterSlot> register_slot;
  /** @brief Compare retained state-token register selections. */
  constexpr bool operator==(const MbarrierStateToken&) const noexcept = default;
};

/** @brief Format a predicate using its bound register identity and inversion. */
[[nodiscard]] auto to_string(const Predicate& value) -> std::string;
/** @brief Format a register-or-sink destination without source spelling. */
[[nodiscard]] auto to_string(const RegisterOrSink& value) -> std::string;
/** @brief Format a special-register reference and its optional component. */
[[nodiscard]] auto to_string(const SpecialRegisterRef& value) -> std::string;
/** @brief Format a vector-register base by its bound register identity. */
[[nodiscard]] auto to_string(const VectorRegisterRef& value) -> std::string;
/** @brief Format a vector-capable special-register base by stable identity. */
[[nodiscard]] auto to_string(const VectorSpecialRegisterRef& value)
    -> std::string;
/** @brief Format a brace-packed vector, preserving sink positions. */
[[nodiscard]] auto to_string(const RegisterVector& value) -> std::string;
/** @brief Format a pair of predicate results. */
[[nodiscard]] auto to_string(const PredicatePair& value) -> std::string;
/** @brief Format a shuffle destination including either optional result. */
[[nodiscard]] auto to_string(const ShflDestination& value) -> std::string;
/** @brief Format a stable data-symbol identity. */
[[nodiscard]] auto to_string(const SymbolRef& value) -> std::string;
/** @brief Format a stable function identity. */
[[nodiscard]] auto to_string(const FunctionRef& value) -> std::string;
/** @brief Format a signed byte offset and its raw bit representation. */
[[nodiscard]] auto to_string(const AddressOffset& value) -> std::string;
/** @brief Format a bound address base and optional byte offset. */
[[nodiscard]] auto to_string(const Address& value) -> std::string;
/** @brief Format a bound branch-target declaration identity. */
[[nodiscard]] auto to_string(const BranchTargetSet& value) -> std::string;
/** @brief Format a bound call parameter and optional element index. */
[[nodiscard]] auto to_string(const CallParameterRef& value) -> std::string;
/** @brief Format ordered direct-call arguments in their bound argument order. */
[[nodiscard]] auto to_string(const CallArguments& value) -> std::string;
/** @brief Format an mbarrier state token or its sink representation. */
[[nodiscard]] auto to_string(const MbarrierStateToken& value) -> std::string;

}  // namespace ptxsim::exec_ir
