"""Project normalized frontend facts through backend C++ leaf mappings."""

from __future__ import annotations

from pathlib import Path

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
    return tuple(
        ProjectedInstruction(
            instruction.opcode,
            tuple(
                ProjectedForm(variant, variant.operand_layouts)
                for variant in instruction.variants
            ),
        )
        for instruction in database.instructions
    )


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
