#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <ptxsim/common/raw_value.hpp>
#include <ptxsim/exec_ir/owned_operands.hpp>

namespace ptxsim::exec_ir {
namespace detail {
/** @brief Identity of one declaration scope, distinct from a canonical callable. */
struct FunctionDeclarationIdTag {
  /** @brief Diagnostic prefix for owned declaration-scope metadata. */
  static constexpr std::string_view prefix = "function-declaration";
};
}  // namespace detail
/** @brief Dense declaration identity, including separate prototypes/definitions. */
using FunctionDeclarationId = common::detail::Id<detail::FunctionDeclarationIdTag>;

/** @brief Source-selected linkage, retained as owned callable/storage metadata. */
enum class Linkage : std::uint8_t { None, External, Visible, Weak, Common };
/** @brief Full static storage space, without assigning a runtime address. */
enum class StorageSpace : std::uint8_t { global, constant, shared, local };
/** @brief Whether a storage record defines an object or declares an external. */
enum class StorageDeclarationKind : std::uint8_t { Definition, External };
/** @brief Complete initialization category, distinct from allocation capability. */
enum class StorageInitialization : std::uint8_t {
  uninitialized, zero, explicit_values, external, opaque_static
};
/** @brief Address interpretation preserved by an unevaluated relocation. */
enum class StorageAddressKind : std::uint8_t { StateSpace, Generic, Function };
/** @brief Immutable 128-bit constant bits, with no host arithmetic interpretation. */
struct Constant128 {
  /** @brief Least-significant 64 bits. */
  std::uint64_t bits{};
  /** @brief Most-significant 64 bits. */
  std::uint64_t high_bits{};
  /** @brief Compare the complete stored bit pattern. */
  bool operator==(const Constant128&) const = default;
};
/** @brief Owned relocation that remains unevaluated until runtime materialization. */
struct StorageRelocation {
  /** @brief Exact bound data/resource/canonical-callable target category. */
  std::variant<StorageId, ResourceId, common::FunctionId> target{StorageId{0}};
  /** @brief Optional compact declaration-group member. */
  std::optional<std::uint32_t> member;
  /** @brief Selected generic/state-space/function address interpretation. */
  StorageAddressKind address_kind = StorageAddressKind::StateSpace;
  /** @brief Original 64-bit addend bits, independent of signed host arithmetic. */
  std::uint64_t addend_bits{};
  /** @brief Optional byte mask retained without eager address evaluation. */
  std::optional<std::uint64_t> byte_mask;
  /** @brief Compare all relocation semantics. */
  bool operator==(const StorageRelocation&) const = default;
};
/** @brief Sparse initializer element at an explicit byte offset. */
struct StorageInitializerElement {
  /** @brief Byte offset from the start of the declared object. */
  std::uint64_t byte_offset{};
  /** @brief Actual constant or relocation, with no absent-value placeholder. */
  std::variant<Constant128, StorageRelocation> value{Constant128{}};
  /** @brief Compare offset and complete initialization value. */
  bool operator==(const StorageInitializerElement&) const = default;
};
/** @brief Static opaque-resource field name, independent of source spelling. */
enum class OpaqueStaticField : std::uint8_t {
  Width, Height, Depth, ChannelDataType, ChannelOrder, NormalizedCoords,
  FilterMode, AddressMode0, AddressMode1, AddressMode2, ArraySize,
  NumMipmapLevels, NumSamples, ForceUnnormalizedCoords, MemoryLayout
};
/** @brief Closed static opaque values retaining their external PTX/OpenCL codepoints. */
enum class OpaqueStaticEnum : std::uint16_t {
  Nearest, Linear, Wrap, Mirror, ClampOgl, ClampToEdge, ClampToBorder,
  ClSnormInt8 = 0x10d0, ClSnormInt16, ClUnormInt8, ClUnormInt16, ClUnormShort565,
  ClUnormShort555, ClUnormInt101010, ClSignedInt8, ClSignedInt16,
  ClSignedInt32, ClUnsignedInt8, ClUnsignedInt16, ClUnsignedInt32,
  ClHalfFloat, ClFloat, ClR = 0x10b0, ClA, ClRg, ClRa, ClRgb, ClRgba, ClBgra,
  ClArgb, ClIntensity, ClLuminance
};
/** @brief One complete static opaque-resource member. */
struct OpaqueStaticMember {
  /** @brief Selected typed property. */
  OpaqueStaticField field = OpaqueStaticField::Width;
  /** @brief Numeric or enum value as actually selected by the source. */
  std::variant<std::uint64_t, OpaqueStaticEnum> value{std::uint64_t{0}};
  /** @brief Compare selected property and value category. */
  bool operator==(const OpaqueStaticMember&) const = default;
};
/** @brief Static opaque object with its complete multidimensional index. */
struct OpaqueStaticObject {
  /** @brief Source-order outer-to-inner array indices. */
  std::vector<std::uint64_t> indices;
  /** @brief Every explicit property, preserving order. */
  std::vector<OpaqueStaticMember> members;
  /** @brief Compare object index and all explicit members. */
  bool operator==(const OpaqueStaticObject&) const = default;
};
/** @brief Full unified UUID without frontend lifetime or host address state. */
struct UnifiedId {
  /** @brief Upper 64 bits of the source UUID. */
  std::uint64_t upper{};
  /** @brief Lower 64 bits of the source UUID. */
  std::uint64_t lower{};
  /** @brief Compare the complete UUID. */
  bool operator==(const UnifiedId&) const = default;
};
/** @brief Complete owned storage declaration, wider than runtime allocation views. */
struct StorageDeclaration {
  /** @brief Stable owned storage identity. */
  StorageId id{0};
  /** @brief Name used only at explicit external-binding/API boundaries. */
  std::string name;
  /** @brief Canonical owner for function-local/shared declarations. */
  std::optional<common::FunctionId> owner_function;
  /** @brief Full declaration space. */
  StorageSpace space = StorageSpace::global;
  /** @brief Actual scalar versus opaque element category. */
  std::variant<DataType, OpaqueResourceKind> element_type{DataType{}};
  /** @brief Whether the declaration selected the legacy texture representation. */
  bool legacy_texture = false;
  /** @brief Declared element vector width. */
  std::uint8_t vector_width{1};
  /** @brief Outer-to-inner array axes; absent axes remain genuinely unsized. */
  std::vector<std::optional<std::uint64_t>> array_extents;
  /** @brief Known object byte extent; opaque/unsized declarations remain absent. */
  std::optional<std::uint64_t> byte_extent;
  /** @brief Known effective byte alignment, without fabricating opaque alignment. */
  std::optional<std::uint64_t> alignment;
  /** @brief Explicit alignment from any compatible scalar occurrence; opaque facts remain exact. */
  std::optional<std::uint64_t> explicit_alignment;
  /** @brief Source linkage category. */
  Linkage linkage = Linkage::None;
  /** @brief Definition versus external declaration. */
  StorageDeclarationKind declaration_kind = StorageDeclarationKind::Definition;
  /** @brief Whether the external shared object consumes launch dynamic bytes. */
  bool dynamic_shared = false;
  /** @brief Compact declaration-group count, absent for an ordinary single object. */
  std::optional<std::uint32_t> parameterized_count;
  /** @brief Managed declaration attribute. */
  bool managed = false;
  /** @brief Unified declaration UUID, when actually present. */
  std::optional<UnifiedId> unified_id;
  /** @brief Exact source initialization category. */
  StorageInitialization initialization = StorageInitialization::uninitialized;
  /** @brief Ordered explicit sparse initializer elements. */
  std::vector<StorageInitializerElement> initializer;
  /** @brief Complete opaque static objects and properties. */
  std::vector<OpaqueStaticObject> opaque_static_objects;
  /** @brief Compare every execution-relevant declaration property. */
  bool operator==(const StorageDeclaration&) const = default;
};
/** @brief Role of an opaque resource's actual declaration scope. */
enum class ResourceDeclarationRole : std::uint8_t { Module, EntryInput };
/** @brief Opaque resource identity independent of any generic byte allocation. */
struct ResourceDeclaration {
  /** @brief Dense execution resource identity. */
  ResourceId id{0};
  /** @brief External/API boundary name, owned by the program. */
  std::string name;
  /** @brief Typed texture/sampler/surface category. */
  OpaqueResourceKind kind = OpaqueResourceKind::Texture;
  /** @brief Actual module versus entry-input declaration role. */
  ResourceDeclarationRole role = ResourceDeclarationRole::Module;
  /** @brief Concrete entry declaration owner for entry resources. */
  std::optional<FunctionDeclarationId> entry_declaration;
  /** @brief Canonical entry identity, separate from exact declaration scope. */
  std::optional<common::FunctionId> entry_owner;
  /** @brief Underlying full storage record for module-defined resources. */
  std::optional<StorageId> storage;
  /** @brief Optional compact declaration-group count. */
  std::optional<std::uint32_t> parameterized_count;
  /** @brief Explicit byte alignment, without implying opaque element bytes. */
  std::optional<std::uint64_t> explicit_alignment;
  /** @brief Every optional outer-to-inner array axis. */
  std::vector<std::optional<std::uint64_t>> array_extents;
  /** @brief Compare complete resource identity and declaration metadata. */
  bool operator==(const ResourceDeclaration&) const = default;
};
/** @brief Known pointer target metadata, independent from parameter blob packing. */
struct PointerProperties {
  /** @brief Target space when explicitly constrained; absent means generic. */
  std::optional<DeclarationSpace> pointed_space;
  /** @brief Effective target byte alignment from the checked pointer contract. */
  std::uint64_t pointed_alignment{4};
  /** @brief Compare target-space and alignment constraints. */
  bool operator==(const PointerProperties&) const = default;
};
/** @brief Full scalar parameter declaration retaining its specific source scope. */
struct ParameterDeclaration {
  /** @brief Dense declaration identity. */
  ParameterId id{0};
  /** @brief Specific owning declaration; prototypes and definitions stay distinct. */
  FunctionDeclarationId owner_declaration{0};
  /** @brief Canonical callable owner. */
  common::FunctionId owner_function{0};
  /** @brief Full declaration role, without conflating entry headers and body locals. */
  ParameterDeclarationRole role = ParameterDeclarationRole::BodyLocal;
  /** @brief Declared scalar element type. */
  DataType scalar_type{};
  /** @brief Effective declaration byte alignment. */
  std::uint64_t alignment{};
  /** @brief Whether the source explicitly specified alignment. */
  bool explicit_alignment = false;
  /** @brief Declared element vector width. */
  std::uint32_t vector_width{1};
  /** @brief Outer-to-inner optional extents. */
  std::vector<std::optional<std::uint64_t>> array_extents;
  /** @brief Byte extent per named object when known. */
  std::optional<std::uint64_t> byte_extent;
  /** @brief Optional target pointer properties. */
  std::optional<PointerProperties> pointer;
  /** @brief Optional compact declaration-group count. */
  std::optional<std::uint32_t> parameterized_count;
  /** @brief Compare full scoped declaration metadata. */
  bool operator==(const ParameterDeclaration&) const = default;
};
/** @brief ABI vector shape independent of a declaration's storage width. */
enum class AbiVectorShape : std::uint8_t { Scalar = 1, V2 = 2, V4 = 4 };
/** @brief Full ABI parameter contract, not an allocated runtime slot. */
struct FunctionParameterContract {
  /** @brief Register/parameter/local/etc declaration class. */
  DeclarationSpace state_space = DeclarationSpace::Parameter;
  /** @brief Explicit ABI byte alignment when specified. */
  std::optional<std::uint64_t> alignment;
  /** @brief Actual scalar or opaque element type. */
  std::variant<DataType, OpaqueResourceKind> element_type{DataType{}};
  /** @brief Exact Scalar/V2/V4 ABI shape. */
  AbiVectorShape vector_shape = AbiVectorShape::Scalar;
  /** @brief Complete pointer properties; absent only for a non-pointer formal. */
  std::optional<PointerProperties> pointer;
  /** @brief Complete ordered optional ABI array extents. */
  std::vector<std::optional<std::uint64_t>> array_extents;
  /** @brief Compare complete typed ABI semantics. */
  bool operator==(const FunctionParameterContract&) const = default;
};
/** @brief Complete owned callable ABI signature. */
struct FunctionSignature {
  /** @brief Entry versus device callable kind. */
  bool is_entry = false;
  /** @brief Non-returning callable contract. */
  bool is_noreturn = false;
  /** @brief Ordered returned ABI parameters. */
  std::vector<FunctionParameterContract> return_parameters;
  /** @brief Ordered input ABI parameters. */
  std::vector<FunctionParameterContract> parameters;
  /** @brief Compare the complete callable ABI. */
  bool operator==(const FunctionSignature&) const = default;
};
/** @brief Register preservation obligation at a callable boundary. */
struct AbiPreservationContract {
  /** @brief Number of preserved registers in the selected register domain. */
  std::uint32_t count{};
  /** @brief Whether the contract denotes control registers. */
  bool control_registers = false;
  /** @brief Compare count and register domain. */
  bool operator==(const AbiPreservationContract&) const = default;
};
/** @brief Callable allocation/address attribute. */
enum class FunctionAttributeKind : std::uint8_t { Managed, Unified };
/** @brief Full callable attribute with its optional UUID. */
struct FunctionAttribute {
  /** @brief Selected attribute category. */
  FunctionAttributeKind kind = FunctionAttributeKind::Managed;
  /** @brief Actual unified UUID when supplied. */
  std::optional<UnifiedId> unified_id;
  /** @brief Compare selected attribute and UUID. */
  bool operator==(const FunctionAttribute&) const = default;
};
/** @brief Closed kernel resource/directive kind. */
enum class KernelResourceKind : std::uint8_t {
  MaxNreg, MaxNtid, ReqNtid, MinNctaPerSm, ReqNctaPerCluster, ExplicitCluster,
  MaxClusterRank, MaxNctaPerSm
};
/** @brief Complete typed kernel resource contract. */
struct KernelResourceContract {
  /** @brief Selected resource/directive category. */
  KernelResourceKind kind = KernelResourceKind::MaxNreg;
  /** @brief Ordered resource dimensions/count values. */
  std::vector<std::uint32_t> values;
  /** @brief Number of explicitly selected source values. */
  std::uint8_t explicit_value_count{};
  /** @brief Compare resource category and all values. */
  bool operator==(const KernelResourceContract&) const = default;
};
/** @brief Complete owned per-declaration callable contract. */
struct FunctionContract {
  /** @brief Full typed ABI signature. */
  FunctionSignature signature;
  /** @brief Declared linkage. */
  Linkage linkage = Linkage::None;
  /** @brief All managed/unified attributes. */
  std::vector<FunctionAttribute> attributes;
  /** @brief Explicit non-returning contract. */
  bool is_noreturn = false;
  /** @brief Ordinary register-preservation contract. */
  std::optional<AbiPreservationContract> abi_preserve;
  /** @brief Control-register-preservation contract. */
  std::optional<AbiPreservationContract> abi_preserve_control;
  /** @brief Every explicit kernel resource directive. */
  std::vector<KernelResourceContract> resources;
  /** @brief Whether declared blocks are clusters. */
  bool blocks_are_clusters = false;
  /** @brief Owned language metadata values, retained as uninterpreted boundary data. */
  std::optional<std::vector<std::string>> language_values;
  /** @brief Compare all callable declaration facts. */
  bool operator==(const FunctionContract&) const = default;
};
/** @brief Version pair retained without reparsing textual directives. */
struct PtxVersion {
  /** @brief Major PTX version number. */
  std::uint16_t major{};
  /** @brief Minor PTX version number. */
  std::uint16_t minor{};
  /** @brief Compare both version components. */
  auto operator<=>(const PtxVersion&) const = default;
};
/** @brief Checked source texture mode, independent of resource allocation. */
enum class TextureMode : std::uint8_t { Unified, Independent };
/** @brief Owned source target/profile facts relevant to execution interpretation. */
struct SourceProfile {
  /** @brief Checked source PTX version, when available. */
  std::optional<PtxVersion> version;
  /** @brief Owned target metadata; no string drives execution dispatch. */
  std::vector<std::string> target_options;
  /** @brief Checked texture resource interpretation. */
  TextureMode texture_mode = TextureMode::Unified;
  /** @brief Historical f64 mapping selected by the source target region. */
  bool map_f64_to_f32 = false;
  /** @brief Explicit module address size in bits. */
  std::optional<std::uint32_t> address_size_bits;
  /** @brief Compare all owned source interpretation facts. */
  bool operator==(const SourceProfile&) const = default;
};
/** @brief One concrete declaration scope, including body-less prototypes. */
struct FunctionDeclaration {
  /** @brief Dense specific declaration identity. */
  FunctionDeclarationId id{0};
  /** @brief Bound canonical callable identity. */
  common::FunctionId function{0};
  /** @brief Owned external/API declaration name. */
  std::string name;
  /** @brief Whether this declaration denotes an entry. */
  bool is_entry = false;
  /** @brief True for declarations without source bodies. */
  bool is_prototype = false;
  /** @brief Complete contract from this specific declaration. */
  FunctionContract contract;
  /** @brief Ordered full parameter identities for this declaration scope. */
  std::vector<ParameterId> parameters;
  /** @brief Ordered opaque entry inputs for this exact declaration scope. */
  std::vector<ResourceId> opaque_entry_parameters;
  /** @brief Selected owned source profile index. */
  std::optional<std::size_t> source_profile;
  /** @brief Checked source target when supplied separately for this declaration. */
  std::optional<std::string> source_target;
  /** @brief Checked source version when supplied separately. */
  std::optional<PtxVersion> source_version;
  /** @brief Compare complete specific declaration semantics. */
  bool operator==(const FunctionDeclaration&) const = default;
};
/** @brief Byte layout of one source-ordered packed scalar entry argument. */
struct EntryParameterLayout {
  /** @brief Offset from the first entry argument byte. */
  std::size_t offset{};
  /** @brief Payload byte count. */
  std::size_t size{};
  /** @brief Required byte alignment of this slot. */
  std::size_t alignment{};
  /** @brief Full parameter declaration represented by this derived slot. */
  ParameterId parameter{0};
  /** @brief Compare byte placement and declaration identity. */
  bool operator==(const EntryParameterLayout&) const = default;
};
/** @brief Real body layout, distinct from a body-less callable declaration. */
struct FunctionLayout {
  /** @brief Canonical callable whose definition owns this body. */
  common::FunctionId id{0};
  /** @brief First flat instruction position in immutable program storage. */
  std::size_t begin{};
  /** @brief Number of instructions, including a genuinely empty definition. */
  std::uint32_t instruction_count{};
  /** @brief Required physical width of each actual function-local scalar slot. */
  std::vector<common::RawWidth> register_widths;
  /** @brief Packed scalar entry argument extent in bytes. */
  std::size_t entry_parameter_size{};
  /** @brief Derived entry packing, independent of the full ABI signature. */
  std::vector<EntryParameterLayout> entry_parameters;
  /** @brief Whether this body's complete entry ABI has a supported packed-byte view. */
  bool entry_packing_available = true;
  /** @brief Compare complete static body/register/entry layout. */
  bool operator==(const FunctionLayout&) const = default;
};
/** @brief Canonical callable identity shared by definitions, prototypes and aliases. */
struct FunctionRecord {
  /** @brief Dense canonical callable identity. */
  common::FunctionId id{0};
  /** @brief Owned primary callable name at external/API boundaries. */
  std::string name;
  /** @brief Canonical entry/device role. */
  bool is_entry = false;
  /** @brief Every specific declaration, preserving their independent parameter scopes. */
  std::vector<FunctionDeclarationId> declarations;
  /** @brief Actual defining declaration, absent for external/prototype-only callables. */
  std::optional<FunctionDeclarationId> definition;
  /** @brief Body layout only when a true definition exists. */
  std::optional<FunctionLayout> body;
  /** @brief Compare canonical callable and definition metadata. */
  bool operator==(const FunctionRecord&) const = default;
};
/** @brief Owned alias name mapped to one canonical callable ID. */
struct FunctionAlias {
  /** @brief External/API alias name, owned independently from the frontend. */
  std::string name;
  /** @brief Canonical callable target; aliases never duplicate bodies. */
  common::FunctionId function{0};
  /** @brief Compare alias boundary name and target identity. */
  bool operator==(const FunctionAlias&) const = default;
};
/** @brief Complete ordered function-local branch target set. */
struct BranchTargetSetRecord {
  /** @brief Dense set identity. */
  BranchTargetSetId id{0};
  /** @brief Function whose local PC domain owns every target. */
  common::FunctionId owner_function{0};
  /** @brief Ordered local targets, retaining duplicate entries. */
  std::vector<common::ProgramCounter> targets;
  /** @brief Compare owner and exact ordered target list. */
  bool operator==(const BranchTargetSetRecord&) const = default;
};
/** @brief Complete typed indirect-call prototype metadata. */
struct PrototypeRecord {
  /** @brief Dense prototype identity. */
  PrototypeId id{0};
  /** @brief Specific declaration scope owning this metadata. */
  FunctionDeclarationId owner_declaration{0};
  /** @brief Full callable signature. */
  FunctionSignature signature;
  /** @brief Non-returning contract. */
  bool is_noreturn = false;
  /** @brief Ordinary register-preservation contract. */
  std::optional<AbiPreservationContract> abi_preserve;
  /** @brief Control-register-preservation contract. */
  std::optional<AbiPreservationContract> abi_preserve_control;
  /** @brief Compare complete prototype semantics. */
  bool operator==(const PrototypeRecord&) const = default;
};
/** @brief Complete ordered callable target-set metadata. */
struct CallTargetSetRecord {
  /** @brief Dense callable set identity. */
  CallTargetSetId id{0};
  /** @brief Specific declaration scope owning the metadata. */
  FunctionDeclarationId owner_declaration{0};
  /** @brief Ordered canonical callable targets, retaining duplicates. */
  std::vector<common::FunctionId> targets;
  /** @brief Full common callable ABI. */
  FunctionSignature signature;
  /** @brief Compare exact target order and ABI. */
  bool operator==(const CallTargetSetRecord&) const = default;
};
/** @brief One explicit sparse callable slot. */
struct CallTableSlot {
  /** @brief Original sparse element index. */
  std::uint64_t index{};
  /** @brief Original explicit byte offset. */
  std::uint64_t byte_offset{};
  /** @brief Canonical callable target. */
  common::FunctionId function{0};
  /** @brief Compare sparse position and canonical target. */
  bool operator==(const CallTableSlot&) const = default;
};
/** @brief Complete sparse callable storage table and its callable ABI. */
struct CallTableRecord {
  /** @brief Dense callable table metadata identity. */
  CallTableId id{0};
  /** @brief Actual underlying full storage declaration. */
  StorageId storage{0};
  /** @brief Canonical function owner for a local table. */
  std::optional<common::FunctionId> owner_function;
  /** @brief Static storage space. */
  StorageSpace space = StorageSpace::global;
  /** @brief Scalar table element type. */
  DataType element_type{};
  /** @brief Declared element count, without inventing implicit callable targets. */
  std::uint64_t extent{};
  /** @brief Declared byte extent. */
  std::uint64_t byte_extent{};
  /** @brief Ordered explicit slots; duplicate targets and sparse positions remain data. */
  std::vector<CallTableSlot> slots;
  /** @brief Complete table callable signature. */
  FunctionSignature signature;
  /** @brief Compare complete storage, sparse slots and ABI semantics. */
  bool operator==(const CallTableRecord&) const = default;
};

}  // namespace ptxsim::exec_ir
