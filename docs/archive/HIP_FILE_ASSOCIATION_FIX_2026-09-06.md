# 双击 HIP 工程未加载汉化（2026-09-06）

## 实际原因

本机 .hip、.hipnc、.hiplc 关联至 Houdini 22.0.368/bin/hview.exe。
hview 是启动器，会再创建 houdini.exe 子进程。上一轮审核遗漏了文件关联启动路径。

实际复现的进程链为 hview PID 23788 → houdini PID 8100。
两者进程环境均包含 HOUDINI_PACKAGE_SKIP=1；HOUDINI_PATH 已继承插件目录，
两个汉化实验开关也已继承，但 houdini 子进程没有加载 HoudiniChineseHook.dll。
这说明启动器已解析 package 配置，子进程跳过重复 package 解析时，没有执行 DLL 预加载。
既有 uiready.py 遇到尚未加载的 DLL 直接返回，因此不能恢复这条启动路径。

最初曾怀疑 hview 被主程序白名单拒绝；子进程证据否定了该解释。
该尝试的 C++ 改动已全部撤回，最终 C++ 源码与 f6b50b2 一致。

## 最终修复

增加 package/HoudiniChinese/pythonrc.py，由 Houdini 的官方 Python 启动机制调用：

- 只在 Windows GUI 会话运行。
- 若正常预加载已载入 DLL，直接返回，不重复加载。
- 若 DLL 缺失，通过 hou.findFile 从继承的 HOUDINI_PATH 找到同一引导 DLL，并按受限依赖搜索路径加载。
- 仍由原 C++ 引导器检查 HDK/Qt/宿主身份并启动引擎；脚本不改 Hook、输入保护、词典或工程内容。
- 不设置 HOUDINI_PACKAGE_SKIP，不增加定时器，不使用依赖 __file__ 的路径定位。
- 加载失败写一次错误信息，不阻断 Houdini 的 Python 启动流程。

构建根据目标 Houdini 的 python*libs 目录部署启动脚本，ZIP 明确收入 pythonrc.py 和既有 uiready.py，
不递归收入缓存、报告或私人配置。标准包和实验包均包含补加载入口。

依据：[SideFX Python 启动脚本说明](https://www.sidefx.com/docs/houdini/hom/locations.html#startup)、
[hou.findFile](https://www.sidefx.com/docs/houdini/hom/hou/findFile.html)。

## 验证

- Python 回归 31 项通过：文件关联子进程补加载、直接预加载不重复、非 GUI 跳过、异常不阻断、无 __file__ 执行和 ZIP 资产白名单。
- 完整重新编译成功；最终 C++ 主体未改动，保留原 80 个 Hook。
- 正式目录部署 12 项资产并逐项校验；F3 设置和未翻译采集记录保留，旧文件已有本地备份。
- 再次用 Windows 文件关联打开原始 1.hip：启动器 PID 28832，实际 Houdini PID 24924。
- 该 Houdini 进程从 Documents/houdini22.0/packages/HoudiniChinese 加载引导 DLL 和引擎 DLL。
- uiready 通知 true；20 秒报告 translation_enabled=true、dictionary_ready=true；UI_Look 已挂载，observed_calls=2279。
- 节点文字绘制路径 hookedCVTextWrappedInBox 记录 2 次 translated_calls，说明并非只有 DLL 在进程中存在。
- 原始 1.hip 的 SHA256 与启动前一致；未保存工程、未控制鼠标/键盘或关闭现有进程。

安装包：dist/HoudiniChinese-Houdini-22.0.368-hip-entry-fix.zip。
运行证据：build/deploy-hip-startup-20260906-174143/verified-launch。
修复与用户更新的 dictionary_captured_zh.json 随统一启动版本一并提交。
