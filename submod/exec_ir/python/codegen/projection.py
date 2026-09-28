"""Project normalized frontend facts through backend C++ leaf mappings."""

from __future__ import annotations

from pathlib import Path
from functools import cache

import ptx_frontend.code_gen.cpp_backend
import ptx_frontend.ir.resolved_ir
from ptx_frontend.code_gen.model import CodegenUnit
from ptx_frontend.spec.database import CodegenDatabase, load_codegen_database
from ptx_frontend.spec.model import ModifierKind, ModifierPresence, ModifierSpec
from ptx_frontend.spec.resources import packaged_backend_spec, packaged_spec_dir

from .model import (
    BackendSpec,
    BackendValue,
    GenerationError,
    ProjectedForm,
    ProjectedInstruction,
)


@cache
def frontend_backend() -> CodegenUnit:
    """Load the C++ mapping bundled with the installed frontend package."""
    return ptx_frontend.code_gen.cpp_backend.load_cpp_backend(
        packaged_backend_spec()
    )


def database(spec_dir: Path | None = None) -> CodegenDatabase:
    """Load normalized PTX facts from an explicit or packaged specification."""
    return load_codegen_database(
        spec_dir=spec_dir if spec_dir is not None else packaged_spec_dir()
    )


def project_database(
    database: CodegenDatabase,
    backend: BackendSpec,
) -> tuple[ProjectedInstruction, ...]:
    """Validate C++ leaves and return the complete frontend topology."""
    if backend.ptx_spec_schema != database.spec_schema:
        raise GenerationError("frontend spec schema mismatch")

    modifier_kinds = {
        modifier.kind.value
        for instruction in database.instructions
        for variant in instruction.variants
        for modifier in variant.modifiers
        if modifier.presence is not ModifierPresence.ABSENT
    }
    operand_kinds = {
        operand.kind.value
        for instruction in database.instructions
        for variant in instruction.variants
        for layout in variant.operand_layouts
        for operand in layout.operands
    }
    if set(backend.modifier_kinds) != modifier_kinds:
        raise GenerationError("modifier_kinds must exactly map frontend kinds")
    if set(backend.operand_kinds) != operand_kinds:
        raise GenerationError("operand_kinds must exactly map frontend kinds")
    for instruction in database.instructions:
        for variant in instruction.variants:
            for modifier in variant.modifiers:
                if modifier.presence is ModifierPresence.ABSENT:
                    continue
                mapping = backend.modifier_kinds[modifier.kind.value]
                if any(
                    value not in mapping.values
                    for value in _modifier_values(modifier)
                ):
                    raise GenerationError(
                        f"missing C++ value mapping for modifier {modifier.name!r}"
                    )
    projected: list[ProjectedInstruction] = []
    for instruction in database.instructions:
        source = ptx_frontend.ir.resolved_ir.from_instruction_spec(instruction)
        if source.opcode != instruction.opcode:
            raise GenerationError(f"opcode drift for {instruction.opcode!r}")
        if len(source.variants) != len(instruction.variants):
            raise GenerationError(f"variant count drift for {instruction.opcode!r}")
        forms: list[ProjectedForm] = []
        for variant, source_variant in zip(
            instruction.variants, source.variants, strict=True
        ):
            if source_variant.variant_id != variant.name:
                raise GenerationError(
                    f"variant drift for {instruction.opcode}/{variant.name}"
                )
            if len(source_variant.operand_layouts) != len(variant.operand_layouts):
                raise GenerationError(
                    f"layout count drift for {instruction.opcode}/{variant.name}"
                )
            for layout, source_layout in zip(
                variant.operand_layouts, source_variant.operand_layouts, strict=True
            ):
                if source_layout.layout_id != layout.name:
                    raise GenerationError(
                        "layout drift for "
                        f"{instruction.opcode}/{variant.name}/{layout.name}"
                    )
                if tuple(field.source_name for field in source_layout.fields) != tuple(
                    operand.name for operand in layout.operands
                ):
                    raise GenerationError(
                        "operand field drift for "
                        f"{instruction.opcode}/{variant.name}/{layout.name}"
                    )
            forms.append(
                ProjectedForm(variant, variant.operand_layouts, source_variant)
            )
        projected.append(ProjectedInstruction(instruction.opcode, tuple(forms), source))
    return tuple(projected)


def _modifier_values(modifier: ModifierSpec) -> set[BackendValue]:
    """Return every normalized value a frontend modifier can carry."""
    values = {item.value for item in modifier.values}
    if modifier.kind is ModifierKind.FLAG:
        values.update({False, True})
    if modifier.value is not None:
        values.add(modifier.value)
    if modifier.default is not None:
        values.add(modifier.default)
    return values
