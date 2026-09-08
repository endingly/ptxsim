"""Derive the raw movement execution family from the projected MOV topology."""

from __future__ import annotations

from dataclasses import dataclass

from ptx_frontend.ir.resolved_ir import (
    ResolvedFieldStorage,
    ResolvedOperandAccess,
    ResolvedOperandShape,
    ResolvedOperandTypeExpressionKind,
    ResolvedRegisterWidthPolicy,
    ResolvedVectorTypePolicy,
)

from ptxsim_codegen.exec_ir.model import GenerationError, ProjectedForm, ProjectedInstruction
from ptxsim_codegen.exec_ir.cpp_names import instruction_cpp_name


_SCALAR_TYPES = (
    "b16", "u16", "s16", "b32", "u32", "s32", "f32", "b64", "u64",
    "s64", "b128", "f64",
)


@dataclass(frozen=True)
class MovLayout:
    """One projected scalar MOV operand layout and its movement direction."""

    cpp_name: str
    kind: str
    destination: str
    source: str


@dataclass(frozen=True)
class MovForm:
    """One projected MOV form with fields consumed by its generated adapter."""

    cpp_name: str
    kind: str
    type_field: str | None
    vector_field: str | None
    destination: str | None
    source: str | None
    layouts: tuple[MovLayout, ...]
    type_values: tuple[str, ...]
    vector_values: tuple[str, ...]

    @property
    def function_name(self) -> str:
        """Return the stable private generated preparation adapter name."""
        return f"prepare_mov_{self.cpp_name.lower()}"


@dataclass(frozen=True)
class MovOperation:
    """The complete projected MOV family routed by this execution generator."""

    opcode: str
    cpp_name: str
    forms: tuple[MovForm, ...]


def _field_map(form: ProjectedForm):
    """Return source modifier fields keyed by generated member identity."""
    return {field.name: field for field in form.source.modifier_fields}


def _require_consumed_modifiers(form: ProjectedForm, consumed: set[str]) -> None:
    """Reject a projected modifier field or active token without movement semantics."""
    fields = _field_map(form)
    if set(fields) != consumed:
        raise GenerationError(
            f"mov/{form.variant.name}: unconsumed modifier fields "
            f"{sorted(set(fields) ^ consumed)}"
        )
    source_names = {field.source_name for field in fields.values()}
    unsupported = {
        modifier.name
        for modifier in form.variant.modifiers
        if modifier.presence != "absent" and modifier.name not in source_names
    }
    if unsupported:
        raise GenerationError(
            f"mov/{form.variant.name}: unconsumed modifiers {sorted(unsupported)}"
        )


def _modifier_values(form: ProjectedForm, field: str) -> tuple[str, ...]:
    """Resolve every projected value for one instance modifier field."""
    source = _field_map(form).get(field)
    if source is None:
        raise GenerationError(f"mov/{form.variant.name}: missing modifier field {field}")
    if source.storage is ResolvedFieldStorage.STATIC_CONSTANT:
        if not isinstance(source.constant_value, str):
            raise GenerationError(f"mov/{form.variant.name}/{field}: non-string static selector")
        return (source.constant_value,)
    modifier = next((item for item in form.variant.modifiers if item.name == source.source_name), None)
    if modifier is None or modifier.presence == "absent":
        raise GenerationError(f"mov/{form.variant.name}/{field}: absent selector")
    return tuple(str(item.value) for item in modifier.values)


def _type_field(form: ProjectedForm, layout) -> str | None:
    """Derive one shared modifier-selected raw type field for a layout."""
    fields = {field.name: field for field in layout.fields}
    bindings = {binding.target_field_id: binding for binding in layout.bindings}
    selected = []
    for field in layout.fields:
        binding = bindings.get(field.name)
        if binding is None:
            raise GenerationError(f"mov/{form.variant.name}/{field.name}: missing binding")
        if binding.type_expression.kind is ResolvedOperandTypeExpressionKind.MODIFIER_FIELD:
            selected.append(binding.type_expression.modifier_field_id)
        elif binding.type_expression.kind is ResolvedOperandTypeExpressionKind.FIXED_SCALAR:
            selected.append(None)
        else:
            raise GenerationError(f"mov/{form.variant.name}/{field.name}: unsupported type expression")
    if len(set(selected)) != 1:
        raise GenerationError(f"mov/{form.variant.name}/{layout.layout_id}: divergent type fields")
    field = selected[0]
    if field is not None and field not in _field_map(form):
        raise GenerationError(f"mov/{form.variant.name}: missing selected type field")
    return field


