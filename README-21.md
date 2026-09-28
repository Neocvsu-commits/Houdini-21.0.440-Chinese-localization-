# HoudiniChinese · Houdini 21.0.440 适配版

基于原作者提供的源码重新编译，配套使用本次提供的新版词典。只支持 Windows x64 的 Houdini 21.0.440（Python 3.11、Qt 6.5.3、HDK API 21000440）。不会升级 Houdini，也不会替换 Houdini 安装目录里的任何文件。

## 下载与安装

普通用户只需从 GitHub Releases 下载 `HoudiniChineseInstaller-21.0.440.exe`：

1. 保存工作并关闭 Houdini。
2. 运行安装器，确认自动识别的 Houdini 21.0 `packages` 目录。
3. 点击“安装”。完成后重新启动 Houdini 21.0.440。
4. 按 **F3** 或点击顶部 **中/英** 按钮切换显示语言。

安装器不会修改 Houdini 程序目录或工程文件。覆盖安装前会备份已有的同名包；以后可在 Windows“已安装的应用”中卸载，安装器会恢复该备份。安装器尚未进行商业代码签名，Windows 首次运行时可能显示 SmartScreen 提示。

ZIP 包保留给需要离线检查或手动部署的用户。完全退出 Houdini，解压后运行 `install.bat`。默认安装到 Documents\houdini21.0\packages；设置了 HOUDINI_USER_PREF_DIR 时使用该目录下的 packages，并把 __HVER__ 展开为 21.0。脚本会校验文件哈希，并备份已有的同名包。

## 使用

重新启动 Houdini 21.0.440，按 **F3** 或顶部 **中/英** 按钮切换显示语言。右键中/英按钮可调整快捷键。启动通知成功后会输出 `HoudiniChinese 21 overlay ready`。

手动安装时，只复制 `HoudiniChinese.json` 和 `HoudiniChinese` 文件夹到用户 packages 目录，保持目录结构。不要混用 22 版 DLL，也不要与之前的 HoudiniChinese21 原型同时启用。

## 包内文件

- `HoudiniChinese.json`：仅启用 21.0.440，预加载本次编译的加载器。
- `HoudiniChinese/HoudiniChineseHook.dll`：独立加载器，校验宿主与引擎身份。
- `HoudiniChinese/bin/HoudiniChineseEngine.dll`：使用本机 21.0.440 SDK 和 Qt 6.5.3 重新编译的翻译核心。
- `HoudiniChinese/python3.11libs/`：启动脚本、houdini_chinese_hud21.py（视口 HUD）和 houdini_chinese_tooltips21.py（悬停帮助正文）。
- `HoudiniChinese/translations/`：原作者的五份词典及两份本地 21 版补充词典，合并后共 108,238 个唯一原文键；记录数量不代表整个 Houdini 的覆盖率。
- `HoudiniChinese/engine.json`、`dictionary_manifest.json`、许可证文件。
- `checksums.json`：安装文件的 SHA-256 清单。

新版词典采用分文件格式，不需要旧说明中的单个 dictionary_zh.json；引擎会读取 translations 目录下的全部 *_zh.json。

## 本次适配

加载器与引擎均从源码生成，包内不带原作者的 22.0.368 二进制文件。UI_Text 宽度接口、帮助提示返回对象和树列表接口按 21 版调整；原生输入框增加独立绘制保护。中文宽度只参与控件布局计算，保留英文最小宽度，避免 21 版的文字偏移和缩窄。菜单尺寸路径有虚函数备用拦截，UI 就绪后刷新布局。顶部按钮按文字实际宽度分配空间。

## 验证与限制

针对截图漏译新增：Labs 菜单、选择操作提示、Tab 添加节点提示；退出确认保留文件名、节点悬停提示保留节点名。弹窗标题参与中英文切换。21 版视口 HUD 的粗体标题在生成缓存贴图时临时翻译，恢复原始标签，并在语言变化后刷新贴图。补丁不改 Houdini 的 Python 文件或场景数据。

2026-09-22 更新：Houdini 21 补充词典共 959 条，其中交互界面审校脚本维护 228 条。覆盖时间轴、连接端口、第一人称导航、Labs 扩展菜单、参数预设、毛发、MPM、SOP 火焰特效、Tab 节点菜单、径向菜单、网络视图显示选项，以及主题和几何电子表格中的截图漏译。另从 Houdini 21.0.440 自带的两个主工具架集合读取了 397 个功能项和 382 个帮助入口；397 个标题以及其中 369 条唯一非空悬浮说明均有词典项。新增的工具架帮助词典包含 238 个键，其中包括智能撇号、ASCII 撇号和 H21 帮助缓存兼容形式。

Box 改为“长方体”；Main 显示为“主要”；Delta Mush 的显示改为中文开头的“平滑变形（Delta Mush）”。顶部仅保留“中/英”切换按钮，原作者 Bilibili 链接按钮及其网址已经移除。纠正 Follow Parent Context 被误配成雾噪声说明的问题；拖拽窗格路径提示保留真实路径。默认输入/输出端口、词典已知的具名端口，以及带节点类型的两行提示均保留实际节点名和内部端口名。Houdini 已经拆行或加省略号的工具架标题会按最短且唯一的词典项补全后翻译；原生悬停说明在完整句子被拆分排版前翻译。Python 帮助卡片复制显示数据后翻译正文，保留链接、参数名、默认值及代码段。中英文切换会更新可见的 Python 帮助卡片。

已用本机 21.0.440 独立用户配置启动验证，检查中文主菜单、工具架、节点参数、Tab 菜单和 F3 切换。原作者的引擎加载校验、文字布局测试，以及新增的 21 版宽度、输入保护和帮助字符串回归测试通过。测试盒子保持 8 个点，节点真实名称 Save 和表达式 $F*0.1 未改变。

本包不能保证覆盖第三方节点、用户自定义端口以及后续安装的工具架内容；部分 22 专属绘制入口在 21 中不存在，会自动跳过。Houdini 自带两个主工具架集合之外的工具由原作者词典按已有内容覆盖。节点图上与词典重名的名称可能显示为译文，但编辑输入框和实际场景数据保留原文；F3 可随时查看原文。

## 禁用与恢复

先完全退出 Houdini，把 packages 里的 `HoudiniChinese.json` 改名为 `HoudiniChinese.json.disabled`，再启动即可禁用。若安装器备份过旧包，备份位于用户配置目录的 HoudiniChinese-backups 下。禁用并移走当前同名包后可把备份恢复到 packages。

## 源码与构建

在仓库根目录运行 `build21.cmd` 构建，使用 `test21.cmd` 与 `test-native21.cmd` 执行回归检查，再运行 `python tools/package21.py` 生成 ZIP 发行包。运行 `build-installer21.cmd` 可使用 Inno Setup 6 生成单文件安装器。编译依赖 Houdini 21.0.440 SDK、Visual Studio 2022 Build Tools、Python 和 Inno Setup 6。`prepare21.py` 从源码词典读取数据和 Python 文件，并明确排除其他 Houdini 版本的预编译 DLL。

源代码沿用原项目 GPL 许可证，Detours 许可证另附。原始压缩包与解出的原作者源码保留，早期自写原型未用于本安装包。
