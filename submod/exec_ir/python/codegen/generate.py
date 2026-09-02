#!/usr/bin/env python3
"""Validate the WP0 exec-IR selector against packaged frontend metadata."""

from __future__ import annotations

import argparse
import importlib.resources
import json
import os
from pathlib import Path
import re
import tempfile
from typing import Any


class GenerationError(Exception):
    """A manifest or frontend-model mismatch."""


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    return parser.parse_args()


def load_manifest(path: Path) -> dict[str, Any]:
    try:
        import yaml
    except ImportError as error:
        raise GenerationError("PyYAML is required by ptxsim_exec_ir_codegen") from error

    try:
        value = yaml.safe_load(path.read_text(encoding="utf-8"))
    except OSError as error:
        raise GenerationError(f"cannot read manifest {path}: {error}") from error
    except yaml.YAMLError as error:
        raise GenerationError(f"invalid YAML manifest {path}: {error}") from error
    if not isinstance(value, dict):
        raise GenerationError("manifest root must be a mapping")
    return value


def load_default_manifest() -> dict[str, Any]:
    try:
        resource = importlib.resources.files(
            "ptxsim_exec_ir_codegen.instructions"
        ).joinpath("exec_ir.yaml")
        with importlib.resources.as_file(resource) as path:
            return load_manifest(path)
    except (AttributeError, ModuleNotFoundError, FileNotFoundError, OSError) as error:
        raise GenerationError(f"cannot load bundled exec-IR manifest: {error}") from error


def require_keys(value: dict[str, Any], expected: set[str], context: str) -> None:
    unknown = set(value) - expected
    missing = expected - set(value)
    if unknown:
        raise GenerationError(f"{context} has unknown field(s): {sorted(unknown)}")
    if missing:
        raise GenerationError(f"{context} is missing field(s): {sorted(missing)}")


def selected_values(modifier: Any) -> set[object]:
    values = {value.value for value in modifier.values}
    if modifier.value is not None:
        values.add(modifier.value)
    return values


def find_instruction(database: Any, opcode: str) -> Any:
    for instruction in database.instructions:
        if instruction.opcode == opcode:
            return instruction
    raise GenerationError(f"unknown opcode selector: {opcode!r}")


def find_variant(instruction: Any, name: str) -> Any:
    for variant in instruction.variants:
        if variant.name == name:
            return variant
    raise GenerationError(
        f"unknown variant selector for opcode {instruction.opcode!r}: {name!r}"
    )


def selector_value(value: str | bool | int) -> str:
    return str(value).lower() if isinstance(value, bool) else str(value)


def canonical_selector(mapping: dict[str, Any], form: dict[str, Any]) -> str:
    parts = [mapping["opcode"], form["variant"], *form["layouts"]]
    parts.extend(
        f"{name}={','.join(selector_value(value) for value in values)}"
        for name, values in sorted(form["modifier_values"].items())
    )
    return "/".join(parts)


