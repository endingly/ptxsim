"""Immutable execution projection shared by every generated artifact family.

Semantic field paths remain independent of version-specific C++ member access.
Frozen normalized contract records are specification facts, never runtime
frontend payloads; C++ emission gives them ptxsim-owned typed storage.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from ptx_frontend.ir.resolved_ir import (
        ResolvedAddressAlignmentConstraint,
        ResolvedAtomicAddressQualifierPolicy,
        ResolvedImmediateMultipleOfConstraint,
        ResolvedImmediateRangeConstraint,
        ResolvedImmediateValueConstraint,
        ResolvedMemoryConsistencyConstraint,
        ResolvedMemoryVectorConstraint,
        ResolvedModifierValueAvailability,
        ResolvedOperandBinding,
        ResolvedOperandTypeCompatibility,
    )
    from ptx_frontend.spec.model import (
        AsyncCompletionKind, ConditionCodeEffect, FabricInstructionSpec,
        FloatingPointDefault, MatrixSpec, StackInstructionSpec,
        SurfaceInstructionSpec, TextureInstructionSpec, VideoInstructionSpec,
        WgmmaProtocolAction, SemanticRule,
    )
    from ptx_frontend.ir.resolved_ir import (
        AtomicAddressQualifierValue, TcgenCommitAddressSpelling,
        TcgenFenceDirection, TensorAccessMode, TensorDestination,
    )
    from ptx_frontend.ir.tensor_reduction import TensorReductionOp


class ValueKind(Enum):
    """Closed semantic field domains of the pinned normalized frontend model."""

    BOOL = 'Bool'
    SCALAR_TYPE = 'ScalarType'
    ROUNDING_MODE = 'RoundingMode'
    COMPARISON_OPERATOR = 'ComparisonOperator'
    TEST_PROPERTY = 'TestProperty'
    BOOLEAN_OPERATOR = 'BooleanOperator'
    CACHE_OPERATOR = 'CacheOperator'
    EVICTION_PRIORITY = 'EvictionPriority'
    PREFETCH_SIZE = 'PrefetchSize'
    MEMORY_CONSISTENCY = 'MemoryConsistency'
    MEMORY_SCOPE = 'MemoryScope'
    VECTOR_ARITY = 'VectorArity'
    MEMORY_STATE_SPACE = 'MemoryStateSpace'
    MBARRIER_PHASE_TYPE = 'MbarrierPhaseType'
    MBARRIER_LAYOUT = 'MbarrierLayout'
    CTA_GROUP = 'TcgenCtaGroup'
    TCGEN_SCALE_VECTOR_SIZE = 'TcgenScaleVectorSize'
    TCGEN_COLLECTOR = 'TcgenCollectorControl'
    TCGEN_SHAPE = 'TcgenDataMovementShape'
    TCGEN_NUM = 'TcgenRepeat'
    TCGEN_RED_OP = 'TcgenReductionOp'
    TCGEN_WAIT = 'TcgenWaitClass'
    VIDEO_SCALE = 'VideoScale'
    VIDEO_SHIFT_MODE = 'VideoShiftMode'
    VIDEO_SECONDARY_OP = 'VideoSecondaryOp'
    VIDEO_TYPE = 'VideoType'
    ASYNC_PROXY_KIND = 'AsyncProxyKind'
    PROXY_KIND_PAIR = 'ProxyKindPair'
    REGISTER = 'Register'
    PREDICATE = 'Predicate'
    PREDICATE_OR_SINK = 'PredicateOrSink'
    PREDICATE_SOURCE = 'PredicateSource'
    IMMEDIATE = 'Immediate'
    REG_OR_IMM = 'RegOrImm'
    REGISTER_OR_SINK = 'RegisterOrSink'
    CVT_SOURCE = 'CvtSource'
    MOV_SOURCE = 'MovSource'
    MOV_VECTOR_SOURCE = 'MovVectorSource'
    CP_ASYNC_SOURCE_CONTROL = 'CpAsyncSourceControl'
    VECTOR_REGISTER = 'VectorRegister'
    VECTOR_SPECIAL_REGISTER = 'VectorSpecialRegister'
    BRANCH_TARGET = 'BranchTarget'
    SPECIAL_REGISTER = 'SpecialRegister'
    SYMBOL = 'Symbol'
    ADDRESS = 'Address'
    REGISTER_VECTOR = 'RegisterVector'
    VALUE_VECTOR = 'ValueVector'
    TENSOR_COORDINATE = 'TensorCoordinate'
    TENSOR_IM2COL_INFO = 'TensorIm2colInfo'
    TENSOR_OPERAND = 'TensorOperand'
    FABRIC_HANDLE = 'FabricHandle'
    SURFACE_ACCESS = 'SurfaceAccess'
    SURFACE_QUERY_RESOURCE = 'SurfaceQueryResource'
    STACK_TOKEN = 'StackToken'
    LOCAL_ALLOCATION_RESULT = 'LocalAllocationResult'
    VIDEO_OPERAND = 'VideoOperand'
    TEXTURE_ACCESS = 'TextureAccess'
    TEXTURE_QUERY_RESOURCE = 'TextureQueryResource'
    TEXTURE_RESULT = 'TextureResult'
    TENSOR_MEMORY_ADDRESS = 'TensorMemoryAddress'
    TCGEN_BRACKETED_ADDRESS = 'TcgenBracketedAddress'
    TCGEN_HALF_SPLIT_OFFSET = 'TcgenHalfSplitOffset'
    MATRIX_SCALE_SELECTOR = 'MatrixScaleSelector'
    SHARED_MATRIX_DESCRIPTOR = 'SharedMatrixDescriptor'
    WGMMA_SCALE_D = 'WgmmaScaleD'
    DIRECT_CALL_TARGET = 'DirectCallTarget'
    INDIRECT_CALLEE = 'IndirectCallee'
    BRANCH_TARGET_SET = 'BranchTargetSet'
    CALL_RETURN_PARAMETER = 'CallReturnParameter'
    CALL_ARGUMENTS = 'CallArguments'
    SHFL_DESTINATION = 'ShflDestination'
    PREDICATE_PAIR = 'PredicatePair'
    PREDICATE_PAIR_OR_SINK = 'PredicatePairOrSink'
    MBARRIER_STATE_TOKEN = 'MbarrierStateToken'


class FieldStorage(Enum):
    """Whether a control is selected by a form or stored on its instance."""

    INSTANCE = "instance"
    CONSTANT = "constant"


@dataclass(frozen=True, slots=True)
class FormId:
    """Canonical semantic source identity, independent of C++ naming."""

    opcode: str
    variant: str


@dataclass(frozen=True, slots=True)
class LayoutId:
    """One source layout within an exact semantic form."""

    form: FormId
    name: str


@dataclass(frozen=True, slots=True)
class FieldId:
    """Layout-local identity; equal names need not mean equal storage."""

    form: FormId
    layout: LayoutId | None
    name: str


@dataclass(frozen=True, slots=True)
class DomainValue:
    """One closed typed semantic atom and its owned/source C++ expressions."""

    kind: ValueKind
    ordinal: int
    spelling: str | bool | int
    cpp: str
    frontend_cpp: str


@dataclass(frozen=True, slots=True)
class Domain:
    """Complete backend value vocabulary, never an execution support filter."""

    kind: ValueKind
    cpp_type: str
    frontend_cpp_type: str
    values: tuple[DomainValue, ...]


class SourceContext(Enum):
    """Owned semantic context needed when source C++ values are equivalent."""

    NONE = "none"
    PARAMETER_QUALIFIER = "parameter_qualifier"
    ATOMIC_QUALIFIER = "atomic_qualifier"


@dataclass(frozen=True, slots=True)
class SourceValueGroup:
    """One source C++ value, its semantic aliases and the preserved context."""

    frontend_cpp: str
    semantic_values: tuple[DomainValue, ...]
    canonical: DomainValue
    context: SourceContext


@dataclass(frozen=True, slots=True)
class AtomicQualifierValue:
    """One typed atomic address qualifier independent of the state-space enum."""

    semantic: AtomicAddressQualifierValue
    cpp: str
    frontend_cpp: str


@dataclass(frozen=True, slots=True)
class AtomicQualifierDomain:
    """Closed native domain and binder for selected atomic instance controls."""

    cpp_type: str
    binder: str
    values: tuple[AtomicQualifierValue, ...]


@dataclass(frozen=True, slots=True)
class AtomicQualifierControl:
    """Selected source instance field plus the exact form's admitted values."""

    frontend: CppAccess
    allowed: tuple[AtomicQualifierValue, ...]


