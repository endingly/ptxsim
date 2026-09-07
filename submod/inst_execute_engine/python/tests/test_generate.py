"""Checks for the projected Add, Sub, and Mul ValueALU emitter."""

from __future__ import annotations

from dataclasses import replace
from pathlib import Path
import unittest

from jinja2 import UndefinedError

from ptxsim_codegen.exec_ir.inputs import load_backend, load_projected
from ptxsim_codegen.inst_execute_engine.gen_engine import _TEMPLATES, _value_alu_forms, artifacts
from ptxsim_codegen.exec_ir.model import GenerationError


class GenerateTests(unittest.TestCase):
    """Exercise model-derived ValueALU preparation generation."""

    def projected(self):
        """Return the pinned frontend records that enable ValueALU generation."""
        return load_projected(load_backend(None), None)

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

    def render_instruction(self, instruction):
        """Render one modified enabled instruction with every other enabled binding."""
        enabled = (self.add(), self.sub(), self.mul())
        return artifacts(
            tuple(
                instruction if candidate.opcode == instruction.opcode else candidate
                for candidate in enabled
            ),
            Path(f"{instruction.opcode}.gen.hpp"),
        )

    def test_emits_every_projected_form_and_dynamic_type(self) -> None:
        """Generated dispatch has one adapter per form and every declared dynamic type."""
        instructions = (self.add(), self.sub(), self.mul())
        _, source = artifacts(instructions, Path("value_alu.gen.hpp"))
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
            + source.count("return prepare_mul_"),
            sum(len(instruction.forms) for instruction in instructions),
        )
        for path in ("rn_f32", "lo_u32", "hi_u32", "wide_u32", "wide_s32"):
            self.assertIn(f"auto prepare_mul_{path}", source)
        self.assertNotIn("unsupported_instruction", source)

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
            artifacts((self.add(), self.sub()), Path("add.gen.hpp"))

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


if __name__ == "__main__":
    unittest.main()
