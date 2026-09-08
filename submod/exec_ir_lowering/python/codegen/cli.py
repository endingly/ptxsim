"""Command-line entry point for PTXSim execution-IR lowering generation."""

from __future__ import annotations

import argparse
from pathlib import Path

from ptxsim_codegen.artifacts import atomic_write
from ptxsim_codegen.exec_ir.inputs import load_backend, load_projected
from ptxsim_codegen.exec_ir.model import GenerationError

from .gen_lowering import header, source


def main() -> None:
    """Validate inputs and emit complete execution-IR lowering artifacts."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--backend", type=Path)
    parser.add_argument("--spec-dir", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--source-output", type=Path)
    args = parser.parse_args()
    try:
        if args.output is None or args.source_output is None:
            raise GenerationError("--output and --source-output are required")
        backend = load_backend(args.backend)
        projected = load_projected(backend, args.spec_dir)
        atomic_write(args.output, header(backend, projected))
        atomic_write(args.source_output, source(backend, projected))
    except (GenerationError, ImportError, OSError, ValueError) as error:
        raise SystemExit(f"exec_ir_lowering generation error: {error}") from error
