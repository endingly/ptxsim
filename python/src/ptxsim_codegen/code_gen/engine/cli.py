"""Generate the complete dense typed issue-group dispatcher."""

from __future__ import annotations

import argparse
from pathlib import Path

from ptxsim_codegen.artifacts import write_cmake_plan
from ptxsim_codegen.frontend.v080 import snapshot
from ptxsim_codegen.model import GenerationError

from .native import artifact_plan, generate


def main() -> None:
    """Plan or publish complete deterministic artifacts from one immutable projection."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--backend', type=Path)
    parser.add_argument('--spec-dir', type=Path)
    parser.add_argument('--output-dir', required=True, type=Path)
    parser.add_argument('--cmake-plan-output', type=Path)
    args = parser.parse_args()
    try:
        model = snapshot(args.spec_dir, args.backend)
        if args.cmake_plan_output is not None:
            plan = artifact_plan(model)
            write_cmake_plan(args.cmake_plan_output, 'PTXSIM_INST_EXECUTE_ENGINE_GENERATED',
                             tuple(args.output_dir / value for value in plan.headers),
                             tuple(args.output_dir / value for value in plan.sources))
        else:
            generate(model, args.output_dir)
    except (GenerationError, ImportError, OSError, ValueError) as error:
        raise SystemExit(f'inst_execute_engine generation error: {error}') from error
