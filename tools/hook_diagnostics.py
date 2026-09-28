"""Explicit diagnostics for the already-loaded plugin, inside Houdini Python.

This module never loads the hook into a new process, changes UI, or drives input.
Reports are queued on the GUI thread; a True result means queued, not written.
"""

import ctypes
import os
from pathlib import Path


def _plugin():
    if os.name != "nt":
        raise RuntimeError("Hook diagnostics require the Windows Houdini host")
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.GetModuleHandleW.argtypes = [ctypes.c_wchar_p]
    kernel.GetModuleHandleW.restype = ctypes.c_void_p
    handle = kernel.GetModuleHandleW("HoudiniChineseHook.dll")
    if not handle:
        raise RuntimeError("HoudiniChineseHook.dll is not loaded in this process")
    plugin = ctypes.WinDLL("HoudiniChineseHook.dll", handle=handle)
    plugin.HoudiniChineseEnableHookDiagnostics.argtypes = [ctypes.c_int]
    plugin.HoudiniChineseEnableHookDiagnostics.restype = None
    plugin.HoudiniChineseRequestHookReport.argtypes = [ctypes.c_wchar_p]
    plugin.HoudiniChineseRequestHookReport.restype = ctypes.c_int
    return plugin


def start():
    """Enable cumulative counters; the caller manually exercises their UI."""
    _plugin().HoudiniChineseEnableHookDiagnostics(True)


def stop():
    """Disable new observations; already-running calls may finish their counts."""
    _plugin().HoudiniChineseEnableHookDiagnostics(False)


def loader_status():
    """Read bootstrap status even when no compatible engine could be started."""
    fields = ("magic", "protocol", "machine", "hdk", "qt_major", "qt_minor",
              "qt_patch", "houdini_major", "houdini_minor", "houdini_build", "houdini_patch")

    class Identity(ctypes.Structure):
        _fields_ = [(name, ctypes.c_uint32) for name in fields]

    class Status(ctypes.Structure):
        _fields_ = [(name, ctypes.c_uint32) for name in ("size", "protocol", "status", "error")]
        _fields_ += [("host", Identity)]

    plugin = _plugin()
    function = getattr(plugin, "HoudiniChineseGetLoaderStatus", None)
    if function is None:
        return {"status": "legacy_direct_engine"}
    function.argtypes = [ctypes.POINTER(Status), ctypes.c_uint32]
    function.restype = ctypes.c_int
    value = Status()
    if not function(ctypes.byref(value), ctypes.sizeof(value)):
        raise RuntimeError("Loader status protocol mismatch")
    names = ("waiting", "starting", "engine_loaded", "unsupported_host", "host_timeout",
             "mixed_qt_runtime", "missing_hdk_api", "engine_missing", "identity_mismatch",
             "load_failed", "start_failed", "internal_error", "already_loaded")
    return {"status": names[value.status] if value.status < len(names) else "unknown",
            "error": value.error, "protocol": value.protocol,
            "host": {name: getattr(value.host, name) for name in fields}}


def report(path):
    """Queue an atomic JSON write. The destination's parent must exist."""
    destination = Path(path).expanduser().resolve()
    if not destination.parent.is_dir():
        raise ValueError("The report's parent directory must already exist")
    return bool(_plugin().HoudiniChineseRequestHookReport(str(destination)))
