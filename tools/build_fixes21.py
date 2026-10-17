"""生成术语/空格/语义修订词典 dictionary_zzz_fixes_zh.json。

规则
- Texture -> 纹理，Map -> 贴图（只改独立成词的结尾术语，不动复合词）
- 三维 -> 3D（只改以"三维"开头的短标签）
- 物体 -> 对象（全库统一）
- 汉字与拉丁字母之间补空格（只对单行标签，跳过格式串与代码前缀）
- 逐条定稿改写与语义勘误见下方表

产物按文件名排序最后加载，覆盖此前所有词典；原 7 份不改动。
"""
import io
import json
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "translations", "dictionary_zzz_fixes_zh.json")
REPORT = os.path.join(ROOT, "build", "dictionary_fixes.tsv")
EXTRA = ("dictionary_zz_houdini21_zh.json",
         "dictionary_shelf_help21_zh.json")

CJK = r"\u4e00-\u9fff"
SEP = r"[\s（()）：:、，,/]"
FORMAT_SPEC = re.compile(r"%[-+ 0#]*\d*\*?[sdfgxXeEc]")
CODE_PREFIX = re.compile(r"^[%$@]|^[A-Za-z_][A-Za-z0-9_]*:")

REWRITES = {
    "A scale factor on the length of axes when placed inside the rotate sphere.":
        "缩放系数，作用于旋转球内坐标轴的长度。",
    "A scale factor on the length of axes when placed outside the rotate sphere.":
        "缩放系数，作用于旋转球外坐标轴的长度。",
    "A very compact UI scheme for low DPI monitors or maximizing usable space.":
        "紧凑界面方案，适合低 DPI 显示器或想最大化可用空间时使用。",
    "A slightly reduced UI scheme, cutting down on margins, spacing and icon size.":
        "略微精简的界面方案，缩小边距、间距和图标尺寸。",
    "Allow Paths along Collision Group Boundary": "允许路径沿碰撞组边界走",
    "Avoid Packing Over Non-Group Islands": "避免在非组岛上打包",
    "Assume Morph Network Setup": "假定形变网络已就绪",
    "Minimum Photon Diffuse Level": "最小光子漫反射层级",
    "Based on Y and Z Axes (below)": "基于 Y 和 Z 轴（见下）",
    "Premultiply Input by Density": "输入按密度预乘",
    "Configure Ragdoll Attributes Per Joint": "逐关节配置布娃娃属性",
    "Geometry Stash Max Concavity": "几何体暂存最大凹陷度",
    "Error Reduction": "误差衰减",
    "At Most One Surface Face per Tet": "每个四面体至多一个表面面片",
}

SEMANTIC = {
    "1 Float Default": "1 个浮点默认值",
    "2 Float Default": "2 个浮点默认值",
    "3 Float Default": "3 个浮点默认值",
    "Domain Error Value": "模拟域误差值",
    "Divide Error Value": "除法误差值",
}

# 原词典里没有、由覆盖率审计与截图发现的新增词条
ADDITIONS = {
    "Shift+left-drag to mirror drag on opposite bound. Ctrl+left-drag to uniformly drag corresponding bounds.":
        "Shift+左键拖拽可镜像拖动对侧边界。Ctrl+左键拖拽可同步拖拽对应边界。",
    "Shift-click to add to existing filters": "Shift+点击可加入现有过滤器",
    "Ctrl-Click to reset all simulations": "Ctrl+点击可重置所有模拟",
    "Shift-Click to reset out of date simulations.": "Shift+点击可重置过期模拟。",
    "Click to reset to 1.": "点击重置为 1。",
    "Click to reset to 0.": "点击重置为 0。",
    "Select output file:": "选择输出文件：",
    "Select COP Color": "选择 COP 颜色",
    "Select From All Objects": "从所有对象中选择",
    "Select HUD Color": "选择 HUD 颜色",
    "Shelf tools use this for their target.": "工具架工具会以此作为目标。",
    "The playbar shows the current simulation's cache.": "播放栏显示当前模拟的缓存。",
    "Right toolbar buttons affect this display set.": "右侧工具栏按钮影响此显示集。",
    "Note: Scene Object sets are only affected when the viewer is at the object level":
        "注意：仅当查看器位于对象层级时，场景对象集才受影响",
    "Sets the frame rate we use to interpret the video file.":
        "设置解读视频文件时使用的帧率。",
    "Which objects this light will illuminate.": "该灯光将照亮的对象。",
}


