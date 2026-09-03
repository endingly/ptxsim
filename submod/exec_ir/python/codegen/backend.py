"""Strict backend support-map loading and C++ leaf mappings."""

from __future__ import annotations

import re
from pathlib import Path
from types import MappingProxyType
from typing import Any, Mapping

from .model import (
    BackendSpec,
    BackendValue,
    CppKindMapping,
    FormMapping,
    GenerationError,
    InstructionMapping,
)


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
    "unsigned using virtual void volatile wchar_t while xor xor_eq".split()
)


def load_yaml(path: Path) -> BackendSpec:
    """Load one strict backend support mapping into frozen typed records."""
    import yaml

    raw = yaml.safe_load(path.read_text(encoding="utf-8"))
    if not isinstance(raw, dict):
        raise GenerationError("backend root must be a mapping")
    return _parse_backend(raw)


def _parse_backend(raw: dict[str, Any]) -> BackendSpec:
    """Validate raw YAML at the trust boundary and construct a backend spec."""
    _require_keys(
        raw,
        {
            "schema",
            "ptx_spec_schema",
            "namespace",
            "modifier_kinds",
            "operand_kinds",
            "instructions",
        },
        "backend",
    )
    schema = raw["schema"]
    if not isinstance(schema, str) or schema != "ptxsim-exec-ir-backend/v1":
        raise GenerationError("unsupported backend schema")
    ptx_spec_schema = raw["ptx_spec_schema"]
    if not isinstance(ptx_spec_schema, str):
        raise GenerationError("ptx_spec_schema must be a string")
    namespace = raw["namespace"]
    if not _valid_namespace(namespace):
        raise GenerationError("namespace must be a valid C++ qualified name")
    return BackendSpec(
        schema=schema,
        ptx_spec_schema=ptx_spec_schema,
        namespace=namespace,
        modifier_kinds=_parse_kind_mappings(raw["modifier_kinds"], "modifier_kinds"),
        operand_kinds=_parse_kind_mappings(raw["operand_kinds"], "operand_kinds"),
        instructions=_parse_instructions(raw["instructions"]),
    )


def _parse_kind_mappings(raw: Any, section: str) -> Mapping[str, CppKindMapping]:
    """Normalize one C++ kind-mapping section from the raw backend YAML."""
    if not isinstance(raw, dict):
        raise GenerationError(f"{section} must be a mapping")
    mappings: dict[str, CppKindMapping] = {}
    for kind, entry in raw.items():
        if not isinstance(kind, str) or not isinstance(entry, dict):
            raise GenerationError(f"{section} entries must be mappings")
        _require_keys(entry, {"cpp_type", "values"}, f"{section}.{kind}")
        cpp_type = entry["cpp_type"]
        if not isinstance(cpp_type, str) or not cpp_type:
            raise GenerationError(f"{section}.{kind}.cpp_type must be a string")
        values = entry["values"]
        if (
            not isinstance(values, dict)
            or any(
                not isinstance(key, (str, bool, int))
                or not isinstance(value, str)
                or not value
                for key, value in values.items()
            )
        ):
            raise GenerationError(
                f"{section}.{kind}.values must map to C++ expressions"
            )
        mappings[kind] = CppKindMapping(cpp_type, MappingProxyType(dict(values)))
    return MappingProxyType(mappings)


def _parse_instructions(raw: Any) -> tuple[InstructionMapping, ...]:
    """Normalize the backend's ordered executable instruction selection."""
    if not isinstance(raw, list) or not raw:
        raise GenerationError("instructions must be a non-empty list")
    if any(not isinstance(item, dict) for item in raw):
        raise GenerationError("instruction mapping must be a mapping")
    opcodes = [item.get("opcode") for item in raw]
    if any(not isinstance(opcode, str) for opcode in opcodes) or _duplicates(opcodes):
        raise GenerationError("duplicate or missing opcode selector")
    return tuple(_parse_instruction(item) for item in raw)


def _parse_instruction(raw: dict[str, Any]) -> InstructionMapping:
    """Normalize one raw opcode support selector."""
    _require_keys(raw, {"opcode", "forms", "may_fallthrough"}, "instruction mapping")
    opcode = raw["opcode"]
    if not isinstance(opcode, str):
        raise GenerationError(f"unknown opcode selector: {opcode!r}")
    may_fallthrough = raw["may_fallthrough"]
    if not isinstance(may_fallthrough, bool):
        raise GenerationError(f"{opcode}.may_fallthrough must be a bool")
    forms = raw["forms"]
    if (
        not isinstance(forms, list)
        or not forms
        or any(not isinstance(form, dict) for form in forms)
    ):
        raise GenerationError(f"{opcode}.forms must be a non-empty list")
    variants = [form.get("variant") for form in forms]
    if (
        any(not isinstance(variant, str) for variant in variants)
        or _duplicates(variants)
    ):
        raise GenerationError(f"duplicate or missing variant selector for {opcode}")
    return InstructionMapping(
        opcode,
        tuple(_parse_form(form) for form in forms),
        may_fallthrough,
    )


def _parse_form(raw: dict[str, Any]) -> FormMapping:
    """Normalize one raw frontend-form support selector."""
    _require_keys(raw, {"variant", "layouts", "modifier_values"}, "instruction form")
    variant = raw["variant"]
    if not isinstance(variant, str):
        raise GenerationError(f"unknown variant selector: {variant!r}")
    layouts = raw["layouts"]
    if not isinstance(layouts, list) or not layouts or _duplicates(layouts):
        raise GenerationError(
            f"duplicate, missing, or empty layout selector for {variant!r}"
        )
    if any(not isinstance(layout, str) for layout in layouts):
        raise GenerationError(f"unknown layout selector for {variant!r}")
    raw_values = raw["modifier_values"]
    if not isinstance(raw_values, dict):
        raise GenerationError(f"modifier_values for {variant!r} must be a mapping")
    modifier_values: dict[str, tuple[BackendValue, ...]] = {}
    for name, values in raw_values.items():
        if (
            not isinstance(name, str)
            or not isinstance(values, list)
            or not values
            or _duplicates(values)
            or any(not isinstance(value, (str, bool, int)) for value in values)
        ):
            raise GenerationError(f"invalid modifier value selector for {name!r}")
        modifier_values[name] = tuple(values)
    return FormMapping(
        variant,
        tuple(layouts),
        MappingProxyType(modifier_values),
    )


def _require_keys(value: dict[str, Any], keys: set[str], context: str) -> None:
    """Reject misspelled backend keys rather than silently ignoring them."""
    if set(value) != keys:
        raise GenerationError(f"{context} keys must be {sorted(keys)}")


def _duplicates(values: list[Any]) -> bool:
    """Return whether selector values repeat without assuming they are hashable."""
    return any(values.count(value) > 1 for value in values)


def _valid_namespace(value: Any) -> bool:
    """Return whether a backend namespace is a valid C++ qualified name."""
    return isinstance(value, str) and bool(value) and all(
        _CPP_NAMESPACE_PART.fullmatch(part) and part not in _CPP_KEYWORDS
        for part in value.split("::")
    )
