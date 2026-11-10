"""Version-limited adapter from frontend 0.8.0 into the single execution model.

Only this input boundary reads frontend models and backend helpers. C++ member
spellings are distinct from semantic field identities throughout the snapshot.
"""

from __future__ import annotations

from collections.abc import Mapping
from dataclasses import fields, is_dataclass, replace
from enum import Enum
from importlib import metadata, resources
from pathlib import Path
import re
from types import MappingProxyType
from typing import TypeVar

import yaml

from ptx_frontend.code_gen.context import GenerationInstruction
from ptx_frontend.code_gen.cpp_backend import CppDomain, cpp_domain, load_cpp_backend
from ptx_frontend.code_gen.cpp_names import form_name
from ptx_frontend.code_gen.resolved_field_names import (
    field_value_cpp_type, with_cpp_backend_field_names,
)
from ptx_frontend.code_gen.resolved_layout import operand_slot_for_field, operand_slots
from ptx_frontend.code_gen.resolved_value_traits import (
    RESOLVED_MODIFIER_VALUE_KINDS, modifier_value_cpp_expr, resolved_modifier_value_traits,
)
from ptx_frontend.ir.resolved_ir import AtomicAddressQualifierValue, ResolvedFieldStorage, from_instruction_spec
from ptx_frontend.ir.resolved_value_kind import ResolvedValueKind
from ptx_frontend.spec.database import get_packaged_spec_database, load_spec_database
from ptx_frontend.spec.model import ModifierPresence
from ptx_frontend.spec.resources import packaged_backend_spec

from ptxsim_codegen.model import GenerationError
from ptxsim_codegen.model.names import control_member_name, cpp_identifier
from ptxsim_codegen.model.snapshot import (
    CppAccess, Domain, DomainValue, Field, FieldId, FieldStorage, Form, FormContracts,
    FormId, Instruction, Layout, LayoutId, LeafMapping, Snapshot, ValueKind,
    AtomicQualifierControl, AtomicQualifierDomain, AtomicQualifierValue, SourceContext, SourceValueGroup,
)


_T = TypeVar('_T')


def _freeze(value: _T) -> _T:
    """Copy a normalized fact graph into deeply immutable value storage."""
    if is_dataclass(value) and not isinstance(value, type):
        if not value.__dataclass_params__.frozen:
            raise GenerationError(f'mutable normalized contract {type(value).__name__}')
        return replace(value, **{field.name: _freeze(getattr(value, field.name))
                                 for field in fields(value)})
    if isinstance(value, Mapping):
        return MappingProxyType({_freeze(key): _freeze(item) for key, item in value.items()})
    if isinstance(value, (tuple, list)):
        return tuple(_freeze(item) for item in value)
    if isinstance(value, (set, frozenset)):
        return frozenset(_freeze(item) for item in value)
    if value is None or isinstance(value, (str, bool, int, float, Enum)):
        return value
    raise GenerationError(f'unsupported normalized contract value {type(value).__name__}')


def _keys(value: object, expected: set[str], where: str) -> dict:
    """Validate closed mapping keys at the YAML input boundary."""
    if not isinstance(value, dict) or set(value) != expected:
        raise GenerationError(f'{where} keys must be {sorted(expected)}')
    return value


