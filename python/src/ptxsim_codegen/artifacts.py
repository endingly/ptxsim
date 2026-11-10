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


def write_cmake_plan(path: Path, prefix: str, headers: tuple[Path, ...], sources: tuple[Path, ...]) -> None:
    """Publish complete artifact membership for CMake without generating sources."""
    if not prefix or not all(character.isupper() or character.isdigit() or character == '_' for character in prefix):
        raise ValueError('CMake artifact prefix must be an uppercase identifier')

    def quote(value: Path) -> str:
        """Escape literal absolute paths independently from CMake variable syntax."""
        return '"' + value.absolute().as_posix().replace('"', '\\"').replace('$', '\\$').replace(';', '\\;') + '"'

    lines = ['# Generated artifact membership. Do not edit.\n']
    for suffix, values in [('HEADERS', headers), ('SOURCES', sources)]:
        lines += [f'set({prefix}_{suffix}\n', *(f'    {quote(value)}\n' for value in values), ')\n']
    atomic_write(path, ''.join(lines))