def validate_selector(database: Any, manifest: dict[str, Any]) -> tuple[str, str]:
    require_keys(manifest, {"schema", "ptx_spec_schema", "instructions"}, "manifest")
    if manifest["schema"] != "ptxsim-exec-ir/v1":
        raise GenerationError(
            f"unsupported exec-IR manifest schema: {manifest['schema']!r}"
        )
    if manifest["ptx_spec_schema"] != database.spec_schema:
        raise GenerationError(
            "frontend spec schema mismatch: "
            f"manifest requires {manifest['ptx_spec_schema']!r}, "
            f"frontend provides {database.spec_schema!r}"
        )

    instructions = manifest["instructions"]
    if not isinstance(instructions, list) or len(instructions) != 1:
        raise GenerationError("WP0 manifest must contain exactly one instruction mapping")
    mapping = instructions[0]
    if not isinstance(mapping, dict):
        raise GenerationError("instruction mapping must be a mapping")
    require_keys(mapping, {"opcode", "operation", "forms"}, "instruction mapping")
    if not isinstance(mapping["opcode"], str) or not isinstance(mapping["operation"], str):
        raise GenerationError("instruction opcode and operation must be strings")
    if not re.fullmatch(r"[a-z][a-z0-9_]*", mapping["operation"]):
        raise GenerationError("operation must be a snake_case C++ identifier")
    if not isinstance(mapping["forms"], list) or len(mapping["forms"]) != 1:
        raise GenerationError("WP0 instruction mapping must contain exactly one form")

    form = mapping["forms"][0]
    if not isinstance(form, dict):
        raise GenerationError("instruction form must be a mapping")
    require_keys(form, {"variant", "layouts", "modifier_values"}, "instruction form")
    if not isinstance(form["variant"], str):
        raise GenerationError("form variant must be a string")
    if not isinstance(form["layouts"], list) or len(form["layouts"]) != 1 or not all(
        isinstance(layout, str) for layout in form["layouts"]
    ):
        raise GenerationError("WP0 form layouts must contain exactly one string")
    selectors = form["modifier_values"]
    if not isinstance(selectors, dict) or not selectors or not all(
        isinstance(name, str) and isinstance(values, list) and values
        for name, values in selectors.items()
    ):
        raise GenerationError(
            "modifier_values must be non-empty and map names to non-empty lists"
        )
    if not all(
        isinstance(value, (str, bool, int))
        for values in selectors.values()
        for value in values
    ):
        raise GenerationError("modifier selector values must be scalars")
    if any(len(set(values)) != len(values) for values in selectors.values()):
        raise GenerationError("modifier selector values must not contain duplicates")

    instruction = find_instruction(database, mapping["opcode"])
    variant = find_variant(instruction, form["variant"])
    available_layouts = {layout.name for layout in variant.operand_layouts}
    for layout in form["layouts"]:
        if layout not in available_layouts:
            raise GenerationError(
                f"unknown layout selector for variant {variant.name!r}: {layout!r}"
            )
    modifiers = {modifier.name: modifier for modifier in variant.modifiers}
    for name, values in selectors.items():
        modifier = modifiers.get(name)
        if modifier is None:
            raise GenerationError(
                f"unknown modifier selector for variant {variant.name!r}: {name!r}"
            )
        available_values = selected_values(modifier)
        for value in values:
            if value not in available_values:
                raise GenerationError(
                    f"unknown value selector for modifier {name!r} in "
                    f"variant {variant.name!r}: {value!r}"
                )

    return mapping["operation"], canonical_selector(mapping, form)


def load_database() -> Any:
    try:
        from code_gen.database import load_codegen_database
    except ImportError as error:
        raise GenerationError(
            "cannot import code_gen.database from the selected Python environment"
        ) from error

    try:
        spec_resource = importlib.resources.files("code_gen.resources").joinpath(
            "ptx_spec"
        )
        with importlib.resources.as_file(spec_resource) as spec_dir:
            return load_codegen_database(spec_dir=spec_dir)
    except (
        AttributeError,
        ModuleNotFoundError,
        FileNotFoundError,
        OSError,
        TypeError,
        ValueError,
    ) as error:
        raise GenerationError(f"cannot load packaged ptx_spec: {error}") from error


def render_header(operation: str, selector: str) -> str:
    return f"""// Generated by ptxsim_exec_ir_codegen. Do not edit.\n#pragma once\n\n#include <string_view>\n\nnamespace ptxsim::exec_ir::generated {{\n\ninline constexpr std::string_view {operation}_selector =\n    {json.dumps(selector)};\n\n}}  // namespace ptxsim::exec_ir::generated\n"""


def write_output(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        "w", encoding="utf-8", dir=path.parent, delete=False
    ) as temporary:
        temporary.write(content)
        temporary_path = Path(temporary.name)
    try:
        os.replace(temporary_path, path)
    finally:
        temporary_path.unlink(missing_ok=True)


def main() -> None:
    try:
        args = parse_args()
        database = load_database()
        manifest = load_manifest(args.manifest) if args.manifest else load_default_manifest()
        operation, selector = validate_selector(database, manifest)
        write_output(args.output, render_header(operation, selector))
    except GenerationError as error:
        raise SystemExit(f"exec_ir generation error: {error}") from error


if __name__ == "__main__":
    main()
