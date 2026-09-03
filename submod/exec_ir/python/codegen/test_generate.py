#!/usr/bin/env python3
"""Checks for the packaged-frontend execution topology generator."""

from __future__ import annotations

import importlib.resources
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


MODULE = "ptxsim_exec_ir_codegen"
BACKEND = Path(__file__).parents[1] / "instructions" / "backend.yaml"


class GenerateTests(unittest.TestCase):
    """Exercise the backend-driven public-header emitter."""

    def generate(self, output: Path, backend: Path | None = None,
                 spec_dir: Path | None = None) -> subprocess.CompletedProcess[str]:
        """Run the installed editable generator against one backend file."""
        command = [sys.executable, "-m", MODULE, "--output", str(output)]
        if backend:
            command.extend(["--backend", str(backend)])
        if spec_dir:
            command.extend(["--spec-dir", str(spec_dir)])
        return subprocess.run(command, check=False, text=True, capture_output=True)

    def invalid(self, replacement: str) -> subprocess.CompletedProcess[str]:
        """Run a one-token invalid backend mutation."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            backend = root / "invalid.yaml"
            backend.write_text(BACKEND.read_text().replace(*replacement.split("\n", 1)), encoding="utf-8")
            return self.generate(root / "unused.hpp", backend)

    def test_generates_deterministic_selected_topology(self) -> None:
        """The seven selected opcodes retain fields, constants, and nesting."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first, second = root / "first.hpp", root / "second.hpp"
            self.assertEqual(self.generate(first, BACKEND).returncode, 0)
            self.assertEqual(self.generate(second, BACKEND).returncode, 0)
            self.assertEqual(first.read_bytes(), second.read_bytes())
            header = first.read_text()
            self.assertIn("#include <ptxsim/exec_ir/exec_ir_types.hpp>", header)
            self.assertIn("namespace ptxsim::exec_ir {", header)
            self.assertIn("}  // namespace ptxsim::exec_ir", header)
            for opcode in ("Mov", "Add", "Ld", "St", "Bar", "Bra", "Exit"):
                self.assertIn(f"struct {opcode}", header)
            self.assertIn("struct ScalarOperands", header)
            self.assertIn("using Operands = std::variant<ScalarOperands>", header)
            self.assertIn("MemoryConsistency semantics", header)
            self.assertIn("inline static constexpr bool warp = true", header)
            self.assertIn("inline static constexpr bool sync = true", header)
            self.assertIn("bool uni", header)
            import yaml

            backend = yaml.safe_load(BACKEND.read_text())
            forms = {
                form["variant"]: form["modifier_values"]
                for instruction in backend["instructions"]
                for form in instruction["forms"]
            }
            self.assertEqual(forms["bra_direct"]["uni"], [False, True])
            self.assertEqual(forms["ld_generic_scalar"]["semantics"], ["omitted"])
            self.assertEqual(forms["st_generic_scalar"]["semantics"], ["omitted"])
            self.assertEqual(forms["ld_explicit_scalar"]["semantics"], ["omitted", "weak"])
            self.assertEqual(forms["st_explicit_scalar"]["semantics"], ["omitted", "weak"])

    def test_accepts_an_explicit_frontend_spec_directory(self) -> None:
        """An installed CMake port can select its exported specification tree."""
        with tempfile.TemporaryDirectory() as directory:
            resource = importlib.resources.files("code_gen.resources").joinpath("ptx_spec")
            with importlib.resources.as_file(resource) as spec_dir:
                result = self.generate(Path(directory) / "header.hpp", BACKEND,
                                       spec_dir)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_rejects_invalid_selectors_and_values(self) -> None:
        """Unknown opcode, variant, layout, and modifier values are rejected."""
        for old, new in (("opcode: mov", "opcode: absent"),
                         ("variant: mov_scalar", "variant: mov_absent"),
                         ("layouts: [scalar]", "layouts: [absent]"),
                         ("type: [b32]", "type: [u8]"),
                         ("namespace: ptxsim::exec_ir", "namespace: invalid-name")):
            with self.subTest(new=new):
                result = self.invalid(f"{old}\n{new}")
                self.assertNotEqual(result.returncode, 0)

    def test_backend_type_mapping_changes_output(self) -> None:
        """Changing a backend C++ mapping changes generated storage."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            backend, output = root / "backend.yaml", root / "header.hpp"
            backend.write_text(BACKEND.read_text().replace("cpp_type: DataType", "cpp_type: BackendDataType", 1), encoding="utf-8")
            self.assertEqual(self.generate(output, backend).returncode, 0)
            self.assertIn("BackendDataType type", output.read_text())


if __name__ == "__main__":
    unittest.main()
