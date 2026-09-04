"""Emit the projected PTXSim execution instruction declarations and diagnostics."""

from __future__ import annotations

from ptx_frontend.spec.model import ModifierSpec, OperandLayoutSpec, OperandSpec

from .cpp_names import (
    instruction_cpp_name,
    layout_cpp_name,
    op_enum_name,
    variant_cpp_name,
)
from .model import (
    BackendSpec,
    BackendValue,
    GenerationError,
    ProjectedForm,
    ProjectedInstruction,
)

def _emit_operand_appends(
    operands: tuple[OperandSpec, ...], prefix: str, output: str, indent: str
) -> str:
    """Emit bound operand appends in normalized frontend order."""
    return "".join(
        f"{indent}detail::append_operand({output}, {prefix}{operand.name});\n"
        for operand in operands
    )


def _emit_operand(backend: BackendSpec, operand: OperandSpec, indent: str) -> str:
    """Emit one fully bound operand declaration at the requested indentation."""
    return f"""\
{indent}/** @brief Fully-bound `{operand.role}` operand. */
{indent}{backend.operand_kinds[operand.kind].cpp_type} {operand.name};
"""


def _emit_layout(backend: BackendSpec, layout: OperandLayoutSpec, indent: str) -> str:
    """Emit one operand record declaration in normalized frontend order."""
    name = layout_cpp_name(layout.name)
    operands = "".join(
        _emit_operand(backend, operand, f"{indent}  ") for operand in layout.operands
    )
    return f"""\
{indent}/** @brief Operands bound by the `{layout.name}` layout. */
{indent}struct {name} {{
{operands}{indent}  /** @brief Compare fully-bound layout operands. */
{indent}  constexpr bool operator==(const {name}&) const noexcept = default;
{indent}  /** @brief Format this layout as stable execution-IR diagnostics. */
{indent}  [[nodiscard]] auto to_string() const -> std::string;
{indent}}};"""


def _emit_modifier(backend: BackendSpec, modifier: ModifierSpec) -> str:
    """Emit one non-absent modifier declaration for a projected form."""
    kind_mapping = backend.modifier_kinds[modifier.kind]
    if modifier.presence == "fixed":
        if modifier.value is None:
            raise GenerationError(
                f"fixed modifier {modifier.name!r} has no semantic value"
            )
        return (
            f"""\
    /** @brief Fixed `{modifier.name}` selector required by this form. */
    inline static constexpr {kind_mapping.cpp_type} {modifier.name} = """
            f"""{kind_mapping.cpp_value(modifier.value)};
"""
        )
    return f"""\
    /** @brief Resolved `{modifier.name}` selector carried by this form. */
    {kind_mapping.cpp_type} {modifier.name};
"""


def _emit_modifiers(backend: BackendSpec, form: ProjectedForm) -> str:
    """Emit the non-absent modifier declarations for one projected form."""
    return "".join(
        _emit_modifier(backend, modifier)
        for modifier in form.variant.modifiers
        if modifier.presence != "absent"
    )


def _modifier_value_expression(
    backend: BackendSpec, modifier: ModifierSpec, value: BackendValue
) -> str:
    """Return the C++ member value expression for one frontend semantic value."""
    return backend.modifier_kinds[modifier.kind].cpp_value(value)


def _emit_modifier_value(
    backend: BackendSpec, modifier: ModifierSpec, indent: str
) -> str:
    """Emit frontend-token-aware text for a modifier member's current value."""
    token_values = tuple(value for value in modifier.values if value.token)
    conditions = []
    for index, value in enumerate(token_values):
        keyword = "if" if index == 0 else "else if"
        expression = _modifier_value_expression(backend, modifier, value.value)
        conditions.append(
            f'{indent}{keyword} ({modifier.name} == {expression}) {{\n'
            f'{indent}  output += "{value.token}";\n'
            f"{indent}}}"
        )
    fallback = (
        f"{indent}output += '.';\n"
        f"{indent}output += ::ptxsim::exec_ir::to_string({modifier.name});"
    )
    if not conditions:
        return fallback
    return "\n".join((*conditions, f"{indent}else {{\n{fallback}\n{indent}}}"))


def _emit_modifier_append(
    backend: BackendSpec, modifier: ModifierSpec
) -> str:
    """Emit one modifier suffix, suppressing its optional default value."""
    if backend.modifier_kinds[modifier.kind].cpp_type == "bool":
        token = modifier.token or next(
            (value.token for value in modifier.values if value.value is True),
            None,
        )
        if token is None:
            token = f".{modifier.name}"
        if modifier.presence == "fixed":
            return f'  output += "{token}";\n' if modifier.value else ""
        if modifier.default is not False:
            raise GenerationError(
                f"optional flag {modifier.name!r} requires a false default"
            )
        return f'  if ({modifier.name})\n    output += "{token}";\n'

    if modifier.presence == "fixed":
        if modifier.token:
            return f'  output += "{modifier.token}";\n'
        return _emit_modifier_value(backend, modifier, "  ") + "\n"

    value = _emit_modifier_value(backend, modifier, "    ")
    if modifier.default is None:
        return value.replace("    ", "  ") + "\n"
    default = _modifier_value_expression(backend, modifier, modifier.default)
    return (
        f"  if ({modifier.name} != {default}) {{\n"
        f"{value}\n"
        "  }\n"
    )


