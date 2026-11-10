"""Emit named native contract records and owned constant tables from the snapshot.

The only discarded facts are the explicitly classified frontend target gates.
Semantic admission, selected domains, field links and nested record structure
remain typed. No emitted object borrows a Python or frontend runtime payload.
"""

from __future__ import annotations

from dataclasses import dataclass, fields, is_dataclass
from enum import Enum
import sys
from types import UnionType
from typing import get_args, get_origin

from ptxsim_codegen.model.names import cpp_identifier
from ptxsim_codegen.model.snapshot import Form, FormContracts, Snapshot, SourceContext, ValueKind


def _name(value: str) -> str:
    """Convert a named semantic record path to an owned C++ type identity."""
    return ''.join(part[:1].upper() + part[1:] for part in value.replace('::', '_').replace('.', '_').split('_'))


_TARGET_GATES = {
    ('FormContracts', 'modifier_availability'),
    ('ResolvedOperandTypeCompatibility', 'availability'),
    ('ResolvedAddressStateSpace', 'availability'),
    ('ResolvedParameterAddressConstraint', 'function_availability'),
    ('ResolvedMemoryVectorConstraint', 'availability'),
    ('ResolvedOperandBinding', 'sink_availability'),
}
_FIELD_ROLES = {
    'target_field_id', 'modifier_field_id', 'operand_field_id', 'address_field_ids',
    'type_field_id', 'vector_field_id', 'immediate_operand_field_id',
    'semantics_field_id', 'scope_field_id', 'mmio_field_id', 'cache_field_id',
    'address_field_id', 'state_space_field_id', 'state_space_modifier_field_id',
    'vector_arity_modifier_field_id', 'operand', 'sat_modifier', 'po_modifier',
    'type_modifier',
}
_SCALAR_NAMES = {
    'register_type', 'effective_type', 'scalar_type',
    'allowed_vector_register_types', 'integer_immediate_type',
}
_EXISTING_ENUMS = {
    'ConditionCodeEffect', 'AsyncCompletionKind', 'WgmmaProtocolAction',
    'FloatingPointDefault',
}
# Fixed native representations of normalized semantic numeric fields. New paths
# require an explicit representation mapping; observed values never select types.
_NUMERIC_REPRESENTATIONS = {
    'AddressAlignmentConstraint_alignment': 'std::uint64_t',
    'FormFacts_tensor_im2col_elements_1': 'std::uint64_t',
    'ImmediateMultipleOfConstraint_divisor': 'std::uint64_t',
    'ImmediateRangeConstraint_maximum': 'std::uint64_t',
    'ImmediateRangeConstraint_minimum': 'std::uint64_t',
    'ImmediateValueConstraint_values': 'std::uint64_t',
    'MatrixFragmentShape_register_count': 'std::uint64_t',
    'MatrixScaleSelector_byte_mask': 'std::uint64_t',
    'MatrixScaleSelector_thread_max': 'std::uint64_t',
    'MatrixShape_k': 'std::uint64_t',
    'MatrixShape_m': 'std::uint64_t',
    'MatrixShape_n': 'std::uint64_t',
    'Matrix_matrix_count': 'std::uint64_t',
    'Matrix_scale_vector_size': 'std::uint64_t',
    'OperandBinding_allowed_vector_arities': 'std::uint64_t',
    'OperandBinding_expected_tensor_rank': 'std::uint64_t',
    'OperandBinding_maximum_elements': 'std::uint64_t',
    'OperandBinding_minimum_elements': 'std::uint64_t',
    'OperandBinding_vector_sink_payload_bits': 'std::uint64_t',
    'OperandTypeCompatibility_instruction_width': 'std::uint64_t',
    'StackInstruction_default_alignment': 'std::uint64_t',
    'StackInstruction_width': 'std::uint64_t',
    'SurfaceInstruction_vector_arity': 'std::uint64_t',
    'TextureInstruction_result_arity': 'std::uint64_t',
}


@dataclass(frozen=True)
class NativeType:
    """One exact owned native type, independent of source C++ spellings."""

    kind: str
    name: str = ''
    children: tuple[NativeType, ...] = ()


