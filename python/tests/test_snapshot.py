"""Full semantic projection and ownership checks for the released frontend."""

from collections import defaultdict
from collections.abc import Mapping
from dataclasses import FrozenInstanceError, fields, is_dataclass
from enum import Enum
from importlib import resources
from pathlib import Path
import tempfile
from types import MappingProxyType
import unittest

import yaml

from ptxsim_codegen.frontend.v080 import snapshot
from ptxsim_codegen.model import GenerationError
from ptxsim_codegen.model.snapshot import FieldStorage, SourceContext, ValueKind


class SnapshotTests(unittest.TestCase):
    """Verify full coverage, layout identity and a deeply immutable fact graph."""

    @classmethod
    def setUpClass(cls) -> None:
        """Load the released full model once without changing packaged facts."""
        cls.model = snapshot()

    def test_complete_dense_topology_and_leaf_coverage(self) -> None:
        """Every released form/layout exists independently of executor support."""
        model = self.model
        self.assertEqual((len(model.instructions), len(model.forms), model.layout_count),
                         (137, 5453, 7457))
        self.assertEqual([form.kind for form in model.forms], list(range(5453)))
        self.assertEqual([op.identity for op in model.instructions], list(range(137)))
        self.assertEqual(len({form.cpp_name for form in model.forms}), 5453)
        self.assertEqual(len(model.specification_operand_kinds), 48)
        self.assertEqual(len({kind for form in model.forms for layout in form.layouts
                              for kind in (field.value_kind for field in layout.fields)}), 41)
        mapped = {domain.kind for domain in model.domains} | {leaf.kind for leaf in model.leaves}
        self.assertEqual(mapped, set(ValueKind))
        for form in model.forms:
            for layout in form.layouts:
                self.assertTrue(all(field.identity.form == form.identity and
                                    field.identity.layout == layout.identity
                                    for field in layout.fields))
            for control in form.modifiers:
                self.assertTrue(control.domain)
                if control.storage is FieldStorage.CONSTANT:
                    self.assertIn(control.constant, control.domain)

    def test_layout_local_names_preserve_distinct_payloads(self) -> None:
        """Equal semantic names in different layouts never overwrite each other."""
        overloaded = []
        for form in self.model.forms:
            names = defaultdict(list)
            for layout in form.layouts:
                for field in layout.fields:
                    names[field.identity.name].append(field)
            for name, appearances in names.items():
                if len({field.value_kind for field in appearances}) > 1:
                    overloaded.append((form.identity, name))
                    self.assertEqual(len({field.identity for field in appearances}),
                                     len(appearances))
                    self.assertGreater(len({field.frontend.member for field in appearances}), 1)
        self.assertTrue(overloaded)

    def test_semantic_controls_do_not_inherit_cpp_member_aliases(self) -> None:
        """Version-specific member spelling cannot rename the semantic control."""
        add = next(form for form in self.model.forms
                   if form.identity.opcode == 'add' and form.identity.variant == 'add_float_f32')
        saturation = next(field for field in add.modifiers if field.identity.name == 'sat')
        self.assertEqual(saturation.cpp_name, 'sat')
        self.assertEqual(saturation.frontend.member, 'saturate')

    def test_snapshot_contains_no_mutable_or_opaque_fact_nodes(self) -> None:
        """The shared model has no mutable containers or untyped source object."""
        visited = set()

        def visit(value) -> None:
            """Follow typed immutable ownership, rejecting unknown payloads."""
            if value is None or isinstance(value, (str, bool, int, float, Enum)):
                return
            if id(value) in visited:
                return
            visited.add(id(value))
            if is_dataclass(value):
                self.assertTrue(value.__dataclass_params__.frozen)
                for member in fields(value):
                    self.assertNotIn(member.name, {'source', 'frontend_payload'})
                    visit(getattr(value, member.name))
            elif isinstance(value, Mapping):
                self.assertIsInstance(value, MappingProxyType)
                for key, item in value.items():
                    visit(key)
                    visit(item)
            elif isinstance(value, (tuple, frozenset)):
                for item in value:
                    visit(item)
            else:
                self.fail(f'mutable or opaque model value: {type(value).__name__}')

        visit(self.model)
        with self.assertRaises(FrozenInstanceError):
            self.model.forms[0].kind = 123

    def test_selected_atomic_qualifier_and_source_equivalence_are_explicit(self) -> None:
        """Shared/parameter source aliases keep their additional selected context."""
        atomic = [form for form in self.model.forms if form.atomic_qualifier is not None]
        self.assertEqual(len(atomic), 99)
        self.assertTrue(all(form.atomic_qualifier.frontend.member == 'address_qualifier'
                            and form.atomic_qualifier.frontend.located for form in atomic))
        aliased = [(form, field) for form in self.model.forms for field in form.modifiers
                   if any(len(group.semantic_values) > 1 for group in field.source_groups)]
        self.assertEqual(len(aliased), 61)
        contexts = set()
        for form, field in aliased:
            for group in field.source_groups:
                if len(group.semantic_values) < 2:
                    continue
                contexts.add(group.context)
                self.assertIn(group.canonical, field.domain)
                self.assertEqual(len({value.frontend_cpp for value in group.semantic_values}), 1)
                if group.context is SourceContext.ATOMIC_QUALIFIER:
                    self.assertIsNotNone(form.atomic_qualifier)
                    self.assertEqual(group.canonical.spelling, 'shared')
                elif group.context is SourceContext.PARAMETER_QUALIFIER:
                    self.assertEqual(group.canonical.spelling, 'param')
                    self.assertTrue(any(value.value_kind is ValueKind.ADDRESS
                                        for layout in form.layouts for value in layout.fields))
                else:
                    self.fail('source aliases lack a modeled semantic context')
        self.assertEqual(contexts, {SourceContext.ATOMIC_QUALIFIER, SourceContext.PARAMETER_QUALIFIER})

    def test_backend_cannot_trim_vocabulary_or_alias_owned_enumerators(self) -> None:
        """Backend configuration maps full vocabularies without selecting support."""
        original = yaml.safe_load(resources.files('ptxsim_codegen.resources').joinpath('backend.yaml').read_text())
        mutations = [
            lambda raw: raw['domains']['ScalarType']['values'].pop(),
            lambda raw: raw['domains']['ScalarType']['values'].append({'value': 'fake_scalar', 'cpp': 'DataType::fake_scalar'}),
            lambda raw: raw['domains']['ScalarType']['values'][1].__setitem__('cpp', raw['domains']['ScalarType']['values'][0]['cpp']),
        ]
        for mutate in mutations:
            raw = yaml.safe_load(yaml.safe_dump(original))
            mutate(raw)
            with tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / 'backend.yaml'
                path.write_text(yaml.safe_dump(raw))
                with self.assertRaisesRegex(GenerationError, 'vocabulary|collision'):
                    snapshot(backend_path=path)

    def test_missing_or_unknown_backend_mapping_fails_before_projection(self) -> None:
        """Full binding types cannot silently become unsupported placeholders."""
        original = yaml.safe_load(resources.files('ptxsim_codegen.resources').joinpath('backend.yaml').read_text())
        for mutate in [lambda raw: raw['leaves'].pop('Address'),
                       lambda raw: raw['leaves'].__setitem__('UnknownOperand', {'cpp_type': 'X', 'binder': 'bind_x'})]:
            raw = yaml.safe_load(yaml.safe_dump(original))
            mutate(raw)
            with tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / 'backend.yaml'
                path.write_text(yaml.safe_dump(raw))
                with self.assertRaisesRegex(GenerationError, 'every frontend operand value kind'):
                    snapshot(backend_path=path)


if __name__ == '__main__':
    unittest.main()
