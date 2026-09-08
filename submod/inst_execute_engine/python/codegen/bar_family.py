"""Derive collective barrier preparation from the projected frontend model."""

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

from ptxsim_codegen.exec_ir.cpp_names import instruction_cpp_name, layout_cpp_name
from ptxsim_codegen.exec_ir.model import GenerationError, ProjectedForm, ProjectedInstruction


_SCALAR_SHAPES = (ResolvedOperandShape.REGISTER, ResolvedOperandShape.IMMEDIATE)
_REDUCTION_PROTOCOLS = {
    ".popc": "ReducePopc",
    ".and": "ReduceAnd",
    ".or": "ReduceOr",
}


@dataclass(frozen=True)
class _BarOperand:
    """One scalar projected operand, including whether an immediate needs wrapping."""

    name: str
    wrap_scalar: bool


@dataclass(frozen=True)
class _BarDestination:
    """One result slot retained by a CTA reduction until collective commit."""

    name: str
    predicate: bool


@dataclass(frozen=True)
class _BarLayout:
    """One Bar layout with roles resolved into the collective helper contract."""

    cpp_name: str
    barrier: _BarOperand
    count: _BarOperand | None
    input: str | None
    destination: _BarDestination | None


@dataclass(frozen=True)
class _BarForm:
    """One projected Bar form and its decoded collective protocol."""

    cpp_name: str
    protocol: str | None
    waits: bool
    aggregate: bool
    layouts: tuple[_BarLayout, ...]

    @property
    def function_name(self) -> str:
        """Return the stable private adapter spelling for this projected form."""
        return "prepare_bar_" + re.sub(
            r"(?<=[a-z0-9])(?=[A-Z])", "_", self.cpp_name
        ).lower()

    @property
    def warp_sync(self) -> bool:
        """Return whether this form is the warp rendezvous rather than a CTA barrier."""
        return self.protocol is None

    @property
    def validates_predicate_destination(self) -> bool:
        """Return whether validation must reject a negated predicate destination."""
        return any(
            layout.destination is not None and layout.destination.predicate
            for layout in self.layouts
        )


@dataclass(frozen=True)
class BarOperation:
    """Every projected Bar form routed through the collective preparation family."""

    opcode: str
    cpp_name: str
    forms: tuple[_BarForm, ...]


@dataclass(frozen=True)
class _ControlOperation:
    """One zero-resource control operation selected from the projected model."""

    opcode: str
    cpp_name: str
    form_cpp_name: str
    target: str | None


def bar_operation(instruction: ProjectedInstruction) -> BarOperation:
    """Return every collective Bar adapter supported by the current projection."""
    if instruction.opcode != "bar":
        raise GenerationError(f"{instruction.opcode}: not a Bar opcode")
    forms = tuple(_bar_form(instruction, form) for form in instruction.forms)
    if not forms:
        raise GenerationError("bar: no projected forms")
    if sum(form.warp_sync for form in forms) != 1:
        raise GenerationError("bar: expected exactly one projected warp-sync form")
    return BarOperation(instruction.opcode, instruction_cpp_name(instruction.opcode), forms)


def bra_operation(instruction: ProjectedInstruction) -> _ControlOperation:
    """Derive the sole direct branch form without duplicating its field spelling."""
    return _control_operation(instruction, "bra", control=True)


def exit_operation(instruction: ProjectedInstruction) -> _ControlOperation:
    """Derive the sole bare exit form without retaining a parallel form table."""
    return _control_operation(instruction, "exit", control=False)


