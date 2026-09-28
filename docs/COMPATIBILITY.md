# 兼容层

改造前的稳定基线为 `14c221a`。翻译、输入保护、79 个 Hook 入口、备用挂接路径、
按钮和快捷键均保留。本次新增一个独立加载器，没有改写 Houdini 的源文本或模型数据。

## 加载顺序

1. 官方 Package 预加载 `HoudiniChineseHook.dll`。此文件现在只依赖 Windows
   `KERNEL32.dll`、`VERSION.dll`，不静态链接 Qt、HDK 或外部 MSVC CRT。
2. 在工作线程读取宿主的 `UTgetHDKAPIVersion()`、实际 Qt Core 版本、Qt 各组件
   文件版本和 Houdini 文件版本。等待宿主核心模块的上限为 30 秒，之后不再轮询。
3. 根据 HDK API、完整 Qt 版本、x64 和可选 stub 构建号生成唯一的引擎文件名。
   路径只从加载器所在目录推导，不采用 PATH 或外部清单指定的任意 DLL。
4. 先以资源方式读取引擎内嵌的固定结构标识，不执行 DllMain、不加载其依赖。
   核对协议、平台、HDK、Qt、发布系列和 stub 版本。验证期间保持文件只读锁定。
5. 匹配后加载引擎，调用显式启动导出。引擎再次检查实际 HDK/Qt，等 QApplication
   创建后在 GUI 线程安装现有 Hook。引擎本身的 DllMain 不启动翻译。

`engine.json` 是打包时选择资产和供人阅读的构建记录，运行时不依赖可编辑的
JSON 声明来信任一个 DLL。内嵌标识由 HDK 头文件生成，编译断言校验所用头文件
和 Qt SDK，避免打包信息与代码目标不一致。它是兼容性标识，不是数字签名。

## 版本边界

- 当前构建工具及引擎针对 Qt 6。加载器协议支持识别 Qt 5，但仓库目前没有经过
  旧版 HDK 构建和测试的 Qt 5 引擎，不能声称已经兼容 Qt 5 Houdini。
- 同一 release、HDK API 和 Qt 完整版本可共用同一引擎文件。仍通过导出符号和
  现有类型/虚表检查决定能安装的路径；不猜测新签名、不加入固定地址兜底。
- HDK API 不包含 stub 版本，带 stub 的目标要求完整构建匹配。SideFX Windows
  文件版本字段为 major、minor、stub、build，与 Qt 的字段顺序不同。
- alpha/beta 的 HDK API 标识并非可靠的二进制兼容承诺。私有接口的行为和布局
  也不受公共 API 相同的保证；通过代表环境运行和用户反馈扩展验证记录。
- 仅能匹配引擎和安装入口不等于全界面已汉化，不能自动保证未知版本没有遗漏。

## 功能诊断

`tools/hook_diagnostics.py` 在 Houdini Python 中使用：

```python
import hook_diagnostics
print(hook_diagnostics.loader_status())
hook_diagnostics.start()
# 用户手动查看节点、端口、材质树、视口和 Qt 控件。
hook_diagnostics.report(r"C:\absolute\existing-folder\hooks.json")
hook_diagnostics.stop()
```

加载状态区分宿主等待超时、Qt 组件混用、缺少 HDK API、没有匹配引擎、标识不符、
依赖加载失败和启动失败。`engine_loaded` 仅表示引擎启动已安排，完整报告必须等
GUI 初始化完成才接受排队写入；`report()` 返回 True 仅表示请求已排队。

功能摘要直接来自同一个 79 项清单，按功能及 draw/measure/layout/context/capture
角色统计已安装入口并列出不可用符号。`partial` 不会关闭整个功能或删掉备用入口。
各入口的等待、安装失败、实际调用和翻译命中仍保留在完整报告中；符号缺失、
未进入该界面、词典没有词条应分别排查。报告仅在显式请求时生成，正常绘制不写盘。

## 发布与测试

每个 ZIP 包含加载器、选定引擎、构建信息、菜单兼容资源和两个正式词典文件。
只发布到 `dist`；不会将 INI、采集内容或旧引擎带入包。更新前关闭 Houdini，
完整覆盖包文件，个人设置仍在插件根目录，字典和设置位置没有改变。

自动测试包括真正的 DLL 资源读取、不匹配拒绝执行、无标识 DLL 拒绝执行、
匹配 DLL 加载、stub 和 Qt 5/6 匹配规则、打包资产及原有全部显示回归。
这些测试不代替 Houdini 场景验收。

## 2026-09-05 运行对照

稳定基线 `14c221a` 的已构建包与新兼容层包在 Houdini 22.0.368 / Qt 6.8.3
分别启动测试。词典和引擎正常加载，测试启动环境修正后标准错误日志为空。
两者初次对照的 79 项状态完全一致：16 项 observed、54 项 patched_not_observed、
6 项 installation_failed、3 项 import_registered_not_patched。

尚未挂接的相同 9 项为 BR 帮助提示、UI 帮助提示绘制、UI 帮助正文、RV addText
第二重载、SI 树行上下文、UI_Feel renderMe、UI_Look draw、UI_Text paintText
范围重载和 QPainter QPointF 第二重载。保留了所有现有入口和备用路径，没有
因为摘要显示 partial 而删除或关闭整个功能。

进一步运行细分了错误阶段：前 7 项报 detour_attach / 8；UI_Text 范围重载
还曾报 thread_suspend / 5，说明错误可能随运行条件变化；最后一项没有已替换
的导入槽。它们是原有问题，尚未在本次改造中修复。单独准备并撤销挂接的进程内
探针没有复现错误 8，不能据此断言整个宿主内存不足或已找出分配器的具体缺陷。
没有修改 Detours 分配算法，也没有增加未经验证的替代签名。

对照日志保存在本地 `build/compat-baseline-runtime/`、`build/compat-runtime/`
和 `build/compat-stage-runtime/`。新版仍需手动检查节点、材质树、视口、切换按钮
和快捷键。安装结果及零启动异常不等于这些界面已全部验收通过。

参考：[SideFX HDK API 标识](https://www.sidefx.com/docs/hdk/_u_t___h_d_k_version_8h.html)、
[Windows DLL 初始化约束](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-best-practices)、
[LoadLibraryEx](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-loadlibraryexw)。
