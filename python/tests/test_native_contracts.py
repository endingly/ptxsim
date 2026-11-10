"""Check semantic retention at the Python-to-native constant-data boundary."""

from dataclasses import replace
import unittest

from ptxsim_codegen.code_gen.exec_ir.contracts import ContractSchema, ConstantWriter, NativeType, form_constant
from ptxsim_codegen.frontend.v080 import snapshot


class NativeContractTests(unittest.TestCase):
    """Protect mixed semantic/target-gate records and layout-local references."""

    @classmethod
    def setUpClass(cls) -> None:
        """Load the same immutable projection used by all production emitters."""
        cls.model = snapshot()
        cls.schema = ContractSchema(cls.model)

    def test_indirect_admission_uses_presence_including_empty_surface_gate(self) -> None:
        """An empty availability mapping still admits an indirect resource."""
        surface = next(form.contracts.surface for form in self.model.forms if form.contracts.surface is not None)
        texture = next(form.contracts.texture for form in self.model.forms if form.contracts.texture is not None)
        smember = next(member for member in self.schema.records['SurfaceInstruction'] if member.name == 'allows_indirect')
        tmember = next(member for member in self.schema.records['TextureInstruction'] if member.name == 'allows_indirect')
        self.assertTrue(self.schema.member_value(smember, replace(surface, indirect_availability={})))
        self.assertFalse(self.schema.member_value(tmember, replace(texture, indirect_availability=None)))
        self.assertTrue(self.schema.member_value(tmember, replace(texture, indirect_availability={})))

    def test_adjacent_semantic_obligations_survive_target_predicate_omission(self) -> None:
        """Effective type/width, spaces, direction, modern vectors and sinks remain."""
        expected = {
            'OperandTypeCompatibility': {'target_field_id', 'special_register_kind', 'instruction_width', 'effective_type'},
            'AddressStateSpace': {'value'},
            'ParameterAddressConstraint': {'direction', 'entry_only'},
            'MemoryVectorConstraint': {'type_field_id', 'vector_field_id', 'address_field_id', 'state_space_field_id', 'require_modern'},
        }
        for name, retained in expected.items():
            self.assertEqual({member.source for member in self.schema.records[name]}, retained)
        bindings = {member.source for member in self.schema.records['OperandBinding']}
        self.assertTrue({'allow_destination_sink', 'allow_predicate_sink', 'allow_vector_sink',
                         'vector_sink_payload_bits', 'mbarrier_state_token_form'} <= bindings)
        memory = next(form.contracts.memory_consistency for form in self.model.forms
                      if form.contracts.memory_consistency and form.contracts.memory_consistency.mmio_semantics)
        mmio = next(member for member in self.schema.records['MemoryConsistencyConstraint'] if member.source == 'mmio_semantics')
        self.assertEqual(self.schema.member_value(mmio, memory), tuple(value for value, _ in memory.mmio_semantics))
        self.assertTrue(all(isinstance(value, str) for value in self.schema.member_value(mmio, memory)))
        self.assertNotIn('Any', self.schema.header())
        self.assertNotIn('ptx_frontend', self.schema.header())

    def test_signed_vector_range_has_signed_endpoints_even_for_positive_maximum(self) -> None:
        """Signed source comparisons cannot promote a negative lane to unsigned."""
        member = next(member for member in self.schema.records['OperandBinding']
                      if member.source == 'vector_signed_immediate_range')
        self.assertEqual(self.schema.cpp_type(member.type),
                         'std::optional<contract::SignedVectorImmediateRange>')
        writer = ConstantWriter(self.schema, 'range')
        self.assertIn('{-8LL, 7LL}', writer.value(member.type, (-8, 7)))
        self.assertEqual(writer.value(member.type, None), 'std::nullopt')
        self.assertIn('{0LL, 7LL}', writer.value(member.type, (0, 7)))
        self.assertIn('(-9223372036854775807LL - 1LL)', writer.value(member.type, (-(1 << 63), 0)))

    def test_numeric_representation_is_fixed_and_unsigned_high_bits_survive(self) -> None:
        """Samples validate a declared representation without choosing its signedness."""
        type_ = self.schema.type(int, 'ImmediateValueConstraint_values', 'values')
        self.assertEqual(self.schema.cpp_type(type_), 'std::uint64_t')
        writer = ConstantWriter(self.schema, 'numeric')
        self.assertEqual(writer.value(type_, (1 << 64) - 1), '18446744073709551615ULL')
        self.assertEqual(writer.value(type_, 1 << 63), '9223372036854775808ULL')
        with self.assertRaisesRegex(ValueError, 'does not fit'):
            writer.value(type_, -1)
        self.assertEqual(self.schema.cpp_type(type_), 'std::uint64_t')
        with self.assertRaisesRegex(ValueError, 'unmapped numeric representation'):
            self.schema.type(int, 'NewRecord_new_numeric', 'new_numeric')

    def test_field_references_are_resolved_in_form_and_selected_layout(self) -> None:
        """Equal names in different layout payloads retain distinct local paths."""
        form = next(form for form in self.model.forms if form.identity.variant == 'mov_scalar')
        writer = ConstantWriter(self.schema, 'test')
        writer.form = form
        writer.layout = 1
        reference = writer.field_reference('src')
        self.assertIn('SelectedLayoutOperand', reference)
        self.assertIn('std::uint16_t', reference)
        self.assertIn('  1', ''.join(writer.lines))
        with self.assertRaisesRegex(ValueError, 'unresolved or ambiguous'):
            writer.field_reference('absent_role')
        with self.assertRaisesRegex(ValueError, 'unknown native semantic token'):
            self.schema.enum_atom('FieldRole', 'absent_role')
        # Emit all records to ensure even unexecuted/new forms have complete typed data.
        for exact in self.model.forms:
            form_constant(ConstantWriter(self.schema, 'all'), exact)


if __name__ == '__main__':
    unittest.main()
