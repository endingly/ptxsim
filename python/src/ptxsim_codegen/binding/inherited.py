"""Generate the complete checked frontend-to-owned bridge from one snapshot."""

from __future__ import annotations

from dataclasses import dataclass
import json
from pathlib import Path

from ptxsim_codegen.artifacts import atomic_write
from ptxsim_codegen.model.names import cpp_identifier
from ptxsim_codegen.model.snapshot import FieldStorage, Form, Snapshot, ValueKind


def _layout_name(value: str) -> str:
    """Use the owned layout name policy without importing a second ISA model."""
    import re
    return ''.join(part[:1].upper() + part[1:] for part in re.split(r'[^A-Za-z0-9]+', value) if part) + 'Operands'


@dataclass(frozen=True)
class BindingArtifacts:
    """Every complete bridge artifact, including unused/new semantic forms."""

    headers: tuple[str, ...]
    sources: tuple[str, ...]


def artifacts(model: Snapshot) -> BindingArtifacts:
    """Partition exact bridge methods into bounded translation units."""
    sources = ['binding_dispatch.gen.cpp', 'binding_domains.gen.cpp']
    for op in model.instructions:
        for index in range((len(op.forms) + 63) // 64):
            sources.append(f'{op.category}_{op.opcode}_binding_{index:03d}.gen.cpp')
    return BindingArtifacts(('binding_bridge.gen.hpp', 'binding_domains.gen.hpp'), tuple(sources))


def _domain_function(kind: ValueKind) -> str:
    """Keep conversion identity tied to the semantic domain rather than a field."""
    return 'convert_' + cpp_identifier(kind.value)


def domains_header(model: Snapshot) -> str:
    """Declare all native enum conversions and every required typed leaf binder."""
    lines = ['// Generated from the immutable execution snapshot. Do not edit.\n#pragma once\n',
             '#include <expected>\n#include <optional>\n',
             '#include <ptx_frontend/resolved_ir/ptx_resolved_ir_foundation.hpp>\n',
             '#include <ptxsim/exec_ir/owned_operands.hpp>\n',
             '#include <ptxsim/exec_ir_lowering/exec_ir_lowering.hpp>\n',
             'namespace ptxsim::exec_ir_lowering::detail {\n',
             '/** @brief Cold-only owner/identity tables used by every typed binder. */\nstruct BindingContext;\n']
    for domain in model.domains:
        if len({atom.frontend_cpp for atom in domain.values}) != len(domain.values):
            # These require the exact form's selected domain/context; every
            # dynamic control is converted in its own checked bridge below.
            continue
        source_type = 'bool' if domain.frontend_cpp_type == 'bool' else 'ptx_frontend::resolved_ir::' + domain.frontend_cpp_type
        target_type = 'bool' if domain.cpp_type == 'bool' else 'exec_ir::' + domain.cpp_type
        lines += [f'/** @brief Convert complete {domain.kind.value} vocabulary without execution filtering. */\n',
                  f'[[nodiscard]] auto {_domain_function(domain.kind)}({source_type}) noexcept -> std::optional<{target_type}>;\n']
    for leaf in model.leaves:
        lines += [f'/** @brief Bind every nested {leaf.kind.value} alternative to owned execution identities. */\n',
                  f'[[nodiscard]] auto {leaf.binder}(const ptx_frontend::resolved_ir::{leaf.frontend_cpp_type}&, const BindingContext&) -> std::expected<exec_ir::{leaf.cpp_type}, LoweringError>;\n']
    lines += ['/** @brief Preserve the actual selected atomic qualifier independently from canonical space. */\n',
              '[[nodiscard]] auto bind_atomic_address_qualifier(ptx_frontend::resolved_ir::AtomicAddressQualifier) noexcept -> std::optional<exec_ir::AtomicAddressQualifier>;\n',
              '}  // namespace ptxsim::exec_ir_lowering::detail\n']
    return ''.join(lines)


def domains_source(model: Snapshot) -> str:
    """Emit explicit source equivalence conversion with no duplicate switch cases."""
    lines = ['// Generated from the immutable execution snapshot. Do not edit.\n',
             '#include "binding_domains.gen.hpp"\nnamespace ptxsim::exec_ir_lowering::detail {\nusing namespace ptx_frontend::resolved_ir;\n']
    for domain in model.domains:
        if len({atom.frontend_cpp for atom in domain.values}) != len(domain.values):
            continue
        source_type = 'bool' if domain.frontend_cpp_type == 'bool' else 'ptx_frontend::resolved_ir::' + domain.frontend_cpp_type
        target_type = 'bool' if domain.cpp_type == 'bool' else 'exec_ir::' + domain.cpp_type
        lines += [f'auto {_domain_function(domain.kind)}({source_type} value) noexcept -> std::optional<{target_type}> {{\n']
        if domain.cpp_type == 'bool':
            lines += ['  return value;\n}\n']
            continue
        groups = {}
        for atom in domain.values:
            groups.setdefault(atom.frontend_cpp, []).append(atom)
        for source, atoms in groups.items():
            selected, = atoms
            lines += [f'  if (value == ptx_frontend::resolved_ir::{source}) return exec_ir::{selected.cpp};\n']
        lines += ['  return std::nullopt;\n}\n']
    lines += ['auto bind_atomic_address_qualifier(ptx_frontend::resolved_ir::AtomicAddressQualifier value) noexcept -> std::optional<exec_ir::AtomicAddressQualifier> {\n  switch (value) {\n']
    for atom in model.atomic_qualifier_domain.values:
        lines += [f'    case ptx_frontend::resolved_ir::{atom.frontend_cpp}: return exec_ir::{atom.cpp};\n']
    lines += ['  }\n  return std::nullopt;\n}\n}  // namespace ptxsim::exec_ir_lowering::detail\n']
    return ''.join(lines)


def bridge_header() -> str:
    """Declare the exact identity preflight independently from frontend checking."""
    return '''// Generated from the immutable execution snapshot. Do not edit.
#pragma once
#include <expected>
#include <memory>
#include <ptx_frontend/resolved_ir/ptx_instruction_base.hpp>
#include <ptxsim/exec_ir_lowering/exec_ir_lowering.hpp>
namespace ptxsim::exec_ir_lowering::detail {
/** @brief Cold-only source ownership and dense identity joins. */
struct BindingContext;
/** @brief Reject null, unknown kind and lying dynamic type before frontend visitors. */
[[nodiscard]] auto preflight_instruction(const ptx_frontend::resolved_ir::Instruction*) noexcept -> bool;
/** @brief Bind one exact final frontend object completely or return a structured error. */
[[nodiscard]] auto bind_instruction(const ptx_frontend::resolved_ir::Instruction&, const BindingContext&) -> std::expected<std::unique_ptr<exec_ir::Instruction>, LoweringError>;
}  // namespace ptxsim::exec_ir_lowering::detail
'''


def _form_bridge(form: Form, leaves) -> str:
    """Bind every actual member after exact RTTI and selected-layout checks."""
    name = form.cpp_name
    lines = [f'/** @brief Check actual final identity before any {name} specific-field read. */\n',
             f'auto matches_{name}(const ptx_frontend::resolved_ir::Instruction& input) noexcept -> bool {{\n',
             f'  return dynamic_cast<const ptx_frontend::resolved_ir::{form.frontend_cpp_name}*>(&input) != nullptr;\n}}\n',
             f'/** @brief Fully bind {name} into native owned fields and controls. */\n',
             f'auto lower_{name}(const ptx_frontend::resolved_ir::Instruction& input, const BindingContext& context) -> std::expected<std::unique_ptr<exec_ir::Instruction>, LoweringError> {{\n',
             f'  const auto* typed = dynamic_cast<const ptx_frontend::resolved_ir::{form.frontend_cpp_name}*>(&input);\n',
             '  if (!typed) return malformed(context);\n  const auto& source = *typed;\n',
             '  std::optional<exec_ir::Predicate> predicate;\n',
             '  if (source.execution_predicate) {\n    auto result = bind_predicate(source.execution_predicate->value, context);\n    if (!result) return std::unexpected(result.error());\n    predicate = std::move(*result);\n  }\n',
             f'  exec_ir::{name}::Controls controls;\n']
    for field in form.modifiers:
        if field.storage is FieldStorage.INSTANCE:
            member = cpp_identifier(field.cpp_name)
            type_ = 'bool' if field.cpp_type == 'bool' else 'exec_ir::' + field.cpp_type
            lines += [f'  std::optional<{type_}> converted_{member};\n']
            for group in field.source_groups:
                source_value = (group.frontend_cpp if field.cpp_type == 'bool' else
                                'ptx_frontend::resolved_ir::' + group.frontend_cpp)
                target_value = group.canonical.cpp if field.cpp_type == 'bool' else 'exec_ir::' + group.canonical.cpp
                lines += [f'  if (source.{field.frontend.member}.value == {source_value}) converted_{member} = {target_value};\n']
            lines += [f'  if (!converted_{member}) return malformed(context);\n',
                      f'  controls.{member} = *converted_{member};\n']
    if form.atomic_qualifier is not None:
        lines += ['  const auto selected_qualifier = bind_atomic_address_qualifier(source.address_qualifier.value);\n',
                  '  if (!selected_qualifier) return malformed(context);\n  controls.address_qualifier = *selected_qualifier;\n']
    lines += ['  switch (source.operand_layout.value) {\n']
    all_optional = {field.frontend.member for layout in form.layouts for field in layout.fields if field.frontend.optional}
    for layout in form.layouts:
        selected = {field.frontend.member for field in layout.fields}
        lines += [f'    case {layout.index}: {{\n']
        for member in sorted(all_optional):
            condition = f'!source.{member}' if member in selected else f'source.{member}.has_value()'
            lines += [f'      if ({condition}) return malformed(context);\n']
        lines += [f'      exec_ir::{name}::{_layout_name(layout.identity.name)} operands;\n']
        for field in layout.fields:
            member = cpp_identifier(field.cpp_name)
            access = f'source.{field.frontend.member}' + ('->value' if field.frontend.optional else '.value')
            lines += [f'      auto bound_{member} = {leaves[field.value_kind].binder}({access}, context);\n',
                      f'      if (!bound_{member}) return std::unexpected(bound_{member}.error());\n',
                      f'      operands.{member} = std::move(*bound_{member});\n']
        lines += [f'      auto result = std::make_unique<exec_ir::{name}>(std::move(operands), std::move(controls), std::move(predicate));\n',
                  '      if (!exec_ir::structurally_valid(*result)) return malformed(context);\n',
                  '      return result;\n    }\n']
    lines += ['    default: return malformed(context);\n  }\n}\n']
    return ''.join(lines)


def shard_source(model: Snapshot, op, index: int, forms: tuple[Form, ...]) -> str:
    """Include only the source opcode and one narrow owned shard."""
    return ''.join([
        '// Generated from the immutable execution snapshot. Do not edit.\n',
        '#include "binding_bridge.gen.hpp"\n#include "binding_domains.gen.hpp"\n#include "lowering_detail.hpp"\n',
        f'#include <ptx_frontend/resolved_ir/model/{op.category}/{op.opcode}.gen.hpp>\n',
        f'#include <ptxsim/exec_ir/model/{op.category}/{op.opcode}_forms_{index:03d}.gen.hpp>\n',
        'namespace ptxsim::exec_ir_lowering::detail {\nusing namespace ptx_frontend::resolved_ir;\n',
        *(_form_bridge(form, {leaf.kind: leaf for leaf in model.leaves}) for form in forms),
        '}  // namespace ptxsim::exec_ir_lowering::detail\n'])


def dispatch_source(model: Snapshot) -> str:
    """Generate exhaustive safe switches without source opcode-name diagnostics."""
    lines = ['// Generated from the immutable execution snapshot. Do not edit.\n',
             '#include "binding_bridge.gen.hpp"\n#include "lowering_detail.hpp"\n',
             '#include <ptx_frontend/resolved_ir/ptx_instruction_catalogue.gen.hpp>\n',
             'namespace ptxsim::exec_ir_lowering::detail {\n']
    for form in model.forms:
        lines += [f'/** @brief Exact final identity preflight for {form.cpp_name}. */\n',
                  f'auto matches_{form.cpp_name}(const ptx_frontend::resolved_ir::Instruction&) noexcept -> bool;\n',
                  f'/** @brief Complete exact form binder for {form.cpp_name}. */\n',
                  f'auto lower_{form.cpp_name}(const ptx_frontend::resolved_ir::Instruction&, const BindingContext&) -> std::expected<std::unique_ptr<exec_ir::Instruction>, LoweringError>;\n']
    lines += ['auto preflight_instruction(const ptx_frontend::resolved_ir::Instruction* source) noexcept -> bool {\n',
              '  if (!source) return false;\n  switch (source->instruction_kind()) {\n']
    for form in model.forms:
        lines += [f'    case ptx_frontend::resolved_ir::InstructionKind::{form.frontend_cpp_name}: return matches_{form.cpp_name}(*source);\n']
    lines += ['    default: return false;\n  }\n}\n',
              'auto bind_instruction(const ptx_frontend::resolved_ir::Instruction& source, const BindingContext& context) -> std::expected<std::unique_ptr<exec_ir::Instruction>, LoweringError> {\n',
              '  switch (source.instruction_kind()) {\n']
    for form in model.forms:
        lines += [f'    case ptx_frontend::resolved_ir::InstructionKind::{form.frontend_cpp_name}: return lower_{form.cpp_name}(source, context);\n']
    lines += ['    default: return malformed(context);\n  }\n}\n}  // namespace ptxsim::exec_ir_lowering::detail\n']
    return ''.join(lines)


def generate(model: Snapshot, root: Path) -> BindingArtifacts:
    """Publish full typed binding, requiring every declared leaf binder at link."""
    plan = artifacts(model)
    atomic_write(root / 'binding_bridge.gen.hpp', bridge_header())
    atomic_write(root / 'binding_domains.gen.hpp', domains_header(model))
    atomic_write(root / 'binding_domains.gen.cpp', domains_source(model))
    atomic_write(root / 'binding_dispatch.gen.cpp', dispatch_source(model))
    for op in model.instructions:
        for index, start in enumerate(range(0, len(op.forms), 64)):
            atomic_write(root / f'{op.category}_{op.opcode}_binding_{index:03d}.gen.cpp',
                         shard_source(model, op, index, op.forms[start:start + 64]))
    atomic_write(root / 'artifact_plan.json', json.dumps({'headers': plan.headers, 'sources': plan.sources}, indent=2) + '\n')
    return plan
