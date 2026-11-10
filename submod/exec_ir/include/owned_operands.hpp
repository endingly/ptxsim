#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

#include <ptxsim/common/ids.hpp>
#include <ptxsim/exec_ir/owned_ids.hpp>
#include <ptxsim/exec_ir/value_domains.gen.hpp>

namespace ptxsim::exec_ir {

/** @brief Declaration register class, independent of the instruction's use type. */
enum class RegisterClass : std::uint8_t { General, Predicate };
/** @brief Static enclosing function role retained by scoped operand contracts. */
enum class EnclosingFunctionKind : std::uint8_t { Unknown, Entry, Device };
/** @brief Address interpretation selected for a .param operand. */
enum class ParameterAddressQualifier : std::uint8_t { Default, Entry, Function };
/** @brief Full parameter role; entry launch packing is a separate derived view. */
enum class ParameterDeclarationRole : std::uint8_t {
  EntryInput, DeviceInput, DeviceReturn, BodyLocal, LegacyEntryInput
};
/** @brief Declared scalar object's space, distinct from instruction modifiers. */
enum class DeclarationSpace : std::uint8_t {
  Register, Parameter, Local, Shared, Global, Constant
};
/** @brief Runtime opaque-resource category, with no implied allocation bytes. */
enum class OpaqueResourceKind : std::uint8_t { Texture, Sampler, Surface };
/** @brief Closed intrinsic-register identity, including indexed register families. */
enum class SpecialRegisterKind : std::uint8_t {
  Invalid, LaneId, WarpId, NWarpId, SmId, NSmId, GridId, IsExplicitCluster,
  ClusterCtaRank, ClusterNCtaRank, LaneMaskEq, LaneMaskLe, LaneMaskLt,
  LaneMaskGe, LaneMaskGt, Clock, ClockHi, Clock64, GlobalTimer, GlobalTimerLo,
  GlobalTimerHi, ReservedSmemOffsetBegin, ReservedSmemOffsetEnd,
  ReservedSmemOffsetCap, TotalSmemSize, AggrSmemSize, DynamicSmemSize,
  CurrentGraphExec, Tid, NTid, CtaId, NCtaId, ClusterId, NClusterId,
  ClusterCtaId, ClusterNCtaId, PerformanceMonitor, PerformanceMonitor64,
  Environment, ReservedSmemOffset
};

/** @brief Scalar projection of a real ordinary vector-register declaration. */
struct RegisterComponent {
  /** @brief Zero-based physical lane in the original declared vector. */
  std::uint8_t lane{};
  /** @brief Original declaration lane count, independent of scalar use width. */
  std::uint8_t declaration_width{};
  /** @brief Compare execution storage views, without source selector aliases. */
  bool operator==(const RegisterComponent&) const = default;
};
/** @brief Bound register slot with complete declaration and component metadata. */
struct RegisterRef {
  /** @brief Function-local scalar slot; selected vector components have real slots. */
  common::RegisterSlot slot{0};
  /** @brief Declared predicate/general register class. */
  RegisterClass register_class = RegisterClass::General;
  /** @brief Scalar element type declared for the register. */
  DataType declared_type{};
  /** @brief Whole-vector lane count, absent for scalar/component references. */
  std::optional<std::uint8_t> vector_width;
  /** @brief Actual component view when this denotes a selected vector lane. */
  std::optional<RegisterComponent> component;
  /** @brief Compare bound storage and the complete declaration view. */
  bool operator==(const RegisterRef&) const = default;
};
/** @brief Typed immediate preserving both selected use bits and integer provenance. */
struct Immediate {
  /** @brief Converted use-width bit pattern, retained without host arithmetic. */
  std::uint64_t bits{};
  /** @brief Scalar type selected by this exact operand use. */
  DataType type{};
  /** @brief True only for a signed negative evaluated integer source. */
  bool is_negative = false;
  /** @brief Original evaluated integer bits; absent for floating sources. */
  std::optional<std::uint64_t> integer_source_bits;
  /** @brief Compare value bits, use type and source integer semantics. */
  bool operator==(const Immediate&) const = default;
};
/** @brief Bound predicate read with logical inversion. */
struct Predicate {
  /** @brief Real predicate register declaration and function-local slot. */
  RegisterRef register_ref;
  /** @brief Whether the read is complemented. */
  bool negated = false;
  /** @brief Compare predicate storage and inversion. */
  bool operator==(const Predicate&) const = default;
};
/** @brief One target-independent special-register family member. */
struct SpecialRegisterId {
  /** @brief Intrinsic identity, independent of source spelling. */
  SpecialRegisterKind kind = SpecialRegisterKind::Invalid;
  /** @brief Member index for indexed intrinsic families. */
  std::uint8_t index{};
  /** @brief Compare intrinsic kind and indexed member. */
  bool operator==(const SpecialRegisterId&) const = default;
};
/** @brief Intrinsic register reference with owned semantic type/shape metadata. */
struct SpecialRegisterRef {
  /** @brief Stable intrinsic family and optional family index. */
  SpecialRegisterId id;
  /** @brief Optional zero-based x/y/z component. */
  std::optional<std::uint8_t> component;
  /** @brief Intrinsic element type captured from checked frontend metadata. */
  DataType element_type{};
  /** @brief Whole intrinsic vector lane count. */
  std::uint8_t vector_width{1};
  /** @brief Compare the complete intrinsic value view. */
  bool operator==(const SpecialRegisterRef&) const = default;
};
/** @brief Complementable special-register predicate source. */
struct PredicateSpecialRegister {
  /** @brief Owned special-register view. */
  SpecialRegisterRef register_ref;
  /** @brief Whether the intrinsic predicate read is complemented. */
  bool negated = false;
  /** @brief Compare intrinsic view and inversion. */
  bool operator==(const PredicateSpecialRegister&) const = default;
};
/** @brief Literal predicate source distinct from register-backed predicates. */
struct PredicateConstant {
  /** @brief Logical value, independent of any register allocation. */
  bool value = false;
  /** @brief Compare the literal logical value. */
  bool operator==(const PredicateConstant&) const = default;
};
/** @brief All checked predicate-source alternatives, including constants. */
using PredicateSource = std::variant<Predicate, PredicateSpecialRegister, PredicateConstant>;
/** @brief Register-backed scalar or complete typed immediate. */
using ScalarOperand = std::variant<RegisterRef, Immediate>;
/** @brief General conversion source with retained intrinsic-register identity. */
using CvtSource = std::variant<RegisterRef, Immediate, SpecialRegisterRef>;
/** @brief Register destination or the genuine PTX sink. */
struct RegisterOrSink {
  /** @brief Engaged only when this operand denotes real register storage. */
  std::optional<RegisterRef> register_ref;
  /** @brief Compare destination storage or sink selection. */
  bool operator==(const RegisterOrSink&) const = default;
};
/** @brief Mbarrier token carrier or an admitted sink. */
struct MbarrierStateToken {
  /** @brief Token register, absent only for a semantically admitted sink. */
  std::optional<RegisterRef> register_ref;
  /** @brief Compare retained token storage. */
  bool operator==(const MbarrierStateToken&) const = default;
};
/** @brief Whole ordinary vector declaration without implicit scalar selection. */
struct VectorRegisterRef {
  /** @brief Bound declaration base and complete declared vector width. */
  RegisterRef register_ref;
  /** @brief Compare the declared vector storage view. */
  bool operator==(const VectorRegisterRef&) const = default;
};
/** @brief Whole intrinsic vector reference. */
struct VectorSpecialRegisterRef {
  /** @brief Intrinsic family/member, element type and full width. */
  SpecialRegisterRef register_ref;
  /** @brief Compare the complete intrinsic vector view. */
  bool operator==(const VectorSpecialRegisterRef&) const = default;
};
/** @brief Ordered real register lanes; empty lanes are actual destination sinks. */
struct RegisterVector {
  /** @brief Source-order component views, with owned declaration widths and lanes. */
  std::vector<std::optional<RegisterRef>> elements;
  /** @brief Compare ordered storage views and sink positions. */
  bool operator==(const RegisterVector&) const = default;
};
/** @brief Ordered typed scalar values for vector source operands. */
struct ValueVector {
  /** @brief Register/immediate alternatives in exact source order. */
  std::vector<ScalarOperand> elements;
  /** @brief Compare source values and their complete type provenance. */
  bool operator==(const ValueVector&) const = default;
};
/** @brief Scalar MOV lane including intrinsic register sources. */
using MovVectorLane = std::variant<RegisterRef, Immediate, SpecialRegisterRef>;
/** @brief Owned ordered MOV vector values. */
struct MovValueVector {
  /** @brief Every actual component, without borrowed named-vector provenance. */
  std::vector<MovVectorLane> elements;
  /** @brief Compare ordered movement source alternatives. */
  bool operator==(const MovValueVector&) const = default;
};
/** @brief Explicit MOV vector values or one whole intrinsic vector. */
using MovVectorSource = std::variant<MovValueVector, VectorSpecialRegisterRef>;

/** @brief Predicate destination or the genuine predicate sink. */
struct PredicateOrSink {
  /** @brief Engaged only when a predicate is written. */
  std::optional<Predicate> predicate;
  /** @brief Compare predicate destination or sink selection. */
  bool operator==(const PredicateOrSink&) const = default;
};
/** @brief Ordered pair of predicate destinations. */
struct PredicatePair {
  /** @brief First declared predicate destination. */
  Predicate first;
  /** @brief Second declared predicate destination. */
  Predicate second;
  /** @brief Compare both destination views. */
  bool operator==(const PredicatePair&) const = default;
};
/** @brief Ordered predicate destinations with independent admitted sinks. */
struct PredicatePairOrSink {
  /** @brief First destination, absent only for its actual sink. */
  std::optional<Predicate> first;
  /** @brief Second destination, absent only for its actual sink. */
  std::optional<Predicate> second;
  /** @brief Compare independent destinations and sink choices. */
  bool operator==(const PredicatePairOrSink&) const = default;
};
/** @brief Shuffle result retaining independent data and predicate destinations. */
struct ShflDestination {
  /** @brief Optional data result. */
  std::optional<RegisterRef> data;
  /** @brief Optional predicate result. */
  std::optional<Predicate> predicate;
  /** @brief Compare both retained results. */
  bool operator==(const ShflDestination&) const = default;
};
/** @brief Bound local branch location. */
struct BranchTarget {
  /** @brief Owning callable function; the PC is local to this body. */
  common::FunctionId function{0};
  /** @brief Dense local instruction position. */
  common::ProgramCounter pc{0};
  /** @brief Compare complete semantic code locations. */
  bool operator==(const BranchTarget&) const = default;
};
/** @brief Reference to a complete ordered branch-target set. */
struct BranchTargetSet {
  /** @brief Owned table index; targets remain local to its recorded function. */
  BranchTargetSetId id{0};
  /** @brief Compare owned target-set identity. */
  bool operator==(const BranchTargetSet&) const = default;
};
/** @brief Canonical callable reference, including external/prototype functions. */
struct FunctionRef {
  /** @brief Owned canonical function ID, never a host pointer or invented PC. */
  common::FunctionId id{0};
  /** @brief Whether the checked callable is an entry. */
  bool is_entry = false;
  /** @brief Compare canonical callable identity and kind. */
  bool operator==(const FunctionRef&) const = default;
};
/** @brief Typed indirect metadata reference without conflating prototype and set. */
struct IndirectMetadataRef {
  /** @brief Exact owned metadata identity selected at the binding boundary. */
  std::variant<PrototypeId, CallTargetSetId> target{PrototypeId{0}};
  /** @brief Compare the selected metadata kind and identity. */
  bool operator==(const IndirectMetadataRef&) const = default;
};
/** @brief Reference to an owned sparse callable storage table. */
struct CallTableRef {
  /** @brief Table metadata identity, independent of its underlying storage ID. */
  CallTableId id{0};
  /** @brief Compare table identity. */
  bool operator==(const CallTableRef&) const = default;
};
/** @brief All indirect callee alternatives with their original semantic roles. */
using IndirectCallee = std::variant<RegisterRef, IndirectMetadataRef, CallTableRef>;
/** @brief Addressable parameter declaration and optional compact-group member. */
struct ParameterRef {
  /** @brief Owned full parameter declaration, with role and scope preserved. */
  ParameterId id{0};
  /** @brief Selected member of a parameterized declaration, when present. */
  std::optional<std::uint32_t> member;
  /** @brief Compare declaration and selected member. */
  bool operator==(const ParameterRef&) const = default;
};
/** @brief Call lvalue that is a parameter object or real ordinary register view. */
struct CallParameterRef {
  /** @brief Complete selected storage category. */
  std::variant<ParameterRef, RegisterRef> value{ParameterRef{}};
  /** @brief Compare lvalue category and owned storage. */
  bool operator==(const CallParameterRef&) const = default;
};
/** @brief Source integer value preserving unsigned interpretation. */
struct IntegerConstant {
  /** @brief Evaluated integer bits, without host signed conversion. */
  std::uint64_t bits{};
  /** @brief Whether the evaluated constant is unsigned. */
  bool is_unsigned = false;
  /** @brief Compare integer value and signedness. */
  bool operator==(const IntegerConstant&) const = default;
};
/** @brief Evaluated floating source constant before a call-use conversion. */
struct FloatingConstant {
  /** @brief Exact binary64 source bits, preserving signed zero and NaN payloads. */
  std::uint64_t bits{};
  /** @brief Compare stored source values. */
  bool operator==(const FloatingConstant&) const = default;
};
/** @brief Lexical constant category retained when use conversion is deferred. */
enum class LiteralCategory : std::uint8_t {
  DecimalInteger, HexInteger, F32Hex, F64Hex, DecimalFloat, WarpSize, BinaryInteger
};
/** @brief Call literal with independent source value and optional converted use. */
struct CallLiteral {
  /** @brief Original constant category. */
  LiteralCategory kind = LiteralCategory::DecimalInteger;
  /** @brief Evaluated source value, absent only when genuinely unavailable. */
  std::optional<std::variant<IntegerConstant, FloatingConstant>> source_value;
  /** @brief Converted typed use value, absent for genuinely deferred conversion. */
  std::optional<Immediate> value;
  /** @brief Compare source and use representations. */
  bool operator==(const CallLiteral&) const = default;
};
/** @brief One owned call argument, preserving lvalue versus literal. */
using CallArgument = std::variant<CallParameterRef, CallLiteral>;
/** @brief Complete ordered call argument group. */
struct CallArguments {
  /** @brief Every parameter/register/literal argument in exact order. */
  std::vector<CallArgument> values;
  /** @brief Compare all argument alternatives and order. */
  bool operator==(const CallArguments&) const = default;
};

/** @brief Bound ordinary addressable symbol with semantic role metadata. */
struct SymbolRef {
  /** @brief Storage and parameter IDs remain distinct execution domains. */
  std::variant<StorageId, ParameterId> target{StorageId{0}};
  /** @brief Selected member of a compact declaration group. */
  std::optional<std::uint32_t> member;
  /** @brief Original declared space, when supplied by the checked input. */
  std::optional<DeclarationSpace> declaration_space;
  /** @brief Effective address space retained independently from declaration. */
  std::optional<DeclarationSpace> address_space;
  /** @brief Declared scalar element type where the declaration is scalar. */
  std::optional<DataType> declared_type;
  /** @brief Known address alignment in bytes. */
  std::optional<std::uint64_t> alignment;
  /** @brief Known unified-declaration fact, distinct from unknown metadata. */
  std::optional<bool> is_unified;
  /** @brief Static caller function role for parameter-address interpretation. */
  EnclosingFunctionKind enclosing_function_kind = EnclosingFunctionKind::Unknown;
  /** @brief Selected .param interpretation. */
  ParameterAddressQualifier parameter_qualifier = ParameterAddressQualifier::Default;
  /** @brief Parameter role if this denotes parameter storage. */
  std::optional<ParameterDeclarationRole> parameter_role;
  /** @brief Compare all owned symbol semantics. */
  bool operator==(const SymbolRef&) const = default;
};
/** @brief Bound texture/sampler/surface symbol with an owned execution resource ID. */
struct OpaqueSymbolRef {
  /** @brief Resource declaration in the immutable program table. */
  ResourceId id{0};
  /** @brief Optional compact-group member. */
  std::optional<std::uint32_t> member;
  /** @brief Declared opaque resource category. */
  OpaqueResourceKind kind = OpaqueResourceKind::Texture;
  /** @brief Entry owner when this is a typed entry input; null for module scope. */
  std::optional<common::FunctionId> entry_owner;
  /** @brief Compare resource identity, member and execution role. */
  bool operator==(const OpaqueSymbolRef&) const = default;
};
/** @brief Direct typed resource or an indirect register handle. */
struct OpaqueResourceRef {
  /** @brief Complete selected direct/indirect alternative. */
  std::variant<OpaqueSymbolRef, RegisterRef> value{OpaqueSymbolRef{}};
  /** @brief Resource role expected by this operand use. */
  OpaqueResourceKind expected_kind = OpaqueResourceKind::Texture;
  /** @brief Compare resource role and selected carrier. */
  bool operator==(const OpaqueResourceRef&) const = default;
};
/** @brief Arithmetic sign applied to a byte offset or named-array displacement. */
enum class AddressOffsetOperator : std::uint8_t { Add, Subtract };
/** @brief Full typed signed address offset, including source integer provenance. */
struct AddressOffset {
  /** @brief Operator retained independently of the literal's signedness. */
  AddressOffsetOperator operation = AddressOffsetOperator::Add;
  /** @brief Complete converted offset value. */
  Immediate value;
  /** @brief Compare operator and value provenance. */
  bool operator==(const AddressOffset&) const = default;
};
/** @brief Named-array index that remains independent of a byte-address addend. */
struct NamedArrayIndex {
  /** @brief Evaluated constant index or real bound register view. */
  std::variant<IntegerConstant, RegisterRef> index{IntegerConstant{}};
  /** @brief Optional evaluated source displacement. */
  std::optional<IntegerConstant> displacement;
  /** @brief Addition/subtraction applied to the source displacement. */
  AddressOffsetOperator operation = AddressOffsetOperator::Add;
  /** @brief Scalar byte stride selected by the checked declaration. */
  std::uint64_t scalar_stride{};
  /** @brief Combined constant byte displacement, without losing source operands. */
  std::int64_t byte_displacement{};
  /** @brief Compare the complete named-index contract. */
  bool operator==(const NamedArrayIndex&) const = default;
};
/** @brief Bound address preserving source-selected space and index semantics. */
struct Address {
  /** @brief Complete register/immediate/storage-or-parameter base alternative. */
  std::variant<RegisterRef, Immediate, SymbolRef> base{RegisterRef{}};
  /** @brief Optional independent typed byte offset. */
  std::optional<AddressOffset> offset;
  /** @brief Static caller role for address interpretation. */
  EnclosingFunctionKind enclosing_function_kind = EnclosingFunctionKind::Unknown;
  /** @brief Selected parameter address qualifier, even for canonical param space. */
  ParameterAddressQualifier parameter_qualifier = ParameterAddressQualifier::Default;
  /** @brief Unified-address access selected by the source operand. */
  bool unified = false;
  /** @brief Optional complete named-array index expression. */
  std::optional<NamedArrayIndex> named_index;
  /** @brief Compare every execution-relevant address component. */
  bool operator==(const Address&) const = default;
};
/** @brief All seven MOV source categories; no frontend-owned payload survives. */
using MovSource = std::variant<RegisterRef, Immediate, SpecialRegisterRef,
                               FunctionRef, SymbolRef, Address, OpaqueSymbolRef>;
/** @brief Register carrying an opaque CP-async cache policy. */
struct CpAsyncCachePolicy {
  /** @brief Real register binding and declaration view. */
  RegisterRef register_ref;
  /** @brief Compare policy carrier storage. */
  bool operator==(const CpAsyncCachePolicy&) const = default;
};
/** @brief All CP-async source-control categories. */
using CpAsyncSourceControl = std::variant<RegisterRef, Immediate, Predicate, CpAsyncCachePolicy>;

/** @brief Runtime stack token with a canonical owning function. */
struct StackToken {
  /** @brief Scalar token carrier. */
  RegisterRef register_ref;
  /** @brief Enclosing function semantic kind. */
  EnclosingFunctionKind enclosing_function_kind = EnclosingFunctionKind::Unknown;
  /** @brief Bound function scope identity. */
  common::FunctionId function{0};
  /** @brief Compare carrier and bound function ownership. */
  bool operator==(const StackToken&) const = default;
};
/** @brief Local allocation result with a bound owning function. */
struct LocalAllocationResult {
  /** @brief Scalar result register. */
  RegisterRef register_ref;
  /** @brief Enclosing function semantic kind. */
  EnclosingFunctionKind enclosing_function_kind = EnclosingFunctionKind::Unknown;
  /** @brief Bound function scope identity. */
  common::FunctionId function{0};
  /** @brief Produced address role, retained independently from use width. */
  DeclarationSpace address_space = DeclarationSpace::Local;
  /** @brief Compare carrier, function and address role. */
  bool operator==(const LocalAllocationResult&) const = default;
};
/** @brief Opaque shared-memory matrix descriptor carried by a real register. */
struct SharedMatrixDescriptor {
  /** @brief Register identity and declaration view; contents remain runtime data. */
  RegisterRef register_ref;
  /** @brief Compare descriptor carrier storage. */
  bool operator==(const SharedMatrixDescriptor&) const = default;
};

/** @brief Byte position within one 32-bit video operand. */
struct VideoByteSelector {
  /** @brief Zero-based selected byte, in [0, 3]. */
  std::uint8_t index{};
  /** @brief Compare byte position. */
  bool operator==(const VideoByteSelector&) const = default;
};
/** @brief Halfword position within one 32-bit video operand. */
struct VideoHalfSelector {
  /** @brief Zero-based selected halfword, in [0, 1]. */
  std::uint8_t index{};
  /** @brief Compare halfword position. */
  bool operator==(const VideoHalfSelector&) const = default;
};
/** @brief Ordered two-halfword source swizzle. */
struct VideoHalfSwizzle {
  /** @brief Source halfword index selected for each result position. */
  std::array<std::uint8_t, 2> indices{};
  /** @brief Compare ordered selections. */
  bool operator==(const VideoHalfSwizzle&) const = default;
};
/** @brief Ordered four-byte source swizzle. */
struct VideoByteSwizzle {
  /** @brief Source byte index selected for each result position. */
  std::array<std::uint8_t, 4> indices{};
  /** @brief Compare ordered selections. */
  bool operator==(const VideoByteSwizzle&) const = default;
};
/** @brief Destination halfword mask with explicit selected count. */
struct VideoHalfMask {
  /** @brief Ordered destination positions; only the count prefix is active. */
  std::array<std::uint8_t, 2> indices{};
  /** @brief Number of active positions. */
  std::uint8_t count{};
  /** @brief Compare positions and active count. */
  bool operator==(const VideoHalfMask&) const = default;
};
/** @brief Destination byte mask with explicit selected count. */
struct VideoByteMask {
  /** @brief Ordered destination positions; only the count prefix is active. */
  std::array<std::uint8_t, 4> indices{};
  /** @brief Number of active positions. */
  std::uint8_t count{};
  /** @brief Compare positions and active count. */
  bool operator==(const VideoByteMask&) const = default;
};
/** @brief All typed scalar-selection, source-swizzle and destination-mask forms. */
using VideoSelector = std::variant<VideoByteSelector, VideoHalfSelector,
                                   VideoHalfSwizzle, VideoByteSwizzle,
                                   VideoHalfMask, VideoByteMask>;
/** @brief Video value with independent typed selection and source negation. */
struct VideoOperand {
  /** @brief Real register or complete immediate source. */
  ScalarOperand value{RegisterRef{}};
  /** @brief Optional selector/mask; absence is the actual unselected source form. */
  std::optional<VideoSelector> selector;
  /** @brief Source arithmetic negation. */
  bool negated = false;
  /** @brief Compare value, selection and negation. */
  bool operator==(const VideoOperand&) const = default;
};
/** @brief Tensor Memory address value with retained selected bracket form. */
struct TensorMemoryAddress {
  /** @brief Scalar register or complete typed immediate. */
  ScalarOperand value{RegisterRef{}};
  /** @brief Whether the checked operand selected the bracketed address form. */
  bool bracketed = false;
  /** @brief Compare value and selected address form. */
  bool operator==(const TensorMemoryAddress&) const = default;
};
/** @brief Signedness of the original TCGEN split-offset integer. */
enum class TcgenIntegerSourceKind : std::uint8_t { Signed, Unsigned };
/** @brief TCGEN split offset preserving the evaluated source integer domain. */
struct TcgenHalfSplitOffset {
  /** @brief Original evaluated 64-bit integer bits. */
  std::uint64_t source_bits{};
  /** @brief Signed versus unsigned interpretation before splitting. */
  TcgenIntegerSourceKind source_kind = TcgenIntegerSourceKind::Signed;
  /** @brief Compare exact source integer semantics. */
  bool operator==(const TcgenHalfSplitOffset&) const = default;
};
/** @brief Matrix scale tuple with two independent typed scalar values. */
struct MatrixScaleSelector {
  /** @brief Byte-selection value. */
  ScalarOperand byte_id{RegisterRef{}};
  /** @brief Thread-selection value. */
  ScalarOperand thread_id{RegisterRef{}};
  /** @brief Compare both selectors. */
  bool operator==(const MatrixScaleSelector&) const = default;
};
/** @brief Ordered tensor coordinate values. */
struct TensorCoordinate {
  /** @brief Every coordinate in logical dimension order. */
  std::vector<ScalarOperand> elements;
  /** @brief Compare typed coordinates and order. */
  bool operator==(const TensorCoordinate&) const = default;
};
/** @brief Ordered im2col auxiliary information, independent of syntax ranges. */
struct TensorIm2colInfo {
  /** @brief Every typed auxiliary element in its contract-defined role order. */
  std::vector<ScalarOperand> elements;
  /** @brief Compare all auxiliary values. */
  bool operator==(const TensorIm2colInfo&) const = default;
};
/** @brief Tensor-map address retained independently from coordinate data. */
struct TensorMapRef {
  /** @brief Complete owned address and its parameter/unified/index interpretation. */
  Address address;
  /** @brief Compare the tensor-map address contract. */
  bool operator==(const TensorMapRef&) const = default;
};
/** @brief Logical tensor rank, independent of source bracket spelling. */
enum class TensorRank : std::uint8_t { One = 1, Two, Three, Four, Five };
/** @brief Exact instruction-local interpretation of a tensor coordinate group. */
enum class TensorAccessMode : std::uint8_t {
  Tiled, Im2colNoOffs, Im2col, Im2colW, Im2colW128, TileGather4, TileScatter4
};
/** @brief Fully bound tensor-map, ordered coordinates and selected topology. */
struct TensorOperand {
  /** @brief Complete tensor-map address. */
  TensorMapRef tensor_map;
  /** @brief Coordinates in exact dimension order. */
  TensorCoordinate coordinates;
  /** @brief Selected logical rank. */
  TensorRank rank = TensorRank::One;
  /** @brief Selected access interpretation. */
  TensorAccessMode mode = TensorAccessMode::Tiled;
  /** @brief Compare all tensor value and topology fields. */
  bool operator==(const TensorOperand&) const = default;
};
/** @brief Fabric endpoint and offsets, preserving the optional counter carrier. */
struct FabricHandle {
  /** @brief Opaque endpoint register. */
  RegisterRef endpoint;
  /** @brief Data offset carrier. */
  RegisterRef data_offset;
  /** @brief Counter offset carrier when the selected form has one. */
  std::optional<RegisterRef> counter_offset;
  /** @brief Compare endpoint and all selected offsets. */
  bool operator==(const FabricHandle&) const = default;
};
/** @brief Execution role of one surface coordinate lane. */
enum class SurfaceLaneRole : std::uint8_t { Spatial, ArrayLayer, Ignored };
/** @brief One typed surface coordinate and its topology role. */
struct SurfaceLane {
  /** @brief Real register or complete typed immediate coordinate. */
  ScalarOperand value{RegisterRef{}};
  /** @brief Logical role selected by the checked geometry. */
  SurfaceLaneRole role = SurfaceLaneRole::Spatial;
  /** @brief Compare value and coordinate role. */
  bool operator==(const SurfaceLane&) const = default;
};
/** @brief Bound surface resource and complete coordinate topology. */
struct SurfaceAccess {
  /** @brief Typed direct resource or indirect handle. */
  OpaqueResourceRef surface;
  /** @brief Ordered coordinate lanes with explicit roles. */
  std::vector<SurfaceLane> coordinates;
  /** @brief Whether coordinate values use the selected packed form. */
  bool coordinates_packed = false;
  /** @brief Selected bracket form retained with the layout. */
  bool bracketed = true;
  /** @brief Compare resource, roles, values and selected topology. */
  bool operator==(const SurfaceAccess&) const = default;
};
/** @brief Surface query resource retaining direct/indirect and selected layout. */
struct SurfaceQueryResource {
  /** @brief Exact typed resource alternative. */
  OpaqueResourceRef resource;
  /** @brief Selected bracket form. */
  bool bracketed = true;
  /** @brief Compare selected query resource representation. */
  bool operator==(const SurfaceQueryResource&) const = default;
};
/** @brief Execution role of one texture coordinate lane. */
enum class TextureLaneRole : std::uint8_t { Spatial, ArrayLayer, Sample, Ignored };
/** @brief Typed texture coordinate with its independent geometry role. */
struct TextureLane {
  /** @brief Register/immediate coordinate value. */
  ScalarOperand value{RegisterRef{}};
  /** @brief Selected coordinate role. */
  TextureLaneRole role = TextureLaneRole::Spatial;
  /** @brief Compare value and role. */
  bool operator==(const TextureLane&) const = default;
};
/** @brief Complete texture/sampler binding and coordinate interpretation. */
struct TextureAccess {
  /** @brief Exact texture resource alternative. */
  OpaqueResourceRef texture;
  /** @brief Separate sampler when the selected texture mode uses one. */
  std::optional<OpaqueResourceRef> sampler;
  /** @brief Coordinate values in role order. */
  std::vector<TextureLane> coordinates;
  /** @brief Checked coordinate scalar type. */
  DataType coordinate_type{};
  /** @brief Selected bracket form. */
  bool bracketed = true;
  /** @brief Whether coordinates use the selected packed representation. */
  bool coordinates_packed = false;
  /** @brief Compare every texture access value and role. */
  bool operator==(const TextureAccess&) const = default;
};
/** @brief Texture query resource with complete direct/indirect identity. */
struct TextureQueryResource {
  /** @brief Exact resource alternative and expected role. */
  OpaqueResourceRef resource;
  /** @brief Selected bracket form. */
  bool bracketed = true;
  /** @brief Compare query resource and selected representation. */
  bool operator==(const TextureQueryResource&) const = default;
};
/** @brief Texture data destinations and optional residency predicate. */
struct TextureResult {
  /** @brief Ordered data destination views, including admitted sinks. */
  RegisterVector data;
  /** @brief Selected result scalar type. */
  DataType result_type{};
  /** @brief Actual residency destination when required by the selected layout. */
  std::optional<Predicate> residency;
  /** @brief Compare complete data and residency destinations. */
  bool operator==(const TextureResult&) const = default;
};

}  // namespace ptxsim::exec_ir
