"""Shared, process-local startup for all Houdini GUI launch routes."""
import ctypes
import os
import sys

_library = None
_ready_started = False
_notify_attempts = 0
_notify_accepted = False
MAX_NOTIFY_ATTEMPTS = 30
NOTIFY_INTERVAL_MS = 500


def ensure_loaded():
    """Load the independent bootstrap once; it retains all ABI checks."""
    global _library
    import hou
    if sys.platform != "win32" or not hou.isUIAvailable():
        return None
    if _library is not None:
        return _library
    try:
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.GetModuleHandleW.argtypes = [ctypes.c_wchar_p]
        kernel.GetModuleHandleW.restype = ctypes.c_void_p
        handle = kernel.GetModuleHandleW("HoudiniChineseHook.dll")
        if handle:
            _library = ctypes.WinDLL("HoudiniChineseHook.dll", handle=handle)
        else:
            _library = ctypes.WinDLL(hou.findFile("HoudiniChineseHook.dll"), winmode=0xB00)
        return _library
    except (OSError, hou.OperationFailed) as error:
        print("HoudiniChinese startup load failed: %s" % error, file=sys.stderr)
        return None


def ui_ready():
    """One second-chance load, then bounded optional startup notification."""
    global _ready_started
    if _ready_started:
        return
    plugin = ensure_loaded()
    if plugin is None:
        return
    _ready_started = True
    late = os.environ.get("HOUDINI_CHINESE_EXPERIMENT_LATE_LOOK") == "1"
    directory = os.environ.get("HOUDINI_CHINESE_LATE_LOOK_REPORT_DIR", "")
    if not late and not directory:
        return
    from PySide6.QtCore import QTimer

    def notify():
        global _notify_attempts, _notify_accepted
        try:
            _notify_attempts += 1
            function = plugin.HoudiniChineseNotifyUIReady
            function.argtypes = []
            function.restype = ctypes.c_int
            _notify_accepted = bool(function())
            if not _notify_accepted and _notify_attempts < MAX_NOTIFY_ATTEMPTS:
                QTimer.singleShot(NOTIFY_INTERVAL_MS, notify)
        except (OSError, AttributeError) as error:
            print("HoudiniChinese ready notification failed: %s" % error, file=sys.stderr)

    if directory:
        # Explicit diagnostics only; ordinary startup neither writes nor polls.
        from pathlib import Path
        import json
        output = Path(directory)
        try:
            output.mkdir(parents=True, exist_ok=True)
            observe = plugin.HoudiniChineseEnableHookDiagnostics
            observe.argtypes = [ctypes.c_int]
            observe.restype = None
            observe(1)
            request = plugin.HoudiniChineseRequestHookReport
            request.argtypes = [ctypes.c_wchar_p]
            request.restype = ctypes.c_int

            def report(second):
                try:
                    (output / "uiready.json").write_text(json.dumps({
                        "pid": os.getpid(), "notify_accepted": _notify_accepted,
                        "notify_attempts": _notify_attempts}), encoding="utf-8")
                    request(str(output / ("hooks-%ds.json" % second)))
                except OSError as error:
                    print("HoudiniChinese diagnostic write failed: %s" % error, file=sys.stderr)

            # Queue shutdown first so a failed report cannot leave observation on.
            QTimer.singleShot(151000, lambda: observe(0))
            for second in (1, 5, 20, 50, 125, 150):
                QTimer.singleShot(second * 1000, lambda second=second: report(second))
        except (OSError, AttributeError) as error:
            print("HoudiniChinese diagnostics unavailable: %s" % error, file=sys.stderr)
    if late:
        notify()
