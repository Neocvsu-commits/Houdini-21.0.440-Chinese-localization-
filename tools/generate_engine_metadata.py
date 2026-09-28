"""Bind a built engine to its HDK/Qt ABI without executing the DLL."""
import argparse
import json
from pathlib import Path
import re


def define(path: Path, name: str) -> str:
    match = re.search(rf'^#define\s+{re.escape(name)}\s+"?([0-9.]+)"?\s*$',
                      path.read_text(encoding="utf-8"), re.MULTILINE)
    if not match:
        raise ValueError(f"Missing numeric {name} in {path}")
    return match.group(1)


def generate(hfs: Path, qt: str, output: Path) -> dict:
    if not re.fullmatch(r"[56]\.\d+\.\d+", qt):
        raise ValueError("Expected a full Qt 5/6 version")
    include = hfs / "toolkit/include"
    sys = include / "SYS/SYS_Version.h"
    hdk = int(define(include / "UT/UT_HDKVersion.h", "HDK_API_VERSION"))
    version = [int(define(sys, f"SYS_VERSION_{part}_INT"))
               for part in ("MAJOR", "MINOR", "BUILD", "PATCH")]
    qversion = list(map(int, qt.split(".")))
    if not hdk or any(value > 65535 for value in version + qversion):
        raise ValueError("Invalid build identity")
    stub = f"-build{version[2]}.{version[3]}" if version[3] else ""
    name = f"HoudiniChineseEngine-hdk{hdk}-qt{qt}-x64{stub}.dll"
    identity = dict(schema="houdini-chinese-engine-v1", protocol=1, machine="x64",
                    hdk_api=hdk, qt=qt, built_with_houdini=".".join(map(str, version)),
                    engine=name, coverage="requires_runtime_scenario_validation")
    output.mkdir(parents=True, exist_ok=True)
    values = [0x48434331, 1, 0x8664, hdk, *qversion, *version]
    (output / "engine.rc").write_text(
        '101 RCDATA\nBEGIN\n    ' + ', '.join(f'{value}L' for value in values) +
        '\nEND\n', encoding="ascii")
    # Compile-time assertions bind the resource to the actual engine includes.
    (output / "engine_build_identity.h").write_text(
        '#include <UT/UT_HDKVersion.h>\n#include <SYS/SYS_Version.h>\n'
        '#include <QtCore/qglobal.h>\n'
        f'static_assert(HDK_API_VERSION == {hdk});\n'
        f'static_assert(QT_VERSION == QT_VERSION_CHECK({qversion[0]}, {qversion[1]}, {qversion[2]}));\n'
        f'static_assert(SYS_VERSION_FULL_INT == {int(define(sys, "SYS_VERSION_MAJOR_INT")) << 24 | version[1] << 16 | version[2]});\n'
        f'static_assert(SYS_VERSION_PATCH_INT == {version[3]});\n', encoding="ascii")
    (output / "engine_name.txt").write_text(name, encoding="ascii")
    (output / "engine.json").write_text(json.dumps(identity, indent=2) + "\n", encoding="utf-8")
    return identity


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("hfs", type=Path)
    parser.add_argument("qt")
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    generate(args.hfs, args.qt, args.output)
