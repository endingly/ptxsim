"""Command-line entry point for PTXSim execution IR header generation."""

from __future__ import annotations

import argparse
import importlib.resources
import os
from pathlib import Path
import tempfile

from .backend import load_yaml
from .gen_exec_ir import header
from .model import GenerationError
from .projection import database, select_projection


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
    """Validate backend support and emit its public C++ instruction header."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--backend", type=Path)
    parser.add_argument("--spec-dir", type=Path)
    parser.add_argument("--output", required=True, type=Path)
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
        selected = select_projection(database(args.spec_dir), backend)
        _write(args.output, header(backend, selected))
    except (GenerationError, ImportError, OSError, ValueError) as error:
        raise SystemExit(f"exec_ir generation error: {error}") from error
