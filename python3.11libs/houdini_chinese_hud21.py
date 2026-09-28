"""Translate H21's cached HUD header only while it is rendered to a texture."""
import ctypes
import gc
import weakref

_installed = False
_filter = None


def install():
    global _installed, _filter
    if _installed:
        return
    import hou
    if hou.applicationVersion() != (21, 0, 440):
        return
    import hudinfohandler
    from PySide6 import QtCore, QtWidgets

    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.GetModuleHandleW.argtypes = [ctypes.c_wchar_p]
    kernel.GetModuleHandleW.restype = ctypes.c_void_p
    handle = kernel.GetModuleHandleW('HoudiniChineseEngine.dll')
    if not handle:
        return  # Never load a new DLL here or bypass the loader's ABI check.
    engine = ctypes.WinDLL('HoudiniChineseEngine.dll', handle=handle)
    translate = engine.HoudiniChineseTranslateLabel21
    translate.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_wchar), ctypes.c_int]
    translate.restype = ctypes.c_int
    restore = engine.HoudiniChineseRestoreLabel21
    restore.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_wchar), ctypes.c_int]
    restore.restype = ctypes.c_int
    import houdini_chinese_tooltips21
    houdini_chinese_tooltips21.install(translate, restore)
    handler_type = hudinfohandler.HUDInfoHandler
    handlers = weakref.WeakSet(obj for obj in gc.get_objects() if type(obj) is handler_type)
    original = handler_type.updateOverlay

    def update_overlay(self, *args, **kwargs):
        handlers.add(self)
        label = self._hud.toolInfo()._titlelabel
        source = label.text()
        # Standard template headers are bold markup. Plain custom titles and
        # the separate node-name/value controls stay untouched.
        buffer = ctypes.create_unicode_buffer(8192)
        changed = source.startswith('<b>') and translate(source, buffer, len(buffer))
        if not changed:
            return original(self, *args, **kwargs)
        blocker = QtCore.QSignalBlocker(label)
        try:
            label.setText(buffer.value)
            return original(self, *args, **kwargs)
        finally:
            label.setText(source)
            del blocker

    def refresh():
        for handler in list(handlers):
            try:
                panel = handler._hud.toolInfo()
                # H21's bundled _refreshHUD refers to a removed _panel_layout
                # member; its ToolInfoPanel stores the enum in _paneltype.
                handler._hud.sceneViewer().hudInfo(
                    values={'__sidefx_force_update__': 1}, panel=panel._paneltype)
            except (RuntimeError, hou.Error):
                # A viewer can disappear between queuing and delivering refresh.
                handlers.discard(handler)

    class LanguageFilter(QtCore.QObject):
        def eventFilter(self, obj, event):
            if (event.type() == QtCore.QEvent.DynamicPropertyChange
                    and event.propertyName() == b'hc21_enabled'):
                QtCore.QTimer.singleShot(0, refresh)
            return False

    app = QtWidgets.QApplication.instance()
    _filter = LanguageFilter(app)
    app.installEventFilter(_filter)
    handler_type.updateOverlay = update_overlay
    _installed = True
    QtCore.QTimer.singleShot(0, refresh)
