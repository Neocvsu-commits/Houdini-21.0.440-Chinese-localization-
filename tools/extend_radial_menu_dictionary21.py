"""Add and audit Houdini 21 radial-menu labels."""

from __future__ import annotations

import argparse
import json
import os
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SUPPLEMENT = ROOT / "translations" / "dictionary_zz_houdini21_zh.json"


TRANSLATIONS = {
    # Radial menu editor and manager.
    "Radial Menu": "径向菜单",
    "Radial Menu: ": "径向菜单：",
    "Radial Menus": "径向菜单",
    "Radial Set": "径向集",
    "Menu Name": "菜单名称",
    "Current Set": "当前径向集",
    "Current Menu": "当前径向菜单",
    "Check On/Off": "开关状态检查",
    # Factory radial-menu files.
    "Add Whitewater": "添加白水泡沫",
    "Avoid Obstacle": "避开障碍物",
    "Behaviour": "行为",
    "Clear Layer": "清除图层",
    "Clump Hair": "毛发成簇",
    "Component Display": "组件显示",
    "Contained Geometry": "包含的几何体",
    "Copy Layer": "复制图层",
    "Cut Hair": "剪切毛发",
    "Emit From Object": "从对象发射",
    "FX": "特效",
    "Fluid Tools": "流体工具",
    "Follow Target": "跟随目标",
    "Four Views (bottom)": "四视图（底部）",
    "Grid ": "网格",
    "Guide Brushes": "引导线笔刷",
    "Handles Off": "关闭操纵柄",
    "Handles On": "开启操纵柄",
    "HeightField Quickshade": "高度场快速着色",
    "Hide All": "全部隐藏",
    "Hide/Unhide Hair": "隐藏/显示毛发",
    "Inner Core Off": "关闭内核",
    "Inner Core On": "开启内核",
    "Isolate Layer": "隔离图层",
    "Jump into Hair": "进入毛发网络",
    "Jump to Hair": "跳转到毛发",
    "Laser": "激光",
    "Layer Property": "图层属性",
    "Length Adjust": "调整长度",
    "Lift Hair": "提升毛发",
    "LookDev": "视觉开发",
    "Mask By Object": "按对象生成遮罩",
    "Mask Noise": "遮罩噪波",
    "Mask by Object": "按对象生成遮罩",
    "Muscle Geometry Off": "关闭肌肉几何体",
    "Muscle Geometry On": "开启肌肉几何体",
    "Navigate": "导航",
    "Output Maps": "输出贴图",
    "Particle Emitter": "粒子发射器",
    "Pyro": "火焰特效",
    "Pyro FX": "火焰特效",
    "Ragdolls": "布娃娃",
    "SDF from Polygons": "从多边形生成 SDF",
    "Selection Options": "选择选项",
    "Selection Tools": "选择工具",
    "Solid Conform": "实体贴合",
    "Splice Tiles": "拼接图块",
    "Split into Tiles": "拆分为图块",
    "Terrace": "阶地",
    "Three Views (Bottom)": "三视图（底部）",
    "Three Views (Left)": "三视图（左侧）",
    "Two Views (Side)": "双视图（并排）",
    "Two Views (Stack)": "双视图（堆叠）",
    "Viewport Layout": "视口布局",
    "Visible Geometry": "可见几何体",
    # Curve-state menus generated from Python.
    "Auto-Draw Mode": "自动绘制模式",
    "Bake Rounded Corner": "烘焙圆角",
    "Create Rounded Corner": "创建圆角",
    "Delete Segments": "删除线段",
    "Make Manual": "设为手动",
    "Remove Rounded Corner": "移除圆角",
    "Retract": "缩回",
    # Grooming menus generated from parameter labels and menu choices.
    "Pick Density": "拾取密度",
    "Add / Subtract Mode": "添加/减去模式",
    "Set Mode": "设置模式",
    "Guide Curve Type": "引导线曲线类型",
    "Parting Line Curve Type": "分缝线曲线类型",
    "Fixed Segment Mode": "固定线段模式",
    "Adaptive Segment Mode": "自适应线段模式",
    "Damped Simulation Mode": "阻尼模拟模式",
    "Settle Simulation Mode": "沉降模拟模式",
    "Up to Root / Tip Soft Transform Mode": "延伸到根部/尖端的软变换模式",
    "Up to Root & Tip Soft Transform Mode": "延伸到根部和尖端的软变换模式",
    "Fraction of Curve Length Soft Transform Mode": "按曲线长度比例的软变换模式",
    "Absolute Distance Soft Transform Mode": "按绝对距离的软变换模式",
}


def load_merged() -> dict[str, str]:
    merged: dict[str, str] = {}
    paths = sorted((ROOT / "source" / "translations").glob("*.json"))
    paths += sorted((ROOT / "translations").glob("*.json"))
    for path in paths:
        data = json.loads(path.read_text(encoding="utf-8-sig"))
        merged.update(data.get("translations", {}))
    return merged


def factory_labels(houdini_root: Path) -> set[str]:
    labels: set[str] = set()
    pattern = re.compile(r'"label"\s*,\s*"((?:\\.|[^"\\])*)"')
    for path in (houdini_root / "houdini" / "radialmenu").glob("*.radialmenu"):
        text = path.read_text(encoding="utf-8-sig")
        for match in pattern.finditer(text):
            labels.add(json.loads('"' + match.group(1) + '"'))
    return labels


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--houdini-root",
        type=Path,
        default=Path(os.environ.get("HFS", r"D:\DCC SoftWare\Houdini 21.0.440")),
    )
    args = parser.parse_args()

    document = json.loads(SUPPLEMENT.read_text(encoding="utf-8"))
    entries = document["translations"]
    entries.update(TRANSLATIONS)
    document["translations"] = dict(sorted(entries.items()))
    SUPPLEMENT.write_text(
        json.dumps(document, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )

    labels = factory_labels(args.houdini_root)
    merged = load_merged()
    missing = sorted(
        label
        for label in labels
        if re.search(r"[A-Za-z]", label) and label not in merged
    )
    if missing:
        raise SystemExit(
            "Factory radial-menu labels still missing:\n" + "\n".join(missing)
        )

    print(
        f"wrote {len(entries)} supplement entries; "
        f"audited {len(labels)} factory radial-menu labels"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
