#!/usr/bin/env python3
"""Publish the installed toolchain fingerprint and manifest baseline to Actions."""

import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


def resolve_compiler(variable: str) -> Path:
    """Resolve a required CC/CXX executable, including its symlink target."""
    compiler = os.environ[variable]
    executable = shutil.which(compiler)
    if executable is None:
        raise ValueError(f"{variable} compiler is not executable: {compiler!r}")
    return Path(executable).resolve(strict=True)


def compiler_identity() -> str:
    """Hash installed tools and OS identity, excluding runner image revisions."""
    compilers = [resolve_compiler(variable) for variable in ("CC", "CXX")]
    identity = hashlib.sha256()
    identity.update(Path("/etc/os-release").read_bytes())
    identity.update(f"ImageOS={os.environ.get('ImageOS') or 'local'}\n".encode())
    for command in (["cmake", "--version"], ["ninja", "--version"]):
        identity.update(subprocess.check_output(command))
    for executable in compilers:
        for option in ("--version", "-dumpmachine"):
            identity.update(subprocess.check_output([str(executable), option]))
        digest = hashlib.sha256()
        with executable.open("rb") as source:
            for chunk in iter(lambda: source.read(1024 * 1024), b""):
                digest.update(chunk)
        # Preserve GNU sha256sum's filename and escaping in the hashed stream.
        filename = os.fsencode(executable)
        escaped = b"\\" in filename or b"\n" in filename
        filename = filename.replace(b"\\", b"\\\\").replace(b"\n", b"\\n")
        identity.update(
            (b"\\" if escaped else b"")
            + digest.hexdigest().encode("ascii")
            + b"  " + filename + b"\n"
        )
    return identity.hexdigest()


def main() -> None:
    """Validate all inputs before appending both outputs to GITHUB_OUTPUT."""
    manifest = json.loads(Path("vcpkg.json").read_text(encoding="utf-8"))
    revision = manifest["builtin-baseline"]
    if not isinstance(revision, str) or re.fullmatch(r"[0-9a-f]{40}", revision) is None:
        raise ValueError("vcpkg builtin-baseline must be a full commit SHA")
    identity = compiler_identity()
    with Path(os.environ["GITHUB_OUTPUT"]).open("a", encoding="utf-8") as output:
        output.write(f"compiler={identity}\nvcpkg-revision={revision}\n")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as error:
        sys.exit(f"Cannot compute compiler cache identity: {error}")
