"""Verify complete typed cold binding and its installed CLI contract."""

from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from ptxsim_codegen.binding.inherited import artifacts, generate
from ptxsim_codegen.frontend.v080 import snapshot


class GenerateTests(unittest.TestCase):
    """Exercise every exact source class without filtering execution capability."""

    @classmethod
    def setUpClass(cls) -> None:
        """Share the immutable pinned full projection across publication checks."""
        cls.model = snapshot()

    def test_generates_deterministic_complete_lowering_topology(self) -> None:
        """Every form/layout receives a typed binder and an exact RTTI preflight."""
        with tempfile.TemporaryDirectory() as directory:
            first, second = Path(directory) / 'first', Path(directory) / 'second'
            plan = generate(self.model, first)
            self.assertEqual(plan, artifacts(self.model))
            self.assertEqual(generate(self.model, second), plan)
            for path in (*plan.headers, *plan.sources):
                self.assertEqual((first / path).read_bytes(), (second / path).read_bytes())
            self.assertEqual(json.loads((first / 'artifact_plan.json').read_text()),
                             {'headers': list(plan.headers), 'sources': list(plan.sources)})
            dispatch = (first / 'binding_dispatch.gen.cpp').read_text()
            sources = '\n'.join((first / path).read_text() for path in plan.sources)
            self.assertEqual(sources.count('dynamic_cast<const '), 5453 * 2)
            for form in self.model.forms:
                self.assertIn('return matches_' + form.cpp_name + '(*source)', dispatch)
                self.assertIn('return lower_' + form.cpp_name + '(source, context)', dispatch)
            self.assertIn('if (!source) return false;', dispatch)
            self.assertIn('default: return malformed(context)', dispatch)
            self.assertNotIn('unsupported_instruction', sources)
            self.assertNotIn('unsupported_operand', sources)
            self.assertNotIn('_SUPPORTED_IDENTITIES', sources)
            self.assertIn('address_qualifier', sources)
            self.assertIn('std::make_unique<exec_ir::MovScalar>', sources)
            self.assertNotIn('std::monostate', sources)

    def test_rejects_missing_output(self) -> None:
        """A complete bounded output directory is required, including in plan mode."""
        result = subprocess.run([sys.executable, '-m', 'ptxsim_codegen.code_gen.lowering'],
                                text=True, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('--output-dir', result.stderr)


if __name__ == '__main__':
    unittest.main()
