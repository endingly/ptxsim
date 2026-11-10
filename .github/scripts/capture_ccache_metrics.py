#!/usr/bin/env python3
"""Capture a machine-readable compiler-cache measurement for one CI phase."""

import argparse
import json
from pathlib import Path
import subprocess
import time


def ccache_stats(ccache: str) -> dict[str, int]:
    """Return ccache's stable machine-readable counters from *ccache*.

    ``--print-stats`` is used instead of the human-oriented ``--show-stats``
    output so dashboards and follow-up analysis do not need to parse prose.
    """
    completed = subprocess.run(
        [ccache, "--print-stats", "--format=json"],
        check=True,
        capture_output=True,
        encoding="utf-8",
    )
    parsed = json.loads(completed.stdout)
    if not isinstance(parsed, dict) or not all(
        isinstance(key, str) and isinstance(value, int) for key, value in parsed.items()
    ):
        raise ValueError("ccache statistics must be a JSON object of integer counters")
    return parsed


def metric_record(
    phase: str,
    started_at: float | None,
    finished_at: float | None,
    build_started_at: float | None,
    build_finished_at: float | None,
    build_exit_code: int | None,
    statuses: list[str],
    stats: dict[str, int] | None,
    command_exit_code: int | None = None,
) -> dict[str, object]:
    """Build a versioned measurement record from counters collected after *phase*.

    A build duration is reported only when its own step wrote both endpoints;
    this excludes later metrics-step startup overhead. Action-cache transfer
    bytes are deliberately null: ccache's local size is not a compressed
    Actions transfer size. The cache action log remains the source for
    restore/save transfer-byte and transfer-duration measurements.
    """
    counters = stats or {}
    direct_hits = counters.get("direct_cache_hit")
    preprocessed_hits = counters.get("preprocessed_cache_hit")
    hits = None if direct_hits is None or preprocessed_hits is None else direct_hits + preprocessed_hits
    misses = counters.get("cache_miss")
    total = None if hits is None or misses is None else hits + misses
    captured_at = time.time()
    duration = None
    if started_at is not None and finished_at is not None:
        duration = max(0.0, finished_at - started_at)
    build_duration = None
    if build_started_at is not None and build_finished_at is not None:
        build_duration = max(0.0, build_finished_at - build_started_at)
    if any(status == "failure" for status in statuses):
        status = "failure"
    elif any(status == "cancelled" for status in statuses):
        status = "cancelled"
    elif statuses and all(status == "success" for status in statuses):
        status = "success"
    elif any(status == "skipped" for status in statuses):
        status = "skipped"
    else:
        status = "unknown"
    return {
        "schema_version": 1,
        "phase": phase,
        "captured_at_unix_seconds": captured_at,
        "status": status,
        "phase_wall_seconds": duration,
        "build_wall_seconds": build_duration,
        "build_exit_code": build_exit_code,
        "command_exit_code": command_exit_code,
        "cache_action_transfer": {
            "restore_bytes": None,
            "restore_seconds": None,
            "save_bytes": None,
            "save_seconds": None,
            "source": "GitHub Actions cache action logs; unavailable to the local helper",
        },
        "ccache": {
            "hits": hits,
            "misses": misses,
            "hit_rate": None if not total else hits / total,
            "cache_size_kibibyte": counters.get("cache_size_kibibyte"),
            "max_cache_size_kibibyte": counters.get("max_cache_size_kibibyte"),
            "cleanups_performed": counters.get("cleanups_performed"),
            "available": stats is not None,
            "counters": stats,
        },
    }


def main() -> int:
    """Write one phase record and echo its compact JSON representation."""
    parser = argparse.ArgumentParser()
    parser.add_argument("--phase", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--started-at-file", type=Path)
    parser.add_argument("--finished-at-file", type=Path)
    parser.add_argument("--build-started-at-file", type=Path)
    parser.add_argument("--build-finished-at-file", type=Path)
    parser.add_argument("--build-exit-code-file", type=Path)
    parser.add_argument("--command-exit-code-file", type=Path)
    parser.add_argument("--status", action="append", default=[])
    parser.add_argument("--ccache", default="ccache")
    args = parser.parse_args()

    started_at = None
    if args.started_at_file is not None and args.started_at_file.is_file():
        started_at = float(args.started_at_file.read_text(encoding="utf-8").strip())
    finished_at = None
    if args.finished_at_file is not None and args.finished_at_file.is_file():
        finished_at = float(args.finished_at_file.read_text(encoding="utf-8").strip())
    build_started_at = None
    if args.build_started_at_file is not None and args.build_started_at_file.is_file():
        build_started_at = float(args.build_started_at_file.read_text(encoding="utf-8").strip())
    build_finished_at = None
    if args.build_finished_at_file is not None and args.build_finished_at_file.is_file():
        build_finished_at = float(args.build_finished_at_file.read_text(encoding="utf-8").strip())
    build_exit_code = None
    if args.build_exit_code_file is not None and args.build_exit_code_file.is_file():
        build_exit_code = int(args.build_exit_code_file.read_text(encoding="utf-8").strip())
    command_exit_code = None
    if args.command_exit_code_file is not None and args.command_exit_code_file.is_file():
        command_exit_code = int(args.command_exit_code_file.read_text(encoding="utf-8").strip())
    try:
        stats = ccache_stats(args.ccache)
    except (OSError, subprocess.CalledProcessError, ValueError) as error:
        stats = None
        print(f"Compiler-cache counters unavailable: {error}")
    record = metric_record(
        args.phase,
        started_at,
        finished_at,
        build_started_at,
        build_finished_at,
        build_exit_code,
        args.status,
        stats,
        command_exit_code,
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    serialized = json.dumps(record, sort_keys=True, indent=2) + "\n"
    args.output.write_text(serialized, encoding="utf-8")
    print(serialized, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

