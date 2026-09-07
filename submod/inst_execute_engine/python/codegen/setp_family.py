"""Derive the bounded predicate-comparison execution family from projection."""

from __future__ import annotations

from dataclasses import dataclass
import re

from ptx_frontend.ir.resolved_ir import (
    ResolvedFieldStorage,
    ResolvedOperandAccess,
    ResolvedOperandShape,
    ResolvedOperandTypeExpressionKind,
    ResolvedRegisterWidthPolicy,
)

from ptxsim_codegen.exec_ir.cpp_names import instruction_cpp_name
from ptxsim_codegen.exec_ir.model import GenerationError, ProjectedForm, ProjectedInstruction


_CODECS = {"u32": "std::uint32_t", "s32": "std::int32_t"}
_PREDICATE_KINDS = {"pred", "pred_or_not"}
_CONTROL_VALUES = {
    "ComparisonOperator": {"eq", "lt", "ge"},
    "BooleanOperator": {"and"},
}
_CONTROL_CPP_VALUES = {
    "ComparisonOperator": {"eq": "eq", "lt": "lt", "ge": "ge"},
    "BooleanOperator": {"and": "and_"},
}


@dataclass(frozen=True)
class _SetpOperand:
    """One projected operand selected for the predicate-comparison adapter."""

    name: str
    scalar_type: str


@dataclass(frozen=True)
class _SetpPredicate:
    """One projected predicate operand and whether its inversion is legal."""

    name: str
    allows_negation: bool


@dataclass(frozen=True)
class _SetpControl:
    """One instance modifier whose projection supplies an enum allowlist."""

    field: str
    values: tuple[str, ...]
    cpp_values: tuple[str, ...]


@dataclass(frozen=True)
class _SetpForm:
    """A projected scalar predicate comparison with one or two destinations."""

    cpp_name: str
    operation: str
    destination: _SetpPredicate
    pair_destination: bool
    sources: tuple[_SetpOperand, _SetpOperand]
    combine: _SetpPredicate | None
    comparison: _SetpControl
    boolean: _SetpControl | None

    @property
    def function_name(self) -> str:
        """Return the stable private adapter spelling for this form."""
        return f"prepare_{self.operation}_" + re.sub(
            r"(?<=[a-z0-9])(?=[A-Z])", "_", self.cpp_name
        ).lower()


@dataclass(frozen=True)
class SetpOperation:
    """The complete bounded Setp family selected from frontend projection."""

    opcode: str
    cpp_name: str
    forms: tuple[_SetpForm, ...]


def setp_operation(instruction: ProjectedInstruction) -> SetpOperation:
    """Return the predicate-comparison adapters supported by this projection."""
    forms = tuple(_setp_form(instruction, form) for form in instruction.forms)
    if not forms:
        raise GenerationError(f"{instruction.opcode}: no projected forms")
    return SetpOperation(instruction.opcode, instruction_cpp_name(instruction.opcode), forms)


