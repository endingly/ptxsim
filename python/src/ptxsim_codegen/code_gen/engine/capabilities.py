"""Resolve finite execution admission rules against the complete immutable model."""

from __future__ import annotations

from dataclasses import dataclass
from importlib.resources import files
from pathlib import Path
import re

import yaml

from ptxsim_codegen.model import GenerationError
from ptxsim_codegen.model.snapshot import FieldStorage, Form, Snapshot, ValueKind


# Values identify native sum alternatives, never frontend objects or numeric IDs.
_CATEGORIES = {
    ValueKind.MOV_SOURCE: {'register': 'RegisterRef', 'immediate': 'Immediate',
        'special_register': 'SpecialRegisterRef', 'function': 'FunctionRef',
        'symbol': 'SymbolRef', 'address': 'Address', 'opaque_resource': 'OpaqueSymbolRef'},
    ValueKind.MOV_VECTOR_SOURCE: {'register_vector': 'RegisterVector',
        'special_register': 'VectorSpecialRegisterRef'},
    ValueKind.PREDICATE_SOURCE: {'predicate': 'Predicate', 'constant': 'PredicateConstant',
        'special_register': 'PredicateSpecialRegister'},
}
_INTRINSICS = {name: 'SpecialRegisterKind::' + name for name in (
    'LaneId',
    'WarpId',
    'NWarpId',
    'SmId',
    'NSmId',
    'GridId',
    'IsExplicitCluster',
    'ClusterCtaRank',
    'ClusterNCtaRank',
    'LaneMaskEq',
    'LaneMaskLe',
    'LaneMaskLt',
    'LaneMaskGe',
    'LaneMaskGt',
    'Clock',
    'ClockHi',
    'Clock64',
    'GlobalTimer',
    'GlobalTimerLo',
    'GlobalTimerHi',
    'ReservedSmemOffsetBegin',
    'ReservedSmemOffsetEnd',
    'ReservedSmemOffsetCap',
    'TotalSmemSize',
    'AggrSmemSize',
    'DynamicSmemSize',
    'CurrentGraphExec',
    'Tid',
    'NTid',
    'CtaId',
    'NCtaId',
    'ClusterId',
    'NClusterId',
    'ClusterCtaId',
    'ClusterNCtaId',
    'PerformanceMonitor',
    'PerformanceMonitor64',
    'Environment',
    'ReservedSmemOffset',
)}


@dataclass(frozen=True)
class Selector:
    """One closed layout-local semantic selector and its native inline expression."""

    path: str
    values: tuple[object, ...]
    expression: str
    conditional: bool = False


@dataclass(frozen=True)
class Rule:
    """One exact-form handler registration with finite layout/control admission."""

    kind: int
    handler: str
    layouts: tuple[int, ...]
    controls: tuple[tuple[str, tuple[object, ...]], ...]
    selectors: tuple[tuple[int, tuple[Selector, ...]], ...]


@dataclass(frozen=True)
class Profile:
    """Immutable execution-only registrations; omitted forms retain complete representation."""

    rules: tuple[Rule, ...]

    def for_form(self, form: Form) -> tuple[Rule, ...]:
        """Return only the rules for one already resolved dense semantic identity."""
        return tuple(rule for rule in self.rules if rule.kind == form.kind)


def _mapping(value, context: str) -> dict:
    """Require an explicit mapping rather than silently coercing a malformed rule."""
    if not isinstance(value, dict):
        raise GenerationError(f'{context}: expected mapping')
    return value


def _keys(value: dict, allowed: set[str], context: str) -> None:
    """Reject misspelled keys before they can disappear into default admission."""
    if set(value) - allowed:
        raise GenerationError(f'{context}: unknown keys {set(value) - allowed}')


def _choices(value, domain: tuple[object, ...], context: str) -> tuple[object, ...]:
    """Resolve a nonempty exact finite domain without treating booleans as integers."""
    if not isinstance(value, list) or not value:
        raise GenerationError(f'{context}: expected nonempty value list')
    resolved = []
    for atom in value:
        matches = [item for item in domain if type(item) is type(atom) and item == atom]
        if len(matches) != 1 or any(type(item) is type(atom) and item == atom for item in resolved):
            raise GenerationError(f'{context}: unknown or duplicate value {atom!r}')
        resolved.append(matches[0])
    return tuple(resolved)


def _one_of(expression: str, values: tuple[str, ...]) -> str:
    """Produce an inline typed comparison with no runtime selector callback."""
    return '(' + ' || '.join(f'{expression} == {value}' for value in values) + ')'


