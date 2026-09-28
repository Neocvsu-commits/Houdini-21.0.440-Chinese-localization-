from pathlib import Path
import argparse,json,runpy
parser=argparse.ArgumentParser(description="Package the opt-in UI_Look recovery experiment")
parser.add_argument("--package",type=Path)
parser.add_argument("--tag",default="look-fix-experiment")
args=parser.parse_args()
root=Path(__file__).resolve().parent.parent
package=args.package or root/'build/out-look-fix/package'
package=package.resolve()
distribution=runpy.run_path(str(root/'tools/create_dist.py'))
distribution['stage_dictionaries'](root/'translations',package)
config=package/'HoudiniChinese.json'
data=json.loads(config.read_text(encoding='utf-8'))
data['env']=[{'HOUDINI_CHINESE_EXPERIMENT_QT_INLINE':'1'}, {'HOUDINI_CHINESE_EXPERIMENT_LATE_LOOK':'1'}]
config.write_text(json.dumps(data,indent=2),encoding='utf-8')
# The ordinary build owns startup scripts; experiments only enable flags.
startups = list((package / 'HoudiniChinese').glob('python*libs/pythonrc.py'))
if not startups:
    raise FileNotFoundError('Build the package with the shared startup scripts first')
for startup in startups:
    for script in ('pythonrc.py', 'uiready.py', 'houdini_chinese_startup.py'):
        if not (startup.parent / script).is_file():
            raise FileNotFoundError(startup.parent / script)
create=distribution['create_dist']
archive=create(package,root/'dist','22.0.368',tag=args.tag)
print(archive)
