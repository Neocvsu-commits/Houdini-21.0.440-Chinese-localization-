"""Run only in an isolated test Houdini profile."""
import ctypes
import json
from pathlib import Path
import hou
from PySide6 import QtCore, QtWidgets

root = Path(__file__).resolve().parents[1]
output = Path(hou.getenv('HOUDINI_CHINESE_LATE_LOOK_REPORT_DIR'))
hou.hipFile.setName('HC21-interactions-test.hip')
exec(compile((root/'tests/runtime_probe.py').read_bytes(), 'runtime_probe.py', 'exec'))
engine = ctypes.WinDLL('HoudiniChineseEngine.dll')
translate = engine.HoudiniChineseTranslateLabel21
translate.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_wchar), ctypes.c_int]
translate.restype = ctypes.c_int

def tr(text):
    buffer = ctypes.create_unicode_buffer(8192)
    translate(text, buffer, len(buffer))
    return buffer.value

entries = json.loads((root/'translations/dictionary_zz_houdini21_zh.json').read_text(encoding='utf-8'))['translations']
results = {s: tr(s) for s in entries}
failures = {s: [entries[s], t] for s, t in results.items() if entries[s] != t}
for source, expected in {
    'Output 1 (output1)': '输出 1 (output1)',
    'box_object1 (Geometry)\nOutput 1 (output1)': 'box_object1（几何体）\n输出 1 (output1)',
    '<p>Constrain a selection of points to animated target positions.</p>': '<p>将所选点约束到随动画变化的目标位置。</p>',
}.items():
    actual = tr(source)
    results[source] = actual
    if actual != expected:
        failures[source] = [expected, actual]
(output/'translations.json').write_text(json.dumps(dict(results=results, failures=failures), ensure_ascii=False, indent=2), encoding='utf-8')
print('INTERACTION_LOOKUP', len(results), 'FAILURES', failures, flush=True)
tools = {name: dict(label=tool.label(), help=tool.help(), helpUrl=tool.helpURL())
         for name, tool in hou.shelves.tools().items()}
(output/'shelf-tools.json').write_text(json.dumps(tools, ensure_ascii=False, indent=2), encoding='utf-8')

# A bounded, test-only command file allows further application API probes in
# this process without touching the user's running Houdini or preferences.
def poll():
    path = output/'probe-command.py'
    if path.exists():
        command = path.read_bytes()
        path.unlink()
        try:
            exec(compile(command, str(path), 'exec'), globals())
        except Exception:
            import traceback
            traceback.print_exc()
timer = QtCore.QTimer(QtWidgets.QApplication.instance())
timer.timeout.connect(poll)
timer.start(1000)
