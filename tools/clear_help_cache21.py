"""清理 Houdini 帮助缓存，让 help 覆盖层生效。

覆盖层（Documents\\houdini21.0\\help\\nodes）只影响"帮助系统解析"这一步；
已经生成过的解析缓存（config\\Help\\cache）会优先被 UI 使用，必须清掉。

用法（先完全退出 Houdini）：
    python tools/clear_help_cache21.py
"""
import io
import os
import shutil

PREF = os.environ.get("HOUDINI_USER_PREF_DIR") or os.path.join(
    os.path.expanduser("~"), "Documents", "houdini21.0")
CACHE = os.path.join(PREF, "config", "Help", "cache")
TARGETS = ("nodes", "shelf", "props")


def main():
    if not os.path.isdir(CACHE):
        print("没找到帮助缓存目录:", CACHE)
        return
    removed = 0
    for name in TARGETS:
        folder = os.path.join(CACHE, name)
        if not os.path.isdir(folder):
            continue
        count = sum(len(files) for _, _, files in os.walk(folder))
        shutil.rmtree(folder)
        removed += count
        print("已删除 %-8s %d 个缓存文件" % (name, count))
    print("共清理 %d 个缓存文件；下次打开帮助/悬停提示时会按覆盖层重新生成。" % removed)


if __name__ == "__main__":
    main()
