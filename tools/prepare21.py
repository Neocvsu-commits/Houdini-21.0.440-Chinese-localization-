"""Stage only data/Python from the supplied update; binaries are rebuilt locally."""
from pathlib import Path
import importlib.util
import json
import shutil
import zipfile

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build' / 'houdini21'
PACKAGE = OUT / 'package'
if PACKAGE.exists():
    shutil.rmtree(PACKAGE)
spec = importlib.util.spec_from_file_location('metadata', ROOT/'tools/generate_engine_metadata.py')
metadata = importlib.util.module_from_spec(spec)
spec.loader.exec_module(metadata)
identity = metadata.generate(Path(r'D:\DCC SoftWare\Houdini 21.0.440'), '6.5.3', OUT/'obj')
identity['engine'] = 'HoudiniChineseEngine.dll'
(OUT/'obj/engine.json').write_text(json.dumps(identity,indent=2)+'\n',encoding='utf-8')
(OUT/'obj/detours').mkdir(parents=True,exist_ok=True)
(PACKAGE/'HoudiniChinese/bin').mkdir(parents=True,exist_ok=True)
with zipfile.ZipFile('F:/BaiduNetdiskDownload/HoudiniChinese更新了词典.zip') as archive:
    for item in archive.infolist():
        path = Path(item.filename)
        if path.is_absolute() or '..' in path.parts or ':' in item.filename:
            raise ValueError('Unsafe archive path')
        if item.is_dir() or path.suffix.lower() not in ('.json','.py'):
            continue
        if item.filename in ('HoudiniChinese.json','HoudiniChinese/engine.json'):
            continue
        target = PACKAGE / item.filename.replace('python3.13libs/','python3.11libs/')
        target.parent.mkdir(parents=True,exist_ok=True)
        target.write_bytes(archive.read(item))
# The source repository also contains older author dictionaries. Only add this
# port's supplement; do not let old source data override the supplied update ZIP.
supplement = ROOT/'translations/dictionary_zz_houdini21_zh.json'
shutil.copy2(supplement, PACKAGE/'HoudiniChinese/translations'/supplement.name)
shelf_help = ROOT/'translations/dictionary_shelf_help21_zh.json'
shutil.copy2(shelf_help, PACKAGE/'HoudiniChinese/translations'/shelf_help.name)
translation_dir = PACKAGE/'HoudiniChinese/translations'
dictionary_names = sorted(path.name for path in translation_dir.glob('*_zh.json'))
(PACKAGE/'HoudiniChinese/dictionary_manifest.json').write_text(
    json.dumps(dictionary_names, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
startup = PACKAGE/'HoudiniChinese/python3.11libs/houdini_chinese_startup.py'
text = startup.read_text(encoding='utf-8-sig')
marker = '            _notify_accepted = bool(function())'
assert text.count(marker) == 1, 'Author startup changed; review before patching'
text = text.replace(marker, marker + '\n            if _notify_accepted:\n                import houdini_chinese_hud21\n                houdini_chinese_hud21.install()\n                print("HoudiniChinese 21 overlay ready")')
startup.write_text(text, encoding='utf-8')
shutil.copy2(ROOT/'python3.11libs/houdini_chinese_hud21.py', startup.parent/'houdini_chinese_hud21.py')
shutil.copy2(ROOT/'python3.11libs/houdini_chinese_tooltips21.py', startup.parent/'houdini_chinese_tooltips21.py')
package = {
    'enable': "houdini_version == '21.0.440'",
    'load_package_once': True,
    'hpath': '$HOUDINI_PACKAGE_PATH/HoudiniChinese',
    'preload_libraries': ['$HOUDINI_PACKAGE_PATH/HoudiniChinese/HoudiniChineseHook.dll'],
    'env': [{'HOUDINI_CHINESE_EXPERIMENT_QT_INLINE':'1'}, {'HOUDINI_CHINESE_EXPERIMENT_LATE_LOOK':'1'}],
}
(PACKAGE/'HoudiniChinese.json').write_text(json.dumps(package,indent=2)+'\n',encoding='utf-8')
shutil.copy2(OUT/'obj/engine.json',PACKAGE/'HoudiniChinese/engine.json')
shutil.copy2(ROOT/'LICENSE',PACKAGE/'HoudiniChinese/LICENSE')
shutil.copy2(ROOT/'source/detours/LICENSE.md',PACKAGE/'HoudiniChinese/LICENSE-Detours.md')
print('Target:',identity)
