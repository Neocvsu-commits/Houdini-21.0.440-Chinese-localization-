#!/usr/bin/env python3
"""Generate a release-scoped Houdini package manifest."""

from __future__ import annotations

import argparse
import json
import os
import re
import tempfile
from pathlib import Path


RELEASE_PATTERN = re.compile(r"^[1-9][0-9]*\.[0-9]+$")


def generate(template: Path, output: Path, release: str) -> None:
    if not RELEASE_PATTERN.fullmatch(release):
        raise ValueError(f"unsupported Houdini Qt 6 release: {release}")
    source = template.read_text(encoding="utf-8")
    if source.count("@HOUDINI_VERSION@") != 1:
        raise ValueError("package template must contain exactly one version token")
    rendered = source.replace("@HOUDINI_VERSION@", release)
    if not isinstance(json.loads(rendered), dict):
        raise ValueError("package manifest must be a JSON object")
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
                mode="w", encoding="utf-8", newline="\n", delete=False,
                dir=output.parent, prefix=f".{output.name}.", suffix=".tmp") as stream:
            temporary = Path(stream.name)
            stream.write(rendered)
        os.replace(temporary, output)
    finally:
        if temporary:
            temporary.unlink(missing_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("template", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("release")
    args = parser.parse_args()
    generate(args.template, args.output, args.release)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
