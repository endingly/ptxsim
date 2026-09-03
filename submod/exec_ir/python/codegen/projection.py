"""Join normalized frontend facts with one backend support selection."""

from __future__ import annotations

import importlib.resources
from pathlib import Path

from code_gen.database import CodegenDatabase, load_codegen_database
from code_gen.model import ModifierSpec

from .model import (
    BackendSpec,
    BackendValue,
    GenerationError,
    SelectedForm,
    SelectedInstruction,
)


def database(spec_dir: Path | None = None) -> CodegenDatabase:
    """Load normalized PTX facts from an explicit or packaged specification."""
    if spec_dir is not None:
        return load_codegen_database(spec_dir=spec_dir)
    resource = importlib.resources.files("code_gen.resources").joinpath("ptx_spec")
    with importlib.resources.as_file(resource) as resource_dir:
        return load_codegen_database(spec_dir=resource_dir)


def select_projection(
    database: CodegenDatabase,
    backend: BackendSpec,
) -> tuple[SelectedInstruction, ...]:
    """Validate selectors and return the ordered executable projection."""
    if backend.ptx_spec_schema != database.spec_schema:
        raise GenerationError("frontend spec schema mismatch")

    selected: list[SelectedInstruction] = []
    for instruction_mapping in backend.instructions:
        instruction = next(
            (
                item
                for item in database.instructions
                if item.opcode == instruction_mapping.opcode
            ),
            None,
        )
        if instruction is None:
            raise GenerationError(
                f"unknown opcode selector: {instruction_mapping.opcode!r}"
            )
        selected_forms: list[SelectedForm] = []
        for form_mapping in instruction_mapping.forms:
            variant = next(
                (
                    item
                    for item in instruction.variants
                    if item.name == form_mapping.variant
                ),
                None,
            )
            if variant is None:
                raise GenerationError(
                    f"unknown variant selector: {form_mapping.variant!r}"
                )
            known_layouts = {
                layout.name: layout for layout in variant.operand_layouts
            }
            if any(layout not in known_layouts for layout in form_mapping.layouts):
                raise GenerationError(
                    f"unknown layout selector for {variant.name!r}"
                )
            active = [
                modifier
                for modifier in variant.modifiers
                if modifier.presence != "absent"
            ]
            if set(form_mapping.modifier_values) != {
                modifier.name for modifier in active
            }:
                raise GenerationError(
                    "modifier_values must select every active modifier for "
                    f"{variant.name!r}"
                )
            for modifier in active:
                requested = form_mapping.modifier_values[modifier.name]
                if not set(requested).issubset(_modifier_values(modifier)):
                    raise GenerationError(
                        f"unknown modifier value selector for {modifier.name!r}"
                    )
                kind_mapping = backend.modifier_kinds.get(modifier.kind)
                if kind_mapping is None or any(
                    value not in kind_mapping.values for value in requested
                ):
                    raise GenerationError(
                        f"missing C++ value mapping for modifier {modifier.name!r}"
                    )
                if modifier.presence == "fixed" and requested != (
                    modifier.value,
                ):
                    raise GenerationError(
                        f"fixed modifier {modifier.name!r} must select "
                        f"{modifier.value!r}"
                    )
            for layout_name in form_mapping.layouts:
                for operand in known_layouts[layout_name].operands:
                    if operand.kind not in backend.operand_kinds:
                        raise GenerationError(
                            "missing C++ operand mapping for kind "
                            f"{operand.kind!r}"
                        )
            selected_forms.append(
                SelectedForm(
                    variant,
                    tuple(known_layouts[name] for name in form_mapping.layouts),
                )
            )
        selected.append(
            SelectedInstruction(
                instruction_mapping.opcode,
                tuple(selected_forms),
                instruction_mapping.may_fallthrough,
            )
        )
    return tuple(selected)


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
