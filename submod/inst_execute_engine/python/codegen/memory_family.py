"""Derive ordinary load/store preparation from the projected frontend model."""

from __future__ import annotations

from dataclasses import dataclass
import re

from ptx_frontend.ir.resolved_ir import (
    ResolvedOperandAccess,
    ResolvedOperandShape,
    ResolvedOperandTypeExpressionKind,
    ResolvedRegisterWidthPolicy,
)

from ptxsim_codegen.exec_ir.cpp_names import instruction_cpp_name
from ptxsim_codegen.exec_ir.model import GenerationError, ProjectedForm, ProjectedInstruction


_TYPES = ("b8", "b16", "b32", "b64", "u8", "u16", "u32", "u64",
          "s8", "s16", "s32", "s64", "f32", "f64")
_ORDINARY_FORMS = {"GenericScalar", "ExplicitScalar", "GenericVector", "ExplicitVector"}
_UNSUPPORTED_FORMS = {
    "GlobalU32L1Evict", "GlobalU32L2CacheHint", "GlobalNcL1NoAllocateU32",
}


@dataclass(frozen=True)
class _MemoryForm:
    """One ordinary scalar or vector memory form selected from projection."""

    cpp_name: str
    operation: str
    data: str
    address: str
    type_field: str
    semantics: str
    scope: str
    cache: str
    state_space: str | None
    vector: str | None
    mmio: str | None

    @property
    def function_name(self) -> str:
        """Return the private generated adapter name for this form."""
        return f"prepare_{self.operation}_" + re.sub(
            r"(?<=[a-z0-9])(?=[A-Z])", "_", self.cpp_name
        ).lower()

    @property
    def load(self) -> bool:
        """Return whether this form writes registers rather than memory."""
        return self.operation == "ld"


@dataclass(frozen=True)
class MemoryOperation:
    """Every ordinary form generated for one load/store opcode."""

    opcode: str
    cpp_name: str
    forms: tuple[_MemoryForm, ...]


def memory_operation(instruction: ProjectedInstruction) -> MemoryOperation:
    """Validate and derive the ordinary memory forms of one projected opcode."""
    if instruction.opcode not in {"ld", "st"}:
        raise GenerationError(f"{instruction.opcode}: not a memory opcode")
    names = {form.source.cpp_name for form in instruction.forms}
    unknown = names - _ORDINARY_FORMS - _UNSUPPORTED_FORMS
    missing = _ORDINARY_FORMS - names
    if unknown or missing:
        raise GenerationError(
            f"{instruction.opcode}: memory form drift unknown={sorted(unknown)} missing={sorted(missing)}"
        )
    forms = tuple(
        _memory_form(instruction, form)
        for form in instruction.forms
        if form.source.cpp_name in _ORDINARY_FORMS
    )
    return MemoryOperation(instruction.opcode, instruction_cpp_name(instruction.opcode), forms)


