"""Independent validation tests and a pinned-frontend integration check."""

from __future__ import annotations

from copy import deepcopy
from importlib.resources import as_file, files
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace as Record
import unittest

from .gen_engine import bind, coverage, load_bindings, source
from .model import BackendSpec, CppKindMapping, GenerationError


class EngineBindingTest(unittest.TestCase):
    """Exercise policy validation with deliberately small, independent facts."""

    def setUp(self) -> None:
        """Create a binary form, a layout alternative and an unbound opcode."""
        self.backend = BackendSpec(
            "test", "test", "ptxsim::exec_ir",
            {"type": CppKindMapping("DataType", {
                "u32": "DataType::u32", "b32": "DataType::b32"}),
             "flag": CppKindMapping("bool", {False: "false", True: "true"})},
            {},
        )
        modifier = Record(name="type", kind="type", presence="required",
                          values=(Record(value="u32"), Record(value="b32")),
                          default=None, value=None)
        self.form = Record(
            variant=Record(name="add_integer_no_sat", modifiers=(modifier,)),
            layouts=(Record(name="scalar"), Record(name="packed")),
        )
        self.projected = (
            Record(opcode="add", forms=(self.form,)),
            Record(opcode="exit", forms=(Record(
                variant=Record(name="exit_bare", modifiers=()),
                layouts=(Record(name="default"),)),)),
        )
        self.document = {
            "schema": "ptxsim-engine/v1", "default": "unsupported",
            "bindings": [{
                "id": "add_u32", "opcode": "add", "form": "add_integer_no_sat",
                "layout": "scalar", "modifiers": {"type": ["u32"]},
                "prepare": {"function": "prepare_operation",
                            "arguments": ["registers", "arithmetic", "operation", "successor"]},
                "commit": "scalar",
            }],
        }

    def bindings(self):
        """Validate the current fixture without bypassing the public binder."""
        return bind(self.document, self.backend, self.projected)

    def test_generates_typed_adapter_and_layout_guard(self) -> None:
        """The selector checks layout before any register-frame resolution."""
        generated = source(self.bindings(), self.backend)
        self.assertIn("std::holds_alternative<exec_ir::Add::IntegerNoSat::ScalarOperands>", generated)
        self.assertIn("return prepare_operation(registers->get(), arithmetic, typed, *successor);", generated)
        selector = generated.split("auto select_preparer", 1)[1]
        self.assertNotIn("resolver.resolve", selector)
        self.assertIn("return unsupported_instruction();", selector)

    def test_rejects_unknown_opcode_form_layout_and_modifier(self) -> None:
        """Typos and stale frontend identifiers fail instead of falling back."""
        for key in ("opcode", "form", "layout", "modifiers"):
            with self.subTest(key=key):
                document = deepcopy(self.document)
                document["bindings"][0][key] = {"typo": ["u32"]} if key == "modifiers" else "typo"
                with self.assertRaises(GenerationError):
                    bind(document, self.backend, self.projected)

    def test_requires_explicit_layout_for_alternatives(self) -> None:
        """A multi-layout form cannot implicitly broaden its accepted operands."""
        del self.document["bindings"][0]["layout"]
        with self.assertRaisesRegex(GenerationError, "operand layout"):
            self.bindings()

    def test_rejects_unhandled_dynamic_modifier(self) -> None:
        """Adding a frontend field requires an explicit execution-policy choice."""
        self.form.variant.modifiers += (Record(name="ftz", kind="flag", presence="optional",
                                               values=(), default=False, value=None),)
        with self.assertRaisesRegex(GenerationError, "unhandled dynamic"):
            self.bindings()

    def test_ignored_modifier_requires_rationale(self) -> None:
        """Ignored fields are documented, not silently accepted wildcards."""
        raw = self.document["bindings"][0]
        raw["modifiers"] = {}
        raw["ignored"] = {"type": ""}
        with self.assertRaisesRegex(GenerationError, "rationale"):
            self.bindings()
        raw["ignored"]["type"] = "The test preparer deliberately handles both encodings."
        self.assertEqual(len(self.bindings()), 1)

    def test_rejects_overlap_and_duplicate_ids(self) -> None:
        """Rule order is never used to choose between overlapping semantics."""
        second = deepcopy(self.document["bindings"][0])
        self.document["bindings"].append(second)
        with self.assertRaisesRegex(GenerationError, "duplicate binding"):
            self.bindings()
        second["id"] = "another"
        with self.assertRaisesRegex(GenerationError, "overlapping"):
            self.bindings()
        second["modifiers"]["type"] = ["b32"]
        self.assertEqual(len(self.bindings()), 2)

    def test_rejects_invalid_values_and_duplicate_values(self) -> None:
        """The frontend domain, not the backend's wider enum, bounds selectors."""
        for values in ([], ["u64"], ["u32", "u32"], [False], [1], [None]):
            with self.subTest(values=values):
                self.document["bindings"][0]["modifiers"]["type"] = values
                with self.assertRaises(GenerationError):
                    self.bindings()

    def test_fixed_modifiers_cannot_be_overridden(self) -> None:
        """A fixed frontend value remains fixed even when the enum is wider."""
        modifier = self.form.variant.modifiers[0]
        modifier.presence, modifier.value = "fixed", "u32"
        self.document["bindings"][0]["modifiers"]["type"] = ["b32"]
        with self.assertRaisesRegex(GenerationError, "illegal modifier"):
            self.bindings()

    def test_rejects_embedded_cpp_and_unknown_arguments(self) -> None:
        """Execution policy contains identifiers and a closed adapter vocabulary."""
        for prepare in ({"function": "f(); evil", "arguments": []},
                        {"function": "f", "arguments": ["registers.write(dst)"]},
                        {"function": "f", "arguments": [{"modifier": "typo"}]},
                        {"function": "f", "arguments": [{"kind": "type", "value": "u64"}]}):
            with self.subTest(prepare=prepare):
                self.document["bindings"][0]["prepare"] = prepare
                with self.assertRaises(GenerationError):
                    self.bindings()

    def test_rejects_unknown_schema_keys_and_commit(self) -> None:
        """Misspelled controls never create accidental permissive defaults."""
        for key, value in (("schema", "v0"), ("default", "supported"), ("typo", True)):
            document = deepcopy(self.document)
            document[key] = value
            with self.assertRaises(GenerationError):
                bind(document, self.backend, self.projected)
        self.document["bindings"][0]["commit"] = "transaction"
        with self.assertRaises(GenerationError):
            self.bindings()

    def test_yaml_rejects_duplicate_keys(self) -> None:
        """Duplicate YAML keys are errors even if their values are identical."""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "bindings.yaml"
            path.write_text("schema: a\nschema: a\n", encoding="utf-8")
            with self.assertRaises(GenerationError):
                load_bindings(path)

    def test_output_is_independent_of_binding_order(self) -> None:
        """Stable source and capability reports do not depend on YAML ordering."""
        other = deepcopy(self.document["bindings"][0])
        other["id"], other["modifiers"]["type"] = "add_b32", ["b32"]
        self.document["bindings"].append(other)
        first = self.bindings()
        self.document["bindings"].reverse()
        second = self.bindings()
        self.assertEqual(source(first, self.backend), source(second, self.backend))
        self.assertEqual(coverage(first, self.projected), coverage(second, self.projected))

    def test_coverage_does_not_claim_complete_opcode_support(self) -> None:
        """Unbound layouts and opcodes stay visibly unsupported in the report."""
        report = json.loads(coverage(self.bindings(), self.projected))
        statuses = {(row["opcode"], row["layout"]): row["status"] for row in report["declared"]}
        self.assertEqual(statuses[("add", "scalar")], "conditional")
        self.assertEqual(statuses[("add", "packed")], "unsupported")
        self.assertEqual(statuses[("exit", "default")], "unsupported")


