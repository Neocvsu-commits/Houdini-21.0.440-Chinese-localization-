"""生成 Houdini 21 的 help 覆盖层：在每个参数描述前插入一行中文概括。

原理（借鉴 Stars401/Houdini_ZH_Tooltip）：
    Houdini 通过 bookish.OverlayStore 加载帮助；用户偏好目录下的
    help/nodes/<context>/<name>.txt 会覆盖安装目录 nodes.zip 里的同名文件。
    因此只要提供改了内容的 help txt，就能改变参数 tooltip，无需任何钩子。

数据来源（按顺序）：
    1. data/help_summaries_stars401.json  （MIT，署名 Stars401，已按 H21 原文校准）
    2. 本插件词典里已有的参数说明翻译（只取短句，避免把整段译文塞进提示）

用法：
    python tools/build_help_overlay21.py                  # 只生成到 build/help_overlay
    python tools/build_help_overlay21.py --install        # 生成并装到用户偏好目录
    python tools/build_help_overlay21.py --uninstall      # 删除覆盖层

覆盖层目录：
    %HOUDINI_USER_PREF_DIR%\\help\\nodes\\   （默认 Documents\\houdini21.0\\help\\nodes）
"""
import argparse
import io
import json
import os
import re
import shutil
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SUMMARIES = os.path.join(ROOT, "data", "help_summaries_stars401.json")
OUT_DIR = os.path.join(ROOT, "build", "help_overlay")
PREF_DIR = os.path.join(os.path.expanduser("~"), "Documents", "houdini21.0")
DEFAULT_ZIP = r"D:\DCC SoftWare\Houdini 21.0.440\houdini\help\nodes.zip"

STYLE = '<span style="color: #FFD700; font-weight: bold;">%s</span>'
SUMMARY_LINE = re.compile(r"^\s*【.+】\s*$")
# 参数描述行：缩进较深、大写开头、不是标题/标签
DESCRIPTION = re.compile(r"^(\s{8,})([A-Z][^\s【].{7,})$")
MAX_FALLBACK_CHARS = 80


def normalize(text):
    text = re.sub(r"\s+", " ", text).strip()
    return text.rstrip(":").strip()


def compact(text, limit=120):
    """去掉所有空白和标点差异，便于跨版本、跨换行匹配。"""
    text = re.sub(r"\s+", "", text or "").lower()
    return text[:limit]


class SummaryIndex:
    """整句 + 前缀匹配，容忍 21/22 版换行差异；命中前做前缀互验，避免误配。"""

    PREFIX = 32
    VERIFY = 28

    def __init__(self, mapping):
        self.exact = {}
        self.prefix = {}
        for key, value in mapping.items():
            normal = normalize(key)
            self.exact.setdefault(normal, value)
            # 有些键把下一个参数名也带进来了：按首句再建一条
            head = re.split(r"(?<=\.)\s+", normal, maxsplit=1)[0]
            if head != normal and len(head) >= 12:
                self.exact.setdefault(normalize(head), value)
            mark = compact(normal)
            if len(mark) >= self.VERIFY:
                self.prefix.setdefault(mark[:self.PREFIX], []).append((mark, value))

    def get(self, text):
        normal = normalize(text)
        if normal in self.exact:
            return self.exact[normal]
        mark = compact(normal)
        candidates = self.prefix.get(mark[:self.PREFIX])
        if not candidates:
            return ""
        for full, value in candidates:
            if mark.startswith(full[:self.VERIFY]) or full.startswith(mark[:self.VERIFY]):
                return value
        return ""


def load_summaries():
    if not os.path.exists(SUMMARIES):
        return {}
    data = json.load(io.open(SUMMARIES, encoding="utf-8"))
    return {normalize(k): v.strip() for k, v in data.items() if k and v}


def load_dictionary():
    """本插件词典：英文描述 -> 中文（只用于兜底）。"""
    merged = {}
    files = {}
    packed = os.path.join(ROOT, "source", "translations")
    if os.path.isdir(packed):
        for name in os.listdir(packed):
            if name.endswith("_zh.json"):
                files[name] = os.path.join(packed, name)
    # 仓库 translations/ 只采纳本机覆盖类词典
    extra = os.path.join(ROOT, "translations")
    if os.path.isdir(extra):
        for name in os.listdir(extra):
            if name.endswith("_zh.json") and name.startswith(("dictionary_zz", "dictionary_zzz")):
                files[name] = os.path.join(extra, name)
    for name in sorted(files, key=str.lower):
        document = json.load(io.open(files[name], encoding="utf-8-sig"))
        for key, value in (document.get("translations") or {}).items():
            if isinstance(value, str) and value.strip():
                merged[normalize(key)] = value.strip()
    return merged


def fallback_chinese(text, dictionary):
    """词典兜底：只接受短译文（太长就不塞进提示，避免中文重复整段）。"""
    chinese = dictionary.get(normalize(text))
    if not chinese:
        return ""
    chinese = re.sub(r"\s+", " ", chinese).strip()
    if len(chinese) <= MAX_FALLBACK_CHARS:
        return chinese
    first = re.split(r"[。；;]", chinese)[0].strip()
    return first if len(first) <= MAX_FALLBACK_CHARS else ""


