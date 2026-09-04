"""Emit complete resolved-IR topology lowering with narrow leaf bindings."""

from __future__ import annotations

import ptx_frontend.code_gen.cpp_backend
import ptx_frontend.ir.resolved_ir
import ptx_frontend.spec.model

from .cpp_names import instruction_cpp_name, layout_cpp_name, variant_cpp_name
from .model import BackendSpec, GenerationError, ProjectedForm, ProjectedInstruction

_DOMAINS = {
    "type": "SCALAR_TYPES",
    "rounding": "ROUNDING_MODES",
    "comparison": "COMPARISON_OPERATORS",
    "boolean_op": "BOOLEAN_OPERATORS",
    "cache": "CACHE_OPERATORS",
    "eviction_priority": "EVICTION_PRIORITIES",
    "semantics": "MEMORY_CONSISTENCIES",
    "scope": "MEMORY_SCOPES",
    "vector": "VECTOR_ARITIES",
    "state_space": "MEMORY_STATE_SPACES",
    "phase_type": "MBARRIER_PHASE_TYPES",
    "mbarrier_layout": "MBARRIER_LAYOUTS",
    "proxy": "ASYNC_PROXY_KINDS",
    "proxy_pair": "PROXY_KIND_PAIRS",
}
_RESOLVED_TYPES = {"MemoryStateSpace", "VectorArity"}


def _qualified_type(backend: BackendSpec, cpp_type: str) -> str:
    """Qualify an execution-IR type stored outside the common namespace."""
    if cpp_type == "bool" or "::" in cpp_type:
        return cpp_type
    return f"{backend.namespace}::{cpp_type}"


def _source_type(cpp_type: str) -> str:
    """Return the exact namespace containing one frontend semantic enum."""
    namespace = (
        "ptx_frontend::resolved_ir"
        if cpp_type in _RESOLVED_TYPES
        else "ptx_frontend::base"
    )
    return f"{namespace}::{cpp_type}"


def _field(
    form: ProjectedForm, name: str
) -> ptx_frontend.ir.resolved_ir.ResolvedField:
    """Find one resolved frontend modifier field by its semantic identity."""
    try:
        return next(
            field
            for field in form.source.modifier_fields
            if field.source_name == name
        )
    except StopIteration as error:
        raise GenerationError(
            f"missing resolved modifier field {form.variant.name}/{name}"
        ) from error


def _conversion_name(kind: str) -> str:
    """Return the generated conversion function for one modifier kind."""
    return f"convert_{kind}"


def _target_value(backend: BackendSpec, expression: str) -> str:
    """Qualify one backend enum expression emitted inside the detail namespace."""
    if "::" not in expression:
        return expression
    return f"{backend.namespace}::{expression}"


def _conversion_declarations(backend: BackendSpec) -> str:
    """Emit semantic enum conversions; frontend and execution enums are distinct."""
    chunks: list[str] = []
    for kind, domain_name in _DOMAINS.items():
        domain_id = getattr(
            ptx_frontend.code_gen.cpp_backend.CppDomain, domain_name
        )
        domain = ptx_frontend.code_gen.cpp_backend.cpp_domain(domain_id)
        source_type = _source_type(domain.cpp_type)
        mapping = backend.modifier_kinds[kind]
        target_type = _qualified_type(backend, mapping.cpp_type)
        cases: list[str] = []
        seen: set[str] = set()
        for value, source_value in domain.values.items():
            source_member = source_value.removeprefix(domain.cpp_type + "::")
            if value not in mapping.values or source_member in seen:
                continue
            seen.add(source_member)
            target_value = _target_value(backend, mapping.cpp_value(value))
            cases.append(
                f"    case {source_type}::{source_member}: return {target_value};"
            )
        cases_text = "\n".join(cases)
        chunks.append(
            f"""/** @brief Convert frontend `{kind}` semantics to execution storage. */
[[nodiscard]] constexpr auto {_conversion_name(kind)}(
    {source_type} value) -> std::optional<{target_type}> {{
  switch (value) {{
{cases_text}
  }}
  return std::nullopt;
}}"""
        )
    return "\n\n".join(chunks)


def _modifier_bindings(
    form: ProjectedForm, indent: str
) -> tuple[str, tuple[str, ...]]:
    """Emit conversions for instance modifiers in target aggregate member order."""
    lines, names = [], []
    for modifier in form.variant.modifiers:
        if modifier.presence in {"absent", "fixed"}:
            continue
        source = _field(form, modifier.name)
        if source.storage.name != "INSTANCE":
            raise GenerationError(
                f"unexpected static modifier {form.variant.name}/{modifier.name}"
            )
        name = f"bound_{modifier.name}"
        names.append(name)
        if modifier.kind == "flag":
            lines.append(
                f"{indent}const auto {name} = "
                f"std::optional{{form.{source.name}.value}};"
            )
        else:
            lines.append(
                f"{indent}const auto {name} = "
                f"detail::{_conversion_name(modifier.kind)}("
                f"form.{source.name}.value);"
            )
            lines.append(
                f"{indent}if (!{name})\n"
                f"{indent}  return std::unexpected("
                "detail::unsupported_operand(context));"
            )
    return "\n".join(lines), tuple(names)


