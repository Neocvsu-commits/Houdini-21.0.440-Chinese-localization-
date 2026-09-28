# Hook 覆盖与回归基线

## 当前状态

稳定基线是提交 `a669097`。此前已在 Houdini 22.0.368 中确认材质列表、节点文字和视口的 `Persp / No cam` 能显示中文，用户随后要求保存稳定版本。这些确认只代表当时检查过的场景，不表示该版本所有界面均已覆盖。

本轮增加 Hook 清单、运行状态和命中观察的管理能力。**本轮编译及后台自动测试已通过，Houdini 界面验收仍待完成，不能继承稳定基线的界面验收结论。** 源码清单测试和导出核验通过，也不能代替重新构建后在 Houdini 中检查真实显示。

界面验收由用户手动操作。不得代为控制用户的鼠标、键盘或窗口。

## 入口清单职责

`source/hooks/hook_registry.h` 是当前实际 replacement handler 的静态清单，包含 66 个普通函数和由两个原生宏生成的 14 个函数，共 80 项（其中一项是新增的可选下拉框实验入口）。每个重载各占一项，普通 `GetProcAddress` 访问器不算 Hook。

每项记录实际 handler 标识、目标 DLL、完整导出符号、功能范围、角色、声明的安装策略和回归场景。`HookId` 与 `kHookSpecs` 使用相同顺序；新增或删除 handler 必须同步更新清单和对应场景。

| 角色 | 职责 |
| --- | --- |
| `draw` | 在仍持有字符串的显示边界替换本次显示内容 |
| `measure` | 让宽度、包围框和省略处理与译文一致 |
| `layout` | 在确认属于显示文字的排版阶段处理译文 |
| `context` | 标明原生或 Qt Quick 显示对象的调用范围，保护编辑值 |
| `capture` | 为明确触发的漏词采集记录可见对象入口 |

清单保留 Qt Widgets 的 QPainter、QStyle、QTextLayout、QStaticText、QGraphics 路径，Qt Quick 的所有者、排版、场景图及测量路径，以及 Houdini 的 UI_Text/looks、SI 树、RE、RV、CV 和浏览器帮助文字入口。

不能因为两个入口最终调用了同一函数就删除其中一个。例如视口当前字体的 `RE_RenderUI::putString` 两个重载及 `RE_Font` 的 `UT_StringRef` 测量重载，是 `Persp / No cam` 场景的回归保护项。节点的 RV/CV 重载、材质列表的原生绘制及所有者范围也必须分别保留。

## 安装策略与实际覆盖

### 安装失败细分诊断（2026-09-06）

报告新增 `detour_diagnostic`，记录最近一次 Attach 的阶段、地址查询次数、实际分配次数和底层错误。
例如 `last_error=8`、`stage=detour_allocate_trampoline`、`allocation_attempts=0` 表示没有找到可发起分配的候选，不能直接解释为 VirtualAlloc 失败。
明细每次尝试重置，仅在安装阶段更新；没有增加绘制日志或新的重试。
真实进程复现及边界说明见 [UI_Look::draw 挂接失败诊断](HOOK_INSTALL_DIAGNOSIS_2026-09-06.md)。本次没有合并 Hook 或修改重试策略。

### Qt 函数入口实验（2026-09-05）

第二轮：用户确认材质图标名称成功，首选项下拉框当前值仍为英文。
检查安装目录中的 `qt/qml/houdini/prefs/MenuPref.qml` 和
`qt/qml/houdini/pluto/ComboBox.qml`，确认后者以 `T.TextField` 显示当前值：
`enabled: root.editable`，`readOnly: root.down`。因此不能只按 readOnly
判断，也不能将所有禁用输入框视为标签。

新增 `QQuickTextInput::updatePaintNode` 所有者范围（清单由 79 增至 80 项），
仅在实验开关开启、所属 QQuickComboBox 不可编辑、输入控件禁用、
确为该 ComboBox 的 contentItem、没有选区及输入法预编辑内容时放行。
使用已有 QSG 临时布局翻译路径；不调用输入控件 setText，不改它的原始布局、模型或偏好值。
输入框无选区时仍会传递基于光标位置的 selection 参数，仅在上述已验证范围的
临时译文绘制中将其规范化；原始布局和转发路径保持原参数。

