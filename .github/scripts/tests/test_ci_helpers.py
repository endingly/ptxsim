"""Exercise cleanup safety, phase measurements, and formatting scope with isolated fixtures."""

from contextlib import redirect_stdout, redirect_stderr
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

SCRIPTS = Path(__file__).resolve().parents[1]


def load(name):
    """Import a CI helper without requiring the scripts directory on sys.path."""
    spec = importlib.util.spec_from_file_location(name, SCRIPTS / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


seeds = load("cleanup_ccache_seeds")
pr = load("cleanup_pr_ccache")
metrics = load("capture_ccache_metrics")
formatting = load("check_clang_format")


def prefix(preset="debug"):
    """Build an actual three-matrix-row lineage with a full tool identity."""
    return f"ccache-v9-Linux-X64-ci-linux-clang-{preset}-{'a' * 64}-"


def entry(number, *, preset="debug", ref="refs/heads/main", version="v1", key=None):
    """Return synthetic REST cache metadata with deterministic distinct times/SHAs."""
    return {
        "id": number, "key": key or f"{prefix(preset)}{number:040x}",
        "version": version, "ref": ref, "created_at": f"2026-11-{number:02d}T00:00:00Z",
    }


def pages(entries):
    """Serialize two valid API pages, including an empty page for empty listings."""
    split = len(entries) // 2
    return json.dumps([
        {"total_count": len(entries), "actions_caches": entries[:split]},
        {"total_count": len(entries), "actions_caches": entries[split:]},
    ])


class CleanupTests(unittest.TestCase):
    """Prove deletion scope and fail-before-delete behavior using mocked API calls."""

    def test_main_keeps_current_previous_and_newer_all_three_presets(self):
        for preset in ("debug", "release", "asan-ubsan"):
            with self.subTest(preset=preset):
                objects = [entry(n, preset=preset) for n in range(1, 6)]
                objects += [entry(6, preset=preset, version="other")]
                entries = seeds.parse_pages(pages(objects))
                self.assertEqual(seeds.deletion_plan(entries, objects[3]["key"], prefix(preset), "refs/heads/main"), [1, 2])

    def test_main_rejects_wrong_scope_missing_current_and_collision(self):
        raw = entry(1)
        entries = seeds.parse_pages(pages([raw]))
        cases = [
            (entries, raw["key"], prefix(), "refs/pull/1/merge"),
            (entries, raw["key"], "ccache-", "refs/heads/main"),
            (entries, prefix() + "f" * 40, prefix(), "refs/heads/main"),
            (entries + seeds.parse_pages(pages([entry(2, key=raw["key"])])), raw["key"], prefix(), "refs/heads/main"),
            (entries + seeds.parse_pages(pages([entry(2, ref="refs/heads/dev")])), raw["key"], prefix(), "refs/heads/main"),
        ]
        for values in cases:
            with self.subTest(values=values):
                with self.assertRaises((ValueError, seeds.CacheApiError)):
                    seeds.deletion_plan(*values)

    def test_pagination_anomalies_fail_in_both_helpers(self):
        bad = [
            "{}", "[]", "not-json",
            json.dumps([{"total_count": 2, "actions_caches": [entry(1)]}]),
            json.dumps([{"total_count": 1, "actions_caches": []}, {"total_count": 2, "actions_caches": []}]),
            json.dumps([{"total_count": 2, "actions_caches": [entry(1), entry(1)]}]),
            pages([entry(1) | {"id": True}]),
            pages([entry(1) | {"created_at": "yesterday"}]),
            pages([entry(1) | {"version": ""}]),
        ]
        for module in (seeds, pr):
            for serialized in bad:
                with self.subTest(helper=module.__name__, serialized=serialized):
                    with self.assertRaises(module.CacheApiError):
                        module.parse_pages(serialized)

    def test_pr_retention_partitions_preset_version_and_ignores_unknown_keys(self):
        ref = "refs/pull/49/merge"
        raw = [entry(n, preset=preset, ref=ref) for preset, numbers in (
            ("debug", range(1, 5)), ("release", range(5, 9)), ("asan-ubsan", range(9, 13)),
        ) for n in numbers]
        raw += [entry(13, ref=ref, version="other"), entry(14, ref=ref, key="vcpkg-keep")]
        entries = pr.parse_pages(pages(raw))
        self.assertEqual(pr.deletion_plan(entries, ref, 2), [1, 2, 5, 6, 9, 10])
        self.assertEqual(pr.deletion_plan(entries, ref, 0), list(range(1, 14)))
        with self.assertRaises(pr.CacheApiError):
            pr.deletion_plan(entries + pr.parse_pages(pages([entry(15)])), ref, 2)
        for invalid in ("refs/heads/main", "refs/pull/0/merge", "refs/pull/49/head"):
            with self.assertRaises((ValueError, pr.CacheApiError)):
                pr.cache_endpoint("owner/repo", invalid)

    def test_main_cli_dry_run_and_fail_before_delete(self):
        args = ["helper", "--repository", "owner/repo", "--ref", "refs/heads/main", "--prefix", prefix(), "--current-key", entry(4)["key"]]
        for execute in (False, True):
            with self.subTest(execute=execute), patch.object(sys, "argv", args + (["--execute"] if execute else [])), patch.object(seeds, "gh", return_value=pages([entry(n) for n in range(1, 5)])) as api, redirect_stdout(io.StringIO()):
                self.assertEqual(seeds.main(), 0)
                self.assertEqual(api.call_count, 3 if execute else 1)
                if execute:
                    self.assertTrue(all(call.args[0][:2] == ["--method", "DELETE"] for call in api.call_args_list[1:]))
        for response in (pages([entry(1)]), '{"incomplete":true}'):
            with patch.object(sys, "argv", args + ["--execute"]), patch.object(seeds, "gh", return_value=response) as api, redirect_stderr(io.StringIO()):
                self.assertEqual(seeds.main(), 1)
                self.assertEqual(api.call_count, 1)

    def test_pr_cli_dry_run_and_api_failure_never_delete(self):
        args = ["helper", "--repository", "owner/repo", "--ref", "refs/pull/49/merge"]
        with patch.object(sys, "argv", args), patch.object(pr, "gh", return_value=pages([entry(n, ref="refs/pull/49/merge") for n in range(1, 5)])) as api, redirect_stdout(io.StringIO()):
            self.assertEqual(pr.main(), 0)
            self.assertEqual(api.call_count, 1)
        with patch.object(sys, "argv", args + ["--execute"]), patch.object(pr, "gh", side_effect=subprocess.CalledProcessError(1, "gh")) as api, redirect_stderr(io.StringIO()):
            self.assertEqual(pr.main(), 1)
            self.assertEqual(api.call_count, 1)


class MetricsTests(unittest.TestCase):
    """Keep unavailable evidence distinct from zero and preserve phase outcomes."""

    def test_success_failure_skipped_and_missing_counters(self):
        record = metrics.metric_record("production", 10, 20, 12, 19, 0, ["success"], {"direct_cache_hit": 3, "preprocessed_cache_hit": 1, "cache_miss": 4})
        self.assertEqual(record["phase_wall_seconds"], 10)
        self.assertEqual(record["build_wall_seconds"], 7)
        self.assertEqual(record["ccache"]["hit_rate"], 0.5)
        self.assertIsNone(record["cache_action_transfer"]["restore_bytes"])
        for statuses, expected in ((["failure", "skipped"], "failure"), (["skipped"], "skipped"), (["cancelled"], "cancelled")):
            record = metrics.metric_record("tests", None, None, None, None, None, statuses, None)
            self.assertEqual(record["status"], expected)
            self.assertIsNone(record["phase_wall_seconds"])
            self.assertIsNone(record["ccache"]["hits"])
            self.assertFalse(record["ccache"]["available"])
        record = metrics.metric_record("tests", None, None, None, None, None, [], {})
        self.assertIsNone(record["ccache"]["hit_rate"])

    def test_unavailable_ccache_still_writes_failed_record(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "metrics.json"
            command_exit = Path(directory) / "command.exit-code"
            command_exit.write_text("42\n")
            with patch.object(sys, "argv", ["helper", "--phase", "dependency-preparation", "--output", str(output), "--status", "failure", "--command-exit-code-file", str(command_exit)]), patch.object(metrics, "ccache_stats", side_effect=FileNotFoundError("ccache")), redirect_stdout(io.StringIO()):
                self.assertEqual(metrics.main(), 0)
            record = json.loads(output.read_text())
            self.assertEqual(record["status"], "failure")
            self.assertIsNone(record["ccache"]["counters"])
            self.assertEqual(record["command_exit_code"], 42)
            self.assertIsNone(record["build_exit_code"])


class FormattingTests(unittest.TestCase):
    """Use a real temporary Git repository to prove trusted-base changed-file scope."""

    def test_changed_added_deleted_generated_and_missing_base(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            def git(*arguments):
                """Run Git only inside this disposable fixture."""
                return subprocess.run(["git", *arguments], cwd=root, check=True, capture_output=True, text=True).stdout.strip()
            git("init")
            git("config", "user.email", "fixture@example.invalid")
            git("config", "user.name", "Fixture")
            for filename in ("old.cpp", "changed.cpp", "deleted.cpp"):
                (root / filename).write_text("int x;\n")
            git("add", ".")
            git("commit", "-m", "base")
            base = git("rev-parse", "HEAD")
            self.assertFalse(formatting.baseline_has_policy(root, base))
            policy = root / ".github/scripts/check_clang_format.py"
            policy.parent.mkdir(parents=True)
            policy.write_text("# policy fixture\n")
            git("add", ".")
            git("commit", "-m", "adopt policy")
            self.assertTrue(formatting.baseline_has_policy(root, "HEAD"))
            policy_base = git("rev-parse", "HEAD")
            (root / "changed.cpp").write_text("int   y;\n")
            (root / "added.hpp").write_text("int z;\n")
            (root / "ignored.gen.cpp").write_text("generated\n")
            (root / "deleted.cpp").unlink()
            git("add", ".")
            self.assertEqual(formatting.changed_handwritten_sources(root, base), [Path("added.hpp"), Path("changed.cpp")])
            with self.assertRaises(subprocess.CalledProcessError):
                formatting.changed_handwritten_sources(root, "missing-base")
            with self.assertRaises(subprocess.CalledProcessError):
                formatting.baseline_has_policy(root, "missing-base")
            for comparison, expected, bootstrap in ((base, 0, True), (policy_base, 1, False), ("missing-base", 1, False)):
                output = io.StringIO()
                with patch.object(sys, "argv", ["formatter", "--repository-root", str(root), "--base", comparison]), redirect_stdout(output), redirect_stderr(output):
                    self.assertEqual(formatting.main(), expected)
                self.assertEqual("first adoption" in output.getvalue(), bootstrap)
                if comparison == policy_base:
                    self.assertIn("changed.cpp", output.getvalue())

    def test_formatter_positive_negative_and_version(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / ".clang-format").write_text("BasedOnStyle: LLVM\n")
            (root / "good.cpp").write_text("int main() { return 0; }\n")
            (root / "bad.cpp").write_text("int   main(){return 0;}\n")
            self.assertEqual(formatting.formatting_failures(root, "clang-format-21", [Path("good.cpp")]), [])
            self.assertEqual(formatting.formatting_failures(root, "clang-format-21", [Path("bad.cpp")]), ["bad.cpp"])
        self.assertEqual(formatting.clang_format_major("Ubuntu clang-format version 21.1.8"), 21)
        with self.assertRaises(ValueError):
            formatting.clang_format_major("unknown")


if __name__ == "__main__":
    unittest.main()
