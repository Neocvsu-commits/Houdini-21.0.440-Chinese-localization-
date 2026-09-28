import json
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import generate_engine_metadata as metadata
from create_dist import create_dist, stage_dictionaries


class EnginePackageTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.hfs = self.root / "host"
        include = self.hfs / "toolkit/include"
        (include / "UT").mkdir(parents=True)
        (include / "SYS").mkdir()
        (include / "UT/UT_HDKVersion.h").write_text('#define HDK_API_VERSION 22000368\n')
        (include / "SYS/SYS_Version.h").write_text('\n'.join(
            f'#define SYS_VERSION_{part}_INT {value}'
            for part, value in zip(("MAJOR", "MINOR", "BUILD", "PATCH"), (22, 0, 368, 0))))

    def test_identity_and_filename(self):
        output = self.root / "metadata"
        data = metadata.generate(self.hfs, "6.8.3", output)
        self.assertEqual(data["engine"], "HoudiniChineseEngine-hdk22000368-qt6.8.3-x64.dll")
        self.assertEqual(data["hdk_api"], 22000368)
        self.assertEqual((output / "engine.rc").read_text().count("L"), 11)
        self.assertIn('static_assert(HDK_API_VERSION == 22000368)',
                      (output / "engine_build_identity.h").read_text())
        self.assertEqual(json.loads((output / "engine.json").read_text()), data)
        for invalid in ("6.8", "6.8.3/../evil", "7.0.0", ""):
            with self.assertRaises(ValueError):
                metadata.generate(self.hfs, invalid, output)

    def test_package_contains_only_selected_engine_and_install_assets(self):
        package = self.root / "package"
        plugin = package / "HoudiniChinese"
        data = metadata.generate(self.hfs, "6.8.3", self.root / "metadata")
        assets = ("HoudiniChinese.json", "HoudiniChinese/HoudiniChineseHook.dll",
                  "HoudiniChinese/MainMenuCommon.xml",
                  "HoudiniChinese/translations/dictionary_zh.json",
                  "HoudiniChinese/translations/templates_zh.json",
                  "HoudiniChinese/" + data["engine"])
        for relative in assets:
            file = package / relative
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_bytes(b"fixture")
        (plugin / "engine.json").write_text(json.dumps(data))
        (plugin / "HoudiniChinese.ini").write_text("private config")
        (plugin / "old_engine.dll").write_text("stale")
        target = create_dist(package, self.root / "dist", "22.0.368")
        with zipfile.ZipFile(target) as archive:
            self.assertEqual(set(archive.namelist()), {*assets, "HoudiniChinese/engine.json"})
        source = self.root / "dictionaries"
        source.mkdir()
        for name in ("dictionary_zh.json", "templates_zh.json", "dictionary_extra_zh.json"):
            (source / name).write_text('{"translations":{"English":"Chinese"}}')
        stage_dictionaries(source, package)
        (plugin / "translations/runtime_capture_zh.json").write_text("private")
        startup = plugin / "python3.13libs/uiready.py"
        startup.parent.mkdir()
        startup.write_text("# fixture")
        (startup.parent / "pythonrc.py").write_text("# bootstrap fixture")
        (startup.parent / "houdini_chinese_startup.py").write_text("# shared startup fixture")
        (startup.parent / "widgets.json").write_text("private")
        target = create_dist(package, self.root / "dist", "22.0.368")
        with zipfile.ZipFile(target) as archive:
            self.assertIn("HoudiniChinese/translations/dictionary_extra_zh.json", archive.namelist())
            self.assertIn("HoudiniChinese/python3.13libs/uiready.py", archive.namelist())
            self.assertIn("HoudiniChinese/python3.13libs/pythonrc.py", archive.namelist())
            self.assertIn("HoudiniChinese/python3.13libs/houdini_chinese_startup.py", archive.namelist())
            self.assertNotIn("HoudiniChinese/python3.13libs/widgets.json", archive.namelist())
            self.assertNotIn("HoudiniChinese/translations/runtime_capture_zh.json", archive.namelist())
            self.assertEqual(len(archive.namelist()), len(set(archive.namelist())))
        untagged = target.read_bytes()
        tagged = create_dist(package, self.root / "dist", "22.0.368", tag="audit")
        self.assertNotEqual(tagged, target)
        self.assertEqual(target.read_bytes(), untagged)
        with self.assertRaises(ValueError):
            create_dist(package, self.root / "dist", "22.0.368", tag="../bad")
        manifest = plugin / "dictionary_manifest.json"
        valid_manifest = manifest.read_bytes()
        manifest.write_text(json.dumps(["../private_zh.json"]))
        with self.assertRaises(ValueError):
            create_dist(package, self.root / "dist", "22.0.368")
        self.assertEqual(target.read_bytes(), untagged)
        manifest.write_bytes(valid_manifest)
        previous = target.read_bytes()
        for invalid in ("../engine.dll", "C:/bad.dll", "other.dll"):
            data["engine"] = invalid
            (plugin / "engine.json").write_text(json.dumps(data))
            with self.assertRaises(ValueError):
                create_dist(package, self.root / "dist", "22.0.368")
            self.assertEqual(target.read_bytes(), previous)


if __name__ == "__main__":
    unittest.main()
