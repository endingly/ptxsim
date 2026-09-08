"""Checks for projected ValueALU and predicate-comparison emitters."""

from __future__ import annotations

from copy import deepcopy
from dataclasses import replace
from pathlib import Path
import unittest

from jinja2 import UndefinedError

from ptxsim_codegen.exec_ir.inputs import load_backend, load_projected
from ptxsim_codegen.inst_execute_engine.gen_engine import _TEMPLATES, _value_alu_forms, artifacts
from ptxsim_codegen.inst_execute_engine.memory_family import memory_operation
from ptxsim_codegen.inst_execute_engine.setp_family import setp_operation
from ptxsim_codegen.inst_execute_engine.bar_family import bar_operation, bra_operation, exit_operation
from ptxsim_codegen.exec_ir.model import GenerationError


class GenerateTests(unittest.TestCase):
    """Exercise model-derived ValueALU preparation generation."""

    @classmethod
    def setUpClass(cls) -> None:
        """Load the pinned specification once for this suite's fixture."""
        cls._projected_template = load_projected(load_backend(None), None)

    def setUp(self) -> None:
        """Isolate nested mutable frontend fields from other test cases."""
        self._projected = deepcopy(self._projected_template)

    def projected(self):
        """Return this test's owned copy of the pinned frontend records."""
        return self._projected

    def add(self):
        """Return the complete projected Add instruction from pinned frontend input."""
        return next(
            instruction
            for instruction in self.projected()
            if instruction.opcode == "add"
        )

    def sub(self):
        """Return the complete projected Sub instruction from pinned frontend input."""
        return next(
            instruction
            for instruction in self.projected()
            if instruction.opcode == "sub"
        )

    def mul(self):
        """Return the complete projected Mul instruction from pinned frontend input."""
        return next(
            instruction
            for instruction in self.projected()
            if instruction.opcode == "mul"
        )

    def fma(self):
        """Return the complete projected FMA instruction from pinned frontend input."""
        return next(
            instruction
            for instruction in self.projected()
            if instruction.opcode == "fma"
        )

    def setp(self):
        """Return the complete projected Setp instruction from pinned frontend input."""
        return next(
            instruction
            for instruction in self.projected()
            if instruction.opcode == "setp"
        )

    def ld(self):
        """Return the complete projected Load instruction from pinned frontend input."""
        return next(instruction for instruction in self.projected() if instruction.opcode == "ld")

    def st(self):
        """Return the complete projected Store instruction from pinned frontend input."""
        return next(instruction for instruction in self.projected() if instruction.opcode == "st")

    def bar(self):
        """Return the complete projected Bar instruction from pinned frontend input."""
        return next(instruction for instruction in self.projected() if instruction.opcode == "bar")

    def bra(self):
        """Return the complete projected Bra instruction from pinned frontend input."""
        return next(instruction for instruction in self.projected() if instruction.opcode == "bra")

    def exit(self):
        """Return the complete projected Exit instruction from pinned frontend input."""
        return next(instruction for instruction in self.projected() if instruction.opcode == "exit")

    def all_operations(self):
        """Return every execution family currently emitted by this generator."""
        return (self.add(), self.sub(), self.mul(), self.fma(), self.setp(), self.ld(), self.st(),
                self.bar(), self.bra(), self.exit())

    def render_instruction(self, instruction):
        """Render one modified enabled instruction with every other enabled binding."""
        enabled = self.all_operations()
        return artifacts(
            tuple(
                instruction if candidate.opcode == instruction.opcode else candidate
                for candidate in enabled
            ),
            Path(f"{instruction.opcode}.gen.hpp"),
        )

    def test_emits_every_projected_form_and_dynamic_type(self) -> None:
        """Generated dispatch has one adapter per form and every declared dynamic type."""
        instructions = (self.add(), self.sub(), self.mul(), self.fma(), self.setp())
        _, source = artifacts(self.all_operations(), Path("value_alu.gen.hpp"))
        for instruction in instructions:
            self.assertIn(f'#include "semantics/{instruction.opcode}_semantics.hpp"', source)
        expected_cases = sum(
            len(modifier.values)
            for instruction in instructions
            for form in instruction.forms
            for modifier in form.variant.modifiers
            if modifier.name in {"type", "input_type"}
            and modifier.presence != "fixed"
        )
        self.assertEqual(source.count("case exec_ir::DataType::"), expected_cases)
        self.assertEqual(
            source.count("return prepare_add_") + source.count("return prepare_sub_")
            + source.count("return prepare_mul_") + source.count("return prepare_fma_")
            + source.count("return prepare_setp_"),
            sum(len(instruction.forms) for instruction in instructions),
        )
        for path in ("rn_f32", "lo_u32", "hi_u32", "wide_u32", "wide_s32"):
            self.assertIn(f"auto prepare_mul_{path}", source)
        self.assertNotIn("unsupported_instruction", source)

    def test_fma_inventory_controls_and_three_read_preparation_are_complete(self) -> None:
        """FMA emits every pinned form, selector, and source read without an implicit fallback."""
        fma = self.fma()
        expected_forms = (
            "RnF32", "DirectedF32", "RnF64", "DirectedF64", "F32x2",
            "RnF16", "RnF16x2", "HalfRelu", "HalfOob", "HalfOobRelu",
            "Bf16", "Bf16x2", "Bf16Oob", "Bf16x2Oob", "MixedF32F16",
            "MixedF32Bf16",
        )
        self.assertEqual(tuple(form.source.cpp_name for form in fma.forms), expected_forms)
        dynamic_type_selector_count = sum(
            len(modifier.values)
            for form in fma.forms
            for modifier in form.variant.modifiers
            if modifier.presence == "required" and modifier.kind == "type"
        )
        optional_flag_count = sum(
            1
            for form in fma.forms
            for modifier in form.variant.modifiers
            if modifier.presence == "optional" and modifier.kind == "flag"
        )
        fixed_or_required_cardinality = sum(
            __import__("math").prod(
                len(modifier.values) if modifier.presence == "required" else 2
                for modifier in form.variant.modifiers
                if modifier.presence in {"required", "optional"}
                and (modifier.presence == "required" or modifier.kind == "flag")
            )
            for form in fma.forms
        )
        self.assertEqual(fixed_or_required_cardinality, 70)

        _, source = self.render_instruction(fma)
        fma_begin = source.index("auto prepare_fma_")
        fma_end = source.index("auto prepare_setp_")
        fma_source = source[fma_begin:fma_end]
        emitted_type_paths = sum(
            len(form.dynamic_type_selector.cases)
            if form.dynamic_type_selector is not None else 1
            for form in _value_alu_forms(fma)
        )
        self.assertIn('#include "semantics/fma_semantics.hpp"', source)
        self.assertEqual(source.count("return prepare_fma_"), len(expected_forms))
        self.assertEqual(fma_source.count("case exec_ir::DataType::"), dynamic_type_selector_count)
        self.assertEqual(optional_flag_count, 17)
        self.assertEqual(fma_source.count("read_value<"), 3 * emitted_type_paths)
        self.assertEqual(
            fma_source.count("semantics::fma(arithmetic, form, *src1, *src2, *src3)"),
            emitted_type_paths,
        )

    def test_template_rejects_missing_render_data(self) -> None:
        """Templates fail explicitly instead of silently eliding missing model data."""
        with self.assertRaises(UndefinedError):
            _TEMPLATES.get_template("instruction_preparation.cpp.j2").render(
                header_name="add.gen.hpp"
            )

    def test_value_alu_family_accepts_operation_and_semantic_parameters(self) -> None:
        """The family template does not hard-code the operation-specific C++ names."""
        form = _value_alu_forms(self.add())[0]
        family = _TEMPLATES.get_template("families/value_alu.cpp.j2").module
        source = family.adapter(form, "Dummy", "dummy")
        self.assertIn("exec_ir::Dummy::FloatF32", source)
        self.assertIn("semantics::dummy", source)

    def test_rendering_is_deterministic(self) -> None:
        """Repeated rendering of one projected input produces byte-identical text."""
        instruction = self.add()
        self.assertEqual(
            self.render_instruction(instruction),
            self.render_instruction(instruction),
        )

    def test_derives_mixed_source_and_result_type_paths(self) -> None:
        """Mixed forms keep their source and destination C++ types distinct."""
        instruction = self.add()
        mixed = next(form for form in instruction.forms if form.source.cpp_name == "MixedF32")
        _, source = self.render_instruction(replace(instruction, forms=(mixed,)))
        self.assertIn("read_value<arith::float16_t>(registers->get(), form.src)", source)
        self.assertIn("read_value<arith::float32_t>(registers->get(), form.addend)", source)
        self.assertIn("resolve_destination<arith::float32_t>(registers->get(), form.dst)", source)

    def test_derives_operand_paths_from_the_resolved_layout(self) -> None:
        """Changing projected field spellings changes emitted reads and destination checks."""
        instruction = self.add()
        form = instruction.forms[0]
        layout = form.source.operand_layouts[0]
        renamed_fields = tuple(
            replace(field, name={"dst": "out", "src1": "left", "src2": "right"}[field.name])
            for field in layout.fields
        )
        renamed_bindings = tuple(
            replace(binding, target_field_id={"dst": "out", "src1": "left", "src2": "right"}[binding.target_field_id])
            for binding in layout.bindings
        )
        renamed_layout = replace(layout, fields=renamed_fields, bindings=renamed_bindings)
        renamed_source = replace(form.source, operand_layouts=(renamed_layout,))
        renamed_form = replace(form, source=renamed_source)
        _, source = self.render_instruction(replace(instruction, forms=(renamed_form, *instruction.forms[1:])))
        self.assertIn("form.left", source)
        self.assertIn("form.right", source)
        self.assertIn("form.out", source)

    def test_derives_fixed_type_from_projected_modifier_field(self) -> None:
        """A changed projected static type changes the emitted codec without a form table."""
        instruction = self.add()
        form = instruction.forms[0]
        modifiers = tuple(
            replace(field, constant_value="f64") if field.name == "type" else field
            for field in form.source.modifier_fields
        )
        changed = replace(form, source=replace(form.source, modifier_fields=modifiers))
        _, source = self.render_instruction(replace(instruction, forms=(changed, *instruction.forms[1:])))
        first_adapter = source.partition("auto prepare_add_float_f32")[2].partition("auto prepare_add_float_f32x2")[0]
        self.assertIn("read_value<arith::float64_t>", first_adapter)

    def test_matches_bindings_by_target_field_not_tuple_order(self) -> None:
        """Resolved binding order is irrelevant when its target identities agree."""
        instruction = self.add()
        form = instruction.forms[0]
        layout = form.source.operand_layouts[0]
        reordered = replace(layout, bindings=tuple(reversed(layout.bindings)))
        changed = replace(form, source=replace(form.source, operand_layouts=(reordered,)))
        _, source = self.render_instruction(replace(instruction, forms=(changed, *instruction.forms[1:])))
        self.assertIn("form.src1", source)
        self.assertIn("form.dst", source)

    def test_rejects_projected_value_alu_shape_drift(self) -> None:
        """A projected third source is rejected instead of silently being ignored."""
        instruction = self.add()
        form = instruction.forms[0]
        layout = form.source.operand_layouts[0]
        third_source = replace(layout.fields[0], name="extra", operand_access=layout.fields[1].operand_access)
        third_binding = replace(layout.bindings[0], target_field_id="extra")
        drifted_layout = replace(
            layout,
            fields=(*layout.fields, third_source),
            bindings=(*layout.bindings, third_binding),
        )
        drifted_form = replace(form, source=replace(form.source, operand_layouts=(drifted_layout,)))
        with self.assertRaises(GenerationError):
            self.render_instruction(replace(instruction, forms=(drifted_form, *instruction.forms[1:])))

    def test_rejects_read_write_operand_access(self) -> None:
        """A read-write field cannot be silently omitted from the ValueALU contract."""
        instruction = self.add()
        form = instruction.forms[0]
        layout = form.source.operand_layouts[0]
        changed_fields = (
            replace(layout.fields[0], operand_access=type(layout.fields[0].operand_access).READ_WRITE),
            *layout.fields[1:],
        )
        changed_layout = replace(layout, fields=changed_fields)
        changed_form = replace(form, source=replace(form.source, operand_layouts=(changed_layout,)))
        with self.assertRaises(GenerationError):
            self.render_instruction(replace(instruction, forms=(changed_form, *instruction.forms[1:])))

    def test_rejects_unsupported_value_alu_width_policy(self) -> None:
        """Only exact and same-width policies can enter this fixed ValueALU family."""
        instruction = self.add()
        form = instruction.forms[0]
        layout = form.source.operand_layouts[0]
        changed_binding = replace(layout.bindings[0], register_width_policy=type(layout.bindings[0].register_width_policy).EQUAL_OR_WIDER)
        changed_layout = replace(layout, bindings=(changed_binding, *layout.bindings[1:]))
        changed_form = replace(form, source=replace(form.source, operand_layouts=(changed_layout,)))
        with self.assertRaises(GenerationError):
            self.render_instruction(replace(instruction, forms=(changed_form, *instruction.forms[1:])))

    def test_rejects_a_new_unadapted_control(self) -> None:
        """A projected control cannot be ignored until the semantic adapter owns it."""
        instruction = self.add()
        form = instruction.forms[0]
        changed_variant = replace(
            form.variant,
            modifiers=(replace(form.variant.modifiers[0], name="unadapted"), *form.variant.modifiers[1:]),
        )
        with self.assertRaises(GenerationError):
            self.render_instruction(replace(instruction, forms=(replace(form, variant=changed_variant), *instruction.forms[1:])))

    def test_requires_every_enabled_operation(self) -> None:
        """Omitting an enabled operation fails rather than silently skipping it."""
        with self.assertRaisesRegex(GenerationError, "no mul instruction"):
            artifacts((self.add(), self.sub(), self.setp(), self.ld(), self.st()), Path("add.gen.hpp"))

    def test_setp_derives_every_projected_form_and_predicate_topology(self) -> None:
        """The predicate family derives pair and combine handling from operand projection."""
        family = setp_operation(self.setp())
        self.assertEqual(len(family.forms), 5)
        pair = next(form for form in family.forms if form.pair_destination)
        self.assertEqual(pair.destination.name, "dst")
        self.assertFalse(pair.destination.allows_negation)
        unsigned_combine = next(form for form in family.forms if form.cpp_name == "LtAndU32")
        signed_pair = next(form for form in family.forms if form.cpp_name == "LtAndS32Pair")
        self.assertTrue(unsigned_combine.combine.allows_negation)
        self.assertFalse(signed_pair.combine.allows_negation)
        _, source = artifacts(
            self.all_operations(), Path("setp.gen.hpp")
        )
        self.assertIn("exec_ir::BooleanOperator::and_", source)

    def test_setp_rejects_predicate_shape_drift(self) -> None:
        """A predicate-pair field cannot be silently treated as one destination."""
        instruction = self.setp()
        form = next(form for form in instruction.forms if form.source.cpp_name == "EqU32Pair")
        layout = form.source.operand_layouts[0]
        changed_field = replace(
            layout.fields[0],
            allowed_operand_shapes=(type(layout.fields[0].allowed_operand_shapes[0]).PREDICATE,),
        )
        changed_form = replace(
            form,
            source=replace(form.source, operand_layouts=(replace(layout, fields=(changed_field, *layout.fields[1:])),)),
        )
        with self.assertRaises(GenerationError):
            setp_operation(replace(instruction, forms=(changed_form, *instruction.forms[1:])))

    def test_derives_sub_mixed_operand_names_from_its_layout(self) -> None:
        """Sub mixed forms retain their distinct source and subtrahend field names."""
        sub = self.sub()
        mixed = next(form for form in sub.forms if form.source.cpp_name == "MixedF32")
        _, source = self.render_instruction(replace(sub, forms=(mixed,)))
        self.assertIn("form.src", source)
        self.assertIn("form.subtrahend", source)

    def test_derives_wide_operand_types_independently(self) -> None:
        """Wide Mul result and sources follow their individual projected expressions."""
        mul = self.mul()
        wide_unsigned = next(form for form in mul.forms if form.source.cpp_name == "WideU32")
        wide_signed = next(form for form in mul.forms if form.source.cpp_name == "WideS32")
        _, source = self.render_instruction(replace(mul, forms=(wide_unsigned, wide_signed)))
        self.assertIn("read_value<std::uint32_t>(registers->get(), form.src1)", source)
        self.assertIn("resolve_destination<std::uint64_t>(registers->get(), form.dst)", source)
        self.assertIn("read_value<std::int32_t>(registers->get(), form.src1)", source)
        self.assertIn("resolve_destination<std::int64_t>(registers->get(), form.dst)", source)

    def test_rejects_unresolved_operand_type_expressions(self) -> None:
        """A new projected type transformation cannot silently select a codec."""
        mul = self.mul()
        form = next(form for form in mul.forms if form.source.cpp_name == "WideU32")
        layout = form.source.operand_layouts[0]
        changed_binding = replace(
            layout.bindings[0],
            type_expression=replace(
                layout.bindings[0].type_expression,
                kind=type(layout.bindings[0].type_expression.kind).NONE,
                scalar_type=None,
            ),
        )
        changed = replace(
            form,
            source=replace(
                form.source,
                operand_layouts=(replace(layout, bindings=(changed_binding, *layout.bindings[1:])),),
            ),
        )
        with self.assertRaises(GenerationError):
            self.render_instruction(replace(mul, forms=(changed, *mul.forms[1:])))

    def test_memory_derives_ordinary_forms_and_rejects_hint_forms(self) -> None:
        """Only ordinary frontend forms enter memory preparation; hint forms stay unrouted."""
        load = memory_operation(self.ld())
        store = memory_operation(self.st())
        self.assertEqual(tuple(form.cpp_name for form in load.forms),
                         ("GenericScalar", "ExplicitScalar", "GenericVector", "ExplicitVector"))
        self.assertEqual(tuple(form.cpp_name for form in store.forms),
                         ("GenericScalar", "ExplicitScalar", "GenericVector", "ExplicitVector"))
        _, source = artifacts(self.all_operations(), Path("memory.gen.hpp"))
        self.assertIn("auto prepare_ld_generic_vector", source)
        self.assertIn("auto prepare_st_explicit_vector", source)
        self.assertIn("return prepare_memory_load(", source)
        self.assertIn("return prepare_memory_store(", source)
        self.assertNotIn("prepare_ld_global_u32_l1_evict", source)

    def test_memory_derives_renamed_operand_fields(self) -> None:
        """Memory adapters follow projected operand identities rather than dst/src spellings."""
        load = self.ld()
        form = next(form for form in load.forms if form.source.cpp_name == "GenericScalar")
        layout = form.source.operand_layouts[0]
        renamed = {layout.fields[0].name: "result", layout.fields[1].name: "location"}
        changed_layout = replace(
            layout,
            fields=tuple(replace(field, name=renamed[field.name]) for field in layout.fields),
            bindings=tuple(replace(binding, target_field_id=renamed[binding.target_field_id])
                           for binding in layout.bindings),
        )
        changed = replace(
            form,
            source=replace(
                form.source,
                operand_layouts=(changed_layout,),
                memory_consistency=replace(
                    form.source.memory_consistency, address_field_id="location"
                ),
                address_alignments=tuple(
                    replace(alignment, address_field_ids=("location",))
                    for alignment in form.source.address_alignments
                ),
            ),
        )
        _, source = self.render_instruction(replace(load, forms=(changed, *load.forms[1:])))
        self.assertIn("form.result", source)
        self.assertIn("form.location", source)

    def test_memory_rejects_type_allowlist_drift(self) -> None:
        """A frontend type addition cannot silently acquire byte-width semantics."""
        load = self.ld()
        form = next(form for form in load.forms if form.source.cpp_name == "GenericScalar")
        changed_variant = replace(
            form.variant,
            modifiers=tuple(
                replace(modifier, values=(*modifier.values, replace(modifier.values[0], value="b128")))
                if modifier.name == "type" else modifier
                for modifier in form.variant.modifiers
            ),
        )
        with self.assertRaises(GenerationError):
            self.render_instruction(replace(load, forms=(replace(form, variant=changed_variant), *load.forms[1:])))

    def test_bar_derives_every_projected_form_and_reduction_token(self) -> None:
        """Every projected Bar form reaches the helper using modifier-token protocols."""
        family = bar_operation(self.bar())
        self.assertEqual(len(family.forms), 11)
        self.assertEqual(sum(form.warp_sync for form in family.forms), 1)
        protocols = {form.protocol for form in family.forms if not form.warp_sync}
        self.assertEqual(protocols, {"SyncArrive", "ReducePopc", "ReduceAnd", "ReduceOr"})
        _, source = artifacts(self.all_operations(), Path("bar.gen.hpp"))
        self.assertEqual(source.count("return prepare_bar_") - 1, 11)
        self.assertIn("CtaBarrierProtocol::ReducePopc", source)
        self.assertIn("CtaBarrierProtocol::ReduceAnd", source)
        self.assertIn("CtaBarrierProtocol::ReduceOr", source)
        self.assertIn("prepare_bar_warp_sync(resolver, form.membermask", source)
        self.assertIn("operands.dst.source", source)

    def test_bar_derives_renamed_operand_fields(self) -> None:
        """Collective adapters follow layout identities rather than Bar field spellings."""
        instruction = self.bar()
        form = next(form for form in instruction.forms if form.source.cpp_name == "RedPopcU32")
        source_layout = form.source.operand_layouts[0]
        renamed = {"dst": "result", "barrier": "slot", "predicate": "input"}
        changed_layout = replace(
            source_layout,
            fields=tuple(replace(field, name=renamed[field.name], source_name=renamed[field.source_name])
                         for field in source_layout.fields),
            bindings=tuple(replace(binding, target_field_id=renamed[binding.target_field_id])
                           for binding in source_layout.bindings),
        )
        changed_source_layout = replace(
            form.layouts[0],
            operands=tuple(replace(operand, name=renamed[operand.name])
                           for operand in form.layouts[0].operands),
        )
        changed = replace(
            form,
            layouts=(changed_source_layout, *form.layouts[1:]),
            source=replace(form.source, operand_layouts=(changed_layout, *form.source.operand_layouts[1:])),
        )
        _, source = self.render_instruction(replace(instruction, forms=(changed, *instruction.forms[1:])))
        self.assertIn("operands.slot", source)
        self.assertIn("operands.input", source)
        self.assertIn("operands.result", source)

    def test_bar_rejects_reduction_token_drift(self) -> None:
        """An unrecognized fixed reduction modifier cannot silently acquire a protocol."""
        instruction = self.bar()
        form = next(form for form in instruction.forms if form.source.cpp_name == "RedPopcU32")
        changed_variant = replace(
            form.variant,
            modifiers=tuple(
                replace(modifier, token=".xor") if modifier.name == "reduction" else modifier
                for modifier in form.variant.modifiers
            ),
        )
        with self.assertRaises(GenerationError):
            self.render_instruction(replace(instruction, forms=(replace(form, variant=changed_variant), *instruction.forms[1:])))

    def test_bar_rejects_predicate_destination_shape_drift(self) -> None:
        """Predicate reductions cannot treat an ordinary register as a predicate result."""
        instruction = self.bar()
        form = next(form for form in instruction.forms if form.source.cpp_name == "RedAndPred")
        layout = form.source.operand_layouts[0]
        changed_fields = (
            replace(layout.fields[0], allowed_operand_shapes=(type(layout.fields[0].allowed_operand_shapes[0]).REGISTER,)),
            *layout.fields[1:],
        )
        changed = replace(form, source=replace(form.source, operand_layouts=(replace(layout, fields=changed_fields), *form.source.operand_layouts[1:])))
        with self.assertRaises(GenerationError):
            self.render_instruction(replace(instruction, forms=(changed, *instruction.forms[1:])))

    def test_control_families_derive_direct_target_and_bare_exit(self) -> None:
        """Branch and exit control effects come from their projected operand topology."""
        branch = bra_operation(self.bra())
        exit_form = exit_operation(self.exit())
        self.assertEqual(branch.target, "target")
        self.assertIsNone(exit_form.target)
        _, source = artifacts(self.all_operations(), Path("control.gen.hpp"))
        self.assertIn("auto validate_bra", source)
        self.assertIn(".control = form.target", source)
        self.assertIn("auto validate_exit", source)
        self.assertIn(".control = ExitControl{}", source)

    def test_control_family_rejects_operand_drift(self) -> None:
        """A direct branch cannot silently accept a non-control target operand."""
        instruction = self.bra()
        form = instruction.forms[0]
        layout = form.source.operand_layouts[0]
        changed = replace(
            form,
            source=replace(
                form.source,
                operand_layouts=(replace(layout, fields=(replace(layout.fields[0], operand_access=type(layout.fields[0].operand_access).READ),)),),
            ),
        )
        with self.assertRaises(GenerationError):
            self.render_instruction(replace(instruction, forms=(changed,)))


if __name__ == "__main__":
    unittest.main()
