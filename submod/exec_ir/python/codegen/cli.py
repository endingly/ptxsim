"""Command-line entry point for PTXSim execution IR header generation."""

from __future__ import annotations

import argparse
import importlib.resources
import os
from pathlib import Path
import tempfile

from .backend import load_yaml
from .gen_exec_ir import header, source
from .gen_lowering import header as lowering_header
from .gen_lowering import source as lowering_source
from .model import GenerationError
from .projection import database, project_database


def _write(path: Path, content: str) -> None:
    """Atomically publish one generated build artifact."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        "w",
        encoding="utf-8",
        dir=path.parent,
        delete=False,
    ) as temporary:
        temporary.write(content)
        replacement = Path(temporary.name)
    try:
        os.replace(replacement, path)
    finally:
        replacement.unlink(missing_ok=True)


def main() -> None:
    """Validate mappings and emit requested execution-IR generated artifacts."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--backend", type=Path)
    parser.add_argument("--spec-dir", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--source-output", type=Path)
    parser.add_argument("--lowering-header-output", type=Path)
    parser.add_argument("--lowering-source-output", type=Path)
    args = parser.parse_args()
    try:
        if args.backend is None:
            resource = importlib.resources.files(
                "ptxsim_exec_ir_codegen.instructions"
            ).joinpath("backend.yaml")
            with importlib.resources.as_file(resource) as path:
                backend = load_yaml(path)
        else:
            backend = load_yaml(args.backend)
        if args.output is None and args.source_output is not None:
            raise GenerationError("--source-output requires --output")
        if (args.lowering_header_output is None) != (
            args.lowering_source_output is None
        ):
            raise GenerationError(
                "lowering generation requires both header and source outputs"
            )
        if args.output is None and args.lowering_header_output is None:
            raise GenerationError("at least one generated artifact is required")

        projected = project_database(database(args.spec_dir), backend)
        if args.output is not None:
            _write(args.output, header(backend, projected))
        if args.source_output is not None:
            _write(args.source_output, source(backend, projected))
        if args.lowering_header_output is not None:
            assert args.lowering_source_output is not None
            _write(args.lowering_header_output, lowering_header(backend, projected))
            _write(args.lowering_source_output, lowering_source(backend, projected))
    except (GenerationError, ImportError, OSError, ValueError) as error:
        raise SystemExit(f"exec_ir generation error: {error}") from error
