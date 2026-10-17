"""静态扫描 Houdini 21 安装里的界面文案，跟词典比对，输出覆盖率和待补清单。

回答的问题：不用等用户截图，也能知道"哪些界面文案还没翻译"。

三个来源（置信度从高到低）：
  1. config/Applications/*.ui   Qt Designer 界面文件，<string> 就是要显示的文案
  2. python3.11libs/**/*.py      Python 面板脚本里的界面字符串（按调用点启发式提取）
  3. bin/*.dll                  编译进去的菜单/对话框字符串（启发式筛选，噪声最高）

用法：
    python tools/audit_ui_strings21.py
    python tools/audit_ui_strings21.py --hfs "D:\\DCC SoftWare\\Houdini 21.0.440" --out build/ui_audit
"""
import argparse
import io
import json
import os
import re
import sys
from collections import Counter, defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PACKED_DIR = os.path.join(ROOT, "source", "translations")
EXTRA_DIR = os.path.join(ROOT, "translations")
EXTRA_FILES = ("dictionary_zz_houdini21_zh.json",
               "dictionary_shelf_help21_zh.json",
               "dictionary_zzz_fixes_zh.json",
                   "dictionary_zzz_coverage_zh.json",
                   "dictionary_zzz_imported_zh.json")
DEFAULT_HFS = r"D:\DCC SoftWare\Houdini 21.0.440"

CJK = re.compile(r"[\u3400-\u4dbf\u4e00-\u9fff]")
# 看起来像界面文案的英文串：大写开头、有空格、长度合理、没有代码痕迹
UI_TEXT = re.compile(r"^[A-Z][A-Za-z0-9 ,.'\"/&%#+*()\-:;!?°]{3,300}$")
CODEISH = re.compile(r"(::|->|\$\{|\{[a-z]|%[-+ 0#]*\d*\*?[sdfgxX]|\\\\|\.(dll|exe|py|json)\b)")
TECHNICAL = re.compile(r"^(OpenGL|Vulkan|CUDA|OpenCL|Qt|Python|VEX|USD|Alembic|FBX|OBJ|HAPI|PDG|HDK|"
                       r"Windows|Linux|macOS|x86|AMD|Intel|NVIDIA)\b")
UI_CALLS = re.compile(r"(setText|setWindowTitle|setToolTip|setStatusTip|setPlaceholderText|"
                      r"addItem|addAction|addMenu|setTitle|displayMessage|setLabel|setMessage|"
                      r"setWhatsThis|setAccessibleName|ui\.setupUi)")
# Houdini .ui 里的界面定义语法，不是给人看的文案
UI_DSL = re.compile(r"\b(LAYOUT|JUSTIFY|HSTRETCH|VSTRETCH|ICON|TITLE|PARM|BUTTONS_|ICON_|"
                    r"LABEL_|SIZE|HELP|SPACING|ALIGN|EXPAND|MINIMUM|SWITCHER|SEPARATOR|DIALOG)\b")
CAMEL = re.compile(r"^[A-Z][a-z]+[A-Z][A-Za-z0-9]*$")


def strip_length_prefix(text):
    """二进制界面文件里，字符串前面常常紧跟一个长度字节，会被当成字母。"""
    if len(text) > 3 and text[0].isupper() and text[1].isupper() and text[2].islower():
        return text[1:]
    return text


def dictionary():
    merged = {}
    files = {}
    if os.path.isdir(PACKED_DIR):
        for name in os.listdir(PACKED_DIR):
            if name.endswith("_zh.json"):
                files[name] = os.path.join(PACKED_DIR, name)
    for name in EXTRA_FILES:
        path = os.path.join(EXTRA_DIR, name)
        if os.path.exists(path):
            files[name] = path
    for name in sorted(files, key=str.lower):
        document = json.load(io.open(files[name], encoding="utf-8-sig"))
        for key, value in (document.get("translations") or {}).items():
            if isinstance(value, str) and value.strip():
                merged[key] = value
    return merged


