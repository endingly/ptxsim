"""Shared backend and frontend input loading for execution generators."""

from __future__ import annotations

import importlib.resources
from pathlib import Path

from .backend import load_yaml
from .model import BackendSpec, ProjectedInstruction
from .projection import database, project_database


def load_backend(path: Path | None) -> BackendSpec:
    """Load an explicit backend mapping or the packaged execution-IR mapping."""
    if path is not None:
        return load_yaml(path)
    resource = importlib.resources.files(
        "ptxsim_codegen.exec_ir.instructions"
    ).joinpath("backend.yaml")
    with importlib.resources.as_file(resource) as resource_path:
        return load_yaml(resource_path)


def load_projected(
    backend: BackendSpec, spec_dir: Path | None
) -> tuple[ProjectedInstruction, ...]:
    """Load and validate frontend inputs projected through one backend mapping."""
    return project_database(database(spec_dir), backend)
