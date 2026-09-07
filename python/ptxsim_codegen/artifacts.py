"""Filesystem helpers shared by PTXSim code generators."""

from __future__ import annotations

import os
from pathlib import Path
import tempfile


def atomic_write(path: Path, content: str) -> None:
    """Publish UTF-8 bytes atomically, preserving unchanged artifacts' timestamps."""
    encoded = content.encode("utf-8")
    try:
        if path.read_bytes() == encoded:
            return
    except FileNotFoundError:
        pass
    path.parent.mkdir(parents=True, exist_ok=True)
    replacement = None
    try:
        with tempfile.NamedTemporaryFile(
            "wb", dir=path.parent, delete=False
        ) as temporary:
            replacement = Path(temporary.name)
            temporary.write(encoded)
        os.replace(replacement, path)
    finally:
        if replacement is not None:
            replacement.unlink(missing_ok=True)