def _setp_form(instruction: ProjectedInstruction, form: ProjectedForm) -> _SetpForm:
    """Validate and derive one fixed-width predicate-comparison form."""
    if len(form.source.operand_layouts) != 1 or len(form.layouts) != 1:
        raise GenerationError(f"{instruction.opcode}/{form.variant.name}: Setp requires one layout")
    if (form.source.memory_consistency is not None or form.source.address_alignments
            or form.source.memory_vector is not None or form.source.immediate_value is not None
            or form.source.immediate_ranges or form.source.immediate_multiple_of is not None):
        raise GenerationError(f"{instruction.opcode}/{form.variant.name}: Setp cannot stage non-register effects")
    layout = form.source.operand_layouts[0]
    source_layout = form.layouts[0]
    if layout.layout_id != source_layout.name:
        raise GenerationError(f"{instruction.opcode}/{form.variant.name}: Setp layout identity drift")
    if len(layout.fields) != len(layout.bindings) or len(layout.fields) != len(source_layout.operands):
        raise GenerationError(f"{instruction.opcode}/{form.variant.name}/{layout.layout_id}: Setp field count drift")
    bindings = {binding.target_field_id: binding for binding in layout.bindings}
    source_operands = {operand.name: operand for operand in source_layout.operands}
    if (len(bindings) != len(layout.bindings) or len(source_operands) != len(source_layout.operands)
            or {field.name for field in layout.fields} != set(bindings)
            or {field.source_name for field in layout.fields} != set(source_operands)):
        raise GenerationError(f"{instruction.opcode}/{form.variant.name}/{layout.layout_id}: Setp field identity drift")

    numeric_reads: list[_SetpOperand] = []
    predicate_read: _SetpPredicate | None = None
    destination: _SetpPredicate | None = None
    pair_destination = False
    for field in layout.fields:
        binding = bindings[field.name]
        source = source_operands[field.source_name]
        if field.operand_access not in {ResolvedOperandAccess.READ, ResolvedOperandAccess.WRITE}:
            raise GenerationError(f"{instruction.opcode}/{form.variant.name}/{field.name}: Setp requires read or write access")
        if binding.register_width_policy not in {ResolvedRegisterWidthPolicy.SAME_WIDTH, ResolvedRegisterWidthPolicy.EXACT}:
            raise GenerationError(f"{instruction.opcode}/{form.variant.name}/{field.name}: unsupported Setp width policy")
        if binding.type_expression.kind is not ResolvedOperandTypeExpressionKind.FIXED_SCALAR:
            raise GenerationError(f"{instruction.opcode}/{form.variant.name}/{field.name}: Setp requires fixed scalar types")
        scalar_type = binding.type_expression.scalar_type
        if field.operand_access is ResolvedOperandAccess.WRITE:
            expected_shape = {
                "pred": (ResolvedOperandShape.PREDICATE,),
                "pred_pair": (ResolvedOperandShape.PREDICATE_PAIR,),
            }.get(source.kind)
            if scalar_type != "pred" or expected_shape is None:
                raise GenerationError(f"{instruction.opcode}/{form.variant.name}/{field.name}: unsupported Setp destination")
            if field.allowed_operand_shapes != expected_shape:
                raise GenerationError(f"{instruction.opcode}/{form.variant.name}/{field.name}: unsupported Setp destination shape")
            if destination is not None:
                raise GenerationError(f"{instruction.opcode}/{form.variant.name}: Setp requires one destination field")
            destination = _SetpPredicate(field.name, False)
            pair_destination = source.kind == "pred_pair"
        elif scalar_type == "pred":
            if source.kind not in _PREDICATE_KINDS or field.allowed_operand_shapes != (ResolvedOperandShape.PREDICATE,):
                raise GenerationError(f"{instruction.opcode}/{form.variant.name}/{field.name}: unsupported Setp predicate source")
            if predicate_read is not None:
                raise GenerationError(f"{instruction.opcode}/{form.variant.name}: Setp permits one predicate source")
            predicate_read = _SetpPredicate(field.name, source.kind == "pred_or_not")
        else:
            if scalar_type not in _CODECS or source.kind != "reg_or_imm":
                raise GenerationError(f"{instruction.opcode}/{form.variant.name}/{field.name}: unsupported Setp comparison source")
            if field.allowed_operand_shapes != (ResolvedOperandShape.REGISTER, ResolvedOperandShape.IMMEDIATE):
                raise GenerationError(f"{instruction.opcode}/{form.variant.name}/{field.name}: unsupported Setp source shape")
            numeric_reads.append(_SetpOperand(field.name, scalar_type))
    if destination is None or len(numeric_reads) != 2 or numeric_reads[0].scalar_type != numeric_reads[1].scalar_type:
        raise GenerationError(f"{instruction.opcode}/{form.variant.name}: Setp requires one predicate destination and two same-type sources")

    controls = _controls(instruction, form)
    comparison = controls.get("ComparisonOperator")
    boolean = controls.get("BooleanOperator")
    if comparison is None or len(controls) != 1 + (boolean is not None):
        raise GenerationError(f"{instruction.opcode}/{form.variant.name}: unsupported Setp modifiers")
    if (predicate_read is None) != (boolean is None):
        raise GenerationError(f"{instruction.opcode}/{form.variant.name}: Setp predicate/boolean topology drift")
    return _SetpForm(form.source.cpp_name, instruction.opcode, destination,
                     pair_destination, (numeric_reads[0], numeric_reads[1]),
                     predicate_read, comparison, boolean)


def _controls(instruction: ProjectedInstruction, form: ProjectedForm) -> dict[str, _SetpControl]:
    """Derive dynamic comparison controls and reject unrelated modifiers."""
    source_fields = {field.source_name: field for field in form.source.modifier_fields}
    controls: dict[str, _SetpControl] = {}
    for modifier in form.variant.modifiers:
        if modifier.presence == "absent":
            continue
        field = source_fields.get(modifier.name)
        if field is None:
            raise GenerationError(f"{instruction.opcode}/{form.variant.name}/{modifier.name}: missing Setp modifier field")
        if field.storage is ResolvedFieldStorage.STATIC_CONSTANT:
            if modifier.name != "type" or not isinstance(field.constant_value, str):
                raise GenerationError(f"{instruction.opcode}/{form.variant.name}/{modifier.name}: unsupported static Setp modifier")
            continue
        if field.value_cpp_type not in {"ComparisonOperator", "BooleanOperator"}:
            raise GenerationError(f"{instruction.opcode}/{form.variant.name}/{modifier.name}: unsupported Setp control")
        values = tuple(str(value.value) for value in modifier.values)
        if (not values or field.value_cpp_type in controls
                or not set(values) <= _CONTROL_VALUES[field.value_cpp_type]):
            raise GenerationError(f"{instruction.opcode}/{form.variant.name}/{modifier.name}: invalid Setp control values")
        controls[field.value_cpp_type] = _SetpControl(
            field.name, values,
            tuple(_CONTROL_CPP_VALUES[field.value_cpp_type][value] for value in values),
        )
    return controls
