"""Inventory every installed shelf tool in an isolated Houdini 21 GUI.

The script first switches the overlay off to read the original H21 help text,
then switches it back on and asks the already loaded engine which labels and
summaries are covered. Work is spread across timer ticks so the GUI stays live.
"""
import ctypes
import json
from pathlib import Path
import xml.etree.ElementTree as ET

import hou
from PySide6 import QtCore, QtWidgets


root = Path(__file__).resolve().parents[1]
output = Path(hou.getenv("HOUDINI_CHINESE_LATE_LOOK_REPORT_DIR"))
helper = ctypes.WinDLL(str(root / "build/tests/help_probe21.dll"))
helper.readTooltip21.argtypes = [ctypes.c_char_p,
                                 ctypes.POINTER(ctypes.c_wchar), ctypes.c_int]
helper.readTooltip21.restype = ctypes.c_int
engine = ctypes.WinDLL("HoudiniChineseEngine.dll")
translate = engine.HoudiniChineseTranslateLabel21
translate.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_wchar), ctypes.c_int]
translate.restype = ctypes.c_int


def tr(text):
    buffer = ctypes.create_unicode_buffer(16384)
    return buffer.value if translate(text, buffer, len(buffer)) else ""


def original_help(url):
    buffer = ctypes.create_unicode_buffer(32768)
    return buffer.value if helper.readTooltip21(url.encode("utf-8"), buffer,
                                                len(buffer)) else ""


app = QtWidgets.QApplication.instance()
button = next(widget for widget in app.allWidgets()
              if widget.objectName() == "houdini_chinese_language_button")
if tr("File"):
    button.click()

toolbar = Path(hou.getenv("HFS")) / "houdini" / "toolbar"
target_tool_names = set()
for filename in ("ShelfDefinitions.shelf", "ShelfDefinitions.master_shelf"):
    document = ET.parse(toolbar / filename).getroot()
    for shelf in document.findall(".//toolshelf"):
        target_tool_names.update(member.get("name")
                                 for member in shelf.findall("memberTool"))

all_tools = hou.shelves.tools()
tools = {name: all_tools[name] for name in sorted(target_tool_names)
         if name in all_tools}
rows = {
    name: {
        "label": tool.label(),
        "help_url": tool.helpURL(),
    }
    for name, tool in tools.items()
}
urls = sorted({row["help_url"] for row in rows.values() if row["help_url"]})
help_by_url = {}
index = 0


def write_progress():
    (output / "shelf-help-inventory-progress.json").write_text(
        json.dumps({
            "tool_count": len(rows),
            "unique_help_url_count": len(urls),
            "completed_help_url_count": index,
        }, ensure_ascii=False, indent=2), encoding="utf-8")


def finish():
    button.click()
    missing_labels = {}
    for name, row in rows.items():
        translated = tr(row["label"])
        row["label_zh"] = translated
        if row["label"] and not translated:
            missing_labels[name] = row["label"]
        source = help_by_url.get(row["help_url"], "")
        row["help"] = source
        row["help_zh"] = tr(source) if source else ""
    unique_help = {url: {
        "source": source,
        "translation": tr(source) if source else "",
    } for url, source in help_by_url.items()}
    missing_help = {url: row["source"] for url, row in unique_help.items()
                    if row["source"] and not row["translation"]}
    try:
        shelf_labels = {name: shelf.label()
                        for name, shelf in hou.shelves.shelves().items()}
    except Exception:
        shelf_labels = {}
    result = {
        "tool_count": len(rows),
        "unique_help_url_count": len(urls),
        "nonempty_help_count": sum(bool(value) for value in help_by_url.values()),
        "missing_label_count": len(missing_labels),
        "missing_help_count": len(missing_help),
        "shelf_labels": shelf_labels,
        "missing_labels": missing_labels,
        "missing_help": missing_help,
        "help_by_url": unique_help,
        "tools": rows,
    }
    (output / "shelf-help-inventory.json").write_text(
        json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print("SHELF_HELP_INVENTORY_DONE", len(rows), len(urls),
          len(missing_labels), len(missing_help), flush=True)


def consume_batch():
    global index
    stop = min(index + 5, len(urls))
    while index < stop:
        url = urls[index]
        help_by_url[url] = original_help(url)
        index += 1
    write_progress()
    if index < len(urls):
        QtCore.QTimer.singleShot(0, consume_batch)
    else:
        QtCore.QTimer.singleShot(0, finish)


QtCore.QTimer.singleShot(0, consume_batch)