已用实际 Qt Quick Templates ComboBox/TextField 对象测试两条当前值、切换索引、
中英开关及可编辑/禁用可编辑控件保护。测试通过；Houdini 真实界面仍待第二轮确认。
第一轮确认有效的材质词条和本页缺失的三个线框词条已补入源码正式词典。
第二轮测试包为 `dist/HoudiniChinese-Houdini-22.0.368-qt-inline-combo-experiment.zip`，
构建目录 `build/out-qt-inline-combo`，运行报告 `build/qt-combo-runtime`。

第一轮保留原有 79 项清单，通过启动环境变量
`HOUDINI_CHINESE_EXPERIMENT_QT_INLINE=1` 为其中 12 项增加入口挂接：
8 个 QPainter::drawText 重载、QTextLayout::draw、QPainter::drawStaticText，
以及 QFontMetrics/QFontMetricsF::elidedText。未设置时继续原有 IAT 方式。
清单中的 `inline+iat` 表示允许实验入口及原有回退，不表示两种方式同时安装。
入口挂接必须发生在发布对应 IAT 替换之前；安装失败恢复原始函数指针后走原有路径。
成功时原始指针为 Detours 跳板，迟加载 DLL 通过原函数入口同样受到拦截。

回归针对“首选项下拉框当前值”和“材质图标名称”。补充词条仅放进独立实验包的
词典，未改正式词典和用户新增的采集词典。测试包为
`dist/HoudiniChinese-Houdini-22.0.368-qt-inline-experiment.zip`；
构建目录为 `build/out-qt-inline-experiment`，运行报告在 `build/qt-inline-runtime`。
报告的 `experimental_qt_inline` 表示实验开关实际状态。

验证记录：

- 27 项 Python 测试通过，现有显示适配测试在开关关闭时通过。
- 开关开启时，直接调用 Qt 导出函数（绕过导入槽）的实际绘图与中文参考图像一致；
  QTextLayout 绘图、材质名省略处理、中英开关和原字符串保留均通过。
- QLineEdit 实际渲染在开关前后像素一致，内容仍为原文，防止放大覆盖范围后误翻译输入值。
- Houdini 22.0.368 / Qt 6.8.3 的正常桌面实验进程中，12 项入口全部挂接成功，
  词典就绪、安装器无异常。两个用户截图场景的最终显示仍待手动验收。
- 临时 uiready 仅定时导出报告和读取材质路径，四分钟后关闭观察；不驱动输入、窗口或场景编辑。

| 清单中的策略 | 能表达的范围 |
| --- | --- |
| `iat` | 替换对应 DLL 导入槽；不涵盖绕过导入槽的内部调用 |
| `inline` | 挂接函数实现入口；受目标版本、符号和可安装条件约束 |
| `inline+iat` | 优先内联，失败时保留导入槽路径；备用登记不等于内联覆盖 |
| `inline+vtable+iat` | 优先内联，备用虚表及导入槽；只对实际替换的槽有效 |
| `style-vtable` | 替换已识别样式的指定虚函数槽，不代表全部自定义样式 |

策略字段是声明，不是运行结果。需要分别看目标是否解析、哪种方式安装、替换了哪些槽和 handler 是否实际执行。零命中也可能表示本次没有打开对应场景；单个命中不能证明同类控件全部覆盖。

`HC_OBSERVE` 放在 handler 的第一条语句，用 handler 自己的 `HookId` 建立观察范围。普通函数和原生宏生成函数都需覆盖，提前返回也不能绕开入口计数。观察能力本身仍须通过集成及运行验证。

## 英文未翻译的分类

### 切换语言后的当前页面排版（2026-09-06）

用户澄清：异常发生在当前偏好页面切换语言后，手动切换页面才能恢复，
不是首次创建页面。首次标签创建时排队刷新的试验已撤回，未打包部署。
原流程在第一轮英文刷新清空 QML 标签记录，第二轮及后续切回中文可能
缺少富文本所有者。现保留已参与翻译标签的弱引用，在切换时清除失效引用；
即时及延迟两轮均可重建显示文档。延迟轮先完成父容器的待处理布局，再
刷新文字的换行、对齐及所属窗口。不切换用户页面、不改变模型或源文，
不增加常驻轮询。实际 QML 富文本在同一页面连续五次切换的文档内容、
单行状态、高度及源文保护测试通过；Houdini 截图场景仍待用户确认。