def _selector(field, path: str, values, context: str) -> Selector:
    """Resolve an explicitly supported closed leaf path, excluding ordinary values and IDs."""
    name, _, suffix = path.partition('.')
    expression = 'operands.' + field.cpp_name
    if name != field.identity.name:
        raise GenerationError(f'{context}: selector field does not name the selected layout field')
    kind = field.value_kind
    if suffix == 'category' and kind in _CATEGORIES:
        domain = _CATEGORIES[kind]
        selected = _choices(values, tuple(domain), context)
        checks = [f'std::holds_alternative<{"exec_ir::" + domain[value]}>({expression})'
                  for value in selected]
        return Selector(path, selected, '(' + ' || '.join(checks) + ')')
    if suffix == 'intrinsic' and kind in {ValueKind.MOV_SOURCE, ValueKind.MOV_VECTOR_SOURCE}:
        selected = _choices(values, tuple(_INTRINSICS), context)
        alternative = 'SpecialRegisterRef' if kind is ValueKind.MOV_SOURCE else 'VectorSpecialRegisterRef'
        member = '.id.kind' if kind is ValueKind.MOV_SOURCE else '.register_ref.id.kind'
        intrinsic = f'std::get<exec_ir::{alternative}>({expression}){member}'
        check = _one_of(intrinsic, tuple('exec_ir::' + _INTRINSICS[value] for value in selected))
        return Selector(path, selected,
            f'(!std::holds_alternative<exec_ir::{alternative}>({expression}) || {check})', True)
    if kind is ValueKind.ADDRESS:
        if suffix == 'parameter_qualifier':
            domain = {'default': 'Default', 'entry': 'Entry', 'function': 'Function'}
            selected = _choices(values, tuple(domain), context)
            return Selector(path, selected, _one_of(expression + '.parameter_qualifier', tuple(
                'exec_ir::ParameterAddressQualifier::' + domain[value] for value in selected)))
        if suffix == 'unified':
            selected = _choices(values, (False, True), context)
            return Selector(path, selected, _one_of(expression + '.unified', tuple(
                str(value).lower() for value in selected)))
        if suffix == 'named_index':
            selected = _choices(values, ('absent', 'present'), context)
            check = expression + '.' + suffix
            return Selector(path, selected, _one_of(f'{check}.has_value()', tuple(
                'true' if value == 'present' else 'false' for value in selected)))
    optional_member = None
    if kind is ValueKind.PREDICATE_OR_SINK and suffix == 'present':
        optional_member = '.predicate'
    elif kind is ValueKind.PREDICATE_PAIR_OR_SINK and suffix in {'first.present', 'second.present'}:
        optional_member = '.' + suffix.split('.')[0]
    if optional_member is not None:
        selected = _choices(values, ('absent', 'present'), context)
        return Selector(path, selected, _one_of(expression + optional_member + '.has_value()', tuple(
            'true' if value == 'present' else 'false' for value in selected)))
    raise GenerationError(f'{context}: unknown static selector {path}')


def _overlaps(left: Rule, right: Rule) -> bool:
    """Conservatively reject any intersecting rule domain rather than selecting a first match."""
    layouts = set(left.layouts) & set(right.layouts)
    if not layouts:
        return False
    controls_left, controls_right = dict(left.controls), dict(right.controls)
    for name in controls_left.keys() & controls_right.keys():
        if not set(controls_left[name]) & set(controls_right[name]):
            return False
    for layout in layouts:
        first = {item.path: item for item in dict(left.selectors)[layout] if not item.conditional}
        second = {item.path: item for item in dict(right.selectors)[layout] if not item.conditional}
        disjoint = any(not set(first[path].values) & set(second[path].values)
                       for path in first.keys() & second.keys())
        if disjoint:
            continue
        conditional_left = {item.path: item for item in dict(left.selectors)[layout] if item.conditional}
        conditional_right = {item.path: item for item in dict(right.selectors)[layout] if item.conditional}
        for path in conditional_left.keys() & conditional_right.keys():
            category = path.rsplit('.', 1)[0] + '.category'
            categories = [set(items[category].values) for items in (first, second) if category in items]
            shared = set.intersection(*categories) if categories else set()
            if shared == {'special_register'} and not (
                    set(conditional_left[path].values) & set(conditional_right[path].values)):
                disjoint = True
                break
        if not disjoint:
            return True
    return False


