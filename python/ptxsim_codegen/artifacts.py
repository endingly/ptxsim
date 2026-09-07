"""Filesystem helpers shared by PTXSim code generators."""

from __future__ import annotations

import os
from pathlib import Path
import tempfile


def atomic_write(path: Path, content: str) -> None:
    """Atomically replace one generated text artifact after creating its parent."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        "w", encoding="utf-8", dir=path.parent, delete=False
    ) as temporary:
        temporary.write(content)
        replacement = Path(temporary.name)
    try:
        os.replace(replacement, path)
    finally:
        replacement.unlink(missing_ok=True)
