#!/usr/bin/env python3
"""Checks for the packaged-frontend execution topology generator."""

from __future__ import annotations

import importlib.resources
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from ptx_frontend.spec.resources import packaged_spec_dir


MODULE = "ptxsim_exec_ir_codegen"
BACKEND = Path(__file__).parents[1] / "instructions" / "backend.yaml"


class GenerateTests(unittest.TestCase):
    """Exercise the backend-driven declaration and diagnostic emitters."""

    def generate(
        self,
        output: Path | None = None,
        source_output: Path | None = None,
        backend: Path | None = None,
        spec_dir: Path | None = None,
        lowering_header_output: Path | None = None,
        lowering_source_output: Path | None = None,
    ) -> subprocess.CompletedProcess[str]:
        """Run the installed editable generator against one backend file."""
        command = [sys.executable, "-m", MODULE]
        if output:
            command.extend(["--output", str(output)])
        if source_output:
            command.extend(["--source-output", str(source_output)])
        if backend:
            command.extend(["--backend", str(backend)])
        if spec_dir:
            command.extend(["--spec-dir", str(spec_dir)])
        if lowering_header_output:
            command.extend(
                ["--lowering-header-output", str(lowering_header_output)]
            )
        if lowering_source_output:
            command.extend(
                ["--lowering-source-output", str(lowering_source_output)]
            )
        return subprocess.run(command, check=False, text=True, capture_output=True)

    def invalid(self, replacement: str) -> subprocess.CompletedProcess[str]:
        """Run a one-token invalid backend mutation."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            backend = root / "invalid.yaml"
            backend.write_text(
                BACKEND.read_text().replace(*replacement.split("\n", 1)),
                encoding="utf-8",
            )
            return self.generate(root / "unused.hpp", backend=backend)

    def test_generates_deterministic_full_topology(self) -> None:
        """Every packaged frontend opcode, form, and layout is emitted."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first, second = root / "first.hpp", root / "second.hpp"
            first_source, second_source = root / "first.cpp", root / "second.cpp"
            self.assertEqual(
                self.generate(first, first_source, BACKEND).returncode, 0
            )
            self.assertEqual(
                self.generate(second, second_source, BACKEND).returncode, 0
            )
            self.assertEqual(first.read_bytes(), second.read_bytes())
            self.assertEqual(first_source.read_bytes(), second_source.read_bytes())
            header = first.read_text()
            source = first_source.read_text()
            self.assertIn("#include <ptxsim/exec_ir/exec_ir_types.hpp>", header)
            self.assertNotIn("fmt", header)
            self.assertNotIn("magic_enum", header)
            self.assertNotIn("switch (", header)
            self.assertIn("namespace ptxsim::exec_ir {", header)
            self.assertIn("}  // namespace ptxsim::exec_ir", header)
            from ptx_frontend.spec import load_packaged_spec_database
            from ptx_frontend.base.utils import file_stem_to_pascal_case

            database = load_packaged_spec_database()
            self.assertEqual(len(database.instructions), 69)
            self.assertEqual(
                sum(len(item.variants) for item in database.instructions), 257
            )
            self.assertEqual(
                sum(
                    len(form.operand_layouts)
                    for item in database.instructions
                    for form in item.variants
                ),
                320,
            )
            for instruction in database.instructions:
                self.assertIn(
                    f"struct {file_stem_to_pascal_case(instruction.opcode)}",
                    header,
                )
                for form in instruction.variants:
                    self.assertIn(f"`{form.name}`", header)
                    for layout in form.operand_layouts:
                        self.assertIn(
                            f"// YAML layout: {form.name}/{layout.name}", header
                        )
            op_enum = next(
                line for line in header.splitlines()
                if line.startswith("enum class Op : std::uint8_t")
            )
            self.assertIn("and_, or_, xor_, not_", op_enum)
            self.assertNotIn("and, or, xor, not,", op_enum)
            self.assertNotIn("Generated time:", header)
            self.assertIn(
                "struct Add {\n"
                "  /** @brief Pure opcode tag used by generic instruction "
                "dispatch. */\n"
                "  inline static constexpr Op opcode = Op::add;",
                header,
            )
            self.assertIn(
                "struct And {\n"
                "  /** @brief Pure opcode tag used by generic instruction "
                "dispatch. */\n"
                "  inline static constexpr Op opcode = Op::and_;",
                header,
            )
            self.assertIn("concept InstructionAlternative = requires {", header)
            self.assertIn(
                "std::remove_cvref_t<T>::opcode } "
                "-> std::same_as<const Op&>;",
                header,
            )
            concept_body = header.partition(
                "concept InstructionAlternative"
            )[2].partition("/** @brief Return the pure opcode")[0]
            self.assertNotIn(" || ", concept_body)
            op_body = header.partition("auto op(")[2].partition(
                "auto execution_predicate"
            )[0]
            self.assertIn("return T::opcode;", op_body)
            self.assertNotIn("if constexpr", op_body)
            self.assertIn("struct ScalarOperands {", header)
            self.assertIn(
                "[[nodiscard]] auto to_string() const -> std::string;", header
            )
            self.assertIn(
                "[[nodiscard]] auto to_string(const Instruction& value) "
                "-> std::string;",
                header,
            )
            self.assertNotIn("to_string() const -> std::string {", header)
            self.assertNotIn(
                "to_string(const Instruction& value) -> std::string {", header
            )
            self.assertIn("detail::append_operand(operands, dst);", source)
            self.assertIn(
                'output += ".popc";', source
            )
            self.assertIn('output += ".l";', source)
            self.assertIn('output += ".r";', source)
            self.assertIn('output += ".L1::evict_first";', source)
            self.assertIn(
                "if (cache != CacheOperator::unspecified)", source
            )
            self.assertIn("if (ftz)\n    output += \".ftz\";", source)
            self.assertNotIn("to_string(ftz)", source)
            self.assertIn('#include "exec_ir_diagnostic.hpp"', source)
            self.assertIn(
                "auto Add::IntegerNoSat::to_string() const -> std::string",
                source,
            )
            self.assertNotIn("append_text_field", source)
            self.assertNotIn("append_field", source)

    def test_accepts_an_explicit_frontend_spec_directory(self) -> None:
        """An installed CMake port can select its exported specification tree."""
        with tempfile.TemporaryDirectory() as directory:
            with importlib.resources.as_file(packaged_spec_dir()) as spec_dir:
                result = self.generate(
                    Path(directory) / "header.hpp",
                    Path(directory) / "source.cpp",
                    BACKEND,
                    spec_dir,
                )
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_rejects_invalid_kind_mappings_and_schema(self) -> None:
        """Incomplete leaf mappings and incompatible schemas are rejected."""
        for old, new in (
            ("namespace: ptxsim::exec_ir", "namespace: invalid-name"),
            ("ptx_spec_schema: ptx-instr/v1", "ptx_spec_schema: absent"),
            (
                "reg: {cpp_type: common::RegisterSlot, values: {}}",
                "other: {cpp_type: common::RegisterSlot, values: {}}",
            ),
            ("b32: DataType::b32", "b32: "),
        ):
            with self.subTest(new=new):
                result = self.invalid(f"{old}\n{new}")
                self.assertNotEqual(result.returncode, 0)

    def test_backend_type_mapping_changes_output(self) -> None:
        """Changing a backend C++ mapping changes generated storage."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            backend, output = root / "backend.yaml", root / "header.hpp"
            backend.write_text(
                BACKEND.read_text().replace(
                    "cpp_type: DataType", "cpp_type: BackendDataType", 1
                ),
                encoding="utf-8",
            )
            self.assertEqual(
                self.generate(output, root / "source.cpp", backend).returncode,
                0,
            )
            self.assertIn("BackendDataType type", output.read_text())

    def test_generates_deterministic_complete_lowering_topology(self) -> None:
        """Every frontend opcode, form, and layout owns generated lowering."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first_header = root / "first_lowering.hpp"
            first_source = root / "first_lowering.cpp"
            second_header = root / "second_lowering.hpp"
            second_source = root / "second_lowering.cpp"
            self.assertEqual(
                self.generate(
                    lowering_header_output=first_header,
                    lowering_source_output=first_source,
                ).returncode,
                0,
            )
            self.assertEqual(
                self.generate(
                    lowering_header_output=second_header,
                    lowering_source_output=second_source,
                ).returncode,
                0,
            )
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
                ".operands = ptxsim::exec_ir::Mov::Scalar::Operands{",
                source,
            )
            self.assertIn(
                ".execution_predicate = std::move(*predicate),",
                source,
            )
            self.assertIn("LoweringErrorCode::unsupported_instruction", source)
            self.assertIn("lower_sub", header)
            self.assertNotIn("_SUPPORTED_IDENTITIES", source)
            partial = self.generate(lowering_header_output=root / "partial.hpp")
            self.assertNotEqual(partial.returncode, 0)
            self.assertIn("requires both header and source", partial.stderr)


if __name__ == "__main__":
    unittest.main()