def lookup(description, summaries, dictionary):
    chinese = summaries.get(description)
    if chinese:
        return chinese, "summary"
    chinese = fallback_chinese(description, dictionary)
    if chinese:
        return chinese, "dictionary"
    return "", ""


def process(text, summaries, dictionary, stats):
    lines = text.splitlines()
    output = []
    index = 0
    while index < len(lines):
        line = lines[index]
        match = DESCRIPTION.match(line)
        if match and not (index > 0 and SUMMARY_LINE.match(lines[index - 1])):
            indent = match.group(1)
            description = match.group(2).strip()
            if description.endswith(":"):
                output.append(line)
                index += 1
                continue
            chinese, source = lookup(description, summaries, dictionary)
            # 多行描述：尝试把后续行拼起来再查一次
            if not chinese:
                joined = description
                cursor = index + 1
                while cursor < len(lines) and cursor - index <= 3:
                    follow = lines[cursor].strip()
                    if not follow or follow.endswith(":"):
                        break
                    joined = joined + " " + follow
                    chinese, source = lookup(joined, summaries, dictionary)
                    if chinese:
                        break
                    cursor += 1
            if chinese:
                output.append(indent + "【" + (STYLE % chinese) + "】")
                stats[source] = stats.get(source, 0) + 1
        output.append(line)
        index += 1
    return "\n".join(output) + "\n"


def find_zip(hfs):
    if hfs:
        candidate = os.path.join(hfs, "houdini", "help", "nodes.zip")
        if os.path.exists(candidate):
            return candidate
    env = os.environ.get("HFS")
    if env:
        candidate = os.path.join(env, "houdini", "help", "nodes.zip")
        if os.path.exists(candidate):
            return candidate
    return DEFAULT_ZIP


def build(zip_path, out_dir, summaries, dictionary):
    stats = {}
    files = 0
    inserted_total = 0
    failed = []
    if os.path.isdir(out_dir):
        shutil.rmtree(out_dir)
    os.makedirs(out_dir, exist_ok=True)
    with zipfile.ZipFile(zip_path) as archive:
        for info in archive.infolist():
            if not info.filename.endswith(".txt"):
                continue
            raw = archive.read(info).decode("utf-8", "ignore")
            before = raw.count("【")
            processed = process(raw, summaries, dictionary, stats)
            after = processed.count("【")
            inserted = after - before
            files += 1
            inserted_total += max(0, inserted)
            if inserted <= 0:
                continue
            # 只做基本结构校验：非空且不是二进制/乱码内容
            if not processed.strip() or "\x00" in processed:
                failed.append(info.filename)
                continue
            target = os.path.join(out_dir, info.filename)
            os.makedirs(os.path.dirname(target), exist_ok=True)
            io.open(target, "w", encoding="utf-8", newline="\n").write(processed)
    return files, inserted_total, stats, failed


def pref_help_dir():
    custom = os.environ.get("HOUDINI_USER_PREF_DIR")
    base = custom if custom else PREF_DIR
    return os.path.join(base, "help", "nodes")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--hfs", default=None, help="Houdini 安装根目录（默认读 HFS 或内置路径）")
    parser.add_argument("--zip", default=None, help="直接指定 nodes.zip")
    parser.add_argument("--out", default=OUT_DIR, help="生成目录")
    parser.add_argument("--install", action="store_true", help="装到用户偏好目录")
    parser.add_argument("--uninstall", action="store_true", help="删除覆盖层")
    parser.add_argument("--limit", type=int, default=0, help="只处理前 N 个文件（调试用）")
    args = parser.parse_args()

    target_dir = pref_help_dir()
    if args.uninstall:
        if os.path.isdir(target_dir):
            shutil.rmtree(target_dir)
            print("已删除覆盖层:", target_dir)
        else:
            print("没有覆盖层:", target_dir)
        return

    zip_path = args.zip or find_zip(args.hfs)
    if not os.path.exists(zip_path):
        raise SystemExit("找不到 nodes.zip：%s（用 --zip 指定）" % zip_path)

    raw_summaries = load_summaries()
    summaries = SummaryIndex(raw_summaries)
    dictionary = load_dictionary()
    print("摘要库 %d 条，词典 %d 条" % (len(raw_summaries), len(dictionary)))

    files, inserted, stats, failed = build(zip_path, args.out, summaries, dictionary)
    print("处理帮助文件: %d，插入中文概括: %d" % (files, inserted))
    print("来源: 摘要库 %d，词典兜底 %d" % (stats.get("summary", 0), stats.get("dictionary", 0)))
    if failed:
        print("结构校验失败（未写出）:", len(failed), failed[:5])
    print("生成目录:", args.out)

    if args.install:
        if os.path.isdir(target_dir):
            backup = target_dir + ".bak"
            if os.path.isdir(backup):
                shutil.rmtree(backup)
            os.rename(target_dir, backup)
            print("已有覆盖层已备份到:", backup)
        os.makedirs(os.path.dirname(target_dir), exist_ok=True)
        shutil.copytree(args.out, target_dir)
        print("已安装到:", target_dir)
        print("重启 Houdini 后生效；卸载：python tools/build_help_overlay21.py --uninstall")


if __name__ == "__main__":
    main()