### 公共绘制处理与调用关系（稳定基线 1b60edb 之后）

- 8 个 QPainter ABI 适配入口共用 drawDisplayText：设备分类只做一次，
  显示字符串以引用传给原函数，输入保护作用域覆盖原函数的嵌套调用。
  QStyle 和 QTextLayout 复用已有作用域，去掉第二次设备分类。
- 即时、延迟语言刷新共用 refreshLanguageLayoutPass，保持稳定版的顺序；
  完整尺寸缓存遍历仍只发生在第一阶段。80 个 Hook 的符号和安装策略不变。
- 显式诊断新增 call_edges，仅记录同线程 Hook 的直接嵌套和次数，不采集
  文本、不回溯调用栈。固定计数矩阵为 80×80×8 字节（约 50 KiB）；正常
  关闭诊断时不写矩阵。报告序列化才遍历矩阵，没有新增定时器或后台任务。
- 调用嵌套不能单独证明重复翻译，也不能覆盖异步调用。删除入口前仍需按
  节点、材质、视口、菜单、输入框等场景对照绘制、测量、保护结果。
- 实际 Qt 绘制和中英尺寸恢复测试、嵌套输入保护、8 线程观察计数、诊断
  报告读写及 27 项 Python 测试通过。性能变化尚无整机 CPU 基准结论。
- 测试包：dist/HoudiniChinese-Houdini-22.0.368-shared-display-experiment.zip。
  稳定版 1b60edb 保留，调用报告位于 build/shared-display-runtime。

### 切回英文的宽度恢复（2026-09-06）

QML 标签只要参与过译文宽度计算，就以 QPointer 弱引用登记，包括已经由
译文 QTextDocument 完成测量的标签。语言切换时，在 GUI 线程通过 QQuickText
公开的 `forceLayout()` 重新计算实际文字、字号、内边距和自然宽度；不保存并
硬写旧数值，也不改应用设置的显式 width。英文模式清空旧登记，已销毁对象跳过。
查找源文/译文宽度时关闭递归翻译，避免把译文测量结果误当作英文基准。

测试使用实际 QML Text 及绑定其 implicitWidth 的父容器，通过连续三轮切换、
不同词条和字号、显式宽度保持、对象销毁检查；英文宽度恢复到各自英文基线。
标准字体测量也验证关闭后不再添加中文宽度补偿。
此机制没有增加背景定时扫描，仅登记已参与翻译的标签并在切换时重新排版。
本轮测试包为 `dist/HoudiniChinese-Houdini-22.0.368-width-restore-experiment.zip`，
运行报告在 `build/width-restore-runtime`。Houdini 实际界面效果仍由用户对照验收。

用户对照发现原生工具架、面板标签仍保留中文布局宽度。后续补充在同一次
语言切换的即时及延迟刷新中调用 `UI_Window::relayoutAllWindows(bool)`，
然后重绘；仅 QML forceLayout 和 ApplicationFontChange 无法覆盖原生布局。
接口由当前 libUI.dll 导出符号解析，GUI 线程调用，缺失时保留重绘回退。
不更改窗口位置、大小或业务文本。原生标签效果需独立验收，不能以 QML
测试通过代替。补充测试包为 `dist/HoudiniChinese-Houdini-22.0.368-native-width-restore-experiment.zip`。

该包随后也被用户确认未恢复。进一步复现：实际 QTabBar 在字体测量切换后，
英文宽度 372、中文 156，原刷新流程回英文仍为 156。修复向具体控件发送
FontChange 并更新几何缓存后恢复为 372；测试断言宽度和源标签文字。
控件根从已有安全枚举及 Houdini 主 Qt 窗口获取，不使用 allWidgets。
原生 UI_Feel 也保留已计算尺寸；通过已有受保护遍历，从子到父调用导出的
sizeChanged() 失效尺寸，再要求原生重排，不写私有字段或实际文本。
QML 富文本同时由源文重建显示文档，避免 forceLayout 复用旧译文文档。
新包为 `dist/HoudiniChinese-Houdini-22.0.368-language-size-cache-experiment.zip`。
QTabBar 回归通过不等同于 Houdini 所有原生标签均已验收。

