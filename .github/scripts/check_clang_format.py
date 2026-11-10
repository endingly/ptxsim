#!/usr/bin/env python3
"""Reject formatting drift in tracked handwritten C and C++ sources."""

import argparse
from pathlib import Path
import re
import subprocess
import sys


CLANG_FORMAT_MAJOR = 21
SOURCE_SUFFIXES = frozenset({".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"})
GENERATED_COMPONENTS = frozenset({"generated", "vendor", "third_party"})
GENERATED_SUFFIXES = (".gen.c", ".gen.cc", ".gen.cpp", ".gen.cxx", ".gen.h", ".gen.hh", ".gen.hpp", ".gen.hxx")
VERSION_PATTERN = re.compile(r"clang-format version (\d+)(?:\.|\s)")


def clang_format_major(version_output: str) -> int:
    """Extract clang-format's major version from its version output."""
    match = VERSION_PATTERN.search(version_output)
    if match is None:
        raise ValueError(f"Cannot determine clang-format major version: {version_output!r}")
    return int(match.group(1))


def is_handwritten_source(path: Path) -> bool:
    """Return whether a tracked path is a handwritten C or C++ source file."""
    return (
        path.suffix in SOURCE_SUFFIXES
        and not any(component in GENERATED_COMPONENTS for component in path.parts)
        and not path.name.endswith(GENERATED_SUFFIXES)
    )


def tracked_handwritten_sources(repository_root: Path) -> list[Path]:
    """List tracked handwritten C/C++ files without consulting untracked build output."""
    result = subprocess.run(
        ["git", "ls-files", "-z"],
        cwd=repository_root,
        check=True,
        capture_output=True,
    )
    paths = (Path(raw.decode("utf-8")) for raw in result.stdout.split(b"\0") if raw)
    return sorted(path for path in paths if is_handwritten_source(path))


def trusted_commit(repository_root: Path, base: str) -> str:
    """Resolve a comparison commit; missing history is never a policy bootstrap."""
    return subprocess.run(
        ["git", "rev-parse", "--verify", "--end-of-options", f"{base}^{{commit}}"],
        cwd=repository_root, check=True, capture_output=True, text=True,
    ).stdout.strip()


def baseline_has_policy(repository_root: Path, base: str) -> bool:
    """Detect first adoption from the trusted base tree, without fixed SHA exemptions."""
    commit = trusted_commit(repository_root, base)
    result = subprocess.run(
        ["git", "ls-tree", "--name-only", commit, "--", ".github/scripts/check_clang_format.py"],
        cwd=repository_root, check=True, capture_output=True, text=True,
    )
    return bool(result.stdout.strip())


def changed_handwritten_sources(repository_root: Path, base: str) -> list[Path]:
    """Compare tracked sources with a verified trusted base; invalid bases fail closed."""
    commit = trusted_commit(repository_root, base)
    result = subprocess.run(
        ["git", "diff", "--name-only", "-z", "--diff-filter=ACMR", commit, "--"],
        cwd=repository_root, check=True, capture_output=True,
    )
    changed = {Path(raw.decode("utf-8")) for raw in result.stdout.split(b"\0") if raw}
    return [path for path in tracked_handwritten_sources(repository_root) if path in changed]


def formatting_failures(
    repository_root: Path, clang_format: str, sources: list[Path]
) -> list[str]:
    """Return every source that differs from the configured formatter output."""
    failures: list[str] = []
    for source in sources:
        result = subprocess.run(
            [clang_format, "--dry-run", "--Werror", "--style=file", str(source)],
            cwd=repository_root,
            check=False,
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            failures.append(str(source))
    return failures


def main() -> int:
    """Check changed handwritten files or explicitly audit the full tracked tree."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--clang-format", default="clang-format-21")
    parser.add_argument("--repository-root", type=Path, default=Path("."))
    scope = parser.add_mutually_exclusive_group(required=True)
    scope.add_argument("--base", help="Trusted PR base commit, or previous main commit for periodic runs")
    scope.add_argument("--audit", action="store_true", help="Report full tracked formatting debt")
    arguments = parser.parse_args()
    root = arguments.repository_root.resolve()

    try:
        version = subprocess.run(
            [arguments.clang_format, "--version"],
            check=True,
            capture_output=True,
            text=True,
        ).stdout
        major = clang_format_major(version)
    except (OSError, subprocess.CalledProcessError, ValueError) as error:
        print(f"Cannot verify clang-format: {error}", file=sys.stderr)
        return 1
    if major != CLANG_FORMAT_MAJOR:
        print(
            f"Expected clang-format major {CLANG_FORMAT_MAJOR}, found {major}.",
            file=sys.stderr,
        )
        return 1

    try:
        bootstrap = not arguments.audit and not baseline_has_policy(root, arguments.base)
        if bootstrap:
            print("Formatting policy first adoption: trusted base has no policy helper; reporting full-tree debt without blocking this rollout.")
        sources = (
            tracked_handwritten_sources(root) if arguments.audit or bootstrap
            else changed_handwritten_sources(root, arguments.base)
        )
        failures = formatting_failures(root, arguments.clang_format, sources)
        print(f"Formatting {'audit' if arguments.audit else 'gate'}: {len(sources)} handwritten files, {len(failures)} failures")
    except (OSError, subprocess.CalledProcessError, UnicodeError) as error:
        print(f"Cannot check formatting: {error}", file=sys.stderr)
        return 1
    if failures:
        print("clang-format drift in tracked handwritten sources:", file=sys.stderr)
        print("\n".join(failures), file=sys.stderr)
        return 0 if bootstrap else 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
