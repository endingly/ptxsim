#!/usr/bin/env python3
"""Checks for the packaged execution-IR lowering generator."""

from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


MODULE = "ptxsim_codegen.exec_ir_lowering"


class GenerateTests(unittest.TestCase):
    """Exercise complete resolved-IR lowering generation through its CLI."""

    def generate(
        self,
        output: Path | None = None,
        source_output: Path | None = None,
    ) -> subprocess.CompletedProcess[str]:
        """Run the installed lowering generator with the requested artifacts."""
        command = [sys.executable, "-m", MODULE]
        if output:
            command.extend(["--output", str(output)])
        if source_output:
            command.extend(["--source-output", str(source_output)])
        return subprocess.run(command, check=False, text=True, capture_output=True)

    def test_generates_deterministic_complete_lowering_topology(self) -> None:
        """Every frontend opcode, form, and layout owns generated lowering."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first_header = root / "first_lowering.hpp"
            first_source = root / "first_lowering.cpp"
            second_header = root / "second_lowering.hpp"
            second_source = root / "second_lowering.cpp"
            self.assertEqual(self.generate(first_header, first_source).returncode, 0)
            self.assertEqual(self.generate(second_header, second_source).returncode, 0)
            self.assertEqual(first_header.read_bytes(), second_header.read_bytes())
            self.assertEqual(first_source.read_bytes(), second_source.read_bytes())
            header = first_header.read_text()
            source = first_source.read_text()
            self.assertEqual(header.count("[[nodiscard]] auto lower_"), 70)
            for opcode in ("mov", "add", "sub", "bra", "ld", "st", "bar", "exit"):
                self.assertIn(f"auto lower_{opcode}(", header)
                self.assertIn(f"return lower_{opcode}(", source)
            self.assertEqual(source.count("auto lower_"), 70)
            self.assertIn(
                ".operands = ptxsim::exec_ir::Mov::Scalar::Operands{", source
            )
            self.assertIn(".execution_predicate = std::move(*predicate),", source)
            self.assertIn("LoweringErrorCode::unsupported_instruction", source)
            self.assertIn("lower_sub", header)
            self.assertNotIn("_SUPPORTED_IDENTITIES", source)

    def test_rejects_missing_output(self) -> None:
        """Lowering generation requires both generated artifacts."""
        with tempfile.TemporaryDirectory() as directory:
            partial = self.generate(Path(directory) / "partial.hpp")
        self.assertNotEqual(partial.returncode, 0)
        self.assertIn("--output and --source-output are required", partial.stderr)


if __name__ == "__main__":
    unittest.main()