def _operand_bindings(
    backend: BackendSpec,
    layout: ptx_frontend.spec.model.OperandLayoutSpec,
    source_layout: ptx_frontend.ir.resolved_ir.ResolvedOperandLayout,
    owner: str,
    indent: str,
) -> tuple[str, tuple[str, ...]]:
    """Emit leaf binding calls in exact paired resolved-field order."""
    lines, names = [], []
    for operand, source in zip(layout.operands, source_layout.fields, strict=True):
        if source.storage.name != "INSTANCE":
            raise GenerationError(
                f"operand storage drift for {layout.name}/{operand.name}"
            )
        name = f"bound_{operand.name}"
        names.append(name)
        target = _qualified_type(backend, backend.operand_kinds[operand.kind].cpp_type)
        lines.append(
            f"{indent}const auto {name} = detail::bind_operand<{target}>(\n"
            f"{indent}    {owner}.{source.name}.value, context);"
        )
        lines.append(
            f"{indent}if (!{name})\n"
            f"{indent}  return std::unexpected({name}.error());"
        )
    return "\n".join(lines), tuple(names)


def _target_form(
    backend: BackendSpec,
    instruction: ProjectedInstruction,
    form: ProjectedForm,
    modifier_names: tuple[str, ...],
    operand_names: tuple[str, ...],
    layout: ptx_frontend.spec.model.OperandLayoutSpec,
) -> str:
    """Emit the target aggregate preserving generated target record nesting."""
    target = (
        f"{backend.namespace}::{instruction_cpp_name(instruction.opcode)}::"
        f"{variant_cpp_name(instruction.opcode, form.variant.name)}"
    )
    active = tuple(
        modifier
        for modifier in form.variant.modifiers
        if modifier.presence not in {"absent", "fixed"}
    )
    modifiers = "\n".join(
        f"  .{modifier.name} = std::move(*{name}),"
        for modifier, name in zip(active, modifier_names, strict=True)
    )
    operands = "\n".join(
        f"  .{operand.name} = std::move(*{name}),"
        for operand, name in zip(layout.operands, operand_names, strict=True)
    )
    if len(form.layouts) == 1:
        members = "\n".join(part for part in (modifiers, operands) if part)
        return f"{target}{{\n{members}\n}}"
    nested_operands = "\n".join(f"    {line}" for line in operands.splitlines())
    modifier_prefix = f"{modifiers}\n" if modifiers else ""
    return f"""{target}{{
{modifier_prefix}  .operands = {target}::Operands{{
    {target}::{layout_cpp_name(layout.name)}{{
{nested_operands}
    }},
  }},
}}"""


def _layout_lowering(
    backend: BackendSpec,
    instruction: ProjectedInstruction,
    form: ProjectedForm,
    layout: ptx_frontend.spec.model.OperandLayoutSpec,
    source_layout: ptx_frontend.ir.resolved_ir.ResolvedOperandLayout,
    indent: str,
) -> str:
    """Emit one paired operand-layout branch and its aggregate construction."""
    owner = "form" if len(form.layouts) == 1 else "operands"
    binds, operands = _operand_bindings(
        backend, layout, source_layout, owner, indent
    )
    modifiers, modifier_names = _modifier_bindings(form, indent)
    record = _target_form(
        backend, instruction, form, modifier_names, operands, layout
    )
    record = "\n".join(f"{indent}    {line}" for line in record.splitlines())
    target = f"{backend.namespace}::{instruction_cpp_name(instruction.opcode)}"
    return f"""{modifiers}
{binds}
{indent}return {target}{{
{indent}  .execution_predicate = std::move(*predicate),
{indent}  .variant = {target}::Variant{{
{record},
{indent}  }},
{indent}}};"""


