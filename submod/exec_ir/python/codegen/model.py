"""Typed C++ leaf mappings and full normalized PTX instruction records."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Mapping

from ptx_frontend.spec.model import OperandLayoutSpec, VariantSpec


class GenerationError(Exception):
    """A backend leaf mapping does not match the frontend PTX database."""


BackendValue = str | bool | int


@dataclass(frozen=True)
class CppKindMapping:
    """One frontend kind's C++ storage type and semantic value spellings."""

    cpp_type: str
    values: Mapping[BackendValue, str]

    def cpp_value(self, value: BackendValue) -> str:
        """Return the C++ expression for one previously validated value."""
        return self.values[value]


@dataclass(frozen=True)
class BackendSpec:
    """C++ storage mappings for the complete frontend PTX topology."""

    schema: str
    ptx_spec_schema: str
    namespace: str
    modifier_kinds: Mapping[str, CppKindMapping]
    operand_kinds: Mapping[str, CppKindMapping]


@dataclass(frozen=True)
class ProjectedForm:
    """One frontend variant and all of its operand layouts."""

    variant: VariantSpec
    layouts: tuple[OperandLayoutSpec, ...]


@dataclass(frozen=True)
class ProjectedInstruction:
    """One frontend opcode retained in the generated execution declaration."""

    opcode: str
    forms: tuple[ProjectedForm, ...]
