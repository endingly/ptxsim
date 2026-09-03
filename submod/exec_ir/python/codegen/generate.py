#!/usr/bin/env python3
"""Generate the fully-bound PTXSim execution IR from supported PTX forms."""

from __future__ import annotations

import argparse
import importlib.resources
import os
import re
from dataclasses import dataclass
from pathlib import Path
import tempfile
from typing import Any

from base.utils import file_stem_to_pascal_case


class GenerationError(Exception):
    """A backend support selection does not match the frontend PTX database."""


_CPP_NAMESPACE_PART = re.compile(r"[A-Za-z_][A-Za-z0-9_]*\Z")
_CPP_KEYWORDS = frozenset(
    "alignas alignof and and_eq asm atomic_cancel atomic_commit atomic_noexcept "
    "auto bitand bitor bool break case catch char char8_t char16_t char32_t "
    "class compl concept const consteval constexpr constinit const_cast "
    "co_await co_return co_yield decltype default delete do double dynamic_cast "
    "else enum explicit export extern false float for friend goto if inline int "
    "long mutable namespace new noexcept not not_eq nullptr operator or or_eq "
    "private protected public reflexpr register reinterpret_cast requires return "
    "short signed sizeof static static_assert static_cast struct switch synchronized "
    "template this thread_local throw true try typedef typeid typename union "
    "unsigned using virtual void volatile wchar_t while xor xor_eq"
    .split()
)


@dataclass(frozen=True)
class SelectedForm:
    """One frontend variant and its backend-supported modifier projection."""

    variant: Any
    layouts: tuple[Any, ...]


@dataclass(frozen=True)
class SelectedInstruction:
    """One executable opcode selected by the backend mapping."""

    opcode: str
    forms: tuple[SelectedForm, ...]
    may_fallthrough: bool