@dataclass(frozen=True, slots=True)
class LeafMapping:
    """Owned payload type and required binder for one resolved value kind."""

    kind: ValueKind
    cpp_type: str
    frontend_cpp_type: str
    binder: str


@dataclass(frozen=True, slots=True)
class CppAccess:
    """Version-local frontend member spelling and optional ownership wrapper."""

    member: str
    optional: bool
    located: bool


@dataclass(frozen=True, slots=True)
class Field:
    """One typed control/operand at its complete semantic path."""

    identity: FieldId
    value_kind: ValueKind
    cpp_type: str
    cpp_name: str
    frontend: CppAccess
    storage: FieldStorage
    constant: DomainValue | None
    default: DomainValue | None
    domain: tuple[DomainValue, ...]
    source_groups: tuple[SourceValueGroup, ...]
    binding: ResolvedOperandBinding | None
    specification_kind: str | None


@dataclass(frozen=True, slots=True)
class Layout:
    """All ordered fields and exact selected frontend layout position."""

    identity: LayoutId
    index: int
    cpp_name: str
    fields: tuple[Field, ...]
    forbidden_modifiers: tuple[str, ...]
    forbidden_modifier_slots: tuple[int, ...]


@dataclass(frozen=True, slots=True)
class FormContracts:
    """Named immutable normalized execution contracts for one semantic form."""

    condition_code: ConditionCodeEffect
    completion: AsyncCompletionKind
    wgmma_action: WgmmaProtocolAction
    semantic_rule: SemanticRule | None
    matrix: MatrixSpec | None
    fabric: FabricInstructionSpec | None
    surface: SurfaceInstructionSpec | None
    stack: StackInstructionSpec | None
    texture: TextureInstructionSpec | None
    video: VideoInstructionSpec | None
    floating_point_default: FloatingPointDefault | None
    modifier_availability: tuple[ResolvedModifierValueAvailability, ...]
    operand_type_compatibility: tuple[ResolvedOperandTypeCompatibility, ...]
    memory_consistency: ResolvedMemoryConsistencyConstraint | None
    permits_unified_address: bool
    unified_address_access: str
    address_alignments: tuple[ResolvedAddressAlignmentConstraint, ...]
    memory_vector: ResolvedMemoryVectorConstraint | None
    immediate_value: ResolvedImmediateValueConstraint | None
    immediate_ranges: tuple[ResolvedImmediateRangeConstraint, ...]
    immediate_multiple_of: ResolvedImmediateMultipleOfConstraint | None
    tcgen_commit_address: TcgenCommitAddressSpelling | None
    tcgen_commit_multicast: bool
    tcgen_fence_direction: TcgenFenceDirection | None
    tcgen_copy_pairs: tuple[tuple[str, str], ...]
    tcgen_copy_formats: tuple[tuple[bool, bool, bool], ...]
    tensor_reduction: TensorReductionOp | None
    tensor_access: TensorAccessMode | None
    tensor_destination: TensorDestination | None
    tensor_im2col_elements: tuple[tuple[str, int], ...]
    tensor_multicast: bool
    tensor_cta_group_applicable: bool


