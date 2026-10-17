"""生成节点搜索用的两份数据：中文标签映射与汉字拼音表。

用法（在仓库根目录）：
    python tools/build_node_labels21.py

需要：
- 本机装有 Houdini 21（用来枚举节点类型）；可用 HFS 环境变量指定安装根目录
- 开发机装了 pypinyin（只在生成阶段需要，运行时不需要）
"""
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LIB = ROOT / "python3.11libs"
# 真正进包的 7 份词典：作者的五份在 source/translations，
# 本机补充的两份在仓库 translations/。顺序由 prepare21.py 决定。
TRANSLATION_DIRS = (ROOT / "source" / "translations", ROOT / "translations")
TRANSLATION_EXTRA = ("dictionary_zz_houdini21_zh.json",
                     "dictionary_shelf_help21_zh.json",
                     "dictionary_zzz_fixes_zh.json",
                   "dictionary_zzz_coverage_zh.json",
                   "dictionary_zzz_imported_zh.json")
DUMP = ROOT / "build" / "nodes21.json"

DUMP_SCRIPT = """
import json, hou
out = {}
for category_name, category in sorted(hou.nodeTypeCategories().items()):
    rows = []
    try:
        node_types = category.nodeTypes()
    except Exception:
        continue
    for name, node_type in sorted(node_types.items()):
        try:
            description = node_type.description()
        except Exception:
            description = ""
        rows.append([name, description])
    out[category_name] = rows
with open(r"__DUMP_PATH__", "w", encoding="utf-8") as handle:
    json.dump(out, handle, ensure_ascii=False)
print("node types:", sum(len(v) for v in out.values()))
"""


def find_hython():
    hfs = os.environ.get("HFS") or os.environ.get("HOUDINI_ROOT")
    candidates = []
    if hfs:
        candidates.append(Path(hfs) / "bin" / "hython.exe")
    candidates.append(Path(r"D:\DCC SoftWare\Houdini 21.0.440\bin\hython.exe"))
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return None


def dump_node_types():
    if DUMP.exists():
        return json.loads(DUMP.read_text(encoding="utf-8"))
    hython = find_hython()
    if hython is None:
        raise SystemExit("找不到 hython，请设置 HFS 指向 Houdini 安装目录")
    DUMP.parent.mkdir(parents=True, exist_ok=True)
    script = DUMP_SCRIPT.replace("__DUMP_PATH__", str(DUMP))
    subprocess.run([str(hython), "-c", script], check=True)
    return json.loads(DUMP.read_text(encoding="utf-8"))


def translation_files():
    files = {}
    packed = ROOT / "source" / "translations"
    if packed.is_dir():
        for path in packed.glob("*_zh.json"):
            files[path.name] = path
    for name in TRANSLATION_EXTRA:
        path = ROOT / "translations" / name
        if path.exists():
            files[path.name] = path
    # 与引擎一致：文件名升序，后者覆盖前者
    return [files[name] for name in sorted(files, key=str.lower)]


def load_translations():
    """按引擎的加载顺序（文件名升序，后者覆盖）合并进包的词典。"""
    merged = {}
    paths = translation_files()
    for path in paths:
        document = json.loads(path.read_text(encoding="utf-8-sig"))
        for key, value in (document.get("translations") or {}).items():
            if isinstance(value, str) and value.strip():
                merged[key] = value
    print("词典：%d 份，%d 条" % (len(paths), len(merged)))
    return merged


def build_label_map(nodes, translations):
    labels = {}
    for rows in nodes.values():
        for _name, description in rows:
            if description and description in translations:
                labels[description] = translations[description]
    out = LIB / "node_label_zh.json"
    out.write_text(json.dumps(labels, ensure_ascii=False, sort_keys=True), encoding="utf-8")
    missing = sorted({d for rows in nodes.values() for _n, d in rows
                      if d and d not in translations})
    print("节点标签：%d 条有译文，%d 个英文描述无译文 -> %s"
          % (len(labels), len(missing), out.name))
    return labels


def build_pinyin_table(translations):
    try:
        from pypinyin import Style, pinyin
    except ImportError:
        raise SystemExit("生成拼音表需要 pypinyin：pip install pypinyin")
    import re

    cjk = re.compile(r"[\u3400-\u4dbf\u4e00-\u9fff]")
    chars = set()
    for value in translations.values():
        chars.update(cjk.findall(value))
    table = {}
    for char in sorted(chars):
        readings = []
        for group in pinyin(char, style=Style.NORMAL, heteronym=True):
            for reading in group:
                if reading and reading not in readings:
                    readings.append(reading)
        if readings:
            table[char] = readings[:3]
    out = LIB / "pinyin_table.json"
    out.write_text(json.dumps(table, ensure_ascii=False, sort_keys=True,
                              separators=(",", ":")), encoding="utf-8")
    print("拼音表：%d 个汉字（含 %d 个多音字）-> %s"
          % (len(table), sum(1 for v in table.values() if len(v) > 1), out.name))


def main():
    nodes = dump_node_types()
    translations = load_translations()
    if not translations:
        raise SystemExit("translations 目录里没有 _zh.json 词典")
    build_label_map(nodes, translations)
    build_pinyin_table(translations)
    print("完成。把 python3.11libs 下的三份文件一起打包即可。")


if __name__ == "__main__":
    main()
