"""Typed backend mappings and selected normalized PTX instruction records."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Mapping

from code_gen.model import OperandLayoutSpec, VariantSpec


class GenerationError(Exception):
    """A backend support selection does not match the frontend PTX database."""


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
class FormMapping:
    """One selected frontend variant, layouts, and supported modifier values."""

    variant: str
    layouts: tuple[str, ...]
    modifier_values: Mapping[str, tuple[BackendValue, ...]]


@dataclass(frozen=True)
class InstructionMapping:
    """One selected opcode and its executable forms."""

    opcode: str
    forms: tuple[FormMapping, ...]
    may_fallthrough: bool


@dataclass(frozen=True)
class BackendSpec:
    """The typed ptxsim support projection and C++ representation mappings."""

    schema: str
    ptx_spec_schema: str
    namespace: str
    modifier_kinds: Mapping[str, CppKindMapping]
    operand_kinds: Mapping[str, CppKindMapping]
    instructions: tuple[InstructionMapping, ...]


@dataclass(frozen=True)
class SelectedForm:
    """One frontend variant and its backend-supported modifier projection."""

    variant: VariantSpec
    layouts: tuple[OperandLayoutSpec, ...]


@dataclass(frozen=True)
class SelectedInstruction:
    """One executable opcode selected by the backend mapping."""

    opcode: str
    forms: tuple[SelectedForm, ...]
    may_fallthrough: bool