@dataclass(frozen=True)
class NativeMember:
    """A named semantic member and its sole explicit projection transform."""

    source: str
    name: str
    type: NativeType
    transform: str = 'identity'


class ContractSchema:
    """Closed typed schema derived only from the snapshot's named frozen facts."""

    def __init__(self, model: Snapshot) -> None:
        """Discover all named records and vocabularies before writing artifacts."""
        self.model = model
        self.records: dict[str, tuple[NativeMember, ...]] = {}
        self.enums: dict[str, dict[str, str | int]] = {}
        self.string_values: dict[str, set[str]] = {'FieldRole': {''}}
        self.integer_values: dict[str, set[int]] = {}
        self._classes: dict[str, type] = {FormContracts.__name__: FormContracts}
        self._namespace: dict[str, object] = {}
        self.omissions = set(_TARGET_GATES)
        roots = [form.contracts for form in model.forms]
        roots += [field.binding for form in model.forms for layout in form.layouts
                  for field in layout.fields if field.binding is not None]
        for root in roots:
            self._discover(root)
        self._namespace.update(self._classes)
        for root in roots:
            self.record(type(root))
            self.observe(self.record(type(root)), root)
        for enum in (ValueKind, SourceContext):
            self.enums[enum.__name__] = {member.name: member.value for member in enum}
        self.string_values['LayoutName'] = {layout.identity.name for form in model.forms for layout in form.layouts}
        for form in model.forms:
            self.string_values['FieldRole'].update(field.identity.name for field in form.modifiers)
            for layout in form.layouts:
                self.string_values['FieldRole'].update(field.identity.name for field in layout.fields)
                self.string_values['FieldRole'].update(layout.forbidden_modifiers)
        self._scalar_values = {
            atom.spelling: atom.cpp for domain in model.domains
            if domain.kind is ValueKind.SCALAR_TYPE for atom in domain.values}
        # All scalar-name observations must map to the exact full owned domain.
        for values in [self.string_values.get('DataType', set())]:
            if not values <= self._scalar_values.keys():
                raise ValueError(f'unknown scalar contract values: {values - self._scalar_values.keys()}')

    def _discover(self, value) -> None:
        """Collect the types already contained in the immutable named fact graph."""
        if isinstance(value, Enum) or is_dataclass(value):
            cls = type(value)
            self._classes[cls.__name__] = cls
            self._namespace.update(vars(sys.modules[cls.__module__]))
        if is_dataclass(value):
            for member in fields(value):
                if (type(value).__name__, member.name) in _TARGET_GATES:
                    continue
                if member.name in {'indirect_availability', 'mmio_semantics'}:
                    continue
                self._discover(getattr(value, member.name))
        elif isinstance(value, tuple):
            for item in value:
                self._discover(item)

    def record(self, cls: type) -> NativeType:
        """Translate every retained named member; unknown payload types fail closed."""
        name = 'FormFacts' if cls is FormContracts else cls.__name__.removeprefix('Resolved').removesuffix('Spec')
        if name not in self.records:
            self.records[name] = ()
            members = []
            for field in fields(cls):
                if (cls.__name__, field.name) in _TARGET_GATES:
                    continue
                if field.name == 'indirect_availability':
                    if cls.__name__ not in {'TextureInstructionSpec', 'SurfaceInstructionSpec'}:
                        raise ValueError(f'unclassified admission gate: {cls.__name__}')
                    members.append(NativeMember(field.name, 'allows_indirect', NativeType('bool'), 'presence'))
                    continue
                annotation = field.type
                if isinstance(annotation, str):
                    annotation = eval(annotation, self._namespace)
                transform = 'identity'
                if cls.__name__ == 'ResolvedMemoryConsistencyConstraint' and field.name == 'mmio_semantics':
                    annotation = tuple[str, ...]
                    transform = 'semantic_pairs'
                path = f'{name}_{field.name}'
                members.append(NativeMember(field.name, cpp_identifier(field.name),
                                            self.type(annotation, path, field.name), transform))
            self.records[name] = tuple(members)
        return NativeType('record', name)

    def type(self, annotation, path: str, field: str) -> NativeType:
        """Preserve optionals, ordered sequences, tuples and closed semantic types."""
        if annotation is bool:
            return NativeType('bool')
        if annotation is int:
            if path not in _NUMERIC_REPRESENTATIONS:
                raise ValueError(f'unmapped numeric representation: {path}')
            self.integer_values.setdefault(path, set())
            return NativeType('integer', path)
        if annotation is str:
            if field in _FIELD_ROLES:
                return NativeType('field_reference', 'FieldRole')
            name = 'DataType' if field in _SCALAR_NAMES else _name(path)
            self.string_values.setdefault(name, set())
            return NativeType('string_enum', name)
        origin, args = get_origin(annotation), get_args(annotation)
        if origin is UnionType:
            actual = tuple(item for item in args if item is not type(None))
            if len(actual) != 1 or len(actual) == len(args):
                raise ValueError(f'unmapped contract union {path}: {annotation}')
            return NativeType('optional', children=(self.type(actual[0], path, field),))
        if origin is tuple:
            if field == 'vector_signed_immediate_range':
                if args != (int, int):
                    raise ValueError(f'invalid signed-vector range annotation: {annotation}')
                return NativeType('signed_range')
            if len(args) == 2 and args[-1] is Ellipsis:
                return NativeType('sequence', children=(self.type(args[0], path, field),))
            return NativeType('tuple', children=tuple(
                self.type(item, f'{path}_{index}', field) for index, item in enumerate(args)))
        if isinstance(annotation, type) and issubclass(annotation, Enum):
            self.enums[annotation.__name__] = {member.name: member.value for member in annotation}
            return NativeType('enum', annotation.__name__)
        if isinstance(annotation, type) and is_dataclass(annotation):
            return self.record(annotation)
        raise ValueError(f'unmapped native contract type {path}: {annotation}')

    @staticmethod
    def member_value(member: NativeMember, value):
        """Apply only the classified target-gate-to-semantic conversions."""
        actual = getattr(value, member.source)
        if member.transform == 'presence':
            return actual is not None
        if member.transform == 'semantic_pairs':
            return tuple(semantic for semantic, _target_gate in actual)
        return actual

    def observe(self, type_: NativeType, value) -> None:
        """Collect complete finite vocabularies and numeric representation bounds."""
        if type_.kind == 'record':
            for member in self.records[type_.name]:
                self.observe(member.type, self.member_value(member, value))
        elif type_.kind in {'string_enum', 'field_reference'}:
            self.string_values[type_.name].add(value)
        elif type_.kind == 'integer':
            if not 0 <= value < (1 << 64):
                raise ValueError(f'numeric value does not fit {self.cpp_type(type_)}: {type_.name}/{value}')
            self.integer_values[type_.name].add(value)
        elif type_.kind == 'optional' and value is not None:
            self.observe(type_.children[0], value)
        elif type_.kind == 'sequence':
            for item in value:
                self.observe(type_.children[0], item)
        elif type_.kind == 'tuple':
            if len(value) != len(type_.children):
                raise ValueError('contract tuple cardinality differs from its annotation')
            for child, item in zip(type_.children, value, strict=True):
                self.observe(child, item)

    def cpp_type(self, type_: NativeType) -> str:
        """Return the full named native type without type-erased alternatives."""
        kind = type_.kind
        if kind == 'bool':
            return 'bool'
        if kind == 'field_reference':
            return 'contract::FieldReference'
        if kind == 'signed_range':
            return 'contract::SignedVectorImmediateRange'
        if kind == 'integer':
            return _NUMERIC_REPRESENTATIONS[type_.name]
        if kind == 'string_enum' and type_.name == 'DataType':
            return '::ptxsim::exec_ir::DataType'
        if kind == 'enum' and type_.name in _EXISTING_ENUMS:
            return f'::ptxsim::exec_ir::{type_.name}'
        if kind in {'enum', 'record', 'string_enum'}:
            return f'contract::{type_.name}'
        if kind == 'sequence':
            return f'std::span<const {self.cpp_type(type_.children[0])}>'
        if kind == 'optional':
            return f'std::optional<{self.cpp_type(type_.children[0])}>'
        if kind == 'tuple':
            return 'std::tuple<' + ', '.join(self.cpp_type(child) for child in type_.children) + '>'
        raise ValueError(f'unknown native contract kind {kind}')

    def enum_atom(self, name: str, value: str) -> str:
        """Resolve one finite semantic token, with explicit empty-field identity."""
        if name == 'DataType':
            return self._scalar_values[value]
        if value not in self.string_values[name]:
            raise ValueError(f'unknown native semantic token {name}/{value}')
        return f'contract::{name}::{cpp_identifier(value) if value else "no_field"}'

    def header(self) -> str:
        """Declare the complete owned fact graph with const-span lifetime contracts."""
        lines = ['// Generated from the immutable execution snapshot. Do not edit.\n#pragma once\n',
                 '#include <cstdint>\n#include <optional>\n#include <span>\n#include <tuple>\n',
                 '#include <ptxsim/exec_ir/instruction_contracts.hpp>\n',
                 '#include <ptxsim/exec_ir/value_domains.gen.hpp>\n',
                 'namespace ptxsim::exec_ir::contract {\n']
        for name, values in sorted(self.enums.items()):
            if name in _EXISTING_ENUMS:
                continue
            lines += [f'/** @brief Closed normalized {name} semantic identities. */\nenum class {name} : std::uint16_t {{\n']
            lines += [f'  {cpp_identifier(value)},\n' for value in values]
            lines += ['};\n']
        for name, values in sorted(self.string_values.items()):
            if name == 'DataType':
                continue
            atoms = [cpp_identifier(value) if value else 'no_field' for value in sorted(values)]
            if len(atoms) != len(set(atoms)):
                raise ValueError(f'colliding semantic contract tokens in {name}')
            lines += [f'/** @brief Closed semantic vocabulary for {name}. */\nenum class {name} : std::uint16_t {{\n']
            lines += [f'  {atom},\n' for atom in atoms]
            lines += ['};\n']
        lines += ['''/** @brief Explicit context for a field role inside the containing exact form. */
enum class FieldReferenceScope : std::uint8_t { NoField, FormControl, SelectedLayoutOperand };
/** @brief Form-local field reference; names never identify fields across forms/layouts. */
struct FieldReference {
  /** @brief Local semantic name, interpreted with the containing exact form. */
  FieldRole role;
  /** @brief Whether this names a form control or a selected layout operand. */
  FieldReferenceScope scope;
  /** @brief Exact applicable layout indices for an operand; controls have none. */
  std::span<const std::uint16_t> layouts;
};
/** @brief Inclusive bounds on evaluated signed vector-immediate source values. */
struct SignedVectorImmediateRange {
  /** @brief Inclusive signed lower bound, independent of observed sign. */
  std::int64_t minimum;
  /** @brief Inclusive signed upper bound, independent of observed sign. */
  std::int64_t maximum;
};
''']
        emitted = set()

        def emit_record(name: str) -> None:
            """Topologically declare value-member dependencies before the parent."""
            nonlocal lines
            if name in emitted:
                return
            emitted.add(name)

            def visit(type_: NativeType) -> None:
                """Follow nested optional and tuple record dependencies."""
                if type_.kind == 'record':
                    emit_record(type_.name)
                for child in type_.children:
                    visit(child)

            for member in self.records[name]:
                visit(member.type)
            lines.append(f'/** @brief Owned {name} semantics; spans refer only to library static arrays. */\nstruct {name} {{\n')
            for member in self.records[name]:
                lines += [f'  /** @brief Normalized {member.source} semantic contract. */\n',
                          f'  {self.cpp_type(member.type)} {member.name};\n']
            lines += ['};\n']

        for name in self.records:
            emit_record(name)
        lines += ['}  // namespace ptxsim::exec_ir::contract\n', self.descriptor_header()]
        return ''.join(lines)

    def descriptor_header(self) -> str:
        """Declare typed layout/control joins over the complete named facts."""
        alternatives = list(dict.fromkeys(domain.cpp_type for domain in self.model.domains))
        return '''#include <variant>
namespace ptxsim::exec_ir {
/** @brief Every closed native modifier domain; no untyped or empty alternative. */
using ControlValue = std::variant<''' + ', '.join(alternatives) + '''>;
/** @brief Semantic aliases of one frontend value, interpreted with owned context. */
struct SourceEquivalence {
  /** @brief Complete admitted semantic aliases, backed by owned static data. */
  std::span<const ControlValue> aliases;
  /** @brief Canonical space/control retained alongside its selected qualifier. */
  ControlValue canonical;
  /** @brief Additional typed context that distinguishes equivalent source enums. */
  contract::SourceContext context;
};
/** @brief Full control domain and actual normalized default for one form. */
struct ControlContract {
  /** @brief Semantic field role, independent of source member spelling. */
  contract::FieldRole field;
  /** @brief Typed domain identity. */
  contract::ValueKind value_kind;
  /** @brief True when the selected value is stored on each instruction. */
  bool instance;
  /** @brief Form-static selection when the control is not instance-dependent. */
  std::optional<ControlValue> constant;
  /** @brief Actual normalized default, absent when none exists. */
  std::optional<ControlValue> default_value;
  /** @brief Complete allowed semantic values, independent of execution support. */
  std::span<const ControlValue> values;
  /** @brief Complete source-equivalence/context joins for this domain. */
  std::span<const SourceEquivalence> source_equivalences;
};
/** @brief One exact layout-local field and its complete binding semantics. */
struct OperandFieldContract {
  /** @brief Semantic role interpreted only within this selected layout. */
  contract::FieldRole field;
  /** @brief Complete typed payload identity. */
  contract::ValueKind value_kind;
  /** @brief Complete normalized binding policy, absent only for unbound fields. */
  std::optional<contract::OperandBinding> binding;
};
/** @brief One actual operand-layout alternative, retaining all role-local joins. */
struct LayoutContract {
  /** @brief Semantic layout identity. */
  contract::LayoutName name;
  /** @brief Fields in exact semantic order. */
  std::span<const OperandFieldContract> fields;
  /** @brief Modifier roles forbidden by this selected layout. */
  std::span<const contract::FieldRole> forbidden_modifiers;
  /** @brief Corresponding form-local modifier slot identities. */
  std::span<const std::uint16_t> forbidden_modifier_slots;
};
/** @brief Atomic qualifier linkage and full selected-qualifier domain. */
struct AtomicQualifierContract {
  /** @brief State-space control whose canonical value supplies the base space. */
  contract::FieldRole state_space;
  /** @brief Operand whose address is interpreted with the selected qualifier. */
  contract::FieldRole address;
  /** @brief All admitted actual selected atomic qualifiers. */
  std::span<const AtomicAddressQualifier> allowed;
};
/** @brief Complete native semantic description of one exact instruction form. */
struct FormContract {
  /** @brief Named form-static facts with only frontend target predicates omitted. */
  contract::FormFacts facts;
  /** @brief Complete modifier domains, defaults and source-equivalence rules. */
  std::span<const ControlContract> controls;
  /** @brief All layout alternatives and their full binding policies. */
  std::span<const LayoutContract> layouts;
  /** @brief Atomic selected-qualifier policy when the form has that control. */
  std::optional<AtomicQualifierContract> atomic_qualifier;
};
}  // namespace ptxsim::exec_ir
'''

    def ledger(self) -> tuple[tuple[str, str], ...]:
        """Expose exact retention/omission paths for review without runtime blobs."""
        retained = [(f'{name}.{member.source}', f'contract::{name}.{member.name}')
                    for name, members in self.records.items() for member in members]
        omitted = [(f'{record}.{member}', 'frontend target predicate only: deliberately omitted')
                   for record, member in sorted(self.omissions)]
        converted = [
            ('SurfaceInstructionSpec.indirect_availability', 'SurfaceInstruction.allows_indirect: presence'),
            ('TextureInstructionSpec.indirect_availability', 'TextureInstruction.allows_indirect: presence'),
            ('ResolvedMemoryConsistencyConstraint.mmio_semantics[*][1]', 'frontend target predicate only: deliberately omitted'),
        ]
        return tuple(retained + omitted + converted)