用户随后确认 language-size-cache 测试包的中英宽度恢复成功。
后续按钮跟随优化：语言布局刷新结束时立即定位，并以 16 ms 间隔跟踪
最多 12 次处理延迟布局，随后停止，保留原有 500 ms 兜底定位。
按钮测试覆盖立即移动、后续布局移动和短时定时器自动停止。
用户随后要求先禁用 500 ms 定位检查：已移除该常驻定时器，保留窗口事件
及语言切换的短时跟随。此变更未启动 Houdini；桌面/Take 变化的完整事件
覆盖尚未验收。主窗口所有权的独立 1 秒检查不属于本次定位定时器。

### 输入保护：owner-first-v1

`source/translation/text_context.h` 集中维护当前绘制的三类归属：
`DisplayLabel`、`UserValue`、`Unknown`，Widgets、QML 显示及采集共用分类。

| 证据 | 处理 |
| --- | --- |
| 明确的标签、按钮、不可编辑下拉框、列表显示、只读 QTextBrowser | 显示翻译；其他输入框的焦点和同名内容不能否决它 |
| QLineEdit、数值编辑器、可编辑下拉框、一般 QTextEdit/QPlainTextEdit 及其 viewport | 保护用户值；禁用、失焦或只读不自动改成标签 |
| QML TextInput/TextEdit | 用户值；只有验证 contentItem 归属、不可编辑状态、无选区/预编辑内容的下拉框显示字段例外 |
| 缺乏控件归属的原生调用 | 保留原生 UI_Text 调用栈及焦点内容回退；不得宣称已消除这类路径全部误判 |

保护作用域使用线程局部状态，覆盖本次向原绘制函数转发的全过程；嵌套的
QStyle/QPainter/场景图适配器不能把 `UserValue` 放宽为 `DisplayLabel`。
原生 UI_Text 基类和通用 QStyle 入口本身不是标签证据，保留 `Unknown`，
让具体绘制设备或已知所有者提供归属。递归防护与用户值保护分别维护。
渲染线程上已确认的 QML 标签不再因为 GUI 线程有编辑焦点而被整体屏蔽。
不增加定时扫描或全局窗口遍历；关闭汉化及采集时，绘制设备分类直接返回。

自动回归包括：输入值与标签同为 File、带输入法属性的 QLabel、QML 标签渲染线程、
禁用/只读输入值、文本编辑器 viewport、只读帮助浏览器、嵌套作用域恢复，
以及前两轮材质/下拉框实验。独立显示测试和 27 项 Python 测试通过。
本轮包为 `dist/HoudiniChinese-Houdini-22.0.368-input-policy-experiment.zip`，
运行报告在 `build/input-policy-runtime`；报告记录 `input_protection_policy`。

| 现象或证据 | 应检查的问题 |
| --- | --- |
| 目标模块未加载或导出不存在 | 生命周期或版本兼容性；不能归为缺词 |
| 目标已解析但安装失败，或只有备用登记 | 安装失败原因及实际调用是否经过备用路径 |
| 场景已打开、文字仍英文、对应入口没有命中 | 检查其他重载、渲染后端或未重绘；不能仅凭字典有词条宣布已覆盖 |
| 入口命中且原文能取得，字典没有对应条目 | 补精确词条或已支持的模板 |
| 入口命中但编辑保护或格式规则跳过 | 确认其是显示标签还是用户数据；不应直接取消保护 |
| 已得到译文但界面未更新或出现裁切 | 排版、宽度计算、缓存刷新与中英切换 |

漏词采集通常只能看到已经到达采集或 Hook 路径的内容。未进入这些路径的文字不会自动出现在漏词列表中，所以“没有采集到缺词”不能证明界面汉化完整。

## 新版本的手动验收

下列项目是本轮待验收项，完成后应记录实际 DLL/构建版本、场景、结果及必要截图，不能只标记“测试通过”。