def _backend(path: Path | None, frontend_backend) -> tuple[tuple[Domain, ...], tuple[LeafMapping, ...], AtomicQualifierDomain]:
    """Read owned C++ spellings; derive every source spelling from its backend."""
    text = path.read_text() if path else resources.files('ptxsim_codegen.resources').joinpath('backend.yaml').read_text()
    raw = _keys(yaml.safe_load(text), {'schema', 'ptx_spec_schema', 'domains', 'leaves', 'special_controls'}, 'backend')
    if raw['schema'] != 'ptxsim-exec-ir-backend/v3' or raw['ptx_spec_schema'] != 'ptx-instr/v1':
        raise GenerationError('unsupported execution backend schema')
    if not isinstance(raw['domains'], dict) or not isinstance(raw['leaves'], dict):
        raise GenerationError('domains and leaves must be mappings')
    expected_domains = {kind.value for kind in RESOLVED_MODIFIER_VALUE_KINDS}
    if set(raw['domains']) != expected_domains:
        raise GenerationError('domains must map every frontend modifier value kind exactly')
    expected_leaves = {kind.value for kind in ResolvedValueKind} - expected_domains
    if set(raw['leaves']) != expected_leaves:
        raise GenerationError('leaves must map every frontend operand value kind exactly')
    source_types = cpp_domain(CppDomain.RESOLVED_VALUE_CPP_TYPES, backend=frontend_backend).values
    domains = []
    for name, entry in raw['domains'].items():
        _keys(entry, {'cpp_type', 'values'}, f'domains.{name}')
        if not isinstance(entry['cpp_type'], str) or not entry['cpp_type'] or not isinstance(entry['values'], list):
            raise GenerationError(f'invalid domain mapping {name}')
        source_kind = ResolvedValueKind(name)
        expected_values = (False, True) if source_kind is ResolvedValueKind.BOOL else tuple(
            cpp_domain(resolved_modifier_value_traits(source_kind).cpp_domain, backend=frontend_backend).values)
        expected_keys = {(type(value), value) for value in expected_values}
        supplied_keys = set()
        for item in entry['values']:
            _keys(item, {'value', 'cpp'}, f'domains.{name}.values')
            value = item['value']
            if not isinstance(value, (str, bool, int)):
                raise GenerationError(f'invalid domain value {name}/{value!r}')
            supplied_keys.add((type(value), value))
        if supplied_keys != expected_keys:
            raise GenerationError(f'domain {name} must exactly map the frontend vocabulary')
        atoms = []
        seen = set()
        owned_expressions = set()
        for index, item in enumerate(entry['values']):
            _keys(item, {'value', 'cpp'}, f'domains.{name}.values')
            value, cpp = item['value'], item['cpp']
            key = (type(value), value)
            if not isinstance(value, (str, bool, int)) or key in seen or not isinstance(cpp, str) or not cpp:
                raise GenerationError(f'invalid or duplicate domain value {name}/{value!r}')
            if source_kind is ResolvedValueKind.BOOL:
                if entry['cpp_type'] != 'bool' or cpp != ('true' if value else 'false'):
                    raise GenerationError('Boolean domain must preserve true and false')
            elif not re.fullmatch(re.escape(entry['cpp_type']) + r'::[A-Za-z_][A-Za-z0-9_]*', cpp):
                raise GenerationError(f'owned domain value must name its typed enumerator: {name}/{cpp}')
            if cpp in owned_expressions:
                raise GenerationError(f'owned C++ domain value collision in {name}: {cpp}')
            seen.add(key)
            owned_expressions.add(cpp)
            atoms.append(DomainValue(ValueKind(name), index, value, cpp,
                                     modifier_value_cpp_expr(source_kind, value, backend=frontend_backend)))
        if not atoms:
            raise GenerationError(f'empty domain {name}')
        domains.append(Domain(ValueKind(name), entry['cpp_type'], source_types[name], tuple(atoms)))
    leaves = []
    for name, entry in raw['leaves'].items():
        _keys(entry, {'cpp_type', 'binder'}, f'leaves.{name}')
        if any(not isinstance(entry[key], str) or not entry[key] for key in entry):
            raise GenerationError(f'invalid leaf mapping {name}')
        leaves.append(LeafMapping(ValueKind(name), entry['cpp_type'], source_types[name], entry['binder']))
    special = _keys(raw['special_controls'], {'AtomicAddressQualifier'}, 'special_controls')
    atomic = _keys(special['AtomicAddressQualifier'], {'cpp_type', 'binder', 'values'}, 'atomic qualifier')
    if not isinstance(atomic['values'], list):
        raise GenerationError('atomic qualifier values must be a list')
    atomic_values = []
    semantic_seen = set()
    expression_seen = set()
    for item in atomic['values']:
        _keys(item, {'value', 'cpp'}, 'atomic qualifier value')
        semantic = AtomicAddressQualifierValue(item['value'])
        expression = item['cpp']
        if semantic in semantic_seen or not isinstance(expression, str) or expression in expression_seen:
            raise GenerationError('duplicate atomic qualifier semantic or owned C++ value')
        semantic_seen.add(semantic)
        expression_seen.add(expression)
        source_name = ''.join(part.title() for part in semantic.name.lower().split('_'))
        atomic_values.append(AtomicQualifierValue(semantic, expression, 'AtomicAddressQualifier::' + source_name))
    if semantic_seen != set(AtomicAddressQualifierValue):
        raise GenerationError('atomic qualifiers must exactly map the frontend vocabulary')
    if not all(isinstance(atomic[key], str) and atomic[key] for key in ('cpp_type', 'binder')):
        raise GenerationError('invalid atomic qualifier type or binder')
    atomic_domain = AtomicQualifierDomain(atomic['cpp_type'], atomic['binder'], tuple(atomic_values))
    return tuple(domains), tuple(leaves), atomic_domain


