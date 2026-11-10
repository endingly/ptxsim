"""Verify complete owned artifacts and the installed generation CLI."""

from __future__ import annotations

from dataclasses import replace
import importlib.resources
import json
import re
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import yaml
from ptx_frontend.spec.resources import packaged_spec_dir
from ptxsim_codegen.code_gen.exec_ir.inherited import artifact_plan, generate, domain_header
from ptxsim_codegen.frontend.v080 import snapshot
from ptxsim_codegen.model import GenerationError


class GenerateTests(unittest.TestCase):
    """Exercise the complete hierarchy, sealed construction and deterministic publication."""

    @classmethod
    def setUpClass(cls) -> None:
        """Reuse the exact pinned immutable projection across independent artifact assertions."""
        cls.model = snapshot()

    def test_generates_deterministic_full_topology(self) -> None:
        """Every opcode/form/layout is emitted, including all cold-only semantics."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first, second = root / 'first', root / 'second'
            plan = generate(self.model, first)
            self.assertEqual(generate(self.model, second), plan)
            self.assertEqual(plan, artifact_plan(self.model))
            self.assertEqual(len(self.model.instructions), 137)
            self.assertEqual(len(self.model.forms), 5453)
            self.assertEqual(self.model.layout_count, 7457)
            for relative in plan.headers:
                path = Path('public/ptxsim/exec_ir') / relative
                self.assertEqual((first / path).read_bytes(), (second / path).read_bytes())
            for relative in plan.sources:
                self.assertEqual((first / relative).read_bytes(), (second / relative).read_bytes())
            self.assertEqual(json.loads((first / 'artifact_plan.json').read_text()),
                             {'headers': list(plan.headers), 'sources': list(plan.sources)})
            headers = '\n'.join((first / 'public/ptxsim/exec_ir' / path).read_text() for path in plan.headers)
            self.assertEqual(headers.count(' final : public Instruction'), 5453)
            form_headers = '\n'.join((first / 'public/ptxsim/exec_ir' / path).read_text() for path in plan.headers if '_forms_' in path)
            self.assertEqual(len(re.findall(r'struct \w+Operands \{', form_headers)), 7457)
            self.assertNotIn('std::monostate', headers)
            self.assertNotIn('ptx_frontend::', headers)
            self.assertIn('concept GeneratedForm', headers)
            self.assertIn('private:', headers)
            self.assertIn('const Kind kind_;', headers)
            self.assertIn('const Opcode opcode_;', headers)
            dispatch = (first / 'ir_dispatch.gen.cpp').read_text()
            for form in self.model.forms:
                self.assertIn('&freeze_' + form.cpp_name, dispatch)
                self.assertIn('&validate_' + form.cpp_name, dispatch)
                self.assertIn('&describe_' + form.cpp_name, dispatch)

    def test_accepts_an_explicit_frontend_spec_directory(self) -> None:
        """The installed CLI can select the pinned packaged specification tree."""
        with tempfile.TemporaryDirectory() as directory, importlib.resources.as_file(packaged_spec_dir()) as spec:
            root = Path(directory)
            result = subprocess.run([sys.executable, '-m', 'ptxsim_codegen.code_gen.exec_ir',
                '--output-dir', str(root / 'out'), '--spec-dir', str(spec),
                '--cmake-plan-output', str(root / 'plan.cmake')], text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            text = (root / 'plan.cmake').read_text()
            self.assertIn('PTXSIM_EXEC_IR_GENERATED_HEADERS', text)
            self.assertIn('model/arithmetic/add.gen.hpp', text)
            self.assertIn('ir_dispatch.gen.cpp', text)

    def test_rejects_invalid_kind_mappings_and_schema(self) -> None:
        """Schema errors and omitted/duplicate closed vocabularies fail before emission."""
        resource = importlib.resources.files('ptxsim_codegen.resources').joinpath('backend.yaml')
        for mutation in ('schema', 'spec', 'leaf', 'domain'):
            raw = yaml.safe_load(resource.read_text())
            if mutation == 'schema': raw['schema'] = 'absent'
            if mutation == 'spec': raw['ptx_spec_schema'] = 'absent'
            if mutation == 'leaf': raw['leaves'].pop(next(iter(raw['leaves'])))
            if mutation == 'domain': raw['domains']['ScalarType']['values'].pop()
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory() as directory:
                backend = Path(directory) / 'backend.yaml'
                backend.write_text(yaml.safe_dump(raw))
                with self.assertRaises(GenerationError): snapshot(backend_path=backend)

    def test_backend_type_mapping_changes_output(self) -> None:
        """Owned domain names and atoms come from explicit backend representation mappings."""
        domain = next(item for item in self.model.domains if item.cpp_type == 'DataType')
        modified = replace(domain, cpp_type='BackendDataType', values=tuple(
            replace(atom, cpp=atom.cpp.replace('DataType::', 'BackendDataType::')) for atom in domain.values))
        model = replace(self.model, domains=tuple(modified if item is domain else item for item in self.model.domains))
        header = domain_header(model)
        self.assertIn('enum class BackendDataType', header)
        self.assertNotIn('enum class DataType', header)
        self.assertIn('u32', header)

    def test_rejects_missing_output(self) -> None:
        """Directory publication is mandatory for the complete bounded artifact set."""
        result = subprocess.run([sys.executable, '-m', 'ptxsim_codegen.code_gen.exec_ir'],
                                text=True, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('--output-dir', result.stderr)


if __name__ == '__main__':
    unittest.main()
