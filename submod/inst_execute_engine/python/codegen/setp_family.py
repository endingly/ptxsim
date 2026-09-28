"""Derive integer predicate-comparison execution adapters from frontend forms."""

from __future__ import annotations

from dataclasses import dataclass

from ptx_frontend.ir.resolved_ir import ResolvedOperandAccess, ResolvedOperandShape

from ptxsim_codegen.exec_ir.cpp_names import instruction_cpp_name
from ptxsim_codegen.exec_ir.model import GenerationError, ProjectedForm, ProjectedInstruction


_SUPPORTED = {
    "setp_signed": (("s16", "s32", "s64"), False),
    "setp_signed_boolean": (("s16", "s32", "s64"), True),
    "setp_unsigned": (("u16", "u32", "u64"), False),
    "setp_unsigned_boolean": (("u16", "u32", "u64"), True),
}
_COMPARISONS = {"eq", "ne", "lt", "le", "gt", "ge", "lo", "ls", "hi", "hs"}


@dataclass(frozen=True)
class _SetpForm:
    """One integer SETP variant with single and paired sink-capable layouts."""

    cpp_name: str
    boolean: bool
    type_values: tuple[str, ...]
    comparison_values: tuple[str, ...]

    @property
    def function_name(self) -> str:
        """Return the private preparation adapter's C++ spelling."""
        return f"prepare_setp_{self.cpp_name.lower()}"


@dataclass(frozen=True)
class SetpOperation:
    """Integer SETP adapters; other frontend forms remain executable-IR records."""

    opcode: str
    cpp_name: str
    forms: tuple[_SetpForm, ...]


def _setp_form(form: ProjectedForm) -> _SetpForm:
    """Check the exact integer operand and modifier contract before generation."""
    variant_id = form.variant.name
    expected_types, boolean = _SUPPORTED[variant_id]
    if tuple(layout.name for layout in form.layouts) != ("single", "pair"):
        raise GenerationError(f"setp/{variant_id}: missing single or pair layout")
    expected_names = ("dst", "src1", "src2", "combine") if boolean else ("dst", "src1", "src2")
    for layout, source_layout in zip(form.layouts, form.source.operand_layouts, strict=True):
        if tuple(operand.name for operand in layout.operands) != expected_names:
            raise GenerationError(f"setp/{variant_id}/{layout.name}: operand identity drift")
        expected_kinds = ("pred_or_sink", "reg_or_imm", "reg_or_imm") if layout.name == "single" else ("pred_pair_or_sink", "reg_or_imm", "reg_or_imm")
        if boolean:
            expected_kinds += ("pred_source",)
        if tuple(operand.kind.value for operand in layout.operands) != expected_kinds:
            raise GenerationError(f"setp/{variant_id}/{layout.name}: operand kind drift")
        if tuple(field.source_name for field in source_layout.fields) != expected_names:
            raise GenerationError(f"setp/{variant_id}/{layout.name}: resolved field drift")
        expected_access = (ResolvedOperandAccess.WRITE, ResolvedOperandAccess.READ,
                           ResolvedOperandAccess.READ) + ((ResolvedOperandAccess.READ,) if boolean else ())
        if tuple(field.operand_access for field in source_layout.fields) != expected_access:
            raise GenerationError(f"setp/{variant_id}/{layout.name}: operand access drift")
        destination_shape = (ResolvedOperandShape.PREDICATE,) if layout.name == "single" else (ResolvedOperandShape.PREDICATE_PAIR,)
        if source_layout.fields[0].allowed_operand_shapes != destination_shape:
            raise GenerationError(f"setp/{variant_id}/{layout.name}: destination shape drift")
    modifiers = {modifier.name: modifier for modifier in form.variant.modifiers
                 if modifier.presence.value != "absent"}
    expected_modifiers = {"comparison", "type"} | ({"boolean"} if boolean else set())
    if set(modifiers) != expected_modifiers:
        raise GenerationError(f"setp/{variant_id}: modifier inventory drift")
    types = tuple(value.value for value in modifiers["type"].values)
    if types != expected_types:
        raise GenerationError(f"setp/{variant_id}: integer type inventory drift")
    comparisons = {value.value for value in modifiers["comparison"].values}
    if not comparisons or not comparisons <= _COMPARISONS:
        raise GenerationError(f"setp/{variant_id}: unsupported comparison values")
    if boolean and {value.value for value in modifiers["boolean"].values} != {"and", "or", "xor"}:
        raise GenerationError(f"setp/{variant_id}: unsupported boolean values")
    return _SetpForm(form.source.cpp_name, boolean, types,
                     tuple(value.value for value in modifiers["comparison"].values))


def setp_operation(instruction: ProjectedInstruction) -> SetpOperation:
    """Select all implemented integer forms and require their projected presence."""
    if instruction.opcode != "setp":
        raise GenerationError(f"expected setp, received {instruction.opcode}")
    present = {form.variant.name for form in instruction.forms}
    missing = _SUPPORTED.keys() - present
    if missing:
        raise GenerationError(f"setp: missing integer forms {sorted(missing)}")
    forms = tuple(_setp_form(form) for form in instruction.forms
                  if form.variant.name in _SUPPORTED)
    return SetpOperation("setp", instruction_cpp_name("setp"), forms)