def _atom(domain: Domain, value: str | bool | int) -> DomainValue:
    """Resolve one typed semantic value without conflating bool and integers."""
    for atom in domain.values:
        if type(atom.spelling) is type(value) and atom.spelling == value:
            return atom
    raise GenerationError(f'unmapped {domain.kind.value} value {value!r}')


def _source_groups(domain: Domain, values: tuple[DomainValue, ...], form_id: FormId) -> tuple[SourceValueGroup, ...]:
    """Model source equivalence explicitly instead of choosing the first alias."""
    grouped = {}
    for value in values:
        grouped.setdefault(value.frontend_cpp, []).append(value)
    result = []
    for expression, aliases in grouped.items():
        if len(aliases) == 1:
            canonical, context = aliases[0], SourceContext.NONE
        elif domain.kind is ValueKind.MEMORY_STATE_SPACE and expression == 'MemoryStateSpace::Parameter':
            canonical, context = _atom(domain, 'param'), SourceContext.PARAMETER_QUALIFIER
        elif domain.kind is ValueKind.MEMORY_STATE_SPACE and expression == 'MemoryStateSpace::Shared':
            canonical, context = _atom(domain, 'shared'), SourceContext.ATOMIC_QUALIFIER
        else:
            raise GenerationError(f'unmodeled source value equivalence in {form_id}: {expression}')
        result.append(SourceValueGroup(expression, tuple(aliases), canonical, context))
    return tuple(result)


def _contracts(form) -> FormContracts:
    """Select every named normalized execution contract into immutable storage."""
    return _freeze(FormContracts(
        condition_code=form.condition_code_effect, completion=form.completion_kind,
        wgmma_action=form.wgmma_protocol_action, semantic_rule=form.rule, matrix=form.matrix, fabric=form.fabric,
        surface=form.surface, stack=form.stack, texture=form.texture, video=form.video,
        floating_point_default=form.floating_point_default,
        modifier_availability=form.modifier_value_availabilities,
        operand_type_compatibility=form.operand_type_compatibilities,
        memory_consistency=form.memory_consistency,
        permits_unified_address=form.permits_unified_address,
        unified_address_access=form.unified_address_access,
        address_alignments=form.address_alignments, memory_vector=form.memory_vector,
        immediate_value=form.immediate_value, immediate_ranges=form.immediate_ranges,
        immediate_multiple_of=form.immediate_multiple_of,
        tcgen_commit_address=form.tcgen_commit_address_spelling,
        tcgen_commit_multicast=form.tcgen_commit_multicast,
        tcgen_fence_direction=form.tcgen_fence_direction,
        tcgen_copy_pairs=form.tcgen_copy_pairs, tcgen_copy_formats=form.tcgen_copy_formats,
        tensor_reduction=form.tensor_reduction_op, tensor_access=form.tensor_access_mode,
        tensor_destination=form.tensor_destination,
        tensor_im2col_elements=form.tensor_im2col_info_elements,
        tensor_multicast=form.tensor_multicast,
        tensor_cta_group_applicable=form.tensor_cta_group_applicable,
    ))