def resolve(model: Snapshot, document: object) -> Profile:
    """Validate an execution document without modifying or filtering the full projection."""
    document = _mapping(document, 'execution profile')
    _keys(document, {'version', 'rules'}, 'execution profile')
    if document.get('version') != 1 or type(document.get('version')) is not int:
        raise GenerationError('execution profile: unsupported schema version')
    if not isinstance(document.get('rules'), list):
        raise GenerationError('execution profile: rules must be a list')
    forms = {f'{form.identity.opcode}/{form.identity.variant}': form for form in model.forms}
    if len(forms) != len(model.forms):
        raise GenerationError('execution profile: duplicate semantic form identity')
    result = []
    for index, source in enumerate(document['rules']):
        context = f'execution rule {index}'
        source = _mapping(source, context)
        _keys(source, {'form', 'handler', 'layouts', 'controls', 'operands'}, context)
        if not isinstance(source.get('form'), str) or source['form'] not in forms:
            raise GenerationError(f'{context}: unknown semantic form {source.get("form")!r}')
        form = forms[source['form']]
        handler = source.get('handler')
        if not isinstance(handler, str) or not re.fullmatch(r'[a-z][a-z0-9_]*', handler):
            raise GenerationError(f'{context}: invalid typed handler name')
        layouts = _choices(source.get('layouts'), tuple(layout.identity.name for layout in form.layouts), context)
        controls = []
        for name, values in _mapping(source.get('controls', {}), context).items():
            matches = [field for field in form.modifiers if field.identity.name == name]
            if len(matches) != 1:
                raise GenerationError(f'{context}: unknown control field {name}')
            field = matches[0]
            domain = (field.constant.spelling,) if field.storage is FieldStorage.CONSTANT else tuple(atom.spelling for atom in field.domain)
            controls.append((name, _choices(values, domain, context + '/' + name)))
        operands = _mapping(source.get('operands', {}), context)
        if set(operands) - set(layouts):
            raise GenerationError(f'{context}: operand selectors name an unselected layout')
        selections = []
        for layout in form.layouts:
            if layout.identity.name not in layouts:
                continue
            fields = {field.identity.name: field for field in layout.fields}
            selectors = []
            for path, values in _mapping(operands.get(layout.identity.name, {}), context).items():
                if not isinstance(path, str) or path.partition('.')[0] not in fields:
                    raise GenerationError(f'{context}: unknown layout-local operand field {path!r}')
                selectors.append(_selector(fields[path.partition('.')[0]], path, values, context))
            selections.append((layout.index, tuple(selectors)))
        rule = Rule(form.kind, handler, tuple(layout for layout, _ in selections), tuple(controls), tuple(selections))
        if any(old.kind == rule.kind and _overlaps(old, rule) for old in result):
            raise GenerationError(f'{context}: overlapping execution rules for {source["form"]}')
        result.append(rule)
    return Profile(tuple(result))


def load(model: Snapshot, path: Path | None = None) -> Profile:
    """Load the packaged default or an explicitly supplied execution-only profile."""
    text = (files('ptxsim_codegen.resources').joinpath('execution.yaml').read_text()
            if path is None else path.read_text())
    class UniqueLoader(yaml.SafeLoader):
        """Reject duplicate YAML keys instead of silently choosing a registration."""

    def mapping(loader, node, deep=False):
        """Preserve every source key so duplicates cannot hide overlapping intent."""
        pairs = loader.construct_pairs(node, deep=deep)
        result = {}
        for key, value in pairs:
            if not isinstance(key, str) or key in result:
                raise GenerationError('execution profile: non-string or duplicate mapping key')
            result[key] = value
        return result

    UniqueLoader.add_constructor(yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, mapping)
    try:
        document = yaml.load(text, Loader=UniqueLoader)
    except yaml.YAMLError as error:
        raise GenerationError(f'execution profile: invalid YAML: {error}') from error
    return resolve(model, document)


def expression(form: Form, rules: tuple[Rule, ...], layout: int) -> str:
    """Generate one selected-layout admission expression using resolved owned fields only."""
    alternatives = []
    for rule in rules:
        if layout not in rule.layouts:
            continue
        terms = []
        for name, values in rule.controls:
            field = next(field for field in form.modifiers if field.identity.name == name)
            if field.storage is FieldStorage.CONSTANT:
                continue
            atoms = [next(atom for atom in field.domain if type(atom.spelling) is type(value) and atom.spelling == value)
                     for value in values]
            terms.append(_one_of('form.' + field.cpp_name, tuple(
                str(atom.cpp).lower() if type(atom.spelling) is bool else 'exec_ir::' + atom.cpp for atom in atoms)))
        terms.extend(selector.expression for selector in dict(rule.selectors)[layout])
        alternatives.append('(' + ' && '.join(terms or ['true']) + ')')
    return '(' + ' || '.join(alternatives or ['false']) + ')'
