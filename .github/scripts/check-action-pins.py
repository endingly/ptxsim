#!/usr/bin/env python3
"""Require full commit pins for remote actions in workflows and local actions."""

from pathlib import Path
import re
import sys


def main() -> int:
    """Check action references from the repository root and report their locations."""
    uses_pattern = re.compile(r"^\s*(?:-\s+)?uses:\s*([^\s#]+)")
    invalid = []
    for directory in (Path(".github/workflows"), Path(".github/actions")):
        if not directory.is_dir():
            raise FileNotFoundError(f"Missing action directory: {directory}")
        for source in sorted(directory.rglob("*")):
            if not source.is_file() or source.suffix not in (".yml", ".yaml"):
                continue
            for line_number, line in enumerate(
                source.read_text(encoding="utf-8").splitlines(), start=1
            ):
                match = uses_pattern.match(line)
                if match is None:
                    continue
                reference = match.group(1)
                if reference.startswith(("./", "docker://")):
                    continue
                if re.search(r"@[0-9a-f]{40}$", reference) is None:
                    invalid.append(f"{source}:{line_number}: {reference}")
    if invalid:
        print("Floating third-party action reference(s):", file=sys.stderr)
        print("\n".join(invalid), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, UnicodeError) as error:
        sys.exit(f"Cannot check action pins: {error}")
