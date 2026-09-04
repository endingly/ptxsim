"""Command-line entry point for PTXSim execution IR header generation."""

from __future__ import annotations

import argparse
import importlib.resources
import os
from pathlib import Path
import tempfile

from .backend import load_yaml
from .gen_exec_ir import header, source
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
    """Validate mappings and emit public declarations plus private diagnostics."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--backend", type=Path)
    parser.add_argument("--spec-dir", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--source-output", type=Path)
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
        projected = project_database(database(args.spec_dir), backend)
        _write(args.output, header(backend, projected))
        if args.source_output is not None:
            _write(args.source_output, source(backend, projected))
    except (GenerationError, ImportError, OSError, ValueError) as error:
        raise SystemExit(f"exec_ir generation error: {error}") from error
