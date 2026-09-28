"""Verify the staged H21 engine covers the enumerated built-in shelf text."""
import ctypes
import json
from pathlib import Path

import hou


root = Path(__file__).resolve().parents[1]
inventory = json.loads((root / "build/runtime19/reports/shelf-help-inventory.json")
                       .read_text(encoding="utf-8"))
output = Path(hou.getenv("HOUDINI_CHINESE_LATE_LOOK_REPORT_DIR"))
engine = ctypes.WinDLL("HoudiniChineseEngine.dll")
translate = engine.HoudiniChineseTranslateLabel21
translate.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_wchar), ctypes.c_int]
translate.restype = ctypes.c_int


def tr(text):
    buffer = ctypes.create_unicode_buffer(32768)
    return buffer.value if translate(text, buffer, len(buffer)) else ""


labels = sorted({row["label"].strip() for row in inventory["tools"].values()
                 if row.get("label")})
helps = sorted({row["source"].strip()
                for row in inventory["help_by_url"].values()
                if row.get("source")})
missing_labels = [text for text in labels if not tr(text)]
missing_help = [text for text in helps if not tr(text)]
checks = {
    "Object Select Mode. Right-click for options.": "对象选择模式。单击右键查看选项。",
    "Bounding Source (input1)": "边界源 (input1)",
    "box1 (Box)\nBounding Source (input1)": "box1（长方体）\n边界源 (input1)",
    "Creates a DOPs collision object from deforming SOP Geometry.": "从正在变形的 SOP 几何体创建 DOP 碰撞对象。",
    "Control visibility of external objects": "控制外部对象的可见性",
    "Press F1 for more help.": "按 F1 查看详细帮助。",
}
check_results = {source: tr(source) for source in checks}
check_failures = {source: {"expected": expected,
                           "actual": check_results[source]}
                  for source, expected in checks.items()
                  if check_results[source] != expected}
result = {
    "label_count": len(labels),
    "help_count": len(helps),
    "missing_labels": missing_labels,
    "missing_help": missing_help,
    "checks": check_results,
    "check_failures": check_failures,
}
(output / "shelf-runtime-coverage.json").write_text(
    json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
print("SHELF_RUNTIME_COVERAGE", len(labels), len(missing_labels),
      len(helps), len(missing_help), len(check_failures), flush=True)
