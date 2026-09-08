"""Command-line entry point for PTXSim execution IR header generation."""

from __future__ import annotations

import argparse
from pathlib import Path

from ptxsim_codegen.artifacts import atomic_write

from .gen_exec_ir import header, source
from .inputs import load_backend, load_projected
from .model import GenerationError


def main() -> None:
    """Validate inputs and emit execution-IR generated artifacts."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--backend", type=Path)
    parser.add_argument("--spec-dir", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--source-output", type=Path)
    args = parser.parse_args()
    try:
        if args.output is None and args.source_output is not None:
            raise GenerationError("--source-output requires --output")
        if args.output is None:
            raise GenerationError("--output is required")
        backend = load_backend(args.backend)
        projected = load_projected(backend, args.spec_dir)
        atomic_write(args.output, header(backend, projected))
        if args.source_output is not None:
            atomic_write(args.source_output, source(backend, projected))
    except (GenerationError, ImportError, OSError, ValueError) as error:
        raise SystemExit(f"exec_ir generation error: {error}") from error