def snapshot(spec_dir: Path | None = None, backend_path: Path | None = None) -> Snapshot:
    """Build the entire uncropped semantic snapshot from the exact frontend."""
    if metadata.version('ptx_frontend') != '0.8.0':
        raise GenerationError('the execution adapter requires frontend 0.8.0')
    frontend_backend = load_cpp_backend(packaged_backend_spec())
    domains, leaves, atomic_domain = _backend(backend_path, frontend_backend)
    domain_by_kind = {item.kind: item for item in domains}
    leaf_by_kind = {item.kind: item for item in leaves}
    database = get_packaged_spec_database() if spec_dir is None else load_spec_database(spec_dir=spec_dir)
    if database.spec_schema != 'ptx-instr/v1':
        raise GenerationError('frontend semantic schema mismatch')
    instructions = []
    all_names = set()
    specification_kinds = set()
    next_kind = 0
    for opcode_index, specification in enumerate(database.instructions):
        semantic = from_instruction_spec(specification)
        cpp = with_cpp_backend_field_names(semantic, frontend_backend)
        entry = GenerationInstruction(specification, cpp)
        forms = []
        for semantic_form, cpp_form, source_form in zip(semantic.variants, cpp.variants, specification.variants, strict=True):
            identity = FormId(specification.opcode, semantic_form.variant_id)
            name = form_name(entry, cpp_form)
            if name in all_names:
                raise GenerationError(f'duplicate final C++ identity {name}')
            all_names.add(name)
            modifiers = []
            for field, cpp_field in zip(semantic_form.modifier_fields, cpp_form.modifier_fields, strict=True):
                kind = ValueKind(field.value_kind.value)
                domain = domain_by_kind[kind]
                values = tuple(_atom(domain, value.value) for value in semantic_form.modifier_value_domains
                               if value.source_kind_id == field.source_name)
                storage = FieldStorage.CONSTANT if field.storage is ResolvedFieldStorage.STATIC_CONSTANT else FieldStorage.INSTANCE
                constant = _atom(domain, field.constant_value) if storage is FieldStorage.CONSTANT else None
                groups = _source_groups(domain, values, identity) if storage is FieldStorage.INSTANCE else ()
                owned_domain = tuple(group.canonical for group in groups) if groups else values
                source_default = next((binding.default_value for binding in semantic_form.modifier_bindings
                                       if binding.target_field_id == field.name), None)
                default = _atom(domain, source_default.value) if source_default is not None else None
                if default is not None and groups:
                    default = next(group.canonical for group in groups
                                   if group.frontend_cpp == default.frontend_cpp)
                modifiers.append(Field(FieldId(identity, None, field.name), kind, domain.cpp_type,
                                       control_member_name(field.name), CppAccess(cpp_field.name, False, storage is FieldStorage.INSTANCE),
                                       storage, constant, default, owned_domain, groups, None, None))
            slots = operand_slots(cpp_form, frontend_backend)
            layouts = []
            for index, (layout, cpp_layout, source_layout) in enumerate(zip(semantic_form.operand_layouts, cpp_form.operand_layouts, source_form.operand_layouts, strict=True)):
                layout_id = LayoutId(identity, layout.layout_id)
                operands = []
                for field, binding, cpp_field, source_operand in zip(layout.fields, layout.bindings, cpp_layout.fields, source_layout.operands, strict=True):
                    kind = ValueKind(field.value_kind.value)
                    mapping = leaf_by_kind[kind]
                    actual_type = field_value_cpp_type(cpp_field, backend=frontend_backend)
                    if actual_type != mapping.frontend_cpp_type:
                        raise GenerationError(f'frontend payload type drift for {layout_id}/{field.name}')
                    slot = operand_slot_for_field(slots, cpp_field, frontend_backend)
                    specification_kinds.add(source_operand.kind.value)
                    operands.append(Field(FieldId(identity, layout_id, field.name), kind, mapping.cpp_type,
                                          cpp_identifier(field.name), CppAccess(slot.member_name, slot.optional, True),
                                          FieldStorage.INSTANCE, None, None, (), (), _freeze(binding), source_operand.kind.value))
                layouts.append(Layout(layout_id, index, layout.cpp_name, tuple(operands),
                                      layout.forbidden_modifiers, layout.forbidden_modifier_slots))
            atomic_control = None
            if semantic_form.atomic_address_qualifier_domain:
                admitted = set(semantic_form.atomic_address_qualifier_domain)
                atomic_control = AtomicQualifierControl(CppAccess('address_qualifier', False, True),
                    tuple(value for value in atomic_domain.values if value.semantic in admitted))
                if len(atomic_control.allowed) != len(admitted):
                    raise GenerationError(f'missing selected atomic qualifier mapping in {identity}')
            for control in modifiers:
                for group in control.source_groups:
                    if group.context is SourceContext.ATOMIC_QUALIFIER and atomic_control is None:
                        raise GenerationError(f'aliased shared control lacks atomic qualifier context: {identity}')
                    if group.context is SourceContext.PARAMETER_QUALIFIER and not any(
                            field.value_kind is ValueKind.ADDRESS for layout in layouts for field in layout.fields):
                        raise GenerationError(f'aliased parameter control lacks address context: {identity}')
            forms.append(Form(identity, next_kind, opcode_index, name, name,
                              specification.codegen_category, tuple(modifiers), tuple(layouts),
                              _contracts(semantic_form), atomic_control))
            next_kind += 1
        instructions.append(Instruction(specification.opcode, opcode_index, entry.cpp_name,
                                        specification.codegen_category, tuple(forms), _freeze(semantic.atomic_address_qualifier)))
    return Snapshot('0.8.0', database.spec_schema, tuple(instructions), domains, leaves, atomic_domain,
                    tuple(sorted(specification_kinds)))