- 材质面板：分类树、材质名称和列表显示中文，选择及滚动正常。
- 节点网络：展开 `Principled Shader` 的端口，检查类型标题及字典已有的端口词条；节点名称和参数数据保持原值。
- 视口：`Persp / No cam` 显示为“透视 / 无相机”，文字宽度和下拉操作正常。
- 中英切换：现有界面能来回恢复；随后打开的新面板、窗口也按当前语言显示。
- 菜单、参数、帮助：菜单和提示文字、Qt Quick 面板及帮助卡各检查一个可复现样例；输入框仍可正常编辑原文。
- 生命周期：启动超过一分钟再首次打开相关功能，检查迟加载路径；不同工作区与新建窗口不能只沿用启动时结果。
- 顶部两个 Qt 按钮及快捷键：位置、操作和样式保持稳定基线行为。

## 自动检查及兼容性边界

`tests/python/test_hook_catalog.py` 检查 handler 与清单完全对应、每个入口的观察作用域、模块/符号/策略一致性、唯一性及关键回归路径。它还会只读解析本机 Houdini 22.0.368 的目标 DLL 导出；目标版本不存在或选择了其他版本时，会明确跳过此项兼容性核验。

目前符号清单的验证目标为 Houdini 22.0.368 / Qt 6.8.3。导出名存在只说明该目标可解析，不证明调用约定、安装和实际显示均正确，也不能推导其他 Houdini 或 Qt 版本兼容。

不存在已经证明可以拦截任意 UI 的单一入口。文字变成字形、顶点或纹理后，通常已无法可靠恢复完整原文及控件语义；浏览器、自绘控件和新的渲染后端也可能绕开现有路径。这里统一的是清单、状态、观察和验收，保留各后端所需的独立入口。


## 显式诊断方式

诊断不会创建按钮或窗口，不驱动鼠标和键盘。`tools/hook_diagnostics.py` 仅能在已经
加载本轮插件的 Houdini Python 进程中调用，不会向其他进程注入或加载插件。
在 Houdini Python Shell 中手动执行（仓库路径按实际安装位置替换）：

```python
import runpy
hc = runpy.run_path(r"C:/Users/win10/Desktop/Chinese/HoudiniChinese/tools/hook_diagnostics.py")
hc["start"]()
```

随后由用户手动打开需要检查的场景，再执行：

```python
hc["report"](r"C:/Users/win10/Desktop/Chinese/HoudiniChinese/build/hook-report.json")
hc["stop"]()
```

`report` 返回 True 只表示请求已排入 GUI 线程，必须检查实际文件才能确认写出成功。
目录必须已存在。计数按本进程累计，不会在 start/stop 时清零；关闭时已经开始的
调用可能完成少量尾部计数。报告的 `current_import_slots` 是已观察到且仍在模块快照
中的槽数，不把重复扫描累加；`observed_calls` 是历史调用量，不是所有界面覆盖率。
主方案失败后即使备用已挂接，仍保留最近失败错误用于解释实际采用的路径。

已登记但零匹配的入口显示 `import_registered_not_patched`，实际挂接但诊断未观察到
调用显示 `patched_not_observed`，有调用显示 `observed`。目标模块未加载和符号不存在
分别显示 `waiting_for_module` 与 `symbol_missing`。核心目标 DLL 在会话中被替换时
报告 `target_changed_requires_restart`，本轮不尝试热重绑正在使用的函数指针。

模块身份使用基址、映像大小和 PE 时间戳。轮询已观察到卸载，或身份变化时会重新
扫描；完全相同的模块在两次快照之间卸载并重载，无法仅凭轮询可靠识别。


## 管理层初版后台验证记录（历史）

后续本体审核、统一临时布局、异常恢复及性能结果见
[2026-09-05 本体审核](CODE_AUDIT_2026-09-05.md)。以下记录对应管理层初版测试包。

- 完整 `tests/run.bat` 的原有测试及新增模块扫描、注册表、真实 DLL 导入测试通过；
  Python 共 25 项通过。新增报告序列化/原子写入测试 `tests/diagnostics.bat` 也单独通过，
  并已纳入完整测试入口。
- 注册表测试包含 8 线程、每线程 2000 轮嵌套观察；默认关闭时不计数。
- `tests/analyze.bat` 完成，现有 9 条警告来自 Windows/HDK 头文件，未报告项目源码问题。
- 最终构建为 `build/out-hook-registry-final/package`，未部署到用户正式 Houdini 配置。
- 本轮未启动或控制 Houdini 界面；真实场景显示仍按上方清单由用户手动验收。
