#!/usr/bin/env python3
"""Retain only the current and one prior trusted-main ccache seed per lineage."""

import argparse
from collections.abc import Iterable
from dataclasses import dataclass
from datetime import datetime
import json
import re
import subprocess
import sys
from urllib.parse import urlencode


class CacheApiError(RuntimeError):
    """Report an API response that is unsafe to use for cache deletion."""


@dataclass(frozen=True)
class CacheEntry:
    """The verified cache metadata needed to make one narrow retention decision."""

    cache_id: int
    key: str
    version: str
    ref: str
    created_at: datetime


def cache_entry(value: object) -> CacheEntry:
    """Validate one REST cache object before it can participate in deletion planning."""
    if not isinstance(value, dict):
        raise CacheApiError("cache list contains a non-object entry")
    required = ("id", "key", "version", "ref", "created_at")
    if any(field not in value for field in required):
        raise CacheApiError("cache list entry is missing required metadata")
    cache_id = value["id"]
    key = value["key"]
    version = value["version"]
    ref = value["ref"]
    created_at = value["created_at"]
    if (
        type(cache_id) is not int
        or cache_id < 1
        or not all(isinstance(field, str) and field for field in (key, version, ref, created_at))
    ):
        raise CacheApiError("cache list entry has invalid metadata")
    try:
        if not created_at.endswith("Z"):
            raise ValueError("not UTC")
        created_at = datetime.fromisoformat(f"{created_at[:-1]}+00:00")
    except ValueError as error:
        raise CacheApiError("cache list entry has invalid creation time") from error
    return CacheEntry(cache_id, key, version, ref, created_at)


def parse_pages(serialized: str) -> list[CacheEntry]:
    """Decode ``gh api --paginate --slurp`` output and reject incomplete pagination."""
    try:
        pages = json.loads(serialized)
    except json.JSONDecodeError as error:
        raise CacheApiError("cache list response is not JSON") from error
    if not isinstance(pages, list) or not pages:
        raise CacheApiError("cache list response has no pages")

    expected_total: int | None = None
    entries: list[CacheEntry] = []
    for page in pages:
        if not isinstance(page, dict):
            raise CacheApiError("cache list page is not an object")
        total = page.get("total_count")
        objects = page.get("actions_caches")
        if type(total) is not int or total < 0 or not isinstance(objects, list):
            raise CacheApiError("cache list page has invalid pagination metadata")
        if expected_total is None:
            expected_total = total
        elif total != expected_total:
            raise CacheApiError("cache list total changed during pagination")
        entries.extend(cache_entry(value) for value in objects)

    if expected_total != len(entries):
        raise CacheApiError("cache list pagination is incomplete")
    if len({entry.cache_id for entry in entries}) != len(entries):
        raise CacheApiError("cache list contains duplicate cache IDs")
    return entries


def deletion_plan(
    entries: Iterable[CacheEntry], current_key: str, prefix: str, ref: str
) -> list[int]:
    """Return deletable IDs after proving the current seed and exact lineage scope.

    The current cache's opaque Actions version partitions the lineage. Entries
    from another version, ref, key family, malformed SHA suffix, or a newer
    creation time never enter the deletion plan. The current seed and newest
    strictly older seed are retained.
    """
    validate_scope(ref, prefix)
    if any(entry.ref != ref for entry in entries):
        raise CacheApiError("cache list unexpectedly contains another ref")
    if not current_key.startswith(prefix):
        raise CacheApiError("current key is outside the requested cache prefix")
    seed_key = re.compile(rf"{re.escape(prefix)}[0-9a-f]{{40}}$")
    current = [entry for entry in entries if entry.key == current_key and entry.ref == ref]
    if len(current) != 1:
        raise CacheApiError("current trusted-main seed was not uniquely verified")
    verified_current = current[0]
    if not verified_current.version or not seed_key.fullmatch(verified_current.key):
        raise CacheApiError("current seed has an invalid version or key")

    lineage = [
        entry
        for entry in entries
        if entry.ref == ref
        and entry.version == verified_current.version
        and seed_key.fullmatch(entry.key)
    ]
    if verified_current not in lineage:
        raise CacheApiError("current seed escaped its verified lineage")
    previous = sorted(
        (
            entry
            for entry in lineage
            if entry.cache_id != verified_current.cache_id
            and entry.created_at < verified_current.created_at
        ),
        key=lambda entry: (entry.created_at, entry.cache_id),
        reverse=True,
    )
    retained = {verified_current.cache_id}
    if previous:
        retained.add(previous[0].cache_id)
    return [
        entry.cache_id
        for entry in lineage
        if entry.cache_id not in retained and entry.created_at < verified_current.created_at
    ]


def gh(arguments: list[str]) -> str:
    """Run GitHub CLI with a checked response, exposing no token in diagnostics."""
    completed = subprocess.run(
        ["gh", "api", *arguments], check=True, capture_output=True, encoding="utf-8"
    )
    return completed.stdout


def validate_scope(ref: str, prefix: str) -> None:
    """Allow only trusted-main lineages emitted by the three Linux matrix rows."""
    if ref != "refs/heads/main":
        raise ValueError("seed retention requires refs/heads/main")
    if re.fullmatch(
        r"ccache-v9-Linux-(?:X64|ARM64)-ci-linux-clang-(?:debug|release|asan-ubsan)-[0-9a-f]{64}-",
        prefix,
    ) is None:
        raise ValueError("unrecognized compiler-cache lineage")


def cache_endpoint(repository: str, ref: str, prefix: str) -> str:
    """Build the strictly scoped paginated list endpoint for the target lineage."""
    if re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository) is None:
        raise ValueError("repository must be an owner/repository pair")
    validate_scope(ref, prefix)
    query = urlencode({"ref": ref, "key": prefix, "per_page": "100"})
    return f"repos/{repository}/actions/caches?{query}"


def main() -> int:
    """Verify a current seed, print the plan, and delete only with ``--execute``."""
    parser = argparse.ArgumentParser()
    parser.add_argument("--repository", required=True)
    parser.add_argument("--ref", required=True)
    parser.add_argument("--current-key", required=True)
    parser.add_argument("--prefix", required=True)
    parser.add_argument("--execute", action="store_true")
    args = parser.parse_args()

    try:
        endpoint = cache_endpoint(args.repository, args.ref, args.prefix)
        entries = parse_pages(gh(["--paginate", "--slurp", endpoint]))
        delete_ids = deletion_plan(entries, args.current_key, args.prefix, args.ref)
    except (CacheApiError, subprocess.CalledProcessError, OSError, ValueError) as error:
        print(f"Refusing compiler-cache cleanup: {error}", file=sys.stderr)
        return 1

    print(json.dumps({"delete_ids": delete_ids, "execute": args.execute}, sort_keys=True))
    if not args.execute:
        return 0
    try:
        for cache_id in delete_ids:
            gh(["--method", "DELETE", f"repos/{args.repository}/actions/caches/{cache_id}"])
    except (subprocess.CalledProcessError, OSError) as error:
        print(f"Compiler-cache cleanup stopped after a deletion failure: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

