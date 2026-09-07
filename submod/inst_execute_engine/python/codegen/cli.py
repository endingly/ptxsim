"""Command-line entry point for generated execute-engine preparation sources."""

from __future__ import annotations

import argparse
from pathlib import Path

from jinja2 import TemplateError

from ptxsim_codegen.artifacts import atomic_write
from ptxsim_codegen.exec_ir.inputs import load_backend, load_projected
from ptxsim_codegen.exec_ir.model import GenerationError

from .gen_engine import artifacts


def main() -> None:
    """Validate projected frontend input and emit private ValueALU preparation."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--backend", type=Path)
    parser.add_argument("--spec-dir", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--source-output", type=Path)
    args = parser.parse_args()
    try:
        if args.output is None:
            raise GenerationError("--output is required")
        if args.source_output is None:
            raise GenerationError("--source-output is required")
        backend = load_backend(args.backend)
        try:
            header, source = artifacts(load_projected(backend, args.spec_dir), args.output)
        except TemplateError as error:
            raise GenerationError(f"template rendering failed: {error}") from error
        atomic_write(args.output, header)
        atomic_write(args.source_output, source)
    except (GenerationError, ImportError, OSError, ValueError) as error:
        raise SystemExit(f"inst_execute_engine generation error: {error}") from error