def _load_yaml(path: Path) -> dict[str, Any]:
    """Load one strict backend support mapping."""
    import yaml

    value = yaml.safe_load(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise GenerationError("backend root must be a mapping")
    return value


def _database(spec_dir: Path | None = None) -> Any:
    """Load normalized PTX facts from an explicit or packaged specification."""
    from code_gen.database import load_codegen_database

    if spec_dir is not None:
        return load_codegen_database(spec_dir=spec_dir)
    resource = importlib.resources.files("code_gen.resources").joinpath("ptx_spec")
    with importlib.resources.as_file(resource) as spec_dir:
        return load_codegen_database(spec_dir=spec_dir)


def _require_keys(value: dict[str, Any], keys: set[str], context: str) -> None:
    """Reject misspelled backend keys rather than silently ignoring them."""
    if set(value) != keys:
        raise GenerationError(f"{context} keys must be {sorted(keys)}")


def _duplicates(values: list[Any]) -> set[Any]:
    """Return repeated selector values."""
    return {value for value in values if values.count(value) > 1}


def _mapping(backend: dict[str, Any], name: str) -> dict[str, Any]:
    """Return one required mapping section with a useful error message."""
    value = backend[name]
    if not isinstance(value, dict):
        raise GenerationError(f"{name} must be a mapping")
    return value


def _valid_namespace(value: Any) -> bool:
    """Return whether a backend namespace is a valid C++ qualified name."""
    return isinstance(value, str) and bool(value) and all(
        _CPP_NAMESPACE_PART.fullmatch(part) and part not in _CPP_KEYWORDS
        for part in value.split("::")
    )


def _validate_mappings(backend: dict[str, Any]) -> None:
    """Validate C++ type and enum-expression maps used by the emitter."""
    for section in ("modifier_kinds", "operand_kinds"):
        for kind, mapping in _mapping(backend, section).items():
            if not isinstance(kind, str) or not isinstance(mapping, dict):
                raise GenerationError(f"{section} entries must be mappings")
            _require_keys(mapping, {"cpp_type", "values"}, f"{section}.{kind}")
            if not isinstance(mapping["cpp_type"], str) or not mapping["cpp_type"]:
                raise GenerationError(f"{section}.{kind}.cpp_type must be a string")
            if not isinstance(mapping["values"], dict) or any(
                not isinstance(value, str) or not value
                for value in mapping["values"].values()
            ):
                raise GenerationError(f"{section}.{kind}.values must map to C++ expressions")


def _modifier_values(modifier: Any) -> set[Any]:
    """Return every normalized value a frontend modifier can carry."""
    values = {item.value for item in modifier.values}
    if modifier.kind == "flag":
        values.update({False, True})
    if modifier.value is not None:
        values.add(modifier.value)
    if modifier.default is not None:
        values.add(modifier.default)
    return values


def _validate(database: Any, backend: dict[str, Any]) -> tuple[SelectedInstruction, ...]:
    """Validate selectors and return the ordered executable projection."""
    _require_keys(
        backend,
        {"schema", "ptx_spec_schema", "namespace", "modifier_kinds", "operand_kinds", "instructions"},
        "backend",
    )
    if backend["schema"] != "ptxsim-exec-ir-backend/v1":
        raise GenerationError("unsupported backend schema")
    if backend["ptx_spec_schema"] != database.spec_schema:
        raise GenerationError("frontend spec schema mismatch")
    if not _valid_namespace(backend["namespace"]):
        raise GenerationError("namespace must be a valid C++ qualified name")
    _validate_mappings(backend)
    mappings = backend["instructions"]
    if not isinstance(mappings, list) or not mappings:
        raise GenerationError("instructions must be a non-empty list")
    if any(not isinstance(item, dict) for item in mappings):
        raise GenerationError("instruction mapping must be a mapping")
    opcodes = [item.get("opcode") for item in mappings]
    if any(not isinstance(opcode, str) for opcode in opcodes) or _duplicates(opcodes):
        raise GenerationError("duplicate or missing opcode selector")

    selected: list[SelectedInstruction] = []
    for mapping in mappings:
        _require_keys(mapping, {"opcode", "forms", "may_fallthrough"}, "instruction mapping")
        opcode = mapping["opcode"]
        instruction = next((item for item in database.instructions if item.opcode == opcode), None)
        if instruction is None:
            raise GenerationError(f"unknown opcode selector: {opcode!r}")
        if not isinstance(mapping["may_fallthrough"], bool):
            raise GenerationError(f"{opcode}.may_fallthrough must be a bool")
        forms = mapping["forms"]
        if not isinstance(forms, list) or not forms or any(not isinstance(form, dict) for form in forms):
            raise GenerationError(f"{opcode}.forms must be a non-empty list")
        variants = [form.get("variant") for form in forms]
        if any(not isinstance(variant, str) for variant in variants) or _duplicates(variants):
            raise GenerationError(f"duplicate or missing variant selector for {opcode}")
        selected_forms: list[SelectedForm] = []
        for form in forms:
            _require_keys(form, {"variant", "layouts", "modifier_values"}, "instruction form")
            variant = next((item for item in instruction.variants if item.name == form["variant"]), None)
            if variant is None:
                raise GenerationError(f"unknown variant selector: {form['variant']!r}")
            layouts = form["layouts"]
            if not isinstance(layouts, list) or not layouts or _duplicates(layouts):
                raise GenerationError(f"duplicate, missing, or empty layout selector for {variant.name!r}")
            known_layouts = {layout.name: layout for layout in variant.operand_layouts}
            if any(not isinstance(layout, str) or layout not in known_layouts for layout in layouts):
                raise GenerationError(f"unknown layout selector for {variant.name!r}")
            values = form["modifier_values"]
            if not isinstance(values, dict):
                raise GenerationError(f"modifier_values for {variant.name!r} must be a mapping")
            active = [modifier for modifier in variant.modifiers if modifier.presence != "absent"]
            if set(values) != {modifier.name for modifier in active}:
                raise GenerationError(f"modifier_values must select every active modifier for {variant.name!r}")
            for modifier in active:
                requested = values[modifier.name]
                if not isinstance(requested, list) or not requested or _duplicates(requested):
                    raise GenerationError(f"invalid modifier value selector for {modifier.name!r}")
                if not set(requested).issubset(_modifier_values(modifier)):
                    raise GenerationError(f"unknown modifier value selector for {modifier.name!r}")
                kind_mapping = _mapping(backend, "modifier_kinds").get(modifier.kind)
                if kind_mapping is None or any(value not in kind_mapping["values"] for value in requested):
                    raise GenerationError(f"missing C++ value mapping for modifier {modifier.name!r}")
                if modifier.presence == "fixed" and requested != [modifier.value]:
                    raise GenerationError(f"fixed modifier {modifier.name!r} must select {modifier.value!r}")
            for layout in (known_layouts[name] for name in layouts):
                for operand in layout.operands:
                    if operand.kind not in _mapping(backend, "operand_kinds"):
                        raise GenerationError(f"missing C++ operand mapping for kind {operand.kind!r}")
            selected_forms.append(SelectedForm(variant, tuple(known_layouts[name] for name in layouts)))
        selected.append(SelectedInstruction(opcode, tuple(selected_forms), mapping["may_fallthrough"]))
    return tuple(selected)


def _cpp_type(backend: dict[str, Any], section: str, kind: str) -> str:
    """Return the C++ storage type declared for one frontend kind."""
    return _mapping(backend, section)[kind]["cpp_type"]


def _cpp_value(backend: dict[str, Any], kind: str, value: Any) -> str:
    """Return the C++ expression corresponding to one normalized modifier value."""
    return _mapping(backend, "modifier_kinds")[kind]["values"][value]


def _variant_cpp_name(opcode: str, name: str) -> str:
    """Translate a normalized frontend variant id to its stable C++ name."""
    return file_stem_to_pascal_case(name.removeprefix(f"{opcode}_"))


def _layout_cpp_name(name: str) -> str:
    """Translate an operand-layout selector to a C++ nested record name."""
    return f"{file_stem_to_pascal_case(name)}Operands"


def _emit_layout(backend: dict[str, Any], layout: Any, indent: str) -> list[str]:
    """Emit one operand record in normalized frontend operand order."""
    name = _layout_cpp_name(layout.name)
    lines = [f"{indent}/** @brief Operands selected by the `{layout.name}` layout. */", f"{indent}struct {name} {{"]
    for operand in layout.operands:
        lines += [f"{indent}  /** @brief Fully-bound `{operand.role}` operand. */", f"{indent}  {_cpp_type(backend, 'operand_kinds', operand.kind)} {operand.name};"]
    return lines + [f"{indent}  /** @brief Compare fully-bound layout operands. */", f"{indent}  constexpr bool operator==(const {name}&) const noexcept = default;", f"{indent}}};"]


def _emit_form(backend: dict[str, Any], instruction: SelectedInstruction, form: SelectedForm) -> list[str]:
    """Emit one selected frontend variant with its modifiers and operands."""
    name = _variant_cpp_name(instruction.opcode, form.variant.name)
    lines = [f"  /** @brief Fully-bound `{form.variant.name}` instruction form. */", f"  struct {name} {{"]
    for modifier in form.variant.modifiers:
        if modifier.presence == "absent":
            continue
        cpp_type = _cpp_type(backend, "modifier_kinds", modifier.kind)
        if modifier.presence == "fixed":
            lines += [f"    /** @brief Fixed `{modifier.name}` selector required by this form. */", f"    inline static constexpr {cpp_type} {modifier.name} = {_cpp_value(backend, modifier.kind, modifier.value)};"]
        else:
            lines += [f"    /** @brief Supported `{modifier.name}` selector copied from resolved IR. */", f"    {cpp_type} {modifier.name};"]
    if len(form.layouts) == 1 and len(form.variant.operand_layouts) == 1:
        for operand in form.layouts[0].operands:
            lines += [f"    /** @brief Fully-bound `{operand.role}` operand. */", f"    {_cpp_type(backend, 'operand_kinds', operand.kind)} {operand.name};"]
    else:
        for layout in form.layouts:
            lines += _emit_layout(backend, layout, "    ")
        alternatives = ", ".join(_layout_cpp_name(layout.name) for layout in form.layouts)
        lines += ["    /** @brief Operand-layout alternatives retained from the selected projection. */", f"    using Operands = std::variant<{alternatives}>;", "    /** @brief Operands bound according to the resolved layout. */", "    Operands operands;"]
    return lines + ["    /** @brief Compare fully-bound form fields. */", f"    constexpr bool operator==(const {name}&) const noexcept = default;", "  };"]


def _header(backend: dict[str, Any], selected: tuple[SelectedInstruction, ...]) -> str:
    """Return a C++ header derived entirely from backend and frontend data."""
    classes = [file_stem_to_pascal_case(item.opcode) for item in selected]
    namespace = backend["namespace"]
    lines = ["// Generated by ptxsim_exec_ir_codegen. Do not edit.", "#pragma once", "", "#include <concepts>", "#include <cstdint>", "#include <optional>", "#include <type_traits>", "#include <variant>", "", "#include <ptxsim/exec_ir/exec_ir_types.hpp>", "", f"namespace {namespace} {{", "", "/** @brief Pure execution opcode used by static executor dispatch. */", f"enum class Op : std::uint8_t {{ {', '.join(item.opcode for item in selected)} }};", ""]
    for instruction, class_name in zip(selected, classes, strict=True):
        lines += [f"/** @brief Fully-bound `{instruction.opcode}` execution instruction. */", f"struct {class_name} {{"]
        for form in instruction.forms:
            lines += _emit_form(backend, instruction, form)
        alternatives = ", ".join(_variant_cpp_name(instruction.opcode, form.variant.name) for form in instruction.forms)
        lines += ["  /** @brief Selected frontend form for this opcode. */", f"  using Variant = std::variant<{alternatives}>;", "  /** @brief Predicate evaluated before this instruction observes operands. */", "  std::optional<Predicate> execution_predicate;", "  /** @brief Fully-bound selected form. */", "  Variant variant;", "  /** @brief Compare predicate and selected form. */", f"  constexpr bool operator==(const {class_name}&) const noexcept = default;", "};", ""]
    alternatives = ", ".join(classes)
    concept = " || ".join(f"std::same_as<std::remove_cvref_t<T>, {name}>" for name in classes)
    lines += ["/** @brief Flat sum of executable opcode records. */", f"using Instruction = std::variant<{alternatives}>;", "/** @brief A type held by the generated execution instruction sum. */", "template <typename T>", f"concept InstructionAlternative = {concept};", "/** @brief Return the pure opcode independent of modifiers and operands. */", "[[nodiscard]] constexpr auto op(const Instruction& value) noexcept -> Op {", "  return std::visit([]<InstructionAlternative T>(const T&) constexpr {", *[f"    if constexpr (std::same_as<T, {name}>) return Op::{instruction.opcode};" for instruction, name in zip(selected, classes, strict=True)], "  }, value);", "}", "/** @brief Return the predicate stored by the selected opcode record. */", "[[nodiscard]] constexpr auto execution_predicate(const Instruction& value) -> const std::optional<Predicate>& {", "  return std::visit([]<InstructionAlternative T>(const T& item) -> const std::optional<Predicate>& { return item.execution_predicate; }, value);", "}", "/** @brief Report whether the selected opcode may advance to its successor PC. */", "[[nodiscard]] constexpr auto may_fallthrough(const Instruction& value) noexcept -> bool {", "  return std::visit([]<InstructionAlternative T>(const T& item) constexpr {", *[f"    if constexpr (std::same_as<T, {name}>) return item.execution_predicate.has_value() || {'true' if instruction.may_fallthrough else 'false'};" for instruction, name in zip(selected, classes, strict=True)], "  }, value);", "}", ""]
    lines += [f"}}  // namespace {namespace}", ""]
    return "\n".join(lines)


def _write(path: Path, content: str) -> None:
    """Atomically publish one generated build artifact."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile("w", encoding="utf-8", dir=path.parent, delete=False) as temporary:
        temporary.write(content)
        replacement = Path(temporary.name)
    try:
        os.replace(replacement, path)
    finally:
        replacement.unlink(missing_ok=True)


def main() -> None:
    """Validate backend support and emit its public C++ instruction header."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--backend", type=Path)
    parser.add_argument("--spec-dir", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        if args.backend is None:
            resource = importlib.resources.files("ptxsim_exec_ir_codegen.instructions").joinpath("backend.yaml")
            with importlib.resources.as_file(resource) as path:
                backend = _load_yaml(path)
        else:
            backend = _load_yaml(args.backend)
        selected = _validate(_database(args.spec_dir), backend)
        _write(args.output, _header(backend, selected))
    except (GenerationError, ImportError, OSError, ValueError) as error:
        raise SystemExit(f"exec_ir generation error: {error}") from error


if __name__ == "__main__":
    main()
