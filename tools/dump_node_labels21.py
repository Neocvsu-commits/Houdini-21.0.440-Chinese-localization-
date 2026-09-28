"""Dump the node type labels Houdini uses in Tab menus.

Run this script with the matching Houdini ``hython`` executable.  It avoids
creating scenes or nodes and only reads the registered node type table.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

import hou


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: hython dump_node_labels21.py OUTPUT.json")

    output = Path(sys.argv[1])
    categories: dict[str, list[dict[str, object]]] = {}
    for category_name, category in sorted(hou.nodeTypeCategories().items()):
        records = []
        for type_name, node_type in sorted(category.nodeTypes().items()):
            try:
                label = node_type.description().strip()
            except hou.Error:
                continue
            if not label:
                continue
            try:
                hidden = bool(node_type.hidden())
            except (AttributeError, hou.Error):
                hidden = False
            records.append(
                {
                    "type_name": type_name,
                    "label": label,
                    "hidden": hidden,
                }
            )
        categories[category_name] = records

    payload = {
        "application_version": hou.applicationVersionString(),
        "categories": categories,
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        json.dumps(payload, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(
        f"wrote {sum(map(len, categories.values()))} node type records "
        f"from {len(categories)} categories to {output}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