def _form_lowering(
    backend: BackendSpec,
    instruction: ProjectedInstruction,
    form: ProjectedForm,
) -> str:
    """Emit exact source variant and layout visitation for one target form."""
    source_type = (
        f"ptx_frontend::resolved_ir::{instruction.source.cpp_name}::"
        f"{form.source.cpp_name}"
    )
    if len(form.layouts) == 1:
        lowering = _layout_lowering(
            backend,
            instruction,
            form,
            form.layouts[0],
            form.source.operand_layouts[0],
            "          ",
        )
        return f"""        if constexpr (std::same_as<Form, {source_type}>) {{
{lowering}
        }}"""
    branches: list[str] = []
    for layout, source_layout in zip(
        form.layouts, form.source.operand_layouts, strict=True
    ):
        layout_type = f"{source_type}::{layout_cpp_name(source_layout.layout_id)}"
        lowering = _layout_lowering(
            backend, instruction, form, layout, source_layout, "            "
        )
        branches.append(
            f"""          if constexpr (std::same_as<Operands, {layout_type}>) {{
{lowering}
          }}"""
        )
    branches_text = "\n".join(branches)
    return f"""        if constexpr (std::same_as<Form, {source_type}>) {{
          return std::visit(
              [&](const auto& operands)
                  -> std::expected<exec_ir::Instruction, LoweringError> {{
                using Operands = std::remove_cvref_t<decltype(operands)>;
{branches_text}
                return std::unexpected(detail::unsupported_form(context));
              }}, form.operands);
        }}"""


def _opcode_definition(backend: BackendSpec, instruction: ProjectedInstruction) -> str:
    """Emit lowering for every frontend form of one opcode."""
    forms = "\n".join(
        _form_lowering(backend, instruction, form)
        for form in instruction.forms
    )
    return f"""auto lower_{instruction.opcode}(
    const ptx_frontend::resolved_ir::{instruction.source.cpp_name}& instruction,
    const detail::BindingContext& context)
    -> std::expected<exec_ir::Instruction, LoweringError> {{
  const auto predicate =
      detail::bind_predicate(instruction.execution_predicate, context);
  if (!predicate)
    return std::unexpected(predicate.error());
  return std::visit(
      [&](const auto& form)
          -> std::expected<exec_ir::Instruction, LoweringError> {{
        using Form = std::remove_cvref_t<decltype(form)>;
{forms}
        return std::unexpected(detail::unsupported_form(context));
      }},
      instruction.variant);
}}"""


def _dispatch(instructions: tuple[ProjectedInstruction, ...]) -> str:
    """Emit opcode dispatch over the complete resolved instruction variant."""
    cases = "\n".join(
        f"        if constexpr (std::same_as<Opcode, "
        f"ptx_frontend::resolved_ir::{item.source.cpp_name}>)\n"
        f"          return lower_{item.opcode}(opcode_record, context);"
        for item in instructions
    )
    return f"""auto lower_instruction(
    const ptx_frontend::resolved_ir::ResolvedInstruction& instruction,
    const detail::BindingContext& context)
    -> std::expected<exec_ir::Instruction, LoweringError> {{
  return std::visit(
      [&](const auto& opcode_record)
          -> std::expected<exec_ir::Instruction, LoweringError> {{
        using Opcode = std::remove_cvref_t<decltype(opcode_record)>;
{cases}
        return std::unexpected(LoweringError{{
            LoweringErrorCode::unsupported_instruction, context.function_index,
            context.instruction_index, std::nullopt, std::nullopt}});
      }},
      instruction);
}}"""


def header(backend: BackendSpec, projected: tuple[ProjectedInstruction, ...]) -> str:
    """Return declarations for complete generated lowering topology."""
    declarations = "\n\n".join(
        f"""/** @brief Lower every resolved `{item.opcode}` form. */
[[nodiscard]] auto lower_{item.opcode}(
    const ptx_frontend::resolved_ir::{item.source.cpp_name}& instruction,
    const detail::BindingContext& context)
    -> std::expected<exec_ir::Instruction, LoweringError>;"""
        for item in projected
    )
    return f"""// Generated by ptxsim_exec_ir_codegen. Do not edit.
#pragma once
#include <expected>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include "lowering_detail.hpp"
namespace ptxsim::exec_ir_lowering::generated {{
{declarations}
/** @brief Lower one resolved instruction through complete generated topology. */
[[nodiscard]] auto lower_instruction(
    const ptx_frontend::resolved_ir::ResolvedInstruction& instruction,
    const detail::BindingContext& context)
    -> std::expected<exec_ir::Instruction, LoweringError>;
}}  // namespace ptxsim::exec_ir_lowering::generated
"""


def source(backend: BackendSpec, projected: tuple[ProjectedInstruction, ...]) -> str:
    """Return complete generated lowering implementation."""
    definitions = "\n\n".join(
        _opcode_definition(backend, item) for item in projected
    )
    return f"""// Generated by ptxsim_exec_ir_codegen. Do not edit.
#include "exec_ir_lowering.gen.hpp"
#include <concepts>
#include <type_traits>
#include <utility>
#include <variant>
namespace ptxsim::exec_ir_lowering::detail {{
{_conversion_declarations(backend)}
}}  // namespace ptxsim::exec_ir_lowering::detail
namespace ptxsim::exec_ir_lowering::generated {{
{definitions}
{_dispatch(projected)}
}}  // namespace ptxsim::exec_ir_lowering::generated
"""
