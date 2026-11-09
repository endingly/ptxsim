"""Stable C++ names derived from normalized frontend identifiers."""

from __future__ import annotations

from ptx_frontend.base.utils import file_stem_to_pascal_case


_OP_ENUM_NAMES = {"and": "and_", "or": "or_", "xor": "xor_", "not": "not_"}


def op_enum_name(opcode: str) -> str:
    """Return the non-keyword C++ enum spelling for one PTX opcode."""
    return _OP_ENUM_NAMES.get(opcode, opcode)


def instruction_cpp_name(opcode: str) -> str:
    """Translate one PTX opcode identifier to its C++ record name."""
    return file_stem_to_pascal_case(opcode)


def variant_cpp_name(opcode: str, name: str) -> str:
    """Translate a normalized frontend variant id to its stable C++ name."""
    return file_stem_to_pascal_case(name.removeprefix(f"{opcode}_"))


def layout_cpp_name(name: str) -> str:
    """Translate an operand-layout selector to a C++ nested record name."""
    return f"{file_stem_to_pascal_case(name)}Operands"