def load_packaged():
    files = {}
    packed = os.path.join(ROOT, "source", "translations")
    if os.path.isdir(packed):
        for name in os.listdir(packed):
            if name.endswith("_zh.json"):
                files[name] = os.path.join(packed, name)
    for name in EXTRA:
        path = os.path.join(ROOT, "translations", name)
        if os.path.exists(path):
            files[name] = path
    merged = {}
    for name in sorted(files, key=str.lower):
        document = json.load(io.open(files[name], encoding="utf-8-sig"))
        for key, value in (document.get("translations") or {}).items():
            if isinstance(value, str) and value.strip():
                merged[key] = value
    return merged


def is_label(value):
    return ("\n" not in value and len(value) <= 16 and "。" not in value
            and not FORMAT_SPEC.search(value))


def safe_for_spacing(value):
    if FORMAT_SPEC.search(value) or CODE_PREFIX.match(value):
        return False
    if any(mark in value for mark in ("$", "{", "}", "\\n", "\\t")):
        return False
    return True


def spaced(value):
    value = re.sub(rf"([{CJK}])([A-Za-z])", r"\1 \2", value)
    value = re.sub(rf"([A-Za-z])([{CJK}])", r"\1 \2", value)
    return value


def split_ws(value):
    match = re.match(r"^(\s*)(.*?)(\s*)$", value, re.S)
    return match.group(1), match.group(2), match.group(3)


def unify_texture(key, value):
    lower = key.lower()
    is_map = re.search(r"\bmap\b", lower) is not None
    is_texture = re.search(r"\btexture\b", lower) is not None
    if is_map:
        value = re.sub(rf"(^|{SEP})纹理$", r"\g<1>贴图", value)
    elif is_texture:
        value = re.sub(rf"(^|{SEP})贴图$", r"\g<1>纹理", value)
    return value


def transform(key, value):
    if key in REWRITES:
        return REWRITES[key]
    if key in SEMANTIC:
        return SEMANTIC[key]
    label = is_label(value)
    head, core, tail = split_ws(value)
    if label:
        core = unify_texture(key, core)
        if core.startswith("三维"):
            core = "3D " + core[2:]
        core = re.sub(rf"([{CJK}])3D", r"\1 3D", core)
        core = re.sub(r"3D(?=[{CJK}])", "3D ", core)
    core = core.replace("物体", "对象").replace("对象对象", "对象")
    if label and safe_for_spacing(core):
        core = spaced(core)
    result = head + core + tail
    return result if result != value else None


def conflicts(mapping):
    seen = {}
    for source, value in mapping.items():
        seen.setdefault(value, set()).add(source)
    return {value: sources for value, sources in seen.items() if len(sources) > 1}


def main():
    packaged = load_packaged()
    changed = {}
    for key, value in packaged.items():
        new = transform(key, value)
        if new is not None:
            changed[key] = new
    added = 0
    for key, value in ADDITIONS.items():
        if key not in packaged:
            changed[key] = value
            added += 1
    before = conflicts(packaged)
    after = conflicts({**packaged, **changed})
    print("词典 %d 条，改动 %d 条（其中新增 %d 条）" % (len(packaged), len(changed), added))
    print("中文重复值（英文模式还原不了的那部分）：%d -> %d" % (len(before), len(after)))

    document = {
        "$schema": "sp-translation-v1",
        "id": "dictionary-zzz-fixes-zh",
        "language": "zh-CN",
        "description": "术语统一（纹理/贴图按原文区分，三维→3D，物体→对象）、中英空格、语义勘误与定稿改写。按文件名排序最后加载，覆盖旧词典。",
        "translations": dict(sorted(changed.items())),
    }
    io.open(OUT, "w", encoding="utf-8").write(
        json.dumps(document, ensure_ascii=False, indent=1) + "\n")

    os.makedirs(os.path.dirname(REPORT), exist_ok=True)
    with io.open(REPORT, "w", encoding="utf-8") as handle:
        handle.write("english\tbefore\tafter\n")
        for key in sorted(changed):
            handle.write("%s\t%s\t%s\n" % (key, packaged.get(key, ""), changed[key]))
    print("写出 %s（%d 字节）" % (OUT, os.path.getsize(OUT)))
    print("清单 %s" % REPORT)


if __name__ == "__main__":
    main()
