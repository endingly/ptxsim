#!/usr/bin/env python3
"""WP0 checks for the packaged-frontend exec-IR proof generator."""

from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


MODULE = "ptxsim_exec_ir_codegen"


class GenerateTests(unittest.TestCase):
    def generate(
        self, output: Path, manifest: Path | None = None
    ) -> subprocess.CompletedProcess[str]:
        command = [sys.executable, "-m", MODULE, "--output", str(output)]
        if manifest:
            command.extend(["--manifest", str(manifest)])
        return subprocess.run(
            command,
            check=False,
            text=True,
            capture_output=True,
        )

    def test_generates_deterministic_proof_header(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first = root / "first.hpp"
            second = root / "second.hpp"
            self.assertEqual(self.generate(first).returncode, 0)
            self.assertEqual(self.generate(second).returncode, 0)
            self.assertEqual(first.read_bytes(), second.read_bytes())
            header = first.read_text()
            self.assertIn("integer_add_selector", header)
            self.assertIn("add/add_integer_no_sat/default/type=u32", header)

    def test_rejects_unknown_opcode_selector(self) -> None:
        invalid_manifest = """schema: ptxsim-exec-ir/v1
ptx_spec_schema: ptx-instr/v1
instructions:
  - opcode: absent
    operation: integer_add
    forms:
      - variant: add_integer_no_sat
        layouts: [default]
        modifier_values:
          type: [u32]
"""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = root / "invalid.yaml"
            manifest.write_text(invalid_manifest, encoding="utf-8")
            output = root / "unused.hpp"
            result = self.generate(output, manifest)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("unknown opcode selector", result.stderr)
            self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