class ConstantWriter:
    """Intern native constants whose spans always reference actual static arrays."""

    def __init__(self, schema: ContractSchema, prefix: str) -> None:
        """Start one bounded translation unit's private constant pool."""
        self.schema = schema
        self.prefix = prefix
        self.lines: list[str] = []
        self.arrays: dict[tuple[str, tuple[str, ...]], str] = {}
        self.form: Form | None = None
        self.layout: int | None = None

    def array(self, cpp_type: str, values: tuple[str, ...]) -> str:
        """Return a typed const span backed by an interned owned static array."""
        if not values:
            return f'std::span<const {cpp_type}>{{}}'
        key = cpp_type, values
        if key not in self.arrays:
            name = f'{self.prefix}_data_{len(self.arrays)}'
            self.arrays[key] = name
            self.lines += [f'/** @brief Owned constant elements backing {name} spans. */\n',
                           f'constexpr std::array<{cpp_type}, {len(values)}> {name}{{{{\n',
                           ',\n'.join(f'  {value}' for value in values), '\n}};\n']
        return f'std::span<const {cpp_type}>{{{self.arrays[key]}}}'

    def value(self, type_: NativeType, value) -> str:
        """Render every retained semantic value as a fully typed native aggregate."""
        schema, kind = self.schema, type_.kind
        if kind == 'bool':
            return 'true' if value else 'false'
        if kind == 'integer':
            if not 0 <= value < (1 << 64):
                raise ValueError(f'numeric value does not fit {schema.cpp_type(type_)}: {type_.name}/{value}')
            suffix = 'LL' if schema.cpp_type(type_) == 'std::int64_t' else 'ULL'
            return str(value) + suffix
        if kind == 'signed_range':
            if len(value) != 2 or not all(-(1 << 63) <= bound < (1 << 63) for bound in value):
                raise ValueError('signed-vector immediate bounds must fit signed 64-bit values')
            bounds = [('(-9223372036854775807LL - 1LL)' if bound == -(1 << 63) else str(bound) + 'LL')
                      for bound in value]
            return 'contract::SignedVectorImmediateRange{' + ', '.join(bounds) + '}'
        if kind == 'enum':
            return schema.cpp_type(type_) + '::' + cpp_identifier(value.name)
        if kind == 'string_enum':
            return schema.enum_atom(type_.name, value)
        if kind == 'field_reference':
            return self.field_reference(value)
        if kind == 'optional':
            return 'std::nullopt' if value is None else schema.cpp_type(type_) + '{' + self.value(type_.children[0], value) + '}'
        if kind == 'record':
            return schema.cpp_type(type_) + '{' + ', '.join(
                self.value(member.type, schema.member_value(member, value))
                for member in schema.records[type_.name]) + '}'
        if kind == 'sequence':
            return self.array(schema.cpp_type(type_.children[0]), tuple(
                self.value(type_.children[0], item) for item in value))
        if kind == 'tuple':
            return schema.cpp_type(type_) + '{' + ', '.join(
                self.value(child, item) for child, item in zip(type_.children, value, strict=True)) + '}'
        raise ValueError(f'unrenderable contract value {kind}')

    def field_reference(self, value: str) -> str:
        """Resolve a semantic reference within the current exact form and layout."""
        if self.form is None:
            raise ValueError('field reference emitted outside an exact form')
        controls = [field for field in self.form.modifiers if field.identity.name == value]
        layouts = [layout.index for layout in self.form.layouts
                   if any(field.identity.name == value for field in layout.fields)]
        if value == '':
            scope = 'NoField'
        elif controls and not layouts:
            scope = 'FormControl'
        elif layouts and not controls:
            scope = 'SelectedLayoutOperand'
            if self.layout is not None:
                if self.layout not in layouts:
                    raise ValueError(f'field reference outside selected layout {self.form.identity}/{self.layout}/{value}')
                layouts = [self.layout]
        else:
            raise ValueError(f'unresolved or ambiguous local contract field {self.form.identity}/{value}')
        return 'contract::FieldReference{' + ', '.join((
            self.schema.enum_atom('FieldRole', value), 'contract::FieldReferenceScope::' + scope,
            self.array('std::uint16_t', tuple(str(index) for index in layouts)))) + '}'


