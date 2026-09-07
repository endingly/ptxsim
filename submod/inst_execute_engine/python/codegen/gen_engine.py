"""Emit the private ValueALU preparation path from projected frontend records."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import re

from jinja2 import Environment, PackageLoader, StrictUndefined

from ptx_frontend.ir.resolved_ir import (
    ResolvedFieldStorage,
    ResolvedOperandAccess,
    ResolvedOperandShape,
    ResolvedOperandTypeExpressionKind,
    ResolvedRegisterWidthPolicy,
)

from ptxsim_codegen.exec_ir.model import GenerationError, ProjectedForm, ProjectedInstruction
from ptxsim_codegen.exec_ir.cpp_names import instruction_cpp_name


_CODECS = {
    "u16": "std::uint16_t", "u32": "std::uint32_t", "u64": "std::uint64_t",
    "s16": "std::int16_t", "s32": "std::int32_t", "s64": "std::int64_t",
    "f16": "arith::float16_t", "f16x2": "arith::float16x2_t",
    "f32": "arith::float32_t", "f32x2": "arith::float32x2_t",
    "f64": "arith::float64_t", "bf16": "arith::bfloat16_t",
    "bf16x2": "arith::bfloat16x2_t", "u8x4": "u8x4_t",
    "u16x2": "u16x2_t", "s8x4": "s8x4_t", "s16x2": "s16x2_t",
}


_TEMPLATES = Environment(
    loader=PackageLoader("ptxsim_codegen.inst_execute_engine"),
    undefined=StrictUndefined,
    autoescape=False,
    keep_trailing_newline=True,
    newline_sequence="\n",
)


@dataclass(frozen=True)
class _Operand:
    """One model-derived ValueALU field and its type-selector field."""

    name: str
    type_field: str | None
    fixed_type: str | None


@dataclass(frozen=True)
class _TypePath:
    """C++ value types for one fully resolved destination-and-source path."""

    destination: str
    sources: tuple[str, str]


@dataclass(frozen=True)
class _DynamicTypeCase:
    """One runtime type-selector value and its resolved C++ type path."""

    value: str
    type_path: _TypePath


@dataclass(frozen=True)
class _DynamicTypeSelector:
    """The sole runtime type field accepted by a ValueALU form."""

    field: str
    cases: tuple[_DynamicTypeCase, ...]


@dataclass(frozen=True)
class _ValueAluForm:
    """A projected one-destination, two-source ValueALU form."""

    cpp_name: str
    operation: str
    destination: _Operand
    sources: tuple[_Operand, _Operand]
    type_path: _TypePath | None
    dynamic_type_selector: _DynamicTypeSelector | None

    @property
    def function_name(self) -> str:
        """Return the stable private adapter spelling for this resolved form."""
        return _function_name(self.operation, self)


@dataclass(frozen=True)
class _ValueAluOperation:
    """One enabled operation and its projected ValueALU forms."""

    opcode: str
    cpp_name: str
    forms: tuple[_ValueAluForm, ...]


def _value_alu_forms(instruction: ProjectedInstruction) -> tuple[_ValueAluForm, ...]:
    """Derive every ValueALU adapter from one projected instruction record."""
    forms = tuple(_value_alu_form(instruction, form) for form in instruction.forms)
    if not forms:
        raise GenerationError(f"{instruction.opcode}: no projected forms")
    return forms


def _value_alu_form(instruction: ProjectedInstruction, form: ProjectedForm) -> _ValueAluForm:
    """Reject a projected form that cannot meet the small ValueALU contract."""
    if len(form.source.operand_layouts) != 1:
        raise GenerationError(f"{instruction.opcode}/{form.variant.name}: ValueALU requires one layout")
    if (form.source.memory_consistency is not None or form.source.address_alignments
            or form.source.memory_vector is not None or form.source.immediate_value is not None
            or form.source.immediate_ranges or form.source.immediate_multiple_of is not None):
        raise GenerationError(f"{instruction.opcode}/{form.variant.name}: ValueALU cannot stage non-value effects")
    layout = form.source.operand_layouts[0]
    if len(layout.fields) != 3:
        raise GenerationError(f"{instruction.opcode}/{form.variant.name}/{layout.layout_id}: ValueALU requires exactly three operands")
    if len(layout.fields) != len(layout.bindings):
        raise GenerationError(f"{instruction.opcode}/{form.variant.name}/{layout.layout_id}: field/binding count drift")
    bindings = {binding.target_field_id: binding for binding in layout.bindings}
    if len(bindings) != len(layout.bindings) or set(bindings) != {field.name for field in layout.fields}:
        raise GenerationError(f"{instruction.opcode}/{form.variant.name}/{layout.layout_id}: field/binding identity drift")
    typed_fields: list[tuple[ResolvedOperandAccess, _Operand]] = []
    for field in layout.fields:
        binding = bindings[field.name]
        expression = binding.type_expression
        if field.operand_access is None:
            raise GenerationError(f"{instruction.opcode}/{form.variant.name}/{field.name}: missing operand access")
        if field.operand_access not in {
            ResolvedOperandAccess.READ,
            ResolvedOperandAccess.WRITE,
        }:
            raise GenerationError(f"{instruction.opcode}/{form.variant.name}/{field.name}: ValueALU requires read or write access")
        if binding.register_width_policy not in {
            ResolvedRegisterWidthPolicy.SAME_WIDTH,
            ResolvedRegisterWidthPolicy.EXACT,
        }:
            raise GenerationError(f"{instruction.opcode}/{form.variant.name}/{field.name}: ValueALU requires exact or same-width registers")
        if expression.kind is ResolvedOperandTypeExpressionKind.MODIFIER_FIELD:
            if expression.modifier_field_id is None:
                raise GenerationError(f"{instruction.opcode}/{form.variant.name}/{field.name}: missing operand type field")
            operand = _Operand(field.name, expression.modifier_field_id, None)
        elif expression.kind is ResolvedOperandTypeExpressionKind.FIXED_SCALAR:
            if expression.scalar_type not in _CODECS:
                raise GenerationError(f"{instruction.opcode}/{form.variant.name}/{field.name}: unsupported fixed ValueALU type")
            operand = _Operand(field.name, None, expression.scalar_type)
        else:
            raise GenerationError(f"{instruction.opcode}/{form.variant.name}/{field.name}: unsupported ValueALU operand type expression")
        typed_fields.append((field.operand_access, operand))
    reads = tuple(item for access, item in typed_fields if access is ResolvedOperandAccess.READ)
    writes = tuple(item for access, item in typed_fields if access is ResolvedOperandAccess.WRITE)
    if len(reads) != 2 or len(writes) != 1:
        raise GenerationError(f"{instruction.opcode}/{form.variant.name}/{layout.layout_id}: ValueALU requires exactly two reads and one write")
    fields = {field.name: field for field in layout.fields}
    if fields[writes[0].name].allowed_operand_shapes != (ResolvedOperandShape.REGISTER,):
        raise GenerationError(f"{instruction.opcode}/{form.variant.name}/{writes[0].name}: ValueALU destination must be one register")
    supported_source_shapes = {
        (ResolvedOperandShape.REGISTER,),
        (ResolvedOperandShape.REGISTER, ResolvedOperandShape.IMMEDIATE),
    }
    if any(fields[source.name].allowed_operand_shapes not in supported_source_shapes for source in reads):
        raise GenerationError(f"{instruction.opcode}/{form.variant.name}: ValueALU source shape is unsupported")
    source_fields = {field.name: field for field in form.source.modifier_fields}
    type_fields = {
        operand.type_field for operand in (*reads, writes[0])
        if operand.type_field is not None
    }
    if not type_fields <= source_fields.keys():
        raise GenerationError(f"{instruction.opcode}/{form.variant.name}: operand refers to missing type field")
    type_modifier_names = {source_fields[field].source_name for field in type_fields}
    operands = (*reads, writes[0])
    fixed_operand_types = all(operand.fixed_type is not None for operand in operands)
    unsupported_modifiers = {
        modifier.name
        for modifier in form.variant.modifiers
        if modifier.presence != "absent"
        and modifier.name not in (type_modifier_names | {"rounding", "ftz", "sat"})
        and not (modifier.presence == "fixed" and (
            modifier.name in {"lo", "hi", "wide"}
            or (modifier.name == "type" and fixed_operand_types)
        ))
    }
    if unsupported_modifiers:
        raise GenerationError(
            f"{instruction.opcode}/{form.variant.name}: unsupported ValueALU controls "
            f"{sorted(unsupported_modifiers)}"
        )
    modifier_values = {
        modifier.name: tuple(str(value.value) for value in modifier.values)
        for modifier in form.variant.modifiers
    }
    dynamic_types = []
    static_types = {}
    for field in sorted(type_fields):
        source_field = source_fields[field]
        if source_field.storage is ResolvedFieldStorage.STATIC_CONSTANT:
            if not isinstance(source_field.constant_value, str) or source_field.constant_value not in _CODECS:
                raise GenerationError(f"{instruction.opcode}/{form.variant.name}/{field}: unsupported static ValueALU type")
            static_types[field] = source_field.constant_value
            continue
        values = modifier_values.get(source_field.source_name, ())
        if not values or any(value not in _CODECS for value in values):
            raise GenerationError(f"{instruction.opcode}/{form.variant.name}/{field}: unsupported dynamic ValueALU type")
        dynamic_types.append((field, values))
    if any(not values for _, values in dynamic_types):
        raise GenerationError(f"{instruction.opcode}/{form.variant.name}: dynamic type has no values")
    if len(dynamic_types) > 1:
        raise GenerationError(f"{instruction.opcode}::{form.source.cpp_name}: multiple dynamic type fields")
    if not dynamic_types:
        return _ValueAluForm(
            form.source.cpp_name,
            instruction.opcode,
            writes[0],
            (reads[0], reads[1]),
            _type_path(writes[0], (reads[0], reads[1]), static_types),
            None,
        )
    field, values = dynamic_types[0]
    return _ValueAluForm(
        form.source.cpp_name,
        instruction.opcode,
        writes[0],
        (reads[0], reads[1]),
        None,
        _DynamicTypeSelector(
            field,
            tuple(
                _DynamicTypeCase(
                    value,
                    _type_path(writes[0], (reads[0], reads[1]), {**static_types, field: value}),
                )
                for value in values
            ),
        ),
    )


def _function_name(operation: str, form: _ValueAluForm) -> str:
    """Return the stable private adapter spelling for one resolved form."""
    return f"prepare_{operation}_" + re.sub(
        r"(?<=[a-z0-9])(?=[A-Z])", "_", form.cpp_name
    ).lower()


def _type_path(destination: _Operand, sources: tuple[_Operand, _Operand], selected: dict[str, str]) -> _TypePath:
    """Resolve projected selector values into one C++ ValueALU type path."""
    try:
        return _TypePath(
            _operand_codec(destination, selected),
            (
                _operand_codec(sources[0], selected),
                _operand_codec(sources[1], selected),
            ),
        )
    except KeyError as error:
        raise GenerationError(f"ValueALU: cannot derive type field {error.args[0]!r}") from error


def _operand_codec(operand: _Operand, selected: dict[str, str]) -> str:
    """Return one operand's codec from its fixed or modifier-selected expression."""
    if operand.fixed_type is not None:
        return _CODECS[operand.fixed_type]
    if operand.type_field is None:
        raise GenerationError(f"ValueALU: operand {operand.name} has no type expression")
    return _CODECS[selected[operand.type_field]]


def artifacts(projected: tuple[ProjectedInstruction, ...], header_path: Path) -> tuple[str, str]:
    """Return generated artifacts for every enabled ValueALU operation."""
    bindings = {"add": _value_alu_forms, "sub": _value_alu_forms, "mul": _value_alu_forms}
    projected_by_opcode = {instruction.opcode: instruction for instruction in projected}
    operations = []
    for opcode, bind in bindings.items():
        instruction = projected_by_opcode.get(opcode)
        if instruction is None:
            raise GenerationError(f"projected frontend has no {opcode} instruction")
        operations.append(
            _ValueAluOperation(opcode, instruction_cpp_name(opcode), bind(instruction))
        )
    return (
        _TEMPLATES.get_template("instruction_preparation.hpp.j2").render(
            operations=operations
        ),
        _TEMPLATES.get_template("instruction_preparation.cpp.j2").render(
            operations=operations,
            header_name=header_path.name,
        ),
    )