def _memory_form(instruction: ProjectedInstruction, form: ProjectedForm) -> _MemoryForm:
    """Reject a form that cannot use the bounded ordinary memory helper."""
    name = form.source.cpp_name
    vector = name.endswith("Vector")
    explicit = name.startswith("Explicit")
    if len(form.source.operand_layouts) != 1 or len(form.layouts) != 1:
        raise GenerationError(f"{instruction.opcode}/{form.variant.name}: memory requires one layout")
    layout = form.source.operand_layouts[0]
    source_layout = form.layouts[0]
    if layout.layout_id != source_layout.name or len(layout.fields) != len(layout.bindings):
        raise GenerationError(f"{instruction.opcode}/{form.variant.name}: memory layout drift")
    bindings = {binding.target_field_id: binding for binding in layout.bindings}
    if len(bindings) != len(layout.bindings) or set(bindings) != {field.name for field in layout.fields}:
        raise GenerationError(f"{instruction.opcode}/{form.variant.name}: memory binding identity drift")
    expected = 2
    if len(layout.fields) != expected:
        raise GenerationError(f"{instruction.opcode}/{form.variant.name}: ordinary memory requires two operands")
    data = next(
        (field for field in layout.fields
         if field.operand_access is (ResolvedOperandAccess.WRITE if instruction.opcode == "ld" else ResolvedOperandAccess.READ)
         and field.allowed_operand_shapes == ((ResolvedOperandShape.VECTOR,) if vector else (ResolvedOperandShape.REGISTER,))),
        None,
    )
    address = next(
        (field for field in layout.fields
         if field.operand_access is ResolvedOperandAccess.READ
         and field.allowed_operand_shapes == (ResolvedOperandShape.ADDRESS,)),
        None,
    )
    if data is None or address is None or data is address:
        raise GenerationError(f"{instruction.opcode}/{form.variant.name}: ordinary memory operand topology drift")
    expected_access = ResolvedOperandAccess.WRITE if instruction.opcode == "ld" else ResolvedOperandAccess.READ
    expected_data_shape = ResolvedOperandShape.VECTOR if vector else ResolvedOperandShape.REGISTER
    if data.operand_access is not expected_access or data.allowed_operand_shapes != (expected_data_shape,):
        raise GenerationError(f"{instruction.opcode}/{form.variant.name}/{data.name}: unsupported memory data operand")
    if address.operand_access is not ResolvedOperandAccess.READ or address.allowed_operand_shapes != (ResolvedOperandShape.ADDRESS,):
        raise GenerationError(f"{instruction.opcode}/{form.variant.name}/{address.name}: unsupported memory address operand")
    data_binding = bindings[data.name]
    if (data_binding.type_expression.kind is not ResolvedOperandTypeExpressionKind.MODIFIER_FIELD
            or data_binding.type_expression.modifier_field_id is None
            or data_binding.register_width_policy is not ResolvedRegisterWidthPolicy.EQUAL_OR_WIDER):
        raise GenerationError(f"{instruction.opcode}/{form.variant.name}/{data.name}: memory data binding drift")
    address_binding = bindings[address.name]
    if address_binding.type_expression.kind is not ResolvedOperandTypeExpressionKind.NONE:
        raise GenerationError(f"{instruction.opcode}/{form.variant.name}/{address.name}: memory address type drift")
    modifiers = {modifier.name: modifier for modifier in form.variant.modifiers if modifier.presence != "absent"}
    required = {"semantics", "scope", "cache", "type"}
    if explicit:
        required.add("state_space")
    if vector:
        required.add("vector")
    if not required <= modifiers.keys() or set(modifiers) - (required | {"mmio"}):
        raise GenerationError(f"{instruction.opcode}/{form.variant.name}: unsupported memory modifiers")
    type_values = tuple(str(value.value) for value in modifiers["type"].values)
    if type_values != _TYPES:
        raise GenerationError(f"{instruction.opcode}/{form.variant.name}: memory type allowlist drift")
    source_fields = {field.source_name: field for field in form.source.modifier_fields}
    type_field = source_fields.get("type")
    if type_field is None or type_field.name != data_binding.type_expression.modifier_field_id:
        raise GenerationError(f"{instruction.opcode}/{form.variant.name}: memory type field drift")
    if vector and tuple(str(value.value) for value in modifiers["vector"].values) != ("v2", "v4", "v8"):
        raise GenerationError(f"{instruction.opcode}/{form.variant.name}: memory vector allowlist drift")
    semantics = source_fields.get("semantics")
    scope = source_fields.get("scope")
    cache = source_fields.get("cache")
    mmio = source_fields.get("mmio")
    consistency = form.source.memory_consistency
    if (semantics is None or scope is None or cache is None or consistency is None
            or consistency.semantics_field_id != semantics.name
            or consistency.scope_field_id != scope.name
            or consistency.cache_field_id != cache.name
            or consistency.address_field_id != address.name
            or ("mmio" in modifiers and (mmio is None or consistency.mmio_field_id != mmio.name))
            or ("mmio" not in modifiers and consistency.mmio_field_id not in {None, ""})
            or bool(form.source.memory_vector) != vector):
        raise GenerationError(f"{instruction.opcode}/{form.variant.name}: memory constraint drift")
    if vector:
        vector_constraint = form.source.memory_vector
        if (vector_constraint.type_field_id != type_field.name
                or vector_constraint.vector_field_id != data.name
                or vector_constraint.address_field_id != address.name):
            raise GenerationError(f"{instruction.opcode}/{form.variant.name}: memory vector constraint drift")
    alignment = form.source.address_alignments
    if len(alignment) != 1 or alignment[0].address_field_ids != (address.name,) or alignment[0].type_field_id != type_field.name or bool(alignment[0].vector_field_id) != vector:
        raise GenerationError(f"{instruction.opcode}/{form.variant.name}: memory alignment constraint drift")
    state_field = source_fields.get("state_space") if explicit else None
    if explicit and state_field is None:
        raise GenerationError(f"{instruction.opcode}/{form.variant.name}: missing state-space field")
    return _MemoryForm(name, instruction.opcode, data.name, address.name,
                       type_field.name, semantics.name, scope.name, cache.name,
                       state_field.name if state_field else None,
                       source_fields["vector"].name if vector else None,
                       mmio.name if "mmio" in modifiers and mmio else None)
