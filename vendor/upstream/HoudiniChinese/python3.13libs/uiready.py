# Houdini executes startup scripts without a reliable __file__.
def _hc_startup_entry():
    import hou
    import sys
    if sys.platform != "win32" or not hou.isUIAvailable():
        return
    import houdini_chinese_startup
    houdini_chinese_startup.ui_ready()
_hc_startup_entry()
del _hc_startup_entry