def _movement_layout(form: ProjectedForm, layout) -> MovLayout:
    """Classify one MOV layout solely from its projected operand facts."""
    if len(layout.fields) != 2 or len(layout.bindings) != 2:
        raise GenerationError(f"mov/{form.variant.name}/{layout.layout_id}: requires two operands")
    destination, source = layout.fields
    bindings = {binding.target_field_id: binding for binding in layout.bindings}
    if set(bindings) != {destination.name, source.name}:
        raise GenerationError(f"mov/{form.variant.name}/{layout.layout_id}: binding identity drift")
    if destination.operand_access is not ResolvedOperandAccess.WRITE or source.operand_access is not ResolvedOperandAccess.READ:
        raise GenerationError(f"mov/{form.variant.name}/{layout.layout_id}: requires write/read ordering")
    destination_binding = bindings[destination.name]
    source_binding = bindings[source.name]
    if (destination_binding.register_width_policy is not ResolvedRegisterWidthPolicy.SAME_WIDTH or
            source_binding.register_width_policy is not ResolvedRegisterWidthPolicy.SAME_WIDTH):
        raise GenerationError(f"mov/{form.variant.name}/{layout.layout_id}: width policy drift")
    destination_shapes = destination.allowed_operand_shapes
    source_shapes = source.allowed_operand_shapes
    if destination_shapes == (ResolvedOperandShape.REGISTER,):
        if source_shapes == (ResolvedOperandShape.VECTOR,):
            if source_binding.allowed_vector_arities != (2, 4) or source_binding.allow_vector_sink:
                raise GenerationError(f"mov/{form.variant.name}/{layout.layout_id}: invalid pack vector contract")
            return MovLayout(layout.cpp_name, "pack", destination.name, source.name)
        scalar_sources = {
            ResolvedOperandShape.REGISTER, ResolvedOperandShape.IMMEDIATE,
            ResolvedOperandShape.SPECIAL_REGISTER, ResolvedOperandShape.SYMBOL,
            ResolvedOperandShape.ADDRESS,
        }
        if set(source_shapes) == scalar_sources:
            return MovLayout(layout.cpp_name, "scalar", destination.name, source.name)
    if destination_shapes == (ResolvedOperandShape.VECTOR,) and source_shapes == (ResolvedOperandShape.REGISTER,):
        if destination_binding.allowed_vector_arities != (2, 4) or not destination_binding.allow_vector_sink:
            raise GenerationError(f"mov/{form.variant.name}/{layout.layout_id}: invalid unpack vector contract")
        return MovLayout(layout.cpp_name, "unpack", destination.name, source.name)
    raise GenerationError(f"mov/{form.variant.name}/{layout.layout_id}: unrecognized movement layout")


def _scalar_form(form: ProjectedForm) -> MovForm:
    """Derive the scalar, pack, and unpack adapters from one projected form."""
    layouts = tuple(_movement_layout(form, layout) for layout in form.source.operand_layouts)
    if len(layouts) != 3 or {layout.kind for layout in layouts} != {"scalar", "pack", "unpack"}:
        raise GenerationError(f"mov/{form.variant.name}: incomplete scalar movement layouts")
    fields = {_type_field(form, layout) for layout in form.source.operand_layouts}
    if len(fields) != 1 or None in fields:
        raise GenerationError(f"mov/{form.variant.name}: scalar layouts need one dynamic type field")
    type_field = next(iter(fields))
    assert type_field is not None
    _require_consumed_modifiers(form, {type_field})
    types = _modifier_values(form, type_field)
    if types != _SCALAR_TYPES:
        raise GenerationError(f"mov/{form.variant.name}: unsupported raw type inventory {types}")
    return MovForm(form.source.cpp_name, "scalar", type_field, None, None, None,
                   layouts, types, ())