def _emit_modifier_appends(backend: BackendSpec, form: ProjectedForm) -> str:
    """Emit token-aware suffixes for all non-absent form modifiers."""
    return "".join(
        _emit_modifier_append(backend, modifier)
        for modifier in form.variant.modifiers
        if modifier.presence != "absent"
    )


def _emit_form_operands(backend: BackendSpec, form: ProjectedForm) -> str:
    """Emit flattened or variant-wrapped operands for one projected form."""
    if len(form.layouts) == 1 and len(form.variant.operand_layouts) == 1:
        return "".join(
            _emit_operand(backend, operand, "    ")
            for operand in form.layouts[0].operands
        )
    layouts = "\n".join(
        _emit_layout(backend, layout, "    ") for layout in form.layouts
    )
    alternatives = ", ".join(
        layout_cpp_name(layout.name) for layout in form.layouts
    )
    return f"""\
{layouts}
    /** @brief Operand-layout alternatives retained from the frontend variant. */
    using Operands = std::variant<{alternatives}>;
    /** @brief Operands bound according to the resolved layout. */
    Operands operands;
"""


def _emit_form(
    backend: BackendSpec,
    instruction: ProjectedInstruction,
    form: ProjectedForm,
) -> str:
    """Emit one projected frontend variant declaration."""
    name = variant_cpp_name(instruction.opcode, form.variant.name)
    layout_markers = "".join(
        f"    // YAML layout: {form.variant.name}/{layout.name}\n"
        for layout in form.layouts
    )
    modifiers = _emit_modifiers(backend, form)
    operands = _emit_form_operands(backend, form)
    return f"""\
  /** @brief Fully-bound `{form.variant.name}` instruction form. */
  struct {name} {{
{layout_markers}{modifiers}{operands}    /** @brief Compare fully-bound form fields. */
    constexpr bool operator==(const {name}&) const noexcept = default;
    /** @brief Format this form as stable execution-IR diagnostics. */
    [[nodiscard]] auto to_string() const -> std::string;
  }};"""


def _emit_instruction(backend: BackendSpec, instruction: ProjectedInstruction) -> str:
    """Emit one opcode record declaration and every frontend form it declares."""
    class_name = instruction_cpp_name(instruction.opcode)
    forms = "\n".join(
        _emit_form(backend, instruction, form) for form in instruction.forms
    )
    alternatives = ", ".join(
        variant_cpp_name(instruction.opcode, form.variant.name)
        for form in instruction.forms
    )
    return f"""\
/** @brief Fully-bound `{instruction.opcode}` execution instruction. */
struct {class_name} {{
  /** @brief Pure opcode tag used by generic instruction dispatch. */
  inline static constexpr Op opcode = Op::{op_enum_name(instruction.opcode)};
{forms}
  /** @brief Projected frontend form for this opcode. */
  using Variant = std::variant<{alternatives}>;
  /** @brief Predicate evaluated before this instruction observes operands. */
  std::optional<Predicate> execution_predicate;
  /** @brief Fully-bound projected form. */
  Variant variant;
  /** @brief Compare predicate and projected form. */
  constexpr bool operator==(const {class_name}&) const noexcept = default;
  /** @brief Format this opcode record as stable execution-IR diagnostics. */
  [[nodiscard]] auto to_string() const -> std::string;
}};"""


def _emit_layout_source(
    instruction: ProjectedInstruction,
    form: ProjectedForm,
    layout: OperandLayoutSpec,
) -> str:
    """Emit the out-of-line diagnostic definition for one operand layout."""
    class_name = instruction_cpp_name(instruction.opcode)
    form_name = variant_cpp_name(instruction.opcode, form.variant.name)
    layout_name = layout_cpp_name(layout.name)
    operands = _emit_operand_appends(layout.operands, "", "output", "  ")
    return f"""\
auto {class_name}::{form_name}::{layout_name}::to_string() const -> std::string {{
  std::string output;
{operands}
  return output;
}}"""


