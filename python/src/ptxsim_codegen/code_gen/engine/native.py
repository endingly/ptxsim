"""Emit native typed issue-group adapters while preserving implemented behavior."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import re

from ptxsim_codegen.artifacts import atomic_write
from ptxsim_codegen.model import GenerationError
from ptxsim_codegen.model.snapshot import FieldStorage, Form, Snapshot, ValueKind


_CODECS = {
    'u16': 'std::uint16_t', 'u32': 'std::uint32_t', 'u64': 'std::uint64_t',
    's16': 'std::int16_t', 's32': 'std::int32_t', 's64': 'std::int64_t',
    'f16': 'arith::float16_t', 'f16x2': 'arith::float16x2_t',
    'f32': 'arith::float32_t', 'f32x2': 'arith::float32x2_t',
    'f64': 'arith::float64_t', 'bf16': 'arith::bfloat16_t',
    'bf16x2': 'arith::bfloat16x2_t', 'u8x4': 'u8x4_t',
    'u16x2': 'u16x2_t', 's8x4': 's8x4_t', 's16x2': 's16x2_t',
}
_WIDTHS = {
    'u16': 16, 's16': 16, 'f16': 16, 'bf16': 16,
    'u32': 32, 's32': 32, 'f16x2': 32, 'bf16x2': 32,
    'u8x4': 32, 's8x4': 32, 'u16x2': 32, 's16x2': 32,
    'u64': 64, 's64': 64, 'f32x2': 64, 'f64': 64, 'f32': 32,
}
_VALUE_FORMS = {
    'add': ('add_float_f32', 'add_float_f32x2', 'add_float_f64', 'add_half',
            'add_bfloat', 'add_mixed_f32', 'add_integer_no_sat', 'add_sat', 'add_packed_optional_sat'),
    'sub': ('sub_float_f32', 'sub_float_f32x2', 'sub_float_f64', 'sub_half',
            'sub_bfloat', 'sub_mixed_f32', 'sub_integer_no_sat', 'sub_optional_sat'),
    'mul': ('mul_rn_f32', 'mul_lo_u32', 'mul_hi_u32', 'mul_wide_u32', 'mul_wide_s32'),
    'fma': ('fma_rn_f32', 'fma_directed_f32', 'fma_rn_f64', 'fma_directed_f64',
            'fma_f32x2', 'fma_rn_f16', 'fma_rn_f16x2', 'fma_half_relu',
            'fma_half_oob', 'fma_half_oob_relu', 'fma_bf16', 'fma_bf16x2',
            'fma_bf16_oob', 'fma_bf16x2_oob', 'fma_mixed_f32_f16', 'fma_mixed_f32_bf16'),
}


@dataclass(frozen=True)
class ValueOperand:
    """Exact layout-local field and its projected semantic value type expression."""

    name: str
    fixed: str | None
    selector: str | None


@dataclass(frozen=True)
class ValuePath:
    """One complete typed arithmetic path, selected before entering the lane loop."""

    destination: str
    sources: tuple[str, ...]
    selector: str | None
    value: str | None


def value_paths(form: Form) -> tuple[ValueOperand, tuple[ValueOperand, ...], tuple[ValuePath, ...]]:
    """Resolve supported arithmetic storage/type facts from the full shared model."""
    if form.identity.opcode not in _VALUE_FORMS:
        raise GenerationError(f'{form.identity}: no arithmetic handler ABI')
    if len(form.layouts) != 1:
        raise GenerationError(f'{form.identity}: value arithmetic requires one actual layout')
    _check_controls(form, {'type', 'result_type', 'input_type', 'rounding', 'ftz', 'sat', 'relu', 'oob', 'lo', 'hi', 'wide'})
    modifiers = {field.identity.name: field for field in form.modifiers}
    semantic_types = {field.constant.spelling for field in form.modifiers
                      if field.value_kind is ValueKind.SCALAR_TYPE and field.constant is not None
                      and field.constant.spelling in _CODECS}
    writes, reads = [], []
    for field in form.layouts[0].fields:
        binding = field.binding
        if binding is None:
            raise GenerationError(f'{field.identity}: arithmetic field has no binding policy')
        if binding.target_field_id != field.identity.name:
            raise GenerationError(f'{field.identity}: binding does not name this layout-local field')
        if binding.register_width_policy.name not in {'SAME_WIDTH', 'EXACT'}:
            raise GenerationError(f'{field.identity}: arithmetic requires exact or same-width storage')
        expression = binding.type_expression
        fixed, selector = None, None
        if expression.kind.name == 'FIXED_SCALAR':
            fixed = expression.scalar_type
            if fixed in {'b16', 'b32', 'b64'}:
                candidates = [type_ for type_ in semantic_types if _WIDTHS[type_] == int(fixed[1:])]
                if len(candidates) != 1:
                    raise GenerationError(f'{field.identity}: ambiguous arithmetic container type {candidates}')
                fixed = candidates[0]
            if fixed not in _CODECS:
                raise GenerationError(f'{field.identity}: unsupported implemented arithmetic codec {fixed}')
        elif expression.kind.name == 'MODIFIER_FIELD':
            selector = expression.modifier_field_id
            if selector not in modifiers:
                raise GenerationError(f'{field.identity}: unresolved arithmetic type selector')
        else:
            raise GenerationError(f'{field.identity}: unimplemented arithmetic type expression')
        operand = ValueOperand(field.cpp_name, fixed, selector)
        if binding.access.name == 'WRITE':
            if field.value_kind is not ValueKind.REGISTER:
                raise GenerationError(f'{field.identity}: arithmetic output is not a scalar register')
            writes.append(operand)
        elif binding.access.name == 'READ' and field.value_kind in {ValueKind.REGISTER, ValueKind.REG_OR_IMM}:
            reads.append(operand)
        else:
            raise GenerationError(f'{field.identity}: unimplemented arithmetic operand access/shape')
    if len(writes) != 1 or len(reads) != (3 if form.identity.opcode == 'fma' else 2):
        raise GenerationError(f'{form.identity}: wrong implemented arithmetic arity')
    operands = [writes[0], *reads]
    selected = {}
    dynamic = []
    for role in {operand.selector for operand in operands if operand.selector is not None}:
        control = modifiers[role]
        if control.storage is FieldStorage.CONSTANT:
            selected[role] = control.constant.spelling
        else:
            dynamic.append(control)
    if len(dynamic) > 1:
        raise GenerationError(f'{form.identity}: multiple dynamic arithmetic types')

    def path(values: dict[str, str], selector: str | None, value: str | None) -> ValuePath:
        """Resolve exact codec types without conflating container and semantic values."""
        types = [operand.fixed if operand.fixed is not None else values[operand.selector] for operand in operands]
        if any(type_ not in _CODECS for type_ in types):
            raise GenerationError(f'{form.identity}: unimplemented arithmetic type path {types}')
        return ValuePath(_CODECS[types[0]], tuple(_CODECS[type_] for type_ in types[1:]), selector, value)

    if not dynamic:
        paths = (path(selected, None, None),)
    else:
        control = dynamic[0]
        paths = tuple(path({**selected, control.identity.name: atom.spelling}, control.cpp_name, atom.cpp)
                      for atom in control.domain if atom.spelling in _CODECS)
        if not paths:
            raise GenerationError(f'{form.identity}: no implemented dynamic arithmetic type')
    return writes[0], tuple(reads), paths


def _value_body(form: Form, destination: ValueOperand, sources: tuple[ValueOperand, ...], path: ValuePath) -> str:
    """Prepare one lane through existing numeric semantics with typed owned leaves."""
    lines = ['    const auto registers = resolver.resolve();\n    if (!registers) return std::unexpected(registers.error());\n',
             '    const auto& operands = form.operands;\n']
    for index, (source, codec) in enumerate(zip(sources, path.sources, strict=True)):
        lines += [f'    const auto source_{index} = read_value<{codec}>(registers->get(), operands.{source.name});\n',
                  f'    if (!source_{index}) return std::unexpected(source_{index}.error());\n']
    lines += [f'    if (const auto destination = resolve_destination<{path.destination}>(registers->get(), operands.{destination.name}); !destination) return std::unexpected(destination.error());\n',
              f'    const auto result = semantics::{form.identity.opcode}(arithmetic, form' + ''.join(f', *source_{index}' for index in range(len(sources))) + ');\n',
              '    if (!result) return std::unexpected(LaneFaultCause{result.error()});\n',
              f'    return stage_register_write<{path.destination}>(registers->get(), operands.{destination.name}, *result, *successor);\n']
    return ''.join(lines)


def _group_signature(name: str) -> str:
    """Use one uniform issue entry signature for the dense private table."""
    return f'''auto {name}(runtime::LaunchRuntime& runtime, common::FunctionId function,
    const arith::context& arithmetic, execution_model::Warp& warp,
    const execution_model::WarpIssueGroup& issue, const exec_ir::Instruction& base,
    std::optional<common::ProgramCounter> successor) -> std::expected<PreparedIssue, StepError>'''


def value_group(form: Form) -> str:
    """Emit a direct typed group with all static admission before lane preparation."""
    destination, sources, paths = value_paths(form)
    lines = [f'/** @brief Prepare one issued {form.cpp_name} group with one exact type selection. */\n',
             _group_signature('group_' + form.cpp_name), ' {\n',
             f'  const auto& form = static_cast<const exec_ir::{form.cpp_name}&>(base);\n',
             f'  if (!exec_ir::validate_{form.cpp_name}(form)) return step_error(StepErrorCode::invalid_instruction);\n',
             f'  if (!semantics::valid_{form.identity.opcode}(form)) return step_error(StepErrorCode::unsupported_instruction);\n']
    dynamic = paths[0].selector is not None
    if dynamic:
        lines += [f'  switch (form.{paths[0].selector}) {{\n']
    for path in paths:
        if dynamic:
            lines += [f'    case exec_ir::{path.value}: {{\n']
        lines += ['  auto prepare = [&](LaneResourceResolver& resolver, const arith::context& arithmetic, std::optional<common::ProgramCounter> successor) -> std::expected<PreparedEffect, LaneFaultCause> {\n',
                  _value_body(form, destination, sources, path), '  };\n',
                  '  return prepare_group(runtime, function, arithmetic, warp, issue, form, successor, prepare, PrepareKind::scalar);\n']
        if dynamic:
            lines += ['    }\n']
    if dynamic:
        lines += ['    default: return step_error(StepErrorCode::unsupported_instruction);\n  }\n']
    lines += ['}\n']
    return ''.join(lines)


def selected_value_forms(model: Snapshot) -> tuple[Form, ...]:
    """Locate every previously implemented numeric form without filtering the model."""
    by_id = {(form.identity.opcode, form.identity.variant): form for form in model.forms}
    result = []
    for opcode, names in _VALUE_FORMS.items():
        for name in names:
            if (opcode, name) not in by_id:
                raise GenerationError(f'missing implemented semantic form: {opcode}/{name}')
            result.append(by_id[opcode, name])
    return tuple(result)


def generate_value_probe(model: Snapshot, path: Path) -> None:
    """Publish a bounded arithmetic typed-group probe for focused compilation."""
    forms = selected_value_forms(model)
    lines = ['// Generated typed arithmetic groups. Do not edit.\n',
             '#include "group_execution.hpp"\n#include "value_alu_helpers.hpp"\n',
             *[f'#include "semantics/{opcode}_semantics.hpp"\n' for opcode in _VALUE_FORMS],
             _includes(forms), _validation_declarations(forms), 'namespace ptxsim::inst_execute_engine::detail {\n']
    lines += [value_group(form) for form in forms]
    lines += ['}  // namespace ptxsim::inst_execute_engine::detail\n']
    atomic_write(path, ''.join(lines))


def _typed_group(form: Form, admission: str, preparation: str) -> str:
    """Emit one closed typed issue entry with admission ahead of all lane activity."""
    admission = _owned_fields(admission, form.layouts[0])
    preparation = _owned_fields(preparation, form.layouts[0])
    return (f'/** @brief Prepare an issued {form.cpp_name} through its exact owned form. */\n'
            + _group_signature('group_' + form.cpp_name) + ' {\n'
            + f'  const auto& form = static_cast<const exec_ir::{form.cpp_name}&>(base);\n'
            + f'  if (!exec_ir::validate_{form.cpp_name}(form)) return step_error(StepErrorCode::invalid_instruction);\n'
            + admission
            + '  auto prepare = [&](LaneResourceResolver& resolver, const arith::context&, std::optional<common::ProgramCounter> successor) -> std::expected<PreparedEffect, LaneFaultCause> {\n'
            + preparation + '  };\n'
            + '  return prepare_group(runtime, function, arithmetic, warp, issue, form, successor, prepare, PrepareKind::scalar);\n}\n')


def _layout_group(form: Form, admission: str,
                  layouts: tuple[tuple[str, str, str], ...], kind: str = 'scalar') -> str:
    """Select the exact layout once before entering its statically typed lane loop."""
    lines = [f'/** @brief Prepare one exact issued {form.cpp_name} without lane dispatch. */\n',
             _group_signature('group_' + form.cpp_name), ' {\n',
             f'  const auto& form = static_cast<const exec_ir::{form.cpp_name}&>(base);\n',
             f'  if (!exec_ir::validate_{form.cpp_name}(form)) return step_error(StepErrorCode::invalid_instruction);\n', admission]
    for layout, guard, preparation in layouts:
        source_layout = next(item for item in form.layouts if _layout_cpp(item.identity.name) == layout)
        guard = _owned_fields(guard, source_layout)
        preparation = _owned_fields(preparation, source_layout)
        selected = ('  {\n    const auto& operands = form.operands;\n' if len(form.layouts) == 1 else
                    f'  if (const auto* selected = std::get_if<exec_ir::{form.cpp_name}::{layout}>(&form.operands)) {{\n    const auto& operands = *selected;\n')
        lines += [selected, guard,
                  '    auto prepare = [&](LaneResourceResolver& resolver, const arith::context& arithmetic, std::optional<common::ProgramCounter> successor) -> std::expected<PreparedEffect, LaneFaultCause> {\n',
                  preparation, '    };\n',
                  f'    return prepare_group(runtime, function, arithmetic, warp, issue, form, successor, prepare, PrepareKind::{kind});\n  }}\n']
    lines += ['  return step_error(StepErrorCode::invalid_instruction);\n}\n']
    return ''.join(lines)


def mov_group(form: Form) -> str:
    """Keep movement capability separate from complete native operand binding."""
    if form.identity.variant in {'mov_scalar', 'mov_b128_pack_unpack'}:
        layouts = []
        if form.identity.variant == 'mov_scalar':
            scalar_layout = next(layout.index for layout in form.layouts if layout.identity.name == 'scalar')
            structural = f'    if (!valid_scalar_move_shape(operands.src, form.type, form.static_contract(), {scalar_layout})) return step_error(StepErrorCode::invalid_instruction);\n'
            layouts.append(('ScalarOperands', structural + '''    if (!valid_movement_type(form.type) || form.type == exec_ir::DataType::b128 ||
        std::holds_alternative<exec_ir::FunctionRef>(operands.src) ||
        std::holds_alternative<exec_ir::OpaqueSymbolRef>(operands.src) ||
        !valid_scalar_move_source(operands.src, form.type))
      return step_error(StepErrorCode::unsupported_instruction);
''', '      return prepare_scalar_move(resolver, form.type, operands.dst.slot, operands.src, successor);\n'))
        layouts += [('PackOperands', '    if (!valid_aggregate_move_shape(operands.src, form.type, true)) return step_error(StepErrorCode::invalid_instruction);\n    if (!valid_movement_type(form.type) || !valid_aggregate_move(operands.src, form.type, true)) return step_error(StepErrorCode::unsupported_instruction);\n',
                     '      return prepare_pack_move(resolver, form.type, operands.dst.slot, operands.src, successor);\n'),
                    ('UnpackOperands', '    if (!valid_aggregate_move_shape(operands.dst, form.type, false)) return step_error(StepErrorCode::invalid_instruction);\n    if (!valid_movement_type(form.type) || !valid_aggregate_move(operands.dst, form.type, false)) return step_error(StepErrorCode::unsupported_instruction);\n',
                     '      return prepare_unpack_move(resolver, form.type, operands.dst, operands.src.slot, successor);\n')]
        return _layout_group(form, '', tuple(layouts))
    if form.identity.variant == 'mov_v4_u32':
        admission = '''  if (form.vector != exec_ir::VectorArity::v4 || form.type != exec_ir::DataType::u32 ||
      !std::holds_alternative<exec_ir::VectorSpecialRegisterRef>(form.operands.src))
    return step_error(StepErrorCode::unsupported_instruction);
  const auto intrinsic = std::get<exec_ir::VectorSpecialRegisterRef>(form.operands.src).register_ref.id.kind;
  if (intrinsic != exec_ir::SpecialRegisterKind::Tid && intrinsic != exec_ir::SpecialRegisterKind::NTid &&
      intrinsic != exec_ir::SpecialRegisterKind::CtaId && intrinsic != exec_ir::SpecialRegisterKind::NCtaId)
    return step_error(StepErrorCode::unsupported_instruction);
'''
        preparation = '''    return prepare_vector_special_move(resolver, form.vector, form.type, form.operands.dst,
        std::get<exec_ir::VectorSpecialRegisterRef>(form.operands.src), successor);
'''
    elif form.identity.variant == 'mov_pred':
        admission = '  if (std::holds_alternative<exec_ir::PredicateSpecialRegister>(form.operands.src)) return step_error(StepErrorCode::unsupported_instruction);\n'
        preparation = '    return prepare_predicate_move(resolver, form.operands.dst, form.operands.src, successor);\n'
    else:
        raise GenerationError(f'{form.identity}: movement handler not implemented')
    return _typed_group(form, admission, preparation)


def generate_movement_probe(model: Snapshot, path: Path) -> None:
    """Publish owned movement entries for a focused native compile before integration."""
    forms = [form for form in model.forms if form.identity.opcode == 'mov'
             and form.identity.variant in {'mov_scalar', 'mov_b128_pack_unpack', 'mov_v4_u32', 'mov_pred'}]
    if len(forms) != 4:
        raise GenerationError('missing implemented native movement form')
    atomic_write(path, '#include "group_execution.hpp"\n#include "mov_preparation.hpp"\n'
                 + '#include <ptxsim/exec_ir/model/data_movement/mov.gen.hpp>\n'
                 + _validation_declarations(forms) + 'namespace ptxsim::inst_execute_engine::detail {\n'
                 + ''.join(mov_group(form) for form in forms) + '}\n')


def _layout_cpp(name: str) -> str:
    """Use the common generated nested layout naming convention."""
    return ''.join(part[:1].upper() + part[1:] for part in re.split(r'[^A-Za-z0-9]+', name) if part) + 'Operands'


def setp_group(form: Form) -> str:
    """Retain the existing s32/u32 comparison and optional AND behavior."""
    signed = form.identity.variant.startswith('setp_signed')
    boolean = form.identity.variant.endswith('_boolean')
    type_ = 's32' if signed else 'u32'
    codec = 'std::int32_t' if signed else 'std::uint32_t'
    admission = f'''  if (form.type != exec_ir::DataType::{type_} ||
      (form.comparison != exec_ir::ComparisonOperator::eq &&
       form.comparison != exec_ir::ComparisonOperator::lt &&
       form.comparison != exec_ir::ComparisonOperator::ge))
    return step_error(StepErrorCode::unsupported_instruction);
'''
    if boolean:
        admission += '  if (form.boolean != exec_ir::BooleanOperator::and_) return step_error(StepErrorCode::unsupported_instruction);\n'
    layouts = []
    for layout in form.layouts:
        pair = layout.identity.name == 'pair'
        if layout.identity.name not in {'single', 'pair'}:
            raise GenerationError(f'{form.identity}: unknown implemented comparison layout')
        destinations = ['operands.dst.first', 'operands.dst.second'] if pair else ['operands.dst.predicate']
        guard = ''.join(f'    if (!{dst}) return step_error(StepErrorCode::unsupported_instruction);\n' for dst in destinations)
        if boolean:
            guard += '    if (!std::holds_alternative<exec_ir::Predicate>(operands.combine)) return step_error(StepErrorCode::unsupported_instruction);\n'
        body = '      const auto registers = resolver.resolve();\n      if (!registers) return std::unexpected(registers.error());\n'
        for name in ('src1', 'src2'):
            body += f'      const auto {name} = read_value<{codec}>(registers->get(), operands.{name});\n      if (!{name}) return std::unexpected({name}.error());\n'
        if boolean:
            body += '      const auto combine = read_predicate(registers->get(), std::get<exec_ir::Predicate>(operands.combine));\n      if (!combine) return std::unexpected(combine.error());\n'
        for dst in destinations:
            body += f'      if (const auto destination = resolve_destination<bool>(registers->get(), {dst}->register_ref.slot); !destination) return std::unexpected(destination.error());\n'
        body += '      const auto comparison = semantics::compare(arithmetic, form.comparison, *src1, *src2);\n      if (!comparison) return std::unexpected(LaneFaultCause{comparison.error()});\n'
        primary = '(*comparison && *combine)' if boolean else '*comparison'
        secondary = '(!*comparison && *combine)' if boolean else '!*comparison'
        body += '      PreparedEffect effect{.control = *successor};\n'
        for index, (dst, value) in enumerate(zip(destinations, (primary, secondary))):
            body += f'      effect.writes[{index}] = PreparedWrite{{registers->get(), {dst}->register_ref.slot, common::RawValue::pred({value})}};\n'
        body += '      return effect;\n'
        layouts.append((_layout_cpp(layout.identity.name), guard, body))
    return _layout_group(form, admission, tuple(layouts))


def memory_group(form: Form) -> str:
    """Admit only implemented ordinary type/control/space and address categories."""
    load = form.identity.opcode == 'ld'
    vector = form.identity.variant.endswith('_vector')
    explicit = '_explicit_' in form.identity.variant
    space = 'form.state_space' if explicit else 'exec_ir::AddressSpace::generic'
    arity = 'form.vector' if vector else 'std::nullopt'
    modifiers = {field.identity.name for field in form.modifiers}
    admission = '''  if ((form.semantics != exec_ir::MemoryConsistency::omitted && form.semantics != exec_ir::MemoryConsistency::weak) ||
      form.scope != exec_ir::MemoryScope::none || form.cache != exec_ir::CacheOperator::unspecified'''
    if 'mmio' in modifiers:
        admission += ' || form.mmio'
    admission += ') return step_error(StepErrorCode::unsupported_instruction);\n'
    types = ('b8', 'b16', 'b32', 'b64', 'u8', 'u16', 'u32', 'u64', 's8', 's16', 's32', 's64', 'f32', 'f64')
    admission += '  if (!(' + ' || '.join(f'form.type == exec_ir::DataType::{type_}' for type_ in types) + ')) return step_error(StepErrorCode::unsupported_instruction);\n'
    if explicit:
        spaces = ['global', 'local', 'shared'] + (['const_', 'param'] if load else [])
        admission += '  if (!(' + ' || '.join(f'form.state_space == exec_ir::AddressSpace::{value}' for value in spaces) + ')) return step_error(StepErrorCode::unsupported_instruction);\n'
    admission += '''  if (form.operands.address.parameter_qualifier == exec_ir::ParameterAddressQualifier::Function ||
      form.operands.address.unified || form.operands.address.named_index)
    return step_error(StepErrorCode::unsupported_instruction);
'''
    admission += f'  if (!valid_memory_shape(form.type, {arity}, {space})) return step_error(StepErrorCode::unsupported_instruction);\n'
    field = 'dst' if load else 'src'
    if vector:
        admission += f'  if (!valid_memory_vector_shape(form.type, form.vector, {space}, form.operands.{field}, {str(load).lower()})) return step_error(StepErrorCode::unsupported_instruction);\n'
    value = f'form.operands.{field}' + ('' if vector else '.slot')
    body = f'    return prepare_memory_{"load" if load else "store"}(resolver, form.operands.address, {value}, form.type, {space}, {arity}, *successor);\n'
    return _typed_group(form, admission, body)


def _validation_declarations(forms: tuple[Form, ...] | list[Form]) -> str:
    """Name generated direct structural thunks without a second dense Kind decode."""
    return ('namespace ptxsim::exec_ir {\n' + ''.join(
        f'/** @brief Direct exact shape validation for {form.cpp_name}. */\n'
        f'auto validate_{form.cpp_name}(const Instruction&) noexcept -> bool;\n'
        for form in forms) + '}\n')


def _owned_fields(text: str, layout) -> str:
    """Resolve named handler roles at the current exact layout, never globally."""
    fields = {field.identity.name: field.cpp_name for field in layout.fields}
    fields.update({field.cpp_name: field.cpp_name for field in layout.fields})
    def member(match: re.Match[str]) -> str:
        """Reject a handler access that cannot be resolved within its selected layout."""
        if match[1] not in fields:
            raise GenerationError(f'{layout.identity}: unresolved handler operand {match[1]}')
        return 'operands.' + fields[match[1]]
    return re.sub(r'\boperands\.(\w+)', member, text)


def barrier_group(form: Form) -> str:
    """Prepare existing warp and CTA protocols with exact layout selection per issue."""
    if form.identity.variant == 'bar_warp_sync':
        body = '    return prepare_bar_warp_sync(resolver, form.operands.membermask, *successor);\n'
        return _typed_group(form, '', body).replace('PrepareKind::scalar', 'PrepareKind::warp_sync')
    name = form.identity.variant
    protocol = ('ReducePopc' if '_red_popc_' in name else 'ReduceAnd' if '_red_and_' in name
                else 'ReduceOr' if '_red_or_' in name else 'SyncArrive')
    waits = not name.endswith('_arrive')
    layouts = []
    for layout in form.layouts:
        fields = {field.identity.name: field for field in layout.fields}
        if 'barrier' not in fields:
            raise GenerationError(f'{form.identity}: implemented barrier layout missing carrier')
        barrier = 'operands.' + fields['barrier'].cpp_name
        if fields['barrier'].value_kind is ValueKind.IMMEDIATE:
            barrier = f'exec_ir::ScalarOperand{{{barrier}}}'
        count = 'operands.' + fields['thread_count'].cpp_name if 'thread_count' in fields else 'std::nullopt'
        predicate = 'operands.' + fields['predicate'].cpp_name if 'predicate' in fields else 'std::nullopt'
        guard = ''
        if 'dst' in fields:
            dst = 'operands.' + fields['dst'].cpp_name
            if fields['dst'].value_kind is ValueKind.PREDICATE:
                guard = f'    if ({dst}.negated) return step_error(StepErrorCode::invalid_instruction);\n'
                dst += '.register_ref.slot'
            else:
                dst += '.slot'
        else:
            dst = 'std::nullopt'
        body = f'      return prepare_cta_barrier(resolver, execution_model::CtaBarrierProtocol::{protocol}, {str(waits).lower()}, {barrier}, {count}, {predicate}, {dst}, *successor);\n'
        layouts.append((_layout_cpp(layout.identity.name), guard, body))
    return _layout_group(form, '', tuple(layouts), 'cta_barrier')


def control_group(form: Form) -> str:
    """Prepare existing direct control forms through their complete owned targets."""
    if form.identity.opcode == 'bra':
        body = '''    if (form.operands.target.function != function)
      return std::unexpected(LaneFaultCause{InvalidBranchTarget{function, form.operands.target.function}});
    return PreparedEffect{.control = form.operands.target.pc};
'''
    elif form.identity.opcode == 'exit':
        body = '    return PreparedEffect{.control = ExitControl{}};\n'
    else:
        raise GenerationError(f'{form.identity}: unimplemented control group')
    return _typed_group(form, '', body)


def implemented_forms(model: Snapshot) -> tuple[Form, ...]:
    """Require every existing implementation, without admitting new semantics by accident."""
    expected = {**_VALUE_FORMS,
        'mov': ('mov_scalar','mov_b128_pack_unpack','mov_v4_u32','mov_pred'),
        'setp': ('setp_signed','setp_signed_boolean','setp_unsigned','setp_unsigned_boolean'),
        'ld': ('ld_generic_scalar','ld_explicit_scalar','ld_generic_vector','ld_explicit_vector'),
        'st': ('st_generic_scalar','st_explicit_scalar','st_generic_vector','st_explicit_vector'),
        'bar': ('bar_sync','bar_cta_sync','bar_arrive','bar_cta_arrive','bar_red_popc_u32',
                'bar_cta_red_popc_u32','bar_red_and_pred','bar_cta_red_and_pred',
                'bar_red_or_pred','bar_cta_red_or_pred','bar_warp_sync'),
        'bra': ('bra_direct',), 'exit': ('exit_bare',)}
    by_id = {(form.identity.opcode, form.identity.variant): form for form in model.forms}
    if len(by_id) != len(model.forms):
        raise GenerationError('duplicate semantic form identity')
    result = []
    for opcode, names in expected.items():
        for name in names:
            if (opcode, name) not in by_id:
                raise GenerationError(f'missing implemented semantic form {opcode}/{name}')
            form = by_id[opcode, name]
            _check_shape(form)
            result.append(form)
    return tuple(result)


def _group(form: Form) -> str:
    """Select one implementation emitter using only semantic identity."""
    _check_shape(form)
    opcode = form.identity.opcode
    if opcode in _VALUE_FORMS:
        return value_group(form)
    if opcode == 'mov':
        return mov_group(form)
    if opcode == 'setp':
        return setp_group(form)
    if opcode in {'ld', 'st'}:
        return memory_group(form)
    if opcode == 'bar':
        return barrier_group(form)
    return control_group(form)


def generate_groups_probe(model: Snapshot, path: Path) -> None:
    """Publish all implemented groups for a bounded native compile."""
    forms = implemented_forms(model)
    lines = ['#include "group_execution.hpp"\n#include "value_alu_helpers.hpp"\n',
             '#include "mov_preparation.hpp"\n#include "memory_preparation.hpp"\n#include "barrier_preparation.hpp"\n',
             '#include "semantics/setp_semantics.hpp"\n',
             *[f'#include "semantics/{opcode}_semantics.hpp"\n' for opcode in _VALUE_FORMS],
             _includes(forms), _validation_declarations(forms), 'namespace ptxsim::inst_execute_engine::detail {\n']
    lines += [_group(form) for form in forms]
    lines += ['}\n']
    atomic_write(path, ''.join(lines))


@dataclass(frozen=True)
class EngineArtifacts:
    """Exact bounded private source membership for the dense issue dispatcher."""

    headers: tuple[str, ...]
    sources: tuple[str, ...]


def artifact_plan(model: Snapshot) -> EngineArtifacts:
    """Plan every Kind entry independently of which semantics are implemented."""
    count = (len(model.forms) + 63) // 64
    return EngineArtifacts(('instruction_preparation.gen.hpp',),
                           tuple(f'issue_groups_{index:03}.gen.cpp' for index in range(count))
                           + ('instruction_preparation.gen.cpp',))


def _includes(forms: tuple[Form, ...]) -> str:
    """Include only opcode headers used by the selected typed implementations."""
    paths = sorted({f'model/{form.category}/{form.identity.opcode}.gen.hpp' for form in forms})
    return ''.join(f'#include <ptxsim/exec_ir/{path}>\n' for path in paths)


def _group_includes(forms: tuple[Form, ...]) -> str:
    """Share native helper declarations without introducing runtime frontend dependencies."""
    return ('#include "group_execution.hpp"\n#include "value_alu_helpers.hpp"\n'
            '#include "mov_preparation.hpp"\n#include "memory_preparation.hpp"\n'
            '#include "barrier_preparation.hpp"\n#include "semantics/setp_semantics.hpp"\n'
            + ''.join(f'#include "semantics/{opcode}_semantics.hpp"\n' for opcode in _VALUE_FORMS)
            + _includes(forms))


def generate(model: Snapshot, root: Path) -> EngineArtifacts:
    """Emit complete dense Kind entries, with unsupported semantics local to their issue."""
    import json
    plan = artifact_plan(model)
    supported = {form.kind for form in implemented_forms(model)}
    declarations = ['// Generated exact issue-group entries. Do not edit.\n#pragma once\n',
                    '#include "instruction_preparation.hpp"\n',
                    'namespace ptxsim::inst_execute_engine::detail {\n']
    for form in model.forms:
        declarations += [f'/** @brief Issue entry fixed to {form.cpp_name}. */\n',
                         _group_signature('group_' + form.cpp_name), ';\n']
    declarations += ['}\n']
    atomic_write(root / plan.headers[0], ''.join(declarations))
    forms = model.forms
    for index, start in enumerate(range(0, len(forms), 64)):
        shard = forms[start:start + 64]
        selected = tuple(form for form in shard if form.kind in supported)
        source = [_group_includes(selected), _validation_declarations(shard),
                  'namespace ptxsim::inst_execute_engine::detail {\n']
        for form in shard:
            if form.kind in supported:
                source.append(_group(form))
            else:
                source += [f'/** @brief Reject unimplemented {form.cpp_name} before any lane activity. */\n',
                           _group_signature('group_' + form.cpp_name), ' {\n',
                           f'  if (!exec_ir::validate_{form.cpp_name}(base)) return step_error(StepErrorCode::invalid_instruction);\n',
                           '  return step_error(StepErrorCode::unsupported_instruction);\n}\n']
        source += ['}\n']
        atomic_write(root / plan.sources[index], ''.join(source))
    source = ['#include "instruction_preparation.gen.hpp"\n#include "issue_validation.hpp"\n#include <array>\n',
              'namespace ptxsim::inst_execute_engine::detail {\nnamespace {\n',
              '/** @brief One exact typed group entry per dense owned Kind. */\n',
              f'using Entry = decltype(&group_{forms[0].cpp_name});\nconstexpr std::array<Entry, exec_ir::instruction_kind_count> entries{{{{\n']
    source += [f'  &group_{form.cpp_name},\n' for form in forms]
    source += ['}};\nstatic_assert(entries.size() == exec_ir::instruction_kind_count);\n}\n',
               _group_signature('prepare_issue').replace('const exec_ir::Instruction& base', 'const exec_ir::Instruction& instruction'),
               ' {\n  const auto index = static_cast<std::size_t>(instruction.instruction_kind());\n',
               '  if (index >= entries.size()) return step_error(StepErrorCode::invalid_instruction);\n',
               '  return entries[index](runtime, function, arithmetic, warp, issue, instruction, successor);\n}\n}\n']
    atomic_write(root / plan.sources[-1], ''.join(source))
    atomic_write(root / 'artifact_plan.json', json.dumps({'headers': plan.headers, 'sources': plan.sources}, indent=2) + '\n')
    return plan


def _check_controls(form: Form, allowed: set[str]) -> None:
    """Reject controls which a typed implementation would otherwise silently ignore."""
    names = [field.identity.name for field in form.modifiers]
    if len(set(names)) != len(names) or set(names) - allowed:
        raise GenerationError(f'{form.identity}: unconsumed or duplicate controls')


def _check_shape(form: Form) -> None:
    """Check the bounded handler ABI without restricting full IR representation."""
    op, name = form.identity.opcode, form.identity.variant
    if op in _VALUE_FORMS:
        value_paths(form)
        return
    allowed = {'mov': {'type','vector'}, 'setp': {'comparison','boolean','type'},
               'ld': {'mmio','semantics','scope','state_space','cache','vector','type'},
               'st': {'mmio','semantics','scope','state_space','cache','vector','type'},
               'bar': {'cta','sync','arrive','red','reduction','result_type','warp'},
               'bra': {'uni'}, 'exit': set()}
    _check_controls(form, allowed[op])
    if op == 'mov':
        layouts = {'scalar','pack','unpack'} if name == 'mov_scalar' else {'pack','unpack'} if name == 'mov_b128_pack_unpack' else {'default'}
    elif op == 'setp': layouts = {'single','pair'}
    elif op == 'bar' and name == 'bar_sync': layouts = {'immediate_barrier','barrier','barrier_and_thread_count'}
    elif op == 'bar' and name == 'bar_cta_sync': layouts = {'barrier','barrier_and_thread_count'}
    elif op == 'bar' and '_red_' in name: layouts = {'without_thread_count','with_thread_count'}
    else: layouts = {'default'}
    if {layout.identity.name for layout in form.layouts} != layouts or len(form.layouts) != len(layouts):
        raise GenerationError(f'{form.identity}: missing, duplicate or unrecognized handler layout')
    for layout in form.layouts:
        fields = {field.identity.name: field for field in layout.fields}
        if len(fields) != len(layout.fields):
            raise GenerationError(f'{layout.identity}: duplicate operand role')
        expected = {}
        if op == 'mov':
            if name == 'mov_pred': expected = {'dst':ValueKind.PREDICATE, 'src':ValueKind.PREDICATE_SOURCE}
            elif name == 'mov_v4_u32': expected = {'dst':ValueKind.REGISTER_VECTOR,'src':ValueKind.MOV_VECTOR_SOURCE}
            elif layout.identity.name == 'scalar': expected = {'dst':ValueKind.REGISTER,'src':ValueKind.MOV_SOURCE}
            elif layout.identity.name == 'pack': expected = {'dst':ValueKind.REGISTER,'src':ValueKind.REGISTER_VECTOR}
            elif layout.identity.name == 'unpack': expected = {'dst':ValueKind.REGISTER_VECTOR,'src':ValueKind.REGISTER}
            else: raise GenerationError(f'{layout.identity}: unrecognized movement layout')
        elif op == 'setp':
            expected = {'dst':ValueKind.PREDICATE_PAIR_OR_SINK if layout.identity.name == 'pair' else ValueKind.PREDICATE_OR_SINK,
                        'src1':ValueKind.REG_OR_IMM,'src2':ValueKind.REG_OR_IMM}
            if name.endswith('_boolean'): expected['combine'] = ValueKind.PREDICATE_SOURCE
        elif op in {'ld','st'}:
            expected = {'address':ValueKind.ADDRESS, 'dst' if op == 'ld' else 'src':
                        ValueKind.REGISTER_VECTOR if name.endswith('_vector') else ValueKind.REGISTER}
        elif op == 'bra': expected = {'target':ValueKind.BRANCH_TARGET}
        elif op == 'bar':
            if name == 'bar_warp_sync': expected = {'membermask':ValueKind.REG_OR_IMM}
            else:
                expected = {'barrier':ValueKind.IMMEDIATE if layout.identity.name == 'immediate_barrier' else ValueKind.REG_OR_IMM}
                if 'thread_count' in fields: expected['thread_count'] = ValueKind.REG_OR_IMM
                if '_red_' in name:
                    expected['predicate'] = ValueKind.PREDICATE
                    expected['dst'] = ValueKind.REGISTER if '_popc_' in name else ValueKind.PREDICATE
        if set(fields) != set(expected) or any(fields[key].value_kind is not kind for key, kind in expected.items()):
            raise GenerationError(f'{layout.identity}: unimplemented operand shape for handler')
        for key, field in fields.items():
            if field.binding is None:
                raise GenerationError(f'{field.identity}: missing typed binding contract')
            if field.binding.target_field_id != field.identity.name:
                raise GenerationError(f'{field.identity}: binding does not name this layout-local field')
            if op == 'mov' and field.binding.register_width_policy.name != 'SAME_WIDTH':
                raise GenerationError(f'{field.identity}: movement handler requires same-width storage')
            if op == 'mov' and name == 'mov_v4_u32' and field.binding.vector_type_policy.name != 'ELEMENT':
                raise GenerationError(f'{field.identity}: topology movement requires element typing')
            if op == 'mov' and field.value_kind is ValueKind.REGISTER_VECTOR and name != 'mov_v4_u32':
                expected_sink = key == 'dst'
                if field.binding.allowed_vector_arities != (2,4) or field.binding.allow_vector_sink != expected_sink:
                    raise GenerationError(f'{field.identity}: unimplemented aggregate sink/arity policy')
    if op == 'mov' and name == 'mov_pred':
        type_ = next((field for field in form.modifiers if field.identity.name == 'type'), None)
        if type_ is None or type_.constant is None or type_.constant.spelling != 'pred':
            raise GenerationError(f'{form.identity}: predicate movement needs fixed predicate type')
    if op == 'bar' and any(field.constant is not None and field.value_kind is ValueKind.BOOL
                           and field.constant.spelling is not True for field in form.modifiers):
        raise GenerationError(f'{form.identity}: unimplemented fixed barrier protocol')