@dataclass(frozen=True, slots=True)
class Form:
    """One exact final class with owned fields and complete static contracts."""

    identity: FormId
    kind: int
    opcode: int
    cpp_name: str
    frontend_cpp_name: str
    category: str
    modifiers: tuple[Field, ...]
    layouts: tuple[Layout, ...]
    contracts: FormContracts
    atomic_qualifier: AtomicQualifierControl | None


@dataclass(frozen=True, slots=True)
class Instruction:
    """Canonical opcode grouping used only for artifact partitioning."""

    opcode: str
    identity: int
    cpp_name: str
    category: str
    forms: tuple[Form, ...]
    atomic_address_qualifier: ResolvedAtomicAddressQualifierPolicy | None


@dataclass(frozen=True, slots=True)
class Snapshot:
    """One full immutable semantic projection for IR, binders and execution."""

    frontend_version: str
    schema: str
    instructions: tuple[Instruction, ...]
    domains: tuple[Domain, ...]
    leaves: tuple[LeafMapping, ...]
    atomic_qualifier_domain: AtomicQualifierDomain
    specification_operand_kinds: tuple[str, ...]

    @property
    def forms(self) -> tuple[Form, ...]:
        """Return the full dense Kind sequence without filtering capability."""
        return tuple(form for op in self.instructions for form in op.forms)

    @property
    def layout_count(self) -> int:
        """Return the number of canonical semantic operand layouts."""
        return sum(len(form.layouts) for form in self.forms)