def _vector_form(form: ProjectedForm) -> MovForm:
    """Derive the topology vector movement form from its projected operands."""
    if len(form.source.operand_layouts) != 1:
        raise GenerationError(f"mov/{form.variant.name}: vector movement needs one layout")
    layout = form.source.operand_layouts[0]
    if len(layout.fields) != 2 or len(layout.bindings) != 2:
        raise GenerationError(f"mov/{form.variant.name}: malformed vector layout")
    destination, source = layout.fields
    bindings = {binding.target_field_id: binding for binding in layout.bindings}
    if (destination.operand_access is not ResolvedOperandAccess.WRITE or
            source.operand_access is not ResolvedOperandAccess.READ or
            destination.allowed_operand_shapes != (ResolvedOperandShape.VECTOR,) or
            source.allowed_operand_shapes != (ResolvedOperandShape.VECTOR,)):
        raise GenerationError(f"mov/{form.variant.name}: unrecognized vector topology")
    destination_binding = bindings.get(destination.name)
    source_binding = bindings.get(source.name)
    if destination_binding is None or source_binding is None:
        raise GenerationError(f"mov/{form.variant.name}: vector binding identity drift")
    if (destination_binding.register_width_policy is not ResolvedRegisterWidthPolicy.SAME_WIDTH or
            source_binding.register_width_policy is not ResolvedRegisterWidthPolicy.SAME_WIDTH or
            destination_binding.vector_type_policy is not ResolvedVectorTypePolicy.ELEMENT or
            source_binding.vector_type_policy is not ResolvedVectorTypePolicy.ELEMENT):
        raise GenerationError(f"mov/{form.variant.name}: vector movement policy drift")
    if (destination_binding.vector_arity_modifier_field_id is None or
            destination_binding.vector_arity_modifier_field_id != source_binding.vector_arity_modifier_field_id):
        raise GenerationError(f"mov/{form.variant.name}: vector arity selector drift")
    type_field = _type_field(form, layout)
    vector_field = destination_binding.vector_arity_modifier_field_id
    if type_field is None:
        raise GenerationError(f"mov/{form.variant.name}: vector type must be dynamic")
    _require_consumed_modifiers(form, {type_field, vector_field})
    types = _modifier_values(form, type_field)
    vectors = _modifier_values(form, vector_field)
    if types != ("u32",) or vectors != ("v4",):
        raise GenerationError(f"mov/{form.variant.name}: unsupported vector topology selectors")
    return MovForm(form.source.cpp_name, "vector", type_field, vector_field,
                   destination.name, source.name, (), types, vectors)


def _predicate_form(form: ProjectedForm) -> MovForm:
    """Derive the predicate movement form from fixed predicate operand bindings."""
    if len(form.source.operand_layouts) != 1:
        raise GenerationError(f"mov/{form.variant.name}: predicate movement needs one layout")
    layout = form.source.operand_layouts[0]
    if len(layout.fields) != 2 or len(layout.bindings) != 2:
        raise GenerationError(f"mov/{form.variant.name}: malformed predicate layout")
    destination, source = layout.fields
    if (destination.operand_access is not ResolvedOperandAccess.WRITE or
            source.operand_access is not ResolvedOperandAccess.READ or
            destination.allowed_operand_shapes != (ResolvedOperandShape.PREDICATE,) or
            set(source.allowed_operand_shapes) != {ResolvedOperandShape.PREDICATE, ResolvedOperandShape.SPECIAL_REGISTER}):
        raise GenerationError(f"mov/{form.variant.name}: unrecognized predicate topology")
    bindings = {binding.target_field_id: binding for binding in layout.bindings}
    if set(bindings) != {destination.name, source.name}:
        raise GenerationError(f"mov/{form.variant.name}: predicate binding identity drift")
    if any(binding.register_width_policy is not ResolvedRegisterWidthPolicy.SAME_WIDTH
           for binding in bindings.values()):
        raise GenerationError(f"mov/{form.variant.name}: predicate width policy drift")
    if any(binding.type_expression.kind is not ResolvedOperandTypeExpressionKind.FIXED_SCALAR or
           binding.type_expression.scalar_type != "pred"
           for binding in bindings.values()):
        raise GenerationError(f"mov/{form.variant.name}: predicate fixed-type drift")
    if _type_field(form, layout) is not None or _modifier_values(form, "type") != ("pred",):
        raise GenerationError(f"mov/{form.variant.name}: predicate type drift")
    _require_consumed_modifiers(form, {"type"})
    return MovForm(form.source.cpp_name, "predicate", None, None,
                   destination.name, source.name, (), ("pred",), ())


def mov_operation(instruction: ProjectedInstruction) -> MovOperation:
    """Validate and derive every projected MOV form without a variant allowlist."""
    if instruction.opcode != "mov":
        raise GenerationError(f"movement family received {instruction.opcode}")
    forms: list[MovForm] = []
    for form in instruction.forms:
        layouts = form.source.operand_layouts
        if len(layouts) == 3:
            forms.append(_scalar_form(form))
        elif len(layouts) == 1:
            fields = layouts[0].fields
            if fields and fields[0].allowed_operand_shapes == (ResolvedOperandShape.VECTOR,):
                forms.append(_vector_form(form))
            else:
                forms.append(_predicate_form(form))
        else:
            raise GenerationError(f"mov/{form.variant.name}: unrecognized form topology")
    if len(forms) != 3 or {form.kind for form in forms} != {"scalar", "vector", "predicate"}:
        raise GenerationError("mov: incomplete projected movement family")
    return MovOperation("mov", instruction_cpp_name("mov"), tuple(forms))
