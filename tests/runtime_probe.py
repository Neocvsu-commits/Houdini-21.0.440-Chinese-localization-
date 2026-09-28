import ctypes
import json
from pathlib import Path
import hou
from PySide6 import QtCore, QtWidgets

output = Path(hou.getenv('HOUDINI_CHINESE_LATE_LOOK_REPORT_DIR'))
geo = hou.node('/obj').createNode('geo', 'hc21_test')
box = geo.createNode('box', 'Save')
box.parm('tx').setExpression('$F*0.1', hou.exprLanguage.Hscript)
box.setDisplayFlag(True)
box.setRenderFlag(True)
box.setCurrent(True, clear_all_selected=True)
editor = hou.ui.paneTabOfType(hou.paneTabType.NetworkEditor)
editor.setPwd(geo)
editor.homeToSelection()

def verify():
    assert box.name() == 'Save'
    assert box.parm('tx').expression() == '$F*0.1'
    assert len(box.geometry().points()) == 8
    lib = ctypes.WinDLL('HoudiniChineseHook.dll')
    status = (ctypes.c_uint32 * 15)()
    lib.HoudiniChineseGetLoaderStatus(status, ctypes.sizeof(status))
    data = dict(version=hou.applicationVersionString(), qt=QtCore.qVersion(),
                node=box.path(), expression=box.parm('tx').expression(),
                points=len(box.geometry().points()), loader=list(status),
                user_pref=hou.getenv('HOUDINI_USER_PREF_DIR'))
    data['buttons'] = [dict(name=w.objectName(), text=w.text(),
                           width=w.width(), height=w.height(),
                           hint=(w.sizeHint().width(), w.sizeHint().height()),
                           font=w.font().toString(), style=str(w.toolButtonStyle()))
                       for w in QtWidgets.QApplication.allWidgets()
                       if isinstance(w, QtWidgets.QToolButton)
                       and w.objectName().startswith('houdini_chinese_')]
    (output / 'scene-check.json').write_text(json.dumps(data, indent=2), encoding='utf-8')
    print('HC21 scene/input-data checks passed', data)

verify()
