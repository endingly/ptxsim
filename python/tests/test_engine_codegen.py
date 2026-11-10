"""Retain execution-adapter derivation, rejection and publication obligations."""

from __future__ import annotations

from dataclasses import replace
from pathlib import Path
import tempfile
import unittest

from ptxsim_codegen.code_gen.engine.native import (
    _group, _owned_fields, artifact_plan, generate, implemented_forms,
    selected_value_forms, value_paths,
)
from ptxsim_codegen.frontend.v080 import snapshot
from ptxsim_codegen.model import GenerationError
from ptxsim_codegen.model.snapshot import FieldStorage, ValueKind


class GenerateTests(unittest.TestCase):
    """Check handler ABI drift independently of the full closed native representation."""

    @classmethod
    def setUpClass(cls) -> None:
        """Load one immutable frontend projection for all independent mutation cases."""
        cls.model = snapshot()
        cls.forms = {(form.identity.opcode,form.identity.variant):form for form in cls.model.forms}

    def form(self, name: str):
        """Select an exact semantic form without relying on a compatibility alias."""
        return next(form for form in self.model.forms if form.identity.variant == name)

    def operand(self, form, index: int, **changes):
        """Mutate one layout-local operand while retaining the rest of the model."""
        layout=form.layouts[0]
        fields=list(layout.fields); fields[index]=replace(fields[index],**changes)
        return replace(form,layouts=(replace(layout,fields=tuple(fields)),*form.layouts[1:]))

    def binding(self, form, index: int, **changes):
        """Mutate one typed binding attached to its exact field identity."""
        return self.operand(form,index,binding=replace(form.layouts[0].fields[index].binding,**changes))

    def model_with(self, changed):
        """Replace one form without losing full untouched opcode/form coverage."""
        return replace(self.model,instructions=tuple(replace(op,forms=tuple(
            changed if form.identity == changed.identity else form for form in op.forms))
            for op in self.model.instructions))

    def test_emits_every_projected_form_and_dynamic_type(self):
        """All full IR Kinds own entries; existing forms own direct typed groups."""
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); plan=generate(self.model,root)
            self.assertEqual(plan,artifact_plan(self.model))
            self.assertEqual(len(implemented_forms(self.model)),67)
            source=(root/'instruction_preparation.gen.cpp').read_text()
            self.assertEqual(source.count('&group_'),5454)  # One type declaration plus every Kind entry.
            for form in self.model.forms: self.assertIn('&group_'+form.cpp_name,source)
            self.assertEqual(source.count('instruction.instruction_kind()'),1)
            bodies='\n'.join((root/path).read_text() for path in plan.sources)
            self.assertNotIn('structurally_valid(',bodies)
            self.assertNotIn('std::visit(',bodies)
            self.assertIn('prepare_group(',bodies)

    def test_mov_derives_scalar_pack_unpack_vector_and_predicate_paths(self):
        """Both original aggregate shapes and the split b128 form preserve old behavior."""
        for name in ('mov_scalar','mov_b128_pack_unpack','mov_v4_u32','mov_pred'):
            self.assertIn('prepare_',_group(self.form(name)))
        scalar=_group(self.form('mov_scalar'))
        for name in ('ScalarOperands','PackOperands','UnpackOperands'): self.assertIn(name,scalar)
        self.assertIn('OpaqueSymbolRef',scalar); self.assertIn('FunctionRef',scalar)
        self.assertLess(scalar.index('unsupported_instruction'),scalar.index('prepare_group('))

    def test_mov_rejects_unrecognized_vector_sink_contract(self):
        """An aggregate source cannot begin accepting sinks without a handler change."""
        form=self.form('mov_b128_pack_unpack')
        changed=self.binding(form,1,allow_vector_sink=True)
        with self.assertRaises(GenerationError): _group(changed)

    def test_mov_rejects_unconsumed_modifier_and_width_policy_drift(self):
        """New controls and storage widening never silently enter movement semantics."""
        form=self.form('mov_scalar'); field=form.modifiers[0]
        changed=replace(form,modifiers=(replace(field,identity=replace(field.identity,name='unknown')),))
        with self.assertRaises(GenerationError): _group(changed)
        policy=type(form.layouts[0].fields[0].binding.register_width_policy).EQUAL_OR_WIDER
        with self.assertRaises(GenerationError): _group(self.binding(form,0,register_width_policy=policy))

    def test_mov_rejects_predicate_fixed_type_drift(self):
        """Predicate movement retains the fixed logical domain."""
        form=self.form('mov_pred'); field=form.modifiers[0]
        atom=next(atom for domain in self.model.domains for atom in domain.values if atom.cpp=='DataType::b32')
        with self.assertRaises(GenerationError): _group(replace(form,modifiers=(replace(field,constant=atom),)))

    def test_mov_rejects_missing_or_duplicate_projected_inventory(self):
        """A missing existing implementation cannot degrade to a default unsupported entry."""
        op=next(op for op in self.model.instructions if op.opcode=='mov')
        for forms in (op.forms[1:],(*op.forms,op.forms[0])):
            changed=replace(self.model,instructions=tuple(replace(item,forms=forms) if item is op else item for item in self.model.instructions))
            with self.assertRaises(GenerationError): implemented_forms(changed)

    def test_fma_inventory_controls_and_three_read_preparation_are_complete(self):
        """All 16 existing FMA forms retain exactly three numeric inputs."""
        forms=[form for form in selected_value_forms(self.model) if form.identity.opcode=='fma']
        self.assertEqual(len(forms),16)
        for form in forms:
            _,sources,paths=value_paths(form); self.assertEqual(len(sources),3)
            self.assertTrue(paths)
            text=_group(form); self.assertIn('semantics::fma',text)
            self.assertIn('source_2',text)

    def test_template_rejects_missing_render_data(self):
        """Template-free emitters still reject missing scoped handler fields."""
        with self.assertRaises(GenerationError): _owned_fields('operands.absent',self.form('mov_pred').layouts[0])

    def test_value_alu_family_accepts_operation_and_semantic_parameters(self):
        """Each existing arithmetic opcode supplies its own numeric operation and arity."""
        for form in selected_value_forms(self.model):
            self.assertIn('semantics::'+form.identity.opcode+'(',_group(form))

    def test_rendering_is_deterministic(self):
        """Complete bounded artifacts retain exact bytes across repeated publication."""
        with tempfile.TemporaryDirectory() as directory:
            first,second=Path(directory)/'a',Path(directory)/'b'
            plan=generate(self.model,first); self.assertEqual(generate(self.model,second),plan)
            for path in (*plan.headers,*plan.sources): self.assertEqual((first/path).read_bytes(),(second/path).read_bytes())

    def test_derives_mixed_source_and_result_type_paths(self):
        """Mixed Add inputs retain distinct selected input and f32 result codecs."""
        _,_,paths=value_paths(self.form('add_mixed_f32'))
        self.assertTrue(all(path.destination=='arith::float32_t' for path in paths))
        self.assertTrue(any('arith::float16_t' in path.sources for path in paths))

    def test_derives_operand_paths_from_the_resolved_layout(self):
        """Physical C++ operand spelling is read from the exact layout field."""
        form=self.form('add_integer_no_sat')
        changed=self.operand(form,1,cpp_name='renamed_source')
        self.assertIn('operands.renamed_source',_group(changed))

    def test_derives_fixed_type_from_projected_modifier_field(self):
        """A fixed semantic scalar selector resolves to a concrete codec."""
        form=self.form('add_integer_no_sat'); control=next(field for field in form.modifiers if field.identity.name=='type')
        atom=next(atom for atom in control.domain if atom.spelling=='u16')
        changed=replace(form,modifiers=tuple(replace(field,storage=FieldStorage.CONSTANT,constant=atom) if field is control else field for field in form.modifiers))
        _,_,paths=value_paths(changed)
        self.assertEqual(len(paths),1); self.assertEqual(paths[0].destination,'std::uint16_t')

    def test_matches_bindings_by_target_field_not_tuple_order(self):
        """Reordering complete fields preserves attached semantic type expressions."""
        form=self.form('add_integer_no_sat'); layout=form.layouts[0]
        changed=replace(form,layouts=(replace(layout,fields=(layout.fields[0],layout.fields[2],layout.fields[1])),))
        _,reads,_=value_paths(changed)
        self.assertEqual(tuple(item.name for item in reads),('src2','src1'))

    def test_rejects_projected_value_alu_shape_drift(self):
        """An extra source is rejected rather than omitted from arithmetic."""
        form=self.form('add_integer_no_sat'); layout=form.layouts[0]
        extra=layout.fields[1]; extra=replace(extra,identity=replace(extra.identity,name='extra'),binding=replace(extra.binding,target_field_id='extra'))
        with self.assertRaises(GenerationError): value_paths(replace(form,layouts=(replace(layout,fields=(*layout.fields,extra)),)))

    def test_rejects_read_write_operand_access(self):
        """Value arithmetic cannot silently ignore a read/write role."""
        form=self.form('add_integer_no_sat'); access=type(form.layouts[0].fields[0].binding.access).READ_WRITE
        with self.assertRaises(GenerationError): value_paths(self.binding(form,0,access=access))

    def test_rejects_unsupported_value_alu_width_policy(self):
        """Arithmetic preserves exact and same-width physical storage policy."""
        form=self.form('add_integer_no_sat'); policy=type(form.layouts[0].fields[0].binding.register_width_policy).EQUAL_OR_WIDER
        with self.assertRaises(GenerationError): value_paths(self.binding(form,0,register_width_policy=policy))

    def test_rejects_a_new_unadapted_control(self):
        """No newly projected control is ignored by an old implementation."""
        form=self.form('add_float_f32'); field=form.modifiers[0]
        changed=replace(form,modifiers=(replace(field,identity=replace(field.identity,name='unknown')),*form.modifiers[1:]))
        with self.assertRaises(GenerationError): value_paths(changed)

    def test_requires_every_enabled_operation(self):
        """Removing an existing opcode fails the handler registration contract."""
        changed=replace(self.model,instructions=tuple(op for op in self.model.instructions if op.opcode!='add'))
        with self.assertRaises(GenerationError): implemented_forms(changed)

    def test_setp_derives_every_projected_form_and_predicate_topology(self):
        """Single/pair layouts retain s32/u32 relations and AND combine scope."""
        for name in ('setp_signed','setp_signed_boolean','setp_unsigned','setp_unsigned_boolean'):
            text=_group(self.form(name))
            self.assertIn('SingleOperands',text); self.assertIn('PairOperands',text)
            self.assertIn('effect.writes[1]',text)
            if name.endswith('_boolean'): self.assertIn('form.boolean != exec_ir::BooleanOperator::and_',text)

    def test_setp_rejects_predicate_shape_drift(self):
        """Single and pair predicate topology cannot be conflated."""
        form=self.form('setp_unsigned')
        with self.assertRaises(GenerationError): _group(self.operand(form,0,value_kind=ValueKind.PREDICATE_PAIR_OR_SINK))

    def test_derives_sub_mixed_operand_names_from_its_layout(self):
        """The distinct source and subtrahend roles remain ordered."""
        text=_group(self.form('sub_mixed_f32'))
        self.assertIn('operands.src',text); self.assertIn('operands.subtrahend',text)

    def test_derives_wide_operand_types_independently(self):
        """Wide Mul uses a 64-bit result and 32-bit operands."""
        _,_,paths=value_paths(self.form('mul_wide_s32'))
        self.assertEqual(paths[0].destination,'std::int64_t')
        self.assertEqual(paths[0].sources,('std::int32_t','std::int32_t'))

    def test_rejects_unresolved_operand_type_expressions(self):
        """A missing referenced modifier does not become a guessed scalar type."""
        form=self.form('add_integer_no_sat'); expression=form.layouts[0].fields[0].binding.type_expression
        with self.assertRaises(GenerationError): value_paths(self.binding(form,0,type_expression=replace(expression,modifier_field_id='absent')))

    def test_memory_derives_ordinary_forms_and_rejects_hint_forms(self):
        """Only the eight existing ordinary memory forms select typed memory groups."""
        selected=implemented_forms(self.model)
        memory=[form for form in selected if form.identity.opcode in {'ld','st'}]
        self.assertEqual(len(memory),8)
        for form in memory:
            text=_group(form); self.assertIn('prepare_memory_',text)
            self.assertIn('ParameterAddressQualifier::Function',text)
        self.assertFalse(any('hint' in form.identity.variant for form in memory))

    def test_memory_derives_renamed_operand_fields(self):
        """A handler resolves memory roles to the selected owned C++ names."""
        form=self.form('ld_generic_scalar'); changed=self.operand(form,1,cpp_name='renamed_address')
        self.assertIn('form.operands.renamed_address',_group(changed))

    def test_memory_rejects_type_allowlist_drift(self):
        """An additional representable type remains outside the current memory admission."""
        text=_group(self.form('ld_generic_scalar'))
        self.assertNotIn('form.type == exec_ir::DataType::b128',text)
        self.assertIn('unsupported_instruction',text)
        self.assertIn('DataType::b8',text); self.assertIn('DataType::f64',text)

    def test_bar_derives_every_projected_form_and_reduction_token(self):
        """All eleven existing collective forms retain their complete protocols."""
        forms=[form for form in implemented_forms(self.model) if form.identity.opcode=='bar']
        self.assertEqual(len(forms),11)
        for form in forms:
            text=_group(form)
            if form.identity.variant=='bar_warp_sync': self.assertIn('PrepareKind::warp_sync',text)
            else: self.assertIn('PrepareKind::cta_barrier',text)
        for name in ('Popc','And','Or'): self.assertIn('Reduce'+name,_group(self.form('bar_red_'+name.lower()+'_'+('u32' if name=='Popc' else 'pred'))))

    def test_bar_derives_renamed_operand_fields(self):
        """A barrier carrier follows its layout-local owned field mapping."""
        form=self.form('bar_warp_sync'); changed=self.operand(form,0,cpp_name='renamed_mask')
        self.assertIn('form.operands.renamed_mask',_group(changed))

    def test_bar_rejects_reduction_token_drift(self):
        """A changed fixed reduction control cannot execute under the old protocol."""
        form=self.form('bar_red_popc_u32'); field=next(field for field in form.modifiers if field.identity.name=='reduction')
        changed=replace(form,modifiers=tuple(replace(item,constant=replace(item.constant,spelling=False)) if item is field else item for item in form.modifiers))
        with self.assertRaises(GenerationError): _group(changed)

    def test_bar_rejects_predicate_destination_shape_drift(self):
        """Predicate reductions retain a logical destination rather than a scalar slot."""
        form=self.form('bar_red_and_pred')
        with self.assertRaises(GenerationError): _group(self.operand(form,0,value_kind=ValueKind.REGISTER))

    def test_control_families_derive_direct_target_and_bare_exit(self):
        """Control entries preserve owned local PC/owner and architectural exit effects."""
        self.assertIn('form.operands.target.pc',_group(self.form('bra_direct')))
        self.assertIn('target.function != function',_group(self.form('bra_direct')))
        self.assertIn('ExitControl{}',_group(self.form('exit_bare')))

    def test_control_family_rejects_operand_drift(self):
        """A non-branch target carrier cannot select the direct branch handler."""
        form=self.form('bra_direct')
        with self.assertRaises(GenerationError): _group(self.operand(form,0,value_kind=ValueKind.REGISTER))


if __name__=='__main__':
    unittest.main()
