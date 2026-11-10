"""Exercise finite execution admission independently of full representation/binding."""
from copy import deepcopy
from dataclasses import replace
from pathlib import Path
import tempfile
import unittest

from ptxsim_codegen.code_gen.engine import capabilities
from ptxsim_codegen.code_gen.engine.native import _group, implemented_forms
from ptxsim_codegen.frontend.v080 import snapshot
from ptxsim_codegen.model import GenerationError


class ExecutionCapabilitiesTests(unittest.TestCase):
    """Check explicit registration, closed selectors, overlap and missing symbols."""

    @classmethod
    def setUpClass(cls):
        """Reuse the complete immutable input instead of synthesizing a reduced ISA."""
        cls.model = snapshot()
        cls.profile = capabilities.load(cls.model)
        cls.form = next(form for form in cls.model.forms if form.identity.variant == 'mov_scalar')

    def document(self):
        """Provide a deliberately small profile while leaving all 5,453 forms intact."""
        return {'version': 1, 'rules': [{'form': 'mov/mov_scalar', 'handler': 'movement',
                'layouts': ['scalar'], 'controls': {'type': ['u32']},
                'operands': {'scalar': {'src.category': ['register', 'special_register'],
                                       'src.intrinsic': ['Tid']}}}]}

    def test_packaged_inventory_has_exact_old_forms_and_full_dense_model(self):
        """The profile keeps 67 prior forms and never prunes the 5,453-form model."""
        self.assertEqual(len(self.profile.rules), 83)
        self.assertEqual(len(implemented_forms(self.model, self.profile)), 67)
        self.assertEqual(len(self.model.forms), 5453)
        fma = [f for f in implemented_forms(self.model, self.profile) if f.identity.opcode == 'fma']
        self.assertEqual(len(fma), 16)

    def test_unknown_form_control_layout_and_enum_are_rejected(self):
        """Misspelled finite identities cannot silently remove execution restrictions."""
        for key, value in [('form', 'mov/missing'), ('controls', {'typo': ['u32']}),
                           ('controls', {'type': ['missing']}), ('layouts', ['missing'])]:
            with self.subTest(key=key, value=value):
                document = self.document(); document['rules'][0][key] = value
                with self.assertRaises(GenerationError): capabilities.resolve(self.model, document)

    def test_unknown_operand_paths_tags_intrinsics_and_ordinary_ids_are_rejected(self):
        """Only declared semantic identities and genuine presence are selectors."""
        for path, values in [('missing.category', ['register']), ('src.category', ['missing']),
                ('src.intrinsic', ['Missing']), ('src.slot', [0]), ('src.bits', [1]),
                ('src.declared_type', ['u32']), ('src.index', [1])]:
            with self.subTest(path=path):
                document = self.document(); document['rules'][0]['operands'] = {'scalar': {path: values}}
                with self.assertRaises(GenerationError): capabilities.resolve(self.model, document)

    def test_duplicate_and_intersecting_rules_fail_instead_of_first_match(self):
        """Intersecting control/category subsets are rejected deterministically."""
        document = self.document(); document['rules'].append(deepcopy(document['rules'][0]))
        with self.assertRaises(GenerationError): capabilities.resolve(self.model, document)
        document['rules'][1]['controls']['type'] = ['u32', 's32']
        with self.assertRaises(GenerationError): capabilities.resolve(self.model, document)
        document['rules'][1]['operands']['scalar']['src.intrinsic'] = ['GridId']
        with self.assertRaises(GenerationError): capabilities.resolve(self.model, document)

    def test_disjoint_control_and_layout_rules_resolve(self):
        """Proven disjoint finite domains need no runtime priority or selector function."""
        document = self.document(); document['rules'].append(deepcopy(document['rules'][0]))
        document['rules'][1]['controls']['type'] = ['s32']
        self.assertEqual(len(capabilities.resolve(self.model, document).rules), 2)

    def test_disjoint_intrinsics_require_exclusive_special_register_categories(self):
        """Conditional intrinsic tests cannot hide overlapping ordinary alternatives."""
        document = self.document(); document['rules'][0]['operands']['scalar']['src.category'] = ['special_register']
        document['rules'].append(deepcopy(document['rules'][0]))
        document['rules'][1]['operands']['scalar']['src.intrinsic'] = ['GridId']
        self.assertEqual(len(capabilities.resolve(self.model, document).rules), 2)
        for rule in document['rules']:
            rule['operands']['scalar']['src.category'].append('register')
        with self.assertRaises(GenerationError): capabilities.resolve(self.model, document)

    def test_default_unsupported_preserves_structural_mov_checks(self):
        """Disabling execution cannot hide malformed aggregate or intrinsic types."""
        profile = capabilities.resolve(self.model, {'version': 1, 'rules': []})
        self.assertFalse(implemented_forms(self.model, profile))
        source = _group(self.form, profile.for_form(self.form))
        self.assertIn('valid_scalar_move_shape', source)
        self.assertIn('valid_aggregate_move_shape', source)
        self.assertLess(source.index('valid_aggregate_move_shape'), source.index('unsupported_instruction'))
        self.assertNotIn('prepare_group(', source)

    def test_missing_handler_produces_a_required_typed_symbol(self):
        """A declared unavailable implementation is never changed into unsupported."""
        document = self.document(); document['rules'][0]['handler'] = 'unavailable_test_handler'
        profile = capabilities.resolve(self.model, document)
        source = _group(self.form, profile.for_form(self.form))
        self.assertIn('handler_unavailable_test_handler_MovScalar', source)
        self.assertIn('const exec_ir::MovScalar& form', source)
        self.assertIn('return handler_unavailable_test_handler_MovScalar(', source)

    def test_profile_and_structural_gate_use_the_same_layout_cpp_access(self):
        """Backend member spelling cannot split the owned structural and admission paths."""
        layout = self.form.layouts[0]
        changed = replace(self.form, layouts=(replace(layout, fields=tuple(
            replace(field, cpp_name='renamed_source') if field.identity.name == 'src' else field
            for field in layout.fields)), *self.form.layouts[1:]))
        model = replace(self.model, instructions=tuple(replace(op, forms=tuple(
            changed if form.identity == changed.identity else form for form in op.forms))
            for op in self.model.instructions))
        profile = capabilities.resolve(model, self.document())
        source = _group(changed, profile.for_form(changed))
        self.assertIn('valid_scalar_move_shape(selected->renamed_source', source)
        self.assertIn('holds_alternative<exec_ir::RegisterRef>(operands.renamed_source)', source)

    def test_custom_relaxed_memory_rule_cannot_expand_handler_semantics(self):
        """A valid wider profile still meets the known memory handler's early bound."""
        form = next(form for form in self.model.forms if form.identity.variant == 'ld_generic_scalar')
        profile = capabilities.resolve(self.model, {'version': 1, 'rules': [{
            'form': 'ld/ld_generic_scalar', 'handler': 'memory', 'layouts': ['default'],
            'controls': {'semantics': ['relaxed']}}]})
        source = _group(form, profile.for_form(form))
        self.assertIn('form.semantics == exec_ir::MemoryConsistency::relaxed', source)
        guard = source.index('form.semantics != exec_ir::MemoryConsistency::omitted')
        self.assertLess(guard, source.index('prepare_group('))
        self.assertIn('return step_error(StepErrorCode::unsupported_instruction);',
                      source[guard:source.index('auto prepare =')])

    def test_custom_xor_comparison_rule_cannot_expand_handler_semantics(self):
        """A registered XOR control cannot bypass the implemented literal-AND bound."""
        form = next(form for form in self.model.forms if form.identity.variant == 'setp_unsigned_boolean')
        profile = capabilities.resolve(self.model, {'version': 1, 'rules': [{
            'form': 'setp/setp_unsigned_boolean', 'handler': 'comparison', 'layouts': ['single'],
            'controls': {'boolean': ['xor'], 'type': ['u32'], 'comparison': ['eq']}}]})
        source = _group(form, profile.for_form(form))
        self.assertIn('form.boolean == exec_ir::BooleanOperator::xor_', source)
        guard = source.index('form.boolean != exec_ir::BooleanOperator::and_')
        self.assertLess(guard, source.index('prepare_group('))
        self.assertIn('return step_error(StepErrorCode::unsupported_instruction);',
                      source[guard:source.index('auto prepare =')])

    def test_duplicate_yaml_keys_are_rejected(self):
        """A duplicate key must not overwrite earlier execution intent."""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'profile.yaml'
            path.write_text('version: 1\nrules: []\nrules: []\n')
            with self.assertRaises(GenerationError): capabilities.load(self.model, path)

    def test_profile_admission_precedes_ordinary_lane_preparation(self):
        """Every registered typed entry has inline profile admission before lane loops."""
        for form in implemented_forms(self.model, self.profile):
            source = _group(form, self.profile.for_form(form))
            self.assertLess(source.index('unsupported_instruction'), source.index('prepare_group('))
            self.assertNotIn('instruction_kind()', source)
            self.assertNotIn('std::function', source)


if __name__ == '__main__':
    unittest.main()