def _control_operation(instruction: ProjectedInstruction, opcode: str, *, control: bool) -> _ControlOperation:
    """Validate one resource-free control family from its operand access contract."""
    if instruction.opcode != opcode:
        raise GenerationError(f"{instruction.opcode}: not a {opcode} opcode")
    if len(instruction.forms) != 1:
        raise GenerationError(f"{opcode}: expected exactly one projected control form")
    form = instruction.forms[0]
    if len(form.layouts) != 1 or len(form.source.operand_layouts) != 1:
        raise GenerationError(f"{opcode}/{form.variant.name}: requires one flat layout")
    source_layout = form.layouts[0]
    layout = form.source.operand_layouts[0]
    _validate_layout_identity(instruction, form, layout, source_layout)
    if (form.source.memory_consistency is not None or form.source.address_alignments
            or form.source.memory_vector is not None or form.source.immediate_value is not None
            or form.source.immediate_ranges or form.source.immediate_multiple_of is not None):
        raise GenerationError(f"{opcode}/{form.variant.name}: cannot stage non-control effects")
    if control:
        _validate_branch_modifiers(form)
    elif form.variant.modifiers or form.source.modifier_fields:
        raise GenerationError(f"{opcode}/{form.variant.name}: bare exit must not have modifiers")
    if control:
        if len(layout.fields) != 1:
            raise GenerationError(f"{opcode}/{form.variant.name}: direct branch requires one target")
        field = layout.fields[0]
        operand = source_layout.operands[0]
        binding = layout.bindings[0]
        if (field.operand_access is not ResolvedOperandAccess.CONTROL
                or field.allowed_operand_shapes != (ResolvedOperandShape.BRANCH_TARGET,)
                or operand.role != "label"
                or binding.type_expression.kind is not ResolvedOperandTypeExpressionKind.NONE):
            raise GenerationError(f"{opcode}/{form.variant.name}: branch target topology drift")
        target = field.name
    else:
        if layout.fields or layout.bindings or source_layout.operands:
            raise GenerationError(f"{opcode}/{form.variant.name}: bare exit must not have operands")
        target = None
    return _ControlOperation(opcode, instruction_cpp_name(opcode), form.source.cpp_name, target)


def _bar_form(instruction: ProjectedInstruction, form: ProjectedForm) -> _BarForm:
    """Decode one Bar form from fixed modifier tokens and operand roles."""
    if len(form.layouts) != len(form.source.operand_layouts) or not form.layouts:
        raise GenerationError(f"bar/{form.variant.name}: layout count drift")
    if (form.source.memory_consistency is not None or form.source.address_alignments
            or form.source.memory_vector is not None or form.source.immediate_value is not None
            or form.source.immediate_ranges or form.source.immediate_multiple_of is not None):
        raise GenerationError(f"bar/{form.variant.name}: collective effects cannot carry unrelated constraints")
    modifiers = {modifier.name: modifier for modifier in form.variant.modifiers}
    if len(modifiers) != len(form.variant.modifiers):
        raise GenerationError(f"bar/{form.variant.name}: duplicate modifier name")
    active = {
        modifier.name: modifier
        for modifier in modifiers.values()
        if modifier.presence != "absent"
    }
    allowed = {"cta", "warp", "sync", "arrive", "red", "reduction", "result_type"}
    if set(modifiers) - allowed:
        raise GenerationError(f"bar/{form.variant.name}: unsupported modifier topology")
    for modifier in active.values():
        if modifier.presence != "fixed":
            raise GenerationError(f"bar/{form.variant.name}: collective modifiers must be fixed")
    _validate_fixed_modifier_projection(form, active)
    warp = "warp" in active
    if warp:
        if (set(active) != {"warp", "sync"} or active["warp"].token != ".warp"
                or active["sync"].token != ".sync"):
            raise GenerationError(f"bar/{form.variant.name}: invalid warp-sync modifiers")
        protocol = None
        waits = True
    elif "sync" in active:
        if (set(active) - {"cta", "sync"} or active["sync"].token != ".sync"
                or ("cta" in active and active["cta"].token != ".cta")):
            raise GenerationError(f"bar/{form.variant.name}: invalid CTA sync modifiers")
        protocol = "SyncArrive"
        waits = True
    elif "arrive" in active:
        if (set(active) - {"cta", "arrive"} or active["arrive"].token != ".arrive"
                or ("cta" in active and active["cta"].token != ".cta")):
            raise GenerationError(f"bar/{form.variant.name}: invalid CTA arrive modifiers")
        protocol = "SyncArrive"
        waits = False
    elif "red" in active:
        reduction = active.get("reduction")
        if (set(active) - {"cta", "red", "reduction", "result_type"}
                or reduction is None or "result_type" not in active
                or reduction.token not in _REDUCTION_PROTOCOLS
                or active["red"].token != ".red"
                or ("cta" in active and active["cta"].token != ".cta")):
            raise GenerationError(f"bar/{form.variant.name}: invalid CTA reduction modifiers")
        protocol = _REDUCTION_PROTOCOLS[reduction.token]
        waits = True
    else:
        raise GenerationError(f"bar/{form.variant.name}: no supported collective modifier")
    layouts = tuple(
        _bar_layout(instruction, form, layout, source_layout, protocol)
        for layout, source_layout in zip(
            form.source.operand_layouts, form.layouts, strict=True
        )
    )
    if protocol == "SyncArrive" and not waits and any(layout.count is None for layout in layouts):
        raise GenerationError(f"bar/{form.variant.name}: arrive requires an explicit thread count")
    aggregate = len(layouts) != 1
    return _BarForm(form.source.cpp_name, protocol, waits, aggregate, layouts)