def normalize(text):
    text = text.replace("&", "").replace("\u2026", "...")
    text = re.sub(r"\s+", " ", text).strip()
    return text.rstrip(".:*").strip().lower()


def looks_ui(text):
    if not UI_TEXT.match(text):
        return False
    if CODEISH.search(text) or TECHNICAL.match(text) or UI_DSL.search(text):
        return False
    if text.endswith(":") and " " not in text:
        return False
    if len(text) >= 6 and len(set(text)) < 5:
        return False
    if " " not in text and CAMEL.match(text):
        return False
    if text.endswith(";"):
        return False
    letters = sum(1 for ch in text if ch.isalpha())
    return letters >= 4


def scan_ui_files(hfs):
    """Houdini 的 .ui 是自家二进制界面格式，直接抽可打印串，精度最高。"""
    found = Counter()
    folder = os.path.join(hfs, "houdini", "config")
    if not os.path.isdir(folder):
        return found
    for dirpath, _, names in os.walk(folder):
        for name in names:
            if not name.endswith((".ui", ".gad")):
                continue
            path = os.path.join(dirpath, name)
            try:
                if os.path.getsize(path) > 8 * 1024 * 1024:
                    continue
                data = open(path, "rb").read()
            except OSError:
                continue
            for match in re.finditer(rb"[\x20-\x7e]{4,300}", data):
                value = strip_length_prefix(match.group().decode("ascii", "ignore").strip())
                if looks_ui(value):
                    found[value] += 1
    return found


def scan_python(hfs):
    found = Counter()
    bases = [os.path.join(hfs, "houdini", "python3.11libs")]
    literal = re.compile(r"""[rbu]?(['"])((?:\\.|(?!\1).){3,160})\1""")
    for base in bases:
        for dirpath, _, names in os.walk(base):
            for name in names:
                if not name.endswith(".py"):
                    continue
                path = os.path.join(dirpath, name)
                try:
                    if os.path.getsize(path) > 4 * 1024 * 1024:
                        continue
                    text = io.open(path, encoding="utf-8", errors="ignore").read()
                except OSError:
                    continue
                for line in text.splitlines():
                    if not UI_CALLS.search(line):
                        continue
                    for match in literal.finditer(line):
                        value = match.group(2).strip()
                        if looks_ui(value):
                            found[value] += 1
    return found


BIN_FILES = ("libGU.dll", "libUI.dll", "libOPUI.dll", "libHOMUI.dll", "libFUSE.dll",
             "libJEDI.dll", "libRE.dll", "libMATUI.dll", "libSHLFUI.dll", "libQTUI.dll",
             "libSTORUI.dll", "libFUI.dll")


def scan_binaries(hfs, limit_files=None):
    found = Counter()
    folder = os.path.join(hfs, "bin")
    for name in BIN_FILES:
        path = os.path.join(folder, name)
        if not os.path.exists(path):
            continue
        try:
            data = open(path, "rb").read()
        except OSError:
            continue
        for match in re.finditer(rb"[\x20-\x7e]{4,300}", data):
            value = strip_length_prefix(match.group().decode("ascii", "ignore").strip())
            if looks_ui(value):
                found[value] += 1
        if limit_files and len(found) > limit_files:
            break
    return found


