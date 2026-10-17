"""只导出"工具架里真实出现"的工具提示，边抓边写，便于中断后续跑。

用法：hython tools/dump_shelf_tooltips21.py [输出文件]
输出：{"url": {"tool": 名称, "shelf": 标签页, "summary": 文本}}
"""
import io
import json
import os
import sys
import time

import hou
from houdinihelp import api

OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    os.path.dirname(__file__), "..", "build", "shelf_tooltips.json")
BUDGET_SECONDS = float(os.environ.get("HC_TOOLTIP_BUDGET", "600"))


def tooltip(url):
    try:
        text = api.getTooltip(url)
    except Exception:
        return ""
    return (text or "").strip()


def main():
    started = time.perf_counter()
    result = {}
    shelves = hou.shelves.shelves()
    print("shelves:", len(shelves), flush=True)
    total = 0
    for shelf_name, shelf in sorted(shelves.items()):
        for tool in shelf.tools():
            total += 1
            if time.perf_counter() - started > BUDGET_SECONDS:
                print("budget reached after %d tools" % total, flush=True)
                break
            name = tool.name()
            candidates = []
            try:
                candidates.append(tool.helpURL())
            except Exception:
                pass
            short = name.split("::")[-1]
            candidates.extend([
                "/shelf/" + short.lower().replace(" ", "_"),
                "/shelf/" + short.lower(),
            ])
            summary = ""
            used = ""
            for url in candidates:
                if not url:
                    continue
                if not url.startswith("/"):
                    url = "/" + url
                summary = tooltip(url)
                if summary:
                    used = url
                    break
            if summary and used:
                result[used] = {"tool": name, "shelf": shelf.label(), "summary": summary}
        if time.perf_counter() - started > BUDGET_SECONDS:
            break
        print("  %-28s -> %d" % (shelf_name[:28], len(result)), flush=True)

    os.makedirs(os.path.dirname(os.path.abspath(OUT)), exist_ok=True)
    io.open(OUT, "w", encoding="utf-8").write(
        json.dumps(result, ensure_ascii=False, indent=1))
    print("tools scanned:", total, "with tooltip:", len(result))
    print("wrote", OUT)


main()
