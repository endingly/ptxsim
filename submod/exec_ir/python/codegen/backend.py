"""Strict loading of backend C++ leaf mappings."""

from __future__ import annotations

import re
from pathlib import Path
from types import MappingProxyType
from typing import Any, Mapping

from .model import (
    BackendSpec,
    BackendValue,
    CppKindMapping,
    GenerationError,
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
    """Load one strict backend leaf mapping into frozen typed records."""
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
        },
        "backend",
    )
    schema = raw["schema"]
    if not isinstance(schema, str) or schema != "ptxsim-exec-ir-backend/v2":
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


def _require_keys(value: dict[str, Any], keys: set[str], context: str) -> None:
    """Reject misspelled backend keys rather than silently ignoring them."""
    if set(value) != keys:
        raise GenerationError(f"{context} keys must be {sorted(keys)}")


def _valid_namespace(value: Any) -> bool:
    """Return whether a backend namespace is a valid C++ qualified name."""
    return isinstance(value, str) and bool(value) and all(
        _CPP_NAMESPACE_PART.fullmatch(part) and part not in _CPP_KEYWORDS
        for part in value.split("::")
    )
