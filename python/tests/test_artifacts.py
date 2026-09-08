"""Regression checks for deterministic generated-artifact publication."""

from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from ptxsim_codegen.artifacts import atomic_write


class ArtifactTests(unittest.TestCase):
    """Check no-op publication and failure cleanup without compiler involvement."""

    def test_unchanged_content_preserves_file(self) -> None:
        """An unchanged artifact retains its timestamp and is never replaced."""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "nested" / "generated.hpp"
            atomic_write(path, "// generated\n")
            timestamp = path.stat().st_mtime_ns
            with patch("ptxsim_codegen.artifacts.os.replace") as replace:
                atomic_write(path, "// generated\n")
                replace.assert_not_called()
            self.assertEqual(path.stat().st_mtime_ns, timestamp)

    def test_changed_content_is_replaced(self) -> None:
        """Changed output is published as deterministic UTF-8 bytes."""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "generated.hpp"
            atomic_write(path, "old\n")
            atomic_write(path, "// 新内容\n")
            self.assertEqual(path.read_bytes(), "// 新内容\n".encode("utf-8"))
            self.assertEqual(list(path.parent.iterdir()), [path])

    def test_failed_replace_preserves_previous_artifact(self) -> None:
        """Failed publication leaves the old artifact and removes temporary bytes."""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "generated.hpp"
            atomic_write(path, "old\n")
            with patch("ptxsim_codegen.artifacts.os.replace", side_effect=OSError):
                with self.assertRaises(OSError):
                    atomic_write(path, "new\n")
            self.assertEqual(path.read_bytes(), b"old\n")
            self.assertEqual(list(path.parent.iterdir()), [path])