def form_constant(writer: ConstantWriter, form: Form) -> str:
    """Emit every layout-local binding and complete modifier/atomic fact join."""
    schema = writer.schema
    writer.form = form
    writer.layout = None

    def atom(value) -> str:
        """Wrap a known native domain atom in the complete typed control sum."""
        return f'ControlValue{{{value.cpp}}}'

    def optional_atom(value) -> str:
        """Distinguish a genuine missing default from every real control value."""
        return 'std::nullopt' if value is None else f'std::optional<ControlValue>{{{atom(value)}}}'

    controls = []
    for field in form.modifiers:
        groups = []
        for group in field.source_groups:
            aliases = writer.array('ControlValue', tuple(atom(value) for value in group.semantic_values))
            groups.append('SourceEquivalence{' + aliases + ', ' + atom(group.canonical) +
                          ', contract::SourceContext::' + group.context.name + '}')
        controls.append('ControlContract{' + ', '.join((
            schema.enum_atom('FieldRole', field.identity.name),
            'contract::ValueKind::' + field.value_kind.name,
            'true' if field.storage.value == 'instance' else 'false',
            optional_atom(field.constant), optional_atom(field.default),
            writer.array('ControlValue', tuple(atom(value) for value in field.domain)),
            writer.array('SourceEquivalence', tuple(groups)))) + '}')
    layouts = []
    for layout in form.layouts:
        writer.layout = layout.index
        operands = []
        for field in layout.fields:
            binding = ('std::nullopt' if field.binding is None else
                       'std::optional<contract::OperandBinding>{' + writer.value(
                           NativeType('record', 'OperandBinding'), field.binding) + '}')
            operands.append('OperandFieldContract{' + ', '.join((
                schema.enum_atom('FieldRole', field.identity.name),
                'contract::ValueKind::' + field.value_kind.name, binding)) + '}')
        layouts.append('LayoutContract{' + ', '.join((
            schema.enum_atom('LayoutName', layout.identity.name),
            writer.array('OperandFieldContract', tuple(operands)),
            writer.array('contract::FieldRole', tuple(schema.enum_atom('FieldRole', value)
                                                    for value in layout.forbidden_modifiers)),
            writer.array('std::uint16_t', tuple(str(value) for value in layout.forbidden_modifier_slots)))) + '}')
    atomic = 'std::nullopt'
    if form.atomic_qualifier is not None:
        policy = schema.model.instructions[form.opcode].atomic_address_qualifier
        if policy is None:
            raise ValueError('selected atomic qualifier lacks its semantic linkage policy')
        atomic = 'std::optional<AtomicQualifierContract>{AtomicQualifierContract{' + ', '.join((
            schema.enum_atom('FieldRole', policy.state_space_field_id),
            schema.enum_atom('FieldRole', policy.address_operand_id),
            writer.array('AtomicAddressQualifier', tuple(value.cpp for value in form.atomic_qualifier.allowed)))) + '}}'
    writer.layout = None
    facts = writer.value(NativeType('record', 'FormFacts'), form.contracts)
    return 'FormContract{' + ', '.join((facts, writer.array('ControlContract', tuple(controls)),
                                        writer.array('LayoutContract', tuple(layouts)), atomic)) + '}'


def source(schema: ContractSchema, forms: tuple[Form, ...], prefix: str) -> str:
    """Define exact public static-contract getters with owned private constants."""
    writer = ConstantWriter(schema, prefix)
    definitions = []
    for form in forms:
        initializer = form_constant(writer, form)
        writer.lines += [f'/** @brief Complete owned semantic facts for {form.cpp_name}. */\n',
                         f'constexpr FormContract {form.cpp_name}_contract = {initializer};\n']
        definitions += [f'auto {form.cpp_name}::static_contract() noexcept -> const FormContract& {{\n',
                        f'  return {form.cpp_name}_contract;\n}}\n',
                        f'/** @brief Dense-table bridge to {form.cpp_name} owned static facts. */\n',
                        f'auto describe_{form.cpp_name}() noexcept -> const FormContract& {{\n',
                        f'  return {form.cpp_name}::static_contract();\n}}\n']
    return 'namespace {\n' + ''.join(writer.lines) + '}  // namespace\n' + ''.join(definitions)