class EngineIntegrationTest(unittest.TestCase):
    """Validate repository bindings against the actual installed frontend model."""

    def test_pinned_frontend_and_cli_outputs(self) -> None:
        """Generation uses real frontend projection and emits tracked build inputs."""
        from .backend import load_yaml
        from .projection import database, project_database

        configured = os.environ.get("PTXSIM_ENGINE_BINDINGS")
        path = Path(configured) if configured else Path(__file__).resolve().parents[3] / "inst_execute_engine/instructions/bindings.yaml"
        resource = files("ptxsim_exec_ir_codegen.instructions").joinpath("backend.yaml")
        with as_file(resource) as backend_path:
            backend = load_yaml(backend_path)
        projected = project_database(database(), backend)
        bindings = bind(load_bindings(path), backend, projected)
        self.assertEqual(len(bindings), 11)
        self.assertEqual({row.instruction.opcode for row in bindings},
                         {"add", "mov", "setp", "ld", "st", "bar", "bra", "exit"})
        with tempfile.TemporaryDirectory(prefix="engine codegen ") as directory:
            output = Path(directory) / "dispatch.inc"
            report = Path(directory) / "coverage.json"
            depfile = Path(directory) / "dispatch.d"
            command = [sys.executable, "-m", "ptxsim_exec_ir_codegen.gen_engine",
                       "--bindings", str(path), "--output", str(output),
                       "--coverage", str(report), "--depfile", str(depfile)]
            subprocess.run(command, check=True, capture_output=True, text=True)
            self.assertEqual(output.read_text(), source(bindings, backend))
            self.assertEqual(report.read_text(), coverage(bindings, projected))
            self.assertIn("bindings.yaml", depfile.read_text())
            self.assertIn("ptx_spec", depfile.read_text())
            self.assertIn(r"engine\ codegen\ ", depfile.read_text())
            previous = output.read_bytes()
            subprocess.run(command, check=True, capture_output=True, text=True)
            self.assertEqual(output.read_bytes(), previous)


if __name__ == "__main__":
    unittest.main()