def _bar_layout(instruction: ProjectedInstruction, form: ProjectedForm, layout, source_layout,
                protocol: str | None) -> _BarLayout:
    """Derive one flat or aggregate Bar layout from its operand roles and bindings."""
    _validate_layout_identity(instruction, form, layout, source_layout)
    fields = {field.source_name: field for field in layout.fields}
    bindings = {binding.target_field_id: binding for binding in layout.bindings}
    operands = {operand.name: operand for operand in source_layout.operands}
    if len(fields) != len(layout.fields) or len(operands) != len(source_layout.operands):
        raise GenerationError(f"bar/{form.variant.name}/{layout.layout_id}: duplicate operand identity")

    def find(role: str, access: ResolvedOperandAccess):
        matches = [
            (operand, fields[operand.name], bindings[fields[operand.name].name])
            for operand in source_layout.operands
            if operand.role == role and fields[operand.name].operand_access is access
        ]
        if len(matches) > 1:
            raise GenerationError(f"bar/{form.variant.name}/{layout.layout_id}: duplicate {role} role")
        return matches[0] if matches else None

    if protocol is None:
        if len(layout.fields) != 1:
            raise GenerationError(f"bar/{form.variant.name}/{layout.layout_id}: warp sync requires one member mask")
        operand = source_layout.operands[0]
        field = layout.fields[0]
        binding = bindings[field.name]
        if (field.operand_access is not ResolvedOperandAccess.READ
                or not _is_u32_scalar(field, binding)
                or operand.role not in {"src", "membermask"}):
            raise GenerationError(f"bar/{form.variant.name}/{layout.layout_id}: warp member mask drift")
        return _BarLayout(layout_cpp_name(source_layout.name),
                          _BarOperand(field.name, field.allowed_operand_shapes == (ResolvedOperandShape.IMMEDIATE,)),
                          None, None, None)

    barrier = find("barrier", ResolvedOperandAccess.READ)
    if barrier is None or not _is_u32_scalar(barrier[1], barrier[2]):
        raise GenerationError(f"bar/{form.variant.name}/{layout.layout_id}: missing u32 barrier")
    count = find("thread_count", ResolvedOperandAccess.READ)
    if count is not None and not _is_u32_scalar(count[1], count[2]):
        raise GenerationError(f"bar/{form.variant.name}/{layout.layout_id}: invalid thread count")
    if protocol == "SyncArrive":
        if len(layout.fields) != 1 + (count is not None):
            raise GenerationError(f"bar/{form.variant.name}/{layout.layout_id}: sync/arrive operand drift")
        reduction_input = None
        destination = None
    else:
        predicate = find("predicate", ResolvedOperandAccess.READ)
        destination_match = next(
            ((operand, field, bindings[field.name])
             for operand, field in zip(source_layout.operands, layout.fields, strict=True)
             if field.operand_access is ResolvedOperandAccess.WRITE),
            None,
        )
        if (predicate is None or predicate[0].kind != "pred_or_not"
                or not _is_predicate(predicate[1], predicate[2])
                or destination_match is None
                or len(layout.fields) != 3 + (count is not None)):
            raise GenerationError(f"bar/{form.variant.name}/{layout.layout_id}: reduction operand drift")
        _, destination_field, destination_binding = destination_match
        predicate_destination = (destination_match[0].kind == "pred"
                                 and _is_predicate(destination_field, destination_binding))
        register_destination = _is_u32_register(destination_field, destination_binding)
        expected_predicate_destination = protocol in {"ReduceAnd", "ReduceOr"}
        if predicate_destination != expected_predicate_destination or register_destination == expected_predicate_destination:
            raise GenerationError(f"bar/{form.variant.name}/{layout.layout_id}: reduction result drift")
        reduction_input = predicate[1].name
        destination = _BarDestination(destination_field.name, predicate_destination)
    return _BarLayout(
        layout_cpp_name(source_layout.name),
        _BarOperand(barrier[1].name, barrier[1].allowed_operand_shapes == (ResolvedOperandShape.IMMEDIATE,)),
        (_BarOperand(count[1].name, count[1].allowed_operand_shapes == (ResolvedOperandShape.IMMEDIATE,))
         if count is not None else None),
        reduction_input,
        destination,
    )


