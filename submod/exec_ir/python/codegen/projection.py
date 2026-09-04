"""Project normalized frontend facts through backend C++ leaf mappings."""

from __future__ import annotations

from importlib.resources import as_file
from pathlib import Path

import ptx_frontend.code_gen.cpp_backend
import ptx_frontend.code_gen.resources
import ptx_frontend.ir.resolved_ir
from ptx_frontend.spec import (
    PtxSpecDatabase,
    load_packaged_spec_database,
    load_spec_database,
)
from ptx_frontend.spec.model import ModifierSpec

from .model import (
    BackendSpec,
    BackendValue,
    GenerationError,
    ProjectedForm,
    ProjectedInstruction,
)


def database(spec_dir: Path | None = None) -> PtxSpecDatabase:
    """Load normalized PTX facts from an explicit or packaged specification."""
    resource = ptx_frontend.code_gen.resources.packaged_cpp_backend()
    with as_file(resource) as backend_path:
        ptx_frontend.code_gen.cpp_backend.configure_cpp_backend(backend_path)
        # Cache the packaged resource before a zip-backed temporary path expires.
        ptx_frontend.code_gen.cpp_backend.get_cpp_backend()
        if spec_dir is not None:
            return load_spec_database(spec_dir=spec_dir)
        return load_packaged_spec_database()


def project_database(
    database: PtxSpecDatabase,
    backend: BackendSpec,
) -> tuple[ProjectedInstruction, ...]:
    """Validate C++ leaves and return the complete frontend topology."""
    if backend.ptx_spec_schema != database.spec_schema:
        raise GenerationError("frontend spec schema mismatch")

    modifier_kinds = {
        modifier.kind
        for instruction in database.instructions
        for variant in instruction.variants
        for modifier in variant.modifiers
        if modifier.presence != "absent"
    }
    operand_kinds = {
        operand.kind
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
                if modifier.presence == "absent":
                    continue
                mapping = backend.modifier_kinds[modifier.kind]
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
    if modifier.kind == "flag":
        values.update({False, True})
    if modifier.value is not None:
        values.add(modifier.value)
    if modifier.default is not None:
        values.add(modifier.default)
    return values