def scan_help_cache(prefs):
    """Houdini 的帮助缓存：节点/工具的工具提示正文都在这儿，最贴近实际显示。"""
    found = Counter()
    folders = [os.path.join(prefs, "config", "Help", "cache", "nodes", "out"),
               os.path.join(prefs, "config", "Help", "cache", "shelf")]
    names = []
    for folder in folders:
        if os.path.isdir(folder):
            names.extend(os.path.join(folder, n) for n in os.listdir(folder) if n.endswith(".json"))
    for path in names:
        try:
            data = json.load(io.open(path, encoding="utf-8"))
        except (OSError, ValueError):
            continue
        summary = data.get("summary")
        if isinstance(summary, list):
            text = "".join(str(part) for part in summary).strip()
            if len(text) >= 8:
                found[text] += 1
    return found

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--hfs", default=os.environ.get("HFS", DEFAULT_HFS))
    parser.add_argument("--out", default=os.path.join(ROOT, "build", "ui_audit"))
    parser.add_argument("--prefs", default=os.environ.get(
        "HOUDINI_USER_PREF_DIR",
        os.path.join(os.path.expanduser("~"), "Documents", "houdini21.0")))
    args = parser.parse_args()
    if not os.path.isdir(args.hfs):
        raise SystemExit("找不到 Houdini 安装目录：%s（用 --hfs 指定）" % args.hfs)
    os.makedirs(args.out, exist_ok=True)

    translations = dictionary()
    known = {normalize(key): key for key in translations}
    print("词典：%d 条" % len(translations))

    sources = {
        "ui_files": scan_ui_files(args.hfs),
        "python": scan_python(args.hfs),
        "help_cache": scan_help_cache(args.prefs),
        "binaries": scan_binaries(args.hfs),
    }

    report = []
    pending = []
    for name, found in sources.items():
        total = len(found)
        covered = [text for text in found if normalize(text) in known]
        missing = [text for text in found if normalize(text) not in known]
        missing.sort(key=lambda text: (-found[text], len(text)))
        report.append((name, total, len(covered), len(missing)))
        for text in missing:
            pending.append((name, found[text], text))

    lines = ["来源\t候选\t已覆盖\t未覆盖", ]
    for name, total, covered, missing in report:
        lines.append("%s\t%d\t%d\t%d" % (name, total, covered, missing))
    header = "\n".join(lines)
    print(header)

    tsv = os.path.join(args.out, "uncovered.tsv")
    with io.open(tsv, "w", encoding="utf-8") as handle:
        handle.write("source\thits\tenglish\n")
        for name, hits, text in pending:
            handle.write("%s\t%d\t%s\n" % (name, hits, text))

    table = {name: (total, covered, missing) for name, total, covered, missing in report}
    # 两档口径：界面文件与 Python 面板是高置信来源；二进制候选池噪声大，单独统计
    strict_names = ("ui_files", "python", "help_cache")
    strict_total = sum(table.get(name, (0, 0, 0))[0] for name in strict_names)
    strict_covered = sum(table.get(name, (0, 0, 0))[1] for name in strict_names)
    wide_total = sum(row[1] for row in report)
    wide_covered = sum(row[2] for row in report)
    summary = {
        "dictionary": len(translations),
        "sources": {name: {"total": t, "covered": c, "missing": m}
                    for name, t, c, m in report},
        "ui_coverage": round(100.0 * strict_covered / strict_total, 1) if strict_total else 0.0,
        "ui_candidates": strict_total,
        "ui_uncovered": strict_total - strict_covered,
        "wide_coverage": round(100.0 * wide_covered / wide_total, 1) if wide_total else 0.0,
    }
    io.open(os.path.join(args.out, "summary.json"), "w", encoding="utf-8").write(
        json.dumps(summary, ensure_ascii=False, indent=2) + "\n")
    print("\n界面文案覆盖率（.ui + Python）：%.1f%%（%d/%d），未覆盖 %d 条"
          % (summary["ui_coverage"], strict_covered, strict_total,
             summary["ui_uncovered"]))
    print("含二进制候选池的宽口径：%.1f%%（仅供参考，噪声大）" % summary["wide_coverage"])
    print("未覆盖清单：", tsv)
    print("\n注意：二进制候选池含内部字符串和不可达文案；界面文件与 Python 面板命中更接近"
          "真实 UI。最终仍以运行时 Shift+~ 抓取为准。")


if __name__ == "__main__":
    main()