def _validate_layout_identity(instruction: ProjectedInstruction, form: ProjectedForm, layout,
                              source_layout) -> None:
    """Reject independently changing frontend layouts, fields, or binding targets."""
    if (layout.layout_id != source_layout.name
            or len(layout.fields) != len(layout.bindings)
            or len(layout.fields) != len(source_layout.operands)):
        raise GenerationError(f"{instruction.opcode}/{form.variant.name}: layout identity drift")
    bound = {binding.target_field_id for binding in layout.bindings}
    field_names = {field.name for field in layout.fields}
    if len(bound) != len(layout.bindings) or bound != field_names:
        raise GenerationError(f"{instruction.opcode}/{form.variant.name}/{layout.layout_id}: binding identity drift")
    if tuple(field.source_name for field in layout.fields) != tuple(
            operand.name for operand in source_layout.operands):
        raise GenerationError(f"{instruction.opcode}/{form.variant.name}/{layout.layout_id}: operand identity drift")


def _is_u32_scalar(field, binding) -> bool:
    """Return whether one read role is a same-width u32 scalar source."""
    return (
        field.allowed_operand_shapes in {(ResolvedOperandShape.IMMEDIATE,), _SCALAR_SHAPES}
        and binding.type_expression.kind is ResolvedOperandTypeExpressionKind.FIXED_SCALAR
        and binding.type_expression.scalar_type == "u32"
        and binding.register_width_policy is ResolvedRegisterWidthPolicy.SAME_WIDTH
    )


def _is_u32_register(field, binding) -> bool:
    """Return whether one write role is a same-width u32 register destination."""
    return (
        field.allowed_operand_shapes == (ResolvedOperandShape.REGISTER,)
        and binding.type_expression.kind is ResolvedOperandTypeExpressionKind.FIXED_SCALAR
        and binding.type_expression.scalar_type == "u32"
        and binding.register_width_policy is ResolvedRegisterWidthPolicy.SAME_WIDTH
    )


def _is_predicate(field, binding) -> bool:
    """Return whether one role carries a same-width predicate value."""
    return (
        field.allowed_operand_shapes == (ResolvedOperandShape.PREDICATE,)
        and binding.type_expression.kind is ResolvedOperandTypeExpressionKind.FIXED_SCALAR
        and binding.type_expression.scalar_type == "pred"
        and binding.register_width_policy is ResolvedRegisterWidthPolicy.SAME_WIDTH
    )


def _validate_fixed_modifier_projection(form: ProjectedForm, active: dict) -> None:
    """Require each active fixed Bar token to have its matching projected field."""
    source_fields = {field.source_name: field for field in form.source.modifier_fields}
    for name, modifier in active.items():
        source = source_fields.get(name)
        if (source is None or source.storage is not ResolvedFieldStorage.STATIC_CONSTANT
                or source.constant_value != modifier.value):
            raise GenerationError(f"bar/{form.variant.name}/{name}: fixed modifier projection drift")
    if "result_type" in active:
        reduction = active.get("reduction")
        expected = "u32" if reduction is not None and reduction.token == ".popc" else "pred"
        if active["result_type"].value != expected:
            raise GenerationError(f"bar/{form.variant.name}: reduction result type drift")


def _validate_branch_modifiers(form: ProjectedForm) -> None:
    """Constrain direct branches to the projected optional uniformity flag."""
    if len(form.variant.modifiers) != 1:
        raise GenerationError(f"bra/{form.variant.name}: unsupported branch modifiers")
    modifier = form.variant.modifiers[0]
    if (modifier.name != "uni" or modifier.kind != "flag"
            or modifier.presence != "optional" or modifier.token != ".uni"):
        raise GenerationError(f"bra/{form.variant.name}: branch uniformity modifier drift")
    fields = {field.source_name: field for field in form.source.modifier_fields}
    source = fields.get("uni")
    if (source is None or source.storage is not ResolvedFieldStorage.INSTANCE
            or source.value_cpp_type != "bool"):
        raise GenerationError(f"bra/{form.variant.name}: branch uniformity field drift")
