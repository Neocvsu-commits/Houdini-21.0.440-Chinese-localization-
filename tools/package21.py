from pathlib import Path
import hashlib
import json
import shutil
import zipfile

root = Path(__file__).resolve().parents[1]
source = root / 'build/houdini21/package'
release = root / 'release/HoudiniChinese-21.0.440'
# Dictionaries are frequently edited after a native build during translation
# iteration. Refresh them here so packaging never ships a stale staged copy.
staged_translations = source / 'HoudiniChinese/translations'
staged_translations.mkdir(parents=True, exist_ok=True)
for name in ('dictionary_zz_houdini21_zh.json',
             'dictionary_shelf_help21_zh.json'):
    shutil.copy2(root / 'translations' / name, staged_translations / name)
if release.exists():
    shutil.rmtree(release)
release.mkdir(parents=True, exist_ok=True)
manifest = {}
for file in source.rglob('*'):
    if not file.is_file() or '__pycache__' in file.parts:
        continue
    relative = file.relative_to(source)
    target = release / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(file, target)
    manifest[relative.as_posix()] = hashlib.sha256(file.read_bytes()).hexdigest()
(release/'checksums.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
shutil.copy2(root/'tools/install21.ps1', release/'install.ps1')
shutil.copy2(root/'README-21.md', release/'README-21.md')
(release/'install.bat').write_text('@echo off\npowershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1"\npause\n',encoding='ascii')
archive = release.parent / (release.name + '.zip')
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
    for name in [*manifest, 'checksums.json','install.ps1','install.bat','README-21.md']:
        z.write(release/name,name)
print(archive)
print('Installed files:',len(manifest))
