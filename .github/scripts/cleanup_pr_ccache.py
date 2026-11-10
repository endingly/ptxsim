#!/usr/bin/env python3
"""Retain only the newest PR-local ccache snapshots per cache lineage."""

from __future__ import annotations

import argparse
from collections import defaultdict
from dataclasses import dataclass
from datetime import datetime
import json
import re
import subprocess
import sys
from typing import Any
from urllib.parse import urlencode

_PR_REF_RE = re.compile(r"^refs/pull/[1-9][0-9]*/merge$")
_CCACHE_KEY_RE = re.compile(r"^(?P<lineage>ccache-v9-Linux-(?:X64|ARM64)-ci-linux-clang-(?:debug|release|asan-ubsan)-[0-9a-f]{64})-(?P<sha>[0-9a-f]{40})$")


class CacheApiError(RuntimeError):
    """Report an API response that is unsafe to use for cache deletion."""


@dataclass(frozen=True)
class CacheEntry:
    """Verified GitHub Actions cache metadata."""

    cache_id: int
    key: str
    version: str
    ref: str
    created_at: datetime


@dataclass(frozen=True)
class CacheGroup:
    """One compatible PR-local ccache lineage."""

    lineage: str
    version: str


def parse_created_at(value: str) -> datetime:
    """Parse a GitHub UTC timestamp."""

    if not value.endswith("Z"):
        raise CacheApiError("cache creation time is not UTC")

    try:
        return datetime.fromisoformat(f"{value[:-1]}+00:00")
    except ValueError as error:
        raise CacheApiError("cache creation time is invalid") from error


def cache_entry(value: object) -> CacheEntry:
    """Validate one cache object returned by the GitHub REST API."""

    if not isinstance(value, dict):
        raise CacheApiError("cache list contains a non-object entry")

    raw: dict[str, Any] = value

    required = ("id", "key", "version", "ref", "created_at")
    if any(field not in raw for field in required):
        raise CacheApiError("cache list entry is missing required metadata")

    cache_id = raw["id"]
    key = raw["key"]
    version = raw["version"]
    ref = raw["ref"]
    created_at = raw["created_at"]

    if type(cache_id) is not int or cache_id < 1:
        raise CacheApiError("cache entry has an invalid id")

    if not isinstance(key, str) or not key:
        raise CacheApiError("cache entry has an invalid key")

    if not isinstance(version, str) or not version:
        raise CacheApiError("cache entry has an invalid version")

    if not isinstance(ref, str) or not ref:
        raise CacheApiError("cache entry has an invalid ref")

    if not isinstance(created_at, str) or not created_at:
        raise CacheApiError("cache entry has an invalid creation time")

    return CacheEntry(
        cache_id=cache_id,
        key=key,
        version=version,
        ref=ref,
        created_at=parse_created_at(created_at),
    )


def parse_pages(serialized: str) -> list[CacheEntry]:
    """Decode output from ``gh api --paginate --slurp``."""

    try:
        decoded: Any = json.loads(serialized)
    except json.JSONDecodeError as error:
        raise CacheApiError("cache list response is not JSON") from error

    if not isinstance(decoded, list) or not decoded:
        raise CacheApiError("cache list response has no pages")

    pages: list[object] = decoded

    expected_total: int | None = None
    entries: list[CacheEntry] = []

    for page_value in pages:
        if not isinstance(page_value, dict):
            raise CacheApiError("cache list page is not an object")

        page: dict[str, Any] = page_value

        total = page.get("total_count")
        objects = page.get("actions_caches")

        if type(total) is not int or total < 0:
            raise CacheApiError("cache list page has invalid total_count")

        if not isinstance(objects, list):
            raise CacheApiError("cache list page has invalid actions_caches")

        if expected_total is None:
            expected_total = total
        elif total != expected_total:
            raise CacheApiError("cache list total changed during pagination")

        entries.extend(cache_entry(value) for value in objects)

    if expected_total is None:
        raise CacheApiError("cache list response has no total_count")

    if expected_total != len(entries):
        raise CacheApiError("cache list pagination is incomplete")

    ids = [entry.cache_id for entry in entries]
    if len(set(ids)) != len(ids):
        raise CacheApiError("cache list contains duplicate cache IDs")

    return entries