def _emit_form_source(
    backend: BackendSpec,
    instruction: ProjectedInstruction,
    form: ProjectedForm,
) -> str:
    """Emit the out-of-line diagnostic definition for one instruction form."""
    class_name = instruction_cpp_name(instruction.opcode)
    form_name = variant_cpp_name(instruction.opcode, form.variant.name)
    modifiers = _emit_modifier_appends(backend, form)
    if len(form.layouts) == 1 and len(form.variant.operand_layouts) == 1:
        fields = _emit_operand_appends(
            form.layouts[0].operands, "", "operands", "  "
        )
        operands = f"""\
  std::string operands;
{fields}  if (!operands.empty()) {{
    output += ' ';
    output += operands;
  }}
"""
    else:
        operands = """\
  const auto operand_text = std::visit(
      [](const auto& operand_layout) { return operand_layout.to_string(); },
      operands);
  if (!operand_text.empty()) {
    output += ' ';
    output += operand_text;
  }
"""
    body = f"{modifiers}{operands}".rstrip()
    return f"""\
auto {class_name}::{form_name}::to_string() const -> std::string {{
  std::string output;
{body}
  return output;
}}"""


def _emit_instruction_source(
    backend: BackendSpec, instruction: ProjectedInstruction
) -> str:
    """Emit out-of-line diagnostics for one opcode record and its forms."""
    class_name = instruction_cpp_name(instruction.opcode)
    layouts = "\n\n".join(
        _emit_layout_source(instruction, form, layout)
        for form in instruction.forms
        for layout in form.layouts
        if len(form.layouts) != 1 or len(form.variant.operand_layouts) != 1
    )
    forms = "\n\n".join(
        _emit_form_source(backend, instruction, form)
        for form in instruction.forms
    )
    definitions = "\n\n".join(part for part in (layouts, forms) if part)
    return f"""\
{definitions}

auto {class_name}::to_string() const -> std::string {{
  std::string output;
  if (execution_predicate) {{
    output += '@';
    output += ::ptxsim::exec_ir::to_string(*execution_predicate);
    output += ' ';
  }}
  output += "{instruction.opcode}";
  output += std::visit(
      [](const auto& form) {{ return form.to_string(); }}, variant);
  return output;
}}"""


def header(backend: BackendSpec, projected: tuple[ProjectedInstruction, ...]) -> str:
    """Return the public C++ declarations derived from frontend topology."""
    classes = [instruction_cpp_name(item.opcode) for item in projected]
    opcodes = ", ".join(op_enum_name(item.opcode) for item in projected)
    instructions = "\n\n".join(
        _emit_instruction(backend, instruction) for instruction in projected
    )
    alternatives = ", ".join(classes)
    return f"""\
// Generated by ptxsim_exec_ir_codegen. Do not edit.
#pragma once

#include <concepts>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>

#include <ptxsim/exec_ir/exec_ir_types.hpp>

namespace {backend.namespace} {{

/** @brief Pure execution opcode used by static executor dispatch. */
enum class Op : std::uint8_t {{ {opcodes}, count }};

{instructions}

/** @brief Flat sum of execution-IR opcode records. */
using Instruction = std::variant<{alternatives}>;
/** @brief A type held by the generated execution instruction sum. */
template <typename T>
concept InstructionAlternative = requires {{
  {{ std::remove_cvref_t<T>::opcode }} -> std::same_as<const Op&>;
}};
/** @brief Return the pure opcode independent of modifiers and operands. */
[[nodiscard]] constexpr auto op(const Instruction& value) noexcept -> Op {{
  return std::visit(
      []<InstructionAlternative T>(const T&) constexpr {{ return T::opcode; }},
      value);
}}
/** @brief Return the predicate stored by the projected opcode record. */
[[nodiscard]] constexpr auto execution_predicate(const Instruction& value)
    -> const std::optional<Predicate>& {{
  return std::visit(
      []<InstructionAlternative T>(const T& item) constexpr
          -> const std::optional<Predicate>& {{ return item.execution_predicate; }},
      value);
}}
/** @brief Format any generated opcode record without execution validation. */
[[nodiscard]] auto to_string(const Instruction& value) -> std::string;

}}  // namespace {backend.namespace}
"""


def source(backend: BackendSpec, projected: tuple[ProjectedInstruction, ...]) -> str:
    """Return private C++ diagnostic definitions for the generated declarations."""
    instructions = "\n\n".join(
        _emit_instruction_source(backend, instruction)
        for instruction in projected
    )
    return f"""\
// Generated by ptxsim_exec_ir_codegen. Do not edit.
#include <ptxsim/exec_ir/exec_ir.gen.hpp>

#include "exec_ir_diagnostic.hpp"

namespace {backend.namespace} {{

{instructions}

auto to_string(const Instruction& value) -> std::string {{
  return std::visit(
      []<InstructionAlternative T>(const T& item) {{ return item.to_string(); }},
      value);
}}

}}  // namespace {backend.namespace}
"""
