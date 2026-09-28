import contextlib
import ctypes
import io
import os
from pathlib import Path
import sys
import types
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[2] / "package/HoudiniChinese"

class StartupBootstrapTests(unittest.TestCase):
    @contextlib.contextmanager
    def runtime(self, *, gui=True, loaded=False, fail=False, late=False):
        class OperationFailed(Exception):
            pass
        hou = types.SimpleNamespace(isUIAvailable=Mock(return_value=gui),
            findFile=Mock(return_value="C:/package/HoudiniChineseHook.dll"), OperationFailed=OperationFailed)
        kernel = types.SimpleNamespace(GetModuleHandleW=Mock(return_value=42 if loaded else 0))
        library = Mock()
        library.HoudiniChineseNotifyUIReady.return_value = 1
        loads, timers = [], []
        def windll(name, **kwargs):
            if name == "kernel32": return kernel
            if "handle" not in kwargs:
                loads.append((name, kwargs))
                if fail: raise OSError("load failed")
                kernel.GetModuleHandleW.return_value = 42
            return library
        startup = types.ModuleType("houdini_chinese_startup")
        exec(compile((ROOT / "houdini_chinese_startup.py").read_text(encoding="utf-8"), "startup.py", "exec"), startup.__dict__)
        qt = types.ModuleType("PySide6.QtCore")
        qt.QTimer = types.SimpleNamespace(singleShot=lambda delay, fn: timers.append((delay, fn)))
        errors = io.StringIO()
        modules = {"hou": hou, "houdini_chinese_startup": startup, "PySide6.QtCore": qt}
        environment = {"HOUDINI_PACKAGE_SKIP": "1", "HOUDINI_CHINESE_EXPERIMENT_LATE_LOOK": "1" if late else "0", "HOUDINI_CHINESE_LATE_LOOK_REPORT_DIR": ""}
        with patch.dict(sys.modules, modules), patch.object(ctypes, "WinDLL", side_effect=windll), patch.dict(os.environ, environment), contextlib.redirect_stderr(errors):
            yield startup, library, loads, timers, hou, errors
            self.assertEqual(os.environ["HOUDINI_PACKAGE_SKIP"], "1")

    def entry(self, name):
        namespace = {}  # Houdini startup execution does not provide __file__.
        exec(compile((ROOT / name).read_text(encoding="utf-8"), name, "exec"), namespace)
        self.assertNotIn("_hc_startup_entry", namespace)

    def test_file_association_child_loads_once_across_both_entry_points(self):
        with self.runtime() as (startup, library, loads, timers, hou, errors):
            for script in ("pythonrc.py", "uiready.py", "pythonrc.py", "uiready.py"):
                self.entry(script)
            self.assertEqual(loads, [("C:/package/HoudiniChineseHook.dll", {"winmode": 0xB00})])
            self.assertIs(startup._library, library)
            hou.findFile.assert_called_once_with("HoudiniChineseHook.dll")
            self.assertEqual(errors.getvalue(), "")
            self.assertEqual(timers, [])

    def test_direct_preload_reuses_module_and_does_not_start_timers(self):
        with self.runtime(loaded=True) as (_, library, loads, timers, hou, _):
            self.entry("pythonrc.py"); self.entry("uiready.py")
            self.assertEqual(loads, [])
            self.assertEqual(timers, [])
            hou.findFile.assert_not_called()
            library.HoudiniChineseNotifyUIReady.assert_not_called()

    def test_ready_entry_can_load_without_early_entry(self):
        with self.runtime() as (_, _, loads, _, _, _):
            self.entry("uiready.py")
            self.assertEqual(len(loads), 1)

    def test_headless_process_does_not_load_gui_plugin(self):
        with self.runtime(gui=False) as (_, _, loads, timers, hou, _):
            self.entry("pythonrc.py"); self.entry("uiready.py")
            self.assertEqual(loads, [])
            self.assertEqual(timers, [])
            hou.findFile.assert_not_called()

    def test_load_failure_does_not_abort_startup_or_poll(self):
        with self.runtime(fail=True) as (_, _, loads, timers, _, errors):
            self.entry("pythonrc.py"); self.entry("uiready.py")
            self.assertEqual(len(loads), 2)
            self.assertEqual(timers, [])
            self.assertEqual(errors.getvalue().count("startup load failed"), 2)

    def test_delayed_initialization_notifies_once_after_becoming_ready(self):
        with self.runtime(late=True) as (startup, library, _, timers, _, _):
            library.HoudiniChineseNotifyUIReady.side_effect = [0, 0, 1]
            self.entry("uiready.py"); self.entry("uiready.py")
            while timers:
                delay, callback = timers.pop(0)
                self.assertEqual(delay, 500)
                callback()
            self.assertTrue(startup._notify_accepted)
            self.assertEqual(library.HoudiniChineseNotifyUIReady.call_count, 3)

    def test_notification_budget_stops_when_engine_never_becomes_ready(self):
        with self.runtime(late=True) as (startup, library, _, timers, _, _):
            library.HoudiniChineseNotifyUIReady.return_value = 0
            self.entry("uiready.py")
            callbacks = 0
            while timers:
                _, callback = timers.pop(0); callback(); callbacks += 1
                self.assertLess(callbacks, 30)
            self.assertFalse(startup._notify_accepted)
            self.assertEqual(library.HoudiniChineseNotifyUIReady.call_count, 30)

if __name__ == "__main__":
    unittest.main()