def ccache_group(entry: CacheEntry) -> CacheGroup | None:
    """Return the verified ccache lineage for one cache entry.

    Unknown cache-key formats are deliberately ignored rather than deleted.
    """

    match = _CCACHE_KEY_RE.fullmatch(entry.key)
    if match is None:
        return None

    return CacheGroup(
        lineage=match.group("lineage"),
        version=entry.version,
    )


def deletion_plan(
    entries: list[CacheEntry],
    ref: str,
    retain: int,
) -> list[int]:
    """Return cache IDs to delete while retaining newest entries per lineage."""

    if _PR_REF_RE.fullmatch(ref) is None:
        raise CacheApiError("ref must have the form refs/pull/<number>/merge")

    if retain < 0:
        raise ValueError("retain must be nonnegative (zero removes recognized PR compiler caches)")

    groups: defaultdict[CacheGroup, list[CacheEntry]] = defaultdict(list)

    for entry in entries:
        # Never operate outside the exact PR merge ref requested.
        if entry.ref != ref:
            raise CacheApiError(
                f"cache list unexpectedly contains another ref: {entry.ref}"
            )

        group = ccache_group(entry)
        if group is not None:
            groups[group].append(entry)

    delete_ids: list[int] = []

    for group_entries in groups.values():
        newest_first = sorted(
            group_entries,
            key=lambda entry: (entry.created_at, entry.cache_id),
            reverse=True,
        )

        delete_ids.extend(entry.cache_id for entry in newest_first[retain:])

    return sorted(delete_ids)


def gh(arguments: list[str]) -> str:
    """Run GitHub CLI without exposing credentials in diagnostics."""

    completed = subprocess.run(
        ["gh", "api", *arguments],
        check=True,
        capture_output=True,
        text=True,
    )
    return completed.stdout


def cache_endpoint(repository: str, ref: str) -> str:
    """Build the strictly scoped Actions-cache endpoint."""

    if re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository) is None:
        raise ValueError("repository must be an owner/repository pair")

    if _PR_REF_RE.fullmatch(ref) is None:
        raise ValueError("ref must have the form refs/pull/<number>/merge")

    query = urlencode(
        {
            "ref": ref,
            "key": "ccache-",
            "per_page": "100",
        }
    )

    return f"repos/{repository}/actions/caches?{query}"


def main() -> int:
    """Print a deletion plan and optionally execute it."""

    parser = argparse.ArgumentParser()
    parser.add_argument("--repository", required=True)
    parser.add_argument("--ref", required=True)
    parser.add_argument("--retain", type=int, default=2)
    parser.add_argument("--execute", action="store_true")
    args = parser.parse_args()

    repository: str = args.repository
    ref: str = args.ref
    retain: int = args.retain
    execute: bool = args.execute

    try:
        endpoint = cache_endpoint(repository, ref)

        entries = parse_pages(gh(["--paginate", "--slurp", endpoint]))

        delete_ids = deletion_plan(
            entries=entries,
            ref=ref,
            retain=retain,
        )

    except (
        CacheApiError,
        subprocess.CalledProcessError,
        OSError,
        ValueError,
    ) as error:
        print(
            f"Refusing PR compiler-cache cleanup: {error}",
            file=sys.stderr,
        )
        return 1

    print(
        json.dumps(
            {
                "ref": ref,
                "retain": retain,
                "delete_ids": delete_ids,
                "execute": execute,
            },
            sort_keys=True,
        )
    )

    if not execute:
        return 0

    try:
        for cache_id in delete_ids:
            gh(
                [
                    "--method",
                    "DELETE",
                    f"repos/{repository}/actions/caches/{cache_id}",
                ]
            )
    except (subprocess.CalledProcessError, OSError) as error:
        print(
            f"PR compiler-cache cleanup stopped after deletion failure: " f"{error}",
            file=sys.stderr,
        )
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

