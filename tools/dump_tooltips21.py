"""在 Houdini(hython) 里把全部工具提示正文导成 JSON。

用法：
    hython tools/dump_tooltips21.py [输出目录]

输出 tooltips.json：
    {"shelf": {url: {"title": ..., "summary": ...}}, "nodes": {...}}
"""
import io
import json
import os
import sys

import hou
from houdinihelp import api

OUT_DIR = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "build")
OUT = os.path.join(OUT_DIR, "tooltips.json")


def tooltip_for(urls):
    for url in urls:
        if not url:
            continue
        if not url.startswith("/"):
            url = "/" + url
        try:
            text = api.getTooltip(url)
        except Exception:
            continue
        if text and text.strip():
            return url, text.strip()
    return None, None


def shelf_entries():
    result = {}
    tools = hou.shelves.tools()
    print("shelf tools:", len(tools), flush=True)
    for index, (name, tool) in enumerate(sorted(tools.items())):
        if index % 200 == 0:
            print("  shelf %d/%d" % (index, len(tools)), flush=True)
        candidates = []
        try:
            candidates.append(tool.helpURL())
        except Exception:
            pass
        short = name.split("::")[-1]
        candidates.extend([
            "/shelf/" + short.lower().replace(" ", "_"),
            "/shelf/" + name.lower().replace(" ", "_"),
        ])
        try:
            url, text = tooltip_for(candidates)
            if url:
                result[url] = {"title": name, "summary": text}
        except Exception:
            continue
    return result


def node_entries():
    result = {}
    categories = hou.nodeTypeCategories()
    for category_name, category in categories.items():
        try:
            types = category.nodeTypes()
        except Exception:
            continue
        print("nodes %s: %d" % (category_name, len(types)), flush=True)
        for name, node_type in types.items():
            clean = name.split("::")[-1].split("/")[-1]
            candidates = [
                "/nodes/%s/%s" % (category_name.lower(), clean),
                "/nodes/%s/%s" % (category_name.lower(), name),
            ]
            try:
                url, text = tooltip_for(candidates)
                if url:
                    result[url] = {"title": "", "summary": text}
            except Exception:
                continue
    return result


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    data = {"shelf": shelf_entries(), "nodes": node_entries()}
    io.open(OUT, "w", encoding="utf-8").write(
        json.dumps(data, ensure_ascii=False, indent=1))
    print("shelf:", len(data["shelf"]), "nodes:", len(data["nodes"]))
    print("wrote", OUT)


main()
