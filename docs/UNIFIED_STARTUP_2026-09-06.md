# 统一启动加载（2026-09-06）

## 执行规则

1. 原 package preload 保留，用于直接启动时尽早加载。
2. Python 初始化阶段的 pythonrc.py 调用共享 ensure_loaded，覆盖文件关联启动器转交后跳过 package 解析的情况。
3. uiready.py 再调用同一个 ensure_loaded，补查早期未完成的加载；成功加载或已有预加载时不再次 LoadLibrary。
4. 两个官方入口共用 houdini_chinese_startup.py，不各自维护引导逻辑；均不依赖 __file__。
5. 保留 C++ 加载器的宿主、HDK、Qt 和来源检查；不修改原生汉化、输入保护或 80 个 Hook。
6. 普通启动不持续检查、不写诊断文件。显式启用原有 UI_Look 实验时，就绪通知若遇初始化未完成，最多调用 30 次，间隔 500 ms；成功立即停止，预算耗尽停止。
7. 显式诊断目录才开启原有有限采样，151 秒关闭观察；测试报告失败不阻断语言加载。

Python 模块检查的是当前 Houdini GUI 会话与已加载 DLL，不按 .hip 等文件扩展名分支，
不监控系统进程，不修改文件关联或 HOUDINI_PACKAGE_SKIP。GPlay/MPlay 等独立程序未扩展支持。

## 构建与部署

标准构建统一部署 pythonrc.py、uiready.py、houdini_chinese_startup.py，使用目标 Houdini 的 python*libs 目录。
打包只收入这三个明确脚本和既有资产，不收入缓存、私人配置或报告。
实验打包器只设置原有实验开关并检查启动文件完整性，不再生成另一套 uiready 脚本。

本轮 C++ 源码与 f6b50b2 相同。完整编译校验通过后，安装包复用已验证且正在部署的相同 C++ DLL，
部署时校验所有 DLL 完全一致并跳过写入，更新 Python 启动文件。没有终止现有进程。
安装包：dist/HoudiniChinese-Houdini-22.0.368-unified-startup.zip，共 13 项资产。

## 测试

Python 34 项通过。新增覆盖：
- 两个启动入口重复执行仍只加载一次；
- 已预加载 DLL 直接复用；
- 未执行早期入口时，界面就绪入口可以独立加载；
- 非 GUI 不加载，失败不阻断宿主；
- 就绪稍晚时重试到成功并停止；
- 始终未就绪时严格停止于 30 次；
- 普通启动不产生定时器，继承的 HOUDINI_PACKAGE_SKIP 不改变；
- 三个脚本进入安装包，私人资产不进入 ZIP。

部署及运行记录：build/deploy-unified-startup-20260906-181439。
通过 Windows 文件关联打开原始 1.hip，启动器 PID 12636；实际运行检查另附在下方。
本轮统一启动修复、回归测试和当前新增词条一并提交。

实际结果：Houdini PID 8836；就绪通知接受，尝试 1 次；hooks-50s.json 中 dictionary_ready=true、translation_enabled=true，累计 translated_calls=171（调用计数，不是独立词条数）。
UI_Look 当前状态：installed；沿用原有有限补挂载策略。本轮验证针对启动加载，不把短时观察当成所有界面覆盖证明。
