"""Exercise output cleanup against real disposable directories on Windows."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
PREPARE = ROOT / "tools" / "prepare_build_output.ps1"


class BuildOutputTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.project = Path(self.folder.name) / "project with ' quote"
        self.project.mkdir()
        self.output = self.project / "build" / "out"

    def prepare(self, output=None):
        return subprocess.run(
            ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
             str(PREPARE), "-ProjectRoot", str(self.project),
             "-OutputDirectory", str(output or self.output)],
            capture_output=True, text=True, check=False,
        )

    def test_managed_output_rebuilds_without_deleting_unrelated_files(self):
        result = self.prepare()
        self.assertEqual(result.returncode, 0, result.stderr)
        (self.output / "package").mkdir()
        (self.output / "package" / "old.dll").write_bytes(b"old")
        (self.output / "keep.log").write_text("keep", encoding="utf-8")
        result = self.prepare()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse((self.output / "package").exists())
        self.assertTrue((self.output / "obj" / "detours").is_dir())
        self.assertEqual((self.output / "keep.log").read_text(), "keep")

    def test_unmarked_output_is_preserved(self):
        self.output.mkdir(parents=True)
        valuable = self.output / "package"
        valuable.mkdir()
        sentinel = valuable / "important.txt"
        sentinel.write_text("original", encoding="utf-8")
        result = self.prepare()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(sentinel.read_text(), "original")

    def test_source_and_ancestor_directories_are_rejected(self):
        for target in (self.project, self.project.parent,
                       self.project / "source", self.project / "build",
                       Path(self.project.anchor)):
            with self.subTest(target=target):
                result = self.prepare(target)
                self.assertNotEqual(result.returncode, 0)

    def test_invalid_marker_is_preserved(self):
        self.output.mkdir(parents=True)
        marker = self.output / ".houdini_chinese_build_output"
        marker.write_text("another project", encoding="ascii")
        self.assertNotEqual(self.prepare().returncode, 0)
        self.assertEqual(marker.read_text(), "another project")

    def test_junction_is_rejected_before_any_generated_file_is_deleted(self):
        self.assertEqual(self.prepare().returncode, 0)
        outside = Path(self.folder.name) / "outside"
        outside.mkdir()
        sentinel = outside / "important.txt"
        sentinel.write_text("original", encoding="utf-8")
        (self.output / "obj" / "keep.obj").write_bytes(b"object")
        package = self.output / "package"
        package.mkdir()
        junction = package / "external"
        environment = os.environ.copy()
        environment["HC_TEST_JUNCTION"] = str(junction)
        environment["HC_TEST_TARGET"] = str(outside)
        created = subprocess.run(
            ["powershell", "-NoProfile", "-Command",
             "New-Item -ItemType Junction -Path $env:HC_TEST_JUNCTION "
             "-Target $env:HC_TEST_TARGET | Out-Null"],
            env=environment, capture_output=True, text=True, check=False,
        )
        self.assertEqual(created.returncode, 0, created.stderr)
        try:
            self.assertNotEqual(self.prepare().returncode, 0)
            self.assertEqual(sentinel.read_text(), "original")
            self.assertTrue((self.output / "obj" / "keep.obj").exists())
        finally:
            # Remove only this verified temporary junction, never its target.
            junction.rmdir()


if __name__ == "__main__":
    unittest.main()
