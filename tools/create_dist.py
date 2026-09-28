"""Publish only installable package assets as a versioned ZIP in dist."""

import argparse
import json
from pathlib import Path
import re
import shutil
import tempfile
import zipfile


def stage_dictionaries(source: Path, package: Path) -> None:
    """Record only explicitly selected source dictionaries, never runtime captures."""
    files = sorted(p for p in source.glob("*_zh.json") if p.is_file())
    if not {"dictionary_zh.json", "templates_zh.json"}.issubset({p.name for p in files}):
        raise ValueError("Required source dictionaries are missing")
    for file in files:
        document = json.loads(file.read_text(encoding="utf-8-sig"))
        if not isinstance(document, dict) or not any(
                isinstance(document.get(key), dict) for key in ("translations", "templates")):
            raise ValueError(f"Invalid dictionary document: {file.name}")
    target = package / "HoudiniChinese/translations"
    target.mkdir(parents=True, exist_ok=True)
    for file in files:
        shutil.copyfile(file, target / file.name)
    (package / "HoudiniChinese/dictionary_manifest.json").write_text(
        json.dumps([p.name for p in files], ensure_ascii=False, indent=2), encoding="utf-8")


def create_dist(package: Path, destination: Path, version: str, *, tag: str = "") -> Path:
    if not re.fullmatch(r"\d+\.\d+\.\d+(?:\.\d+)?", version):
        raise ValueError("Expected a full numeric Houdini build version")
    if tag and not re.fullmatch(r"[a-z0-9-]+", tag):
        raise ValueError("Invalid package tag")
    assets = [
        "HoudiniChinese.json",
        "HoudiniChinese/HoudiniChineseHook.dll",
        "HoudiniChinese/MainMenuCommon.xml",
        "HoudiniChinese/translations/dictionary_zh.json",
        "HoudiniChinese/translations/templates_zh.json",
        "HoudiniChinese/engine.json",
    ]
    identity = json.loads((package / "HoudiniChinese/engine.json").read_text(encoding="utf-8"))
    engine = identity["engine"]
    if not re.fullmatch(r"HoudiniChineseEngine-hdk\d+-qt[56]\.\d+\.\d+-x64(?:-build\d+\.\d+)?\.dll", engine):
        raise ValueError("Invalid engine filename")
    assets.append(f"HoudiniChinese/{engine}")
    draft = "HoudiniChinese/translations/dictionary_rough_zh.json"
    if (package / draft).is_file():
        assets.append(draft)
    manifest = package / "HoudiniChinese/dictionary_manifest.json"
    if manifest.is_file():
        names = json.loads(manifest.read_text(encoding="utf-8"))
        if not isinstance(names, list) or any(
                not isinstance(name, str) or
                not re.fullmatch(r"[A-Za-z0-9_-]+_zh\.json", name) for name in names):
            raise ValueError("Invalid dictionary asset manifest")
        assets.append("HoudiniChinese/dictionary_manifest.json")
        assets.extend("HoudiniChinese/translations/" + name for name in names)
    # The explicit startup notification is required by opt-in recovery packages.
    # Do not recursively archive Python directories, caches or runtime reports.
    for script in ("pythonrc.py", "uiready.py", "houdini_chinese_startup.py"):
        for startup in sorted((package / "HoudiniChinese").glob("python*libs/" + script)):
            if re.fullmatch(r"python\d+\.\d+libs", startup.parent.name):
                assets.append(startup.relative_to(package).as_posix())
    assets = list(dict.fromkeys(assets))
    for relative in assets:
        if not (package / relative).is_file():
            raise FileNotFoundError(package / relative)
    destination.mkdir(parents=True, exist_ok=True)
    suffix = f"-{tag}" if tag else ""
    target = destination / f"HoudiniChinese-Houdini-{version}{suffix}.zip"
    # Finish the archive before replacing a previous package. Never include
    # runtime INI files, capture output, build intermediates or personal data.
    with tempfile.NamedTemporaryFile(dir=destination, suffix=".zip", delete=False) as file:
        temporary = Path(file.name)
    try:
        with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for relative in assets:
                archive.write(package / relative, relative)
        temporary.replace(target)
    finally:
        temporary.unlink(missing_ok=True)
    return target


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("version")
    parser.add_argument("--dictionary-source", type=Path)
    args = parser.parse_args()
    if args.dictionary_source:
        stage_dictionaries(args.dictionary_source, args.package)
    print(create_dist(args.package, args.destination, args.version))
