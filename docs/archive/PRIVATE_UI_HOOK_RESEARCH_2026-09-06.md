# Houdini 私有 UI 统一 Hook 入口核查

2026-09-06。限时 20 分钟的只读调查；源码基线 `9b18ac2`。
目标二进制：本机 Houdini 22.0.368 x64，随软件提供的 Qt 6.8.3。
没有修改插件源码、部署包、正在运行的 Houdini，也没有操作鼠标或键盘。

**当前决策：仅记录调查结果，暂不合并、不删除、不停用任何 Hook。**
下文的改造顺序和数量均为未来候选方案，不是已实施变更或当前待执行任务。
现有功能与回归要求仍以 [Hook 覆盖文档](HOOK_COVERAGE.md) 为准。

后续已单独调查入口安装失败，见 [UI_Look::draw 挂接失败诊断](HOOK_INSTALL_DIAGNOSIS_2026-09-06.md)。该轮只增加诊断，未实施本文的合并方案。

## 结论

找到了局部统一入口和可收拢的转发链，但没有找到能同时覆盖 Qt Widgets、Qt Quick、原生 RE/RV，并且在布局之前拿到完整文字及控件身份的单一入口。

最值得推进的是两件事：

1. 先按二进制中已证实的转发关系，收拢 RE、RV、QPainter 的重复入口；只在底层入口成功安装并通过对照测试后关闭对应上层 Hook，失败时恢复完整组。
2. 修复已有 `UI_Look::draw` Hook 的安装问题，用它统一原生控件上下文；保持文字翻译、测量、布局刷新为不同职责，再逐组验证哪些子类上下文 Hook 可以退出。

不能为了减少数量，删除 CV 的完整文本入口、输入保护或中英切换时的布局失效处理。

## 实际发现及证据

地址均为模块内 RVA，仅用于本次二进制定位，不能作为跨版本硬编码地址。
原始汇编、导入导出表、SHA256 和调用边保存在项目的 `build/private-ui-audit/`。
下文未带目录的 `.asm` 和 `.json` 证据文件均相对于该目录；源码行号对应调查基线 `9b18ac2`。
该目录属于被 Git 忽略的本地分析产物，清理 build 后可能不再存在；本文件独立保留结论、关键地址和验证条件，不依赖这些产物才能理解。

### 1. 原生 UI 的统一绘制分派：UI_Look::draw

`libUI.dll + 0x244270`：

- `0x244294` 调用 `RE_Server::useNewUISkin()`。
- 新 UI 分支创建 `RE_AutoQPainterPaintBlock`，保存 QPainter 状态，转换坐标。
- `0x244309` 取虚表 `+0x28`，`0x24434c` 调用该函数，实参形态对应 `paint(QStylePainter&, QRect const&, void*)`。
- 另一分支创建 `RE_AutoOpenGLPaintBlock`，取虚表 `+0x30`，`0x2443b5` 调用对应原生 render 实现。
- 入口有 look 对象、渲染器、区域和附加数据，没有统一的字符串参数。

因此这是**控件身份/绘制上下文入口**，不是完整文字入口。它适合在调用下游期间建立作用域，识别标签和用户输入；不能只在这里换一段字符串就覆盖一切。

现有源码 `source/hook.cpp:2061` 已经有 `hookedUILookDraw`，不是本次才发现后新增的函数。
已有 `build/shared-display-runtime/hooks.json` 记录该 Hook：`lookup_misses=0`，`inline_patched=false`，`last_error_stage=detour_attach`，`last_error=8`。
这说明该次运行找到符号但安装失败。该报告是已有实验运行的记录，不代表本次重新测量了所有当前进程状态。

本地 Detours 源码在操作对象分配失败、跳板分配失败时均可返回 8；x64 跳板还要求目标附近可用地址空间。现有日志不能区分这两种失败，不能直接断言电脑物理内存不足或已确定根因。
后续应补充分配阶段诊断，在独立实验包内验证安装，再讨论撤掉子类 Hook。

证据：`libUI.dll-00244270.asm`；`source/detours/detours.cpp:1872`、`:1894`；`source/hook.cpp:1499`。

### 2. 原生标签不是全部经过同一个文字函数

- `UI_Label::paint`（`0x23b5a0`）、`UI_IconText::paint`（`0x207440`）可调用 `UI_Text::paint`。
- `UI_Text::paint`（`0x2c55a0`）进入 `UI_Text::paintText`（`0x2c65f0`），里面先测量/裁剪/换行/选择处理，再进入行绘制。
- `UI_Text::paintLineWithColoring`（`0x2c5600`）调用 QPainter 的文本绘制。
- `UI_FormattedText::paint`（`0x1b5740`）自己处理格式片段并调用 QPainter，不能推断其必经 `UI_Text::paintText`。
- `UI_QtPlutoUtils::drawShelfButton`（`0x1689d0`）同时使用文字测量、省略、QTextLayout、QPainter 和文字路径绘制。
- `UI_Text::getStringWidth`（`0x2c4c10`）本身就有 Qt 字体测量与 `RE_Font::getStringWidth` 两个分支。

这解释了为何单抓 `UI_Text` 或单抓 QPainter 绘制，都不能顺带保证格式、完整短语和宽度一致。

### 3. RE：有明确的主文字入口和主宽度入口

| 外层入口 | RVA | 已证实的下游 |
|---|---:|---|
| putString(C 字符串) | 0x1e8f60 | putString(font, begin, end, transform)，0x1e8c40 |
| putString(begin, end) | 0x1e8f30 | 同上 |
| putString(font, StringView, transform) | 0x125e80 | 同上 |
| getStringWidth(StringRef) | 0x124680 | getStringWidth(begin, end, transform)，0x146c60 |

HDK `RE/RE_RenderUI.h:145` 和 `RE/RE_Font.h:170` 可对照参数类型，后者还公开了 StringView 等内联包装。
本次看到的主 putString 将文字加入渲染队列，入口仍有完整的传入字符串范围，适合显示层替换。

当前 RE 组 5 个 Hook，候选方案保留主绘制入口和主宽度入口，即 **5 → 2**。必须验证主入口 inline 安装成功；仅修改调用模块的 IAT 无法保证拦住 DLL 内部直接调用。
这里的完整性仅指传入该入口的范围；上游已经切碎的短语不会在此自动恢复。

### 4. RV：两个 addText 和 renderText 有共同下游

- `addText(x,y)`，`libRV.dll + 0x54380`，在 `0x543b3` 直接调用 `addText(x,y,z)`，`0x543c0`。
- 静态 `renderText`，`0x571d0`，也调用 `addText(x,y,z)`。
- `buildGeometry`，`0x547c0`，后续直接使用 FreeType 的字符索引、字形和位图接口，并构建几何数据。
- 没有在这条 buildGeometry 路径发现它统一调用 RE 的主 putString 或 createFontDeferData。

HDK `RV/RV_TextRender.h:76` 明确这是 Vulkan 视口文字路径；`addText` 参数及渲染队列结构可直接对照。
当前 RV 组 3 个 Hook，候选 **3 → 1**，前提同样是主入口安装成功并通过节点/视口对照。

### 5. CV：找到统一下游，但不能直接用它替换五个现有入口

本次选取的 5 个 `CV_PaintBuffer` 文本接口都能调用 `CV_PaintBuffer::putString`（`libCV.dll + 0x21c70`）。
这个私有下游又在 `0x21d8e` 调用 `RE_RenderUI::putString(begin,end)`。

问题在顺序：

1. 上层先对原文进行宽度计算、换行、截断或文本框对齐。
2. 之后才把处理过的片段传给共同 putString。

所以若只在共同下游翻译，“Wireframe Over Ghost Surfaces”一类完整词条可能已被拆开，布局也已经按原文决定。节点端口、展开节点文字的完整入口应保留，或先设计显示副本的统一布局适配，再用实测证明等价。

证据：`libCV.dll-analysis.json`、`CV-common-entry.json` 及对应汇编。

### 6. QPainter：八个入口中有四个纯转发入口

- QPoint：`Qt6Gui.dll + 0x209a0` → QPointF（`0x1e4a90`）。
- x,y：`0x209c0` → QPointF。
- QPointF：`0x1e4a90` → QPointF + 两个整数参数（`0x1e4ab0`）。
- x,y,w,h：`0x20a00` → QRect（`0x1e5070`）。

因此可优先研究 **8 → 4**：保留最终 QPointF 实现和三个矩形文本接口。
这是入口收拢，不是去掉 QTextLayout、静态文字或格式文本支持。

进一步发现三个矩形接口都调用 `Qt6Gui.dll + 0x1e9e50` 的内部函数，后者使用 QTextLayout 排行、测量并调用 QTextLine::draw。
该内部函数没有找到导出名，本次未完整确认其 ABI，不建议仅为再省两个 Hook 就采用裸 RVA 或模糊特征码定位。

### 7. Qt 的更深层共同点：QTextEngine，但不是安全的单点替换

`QFontMetricsF::horizontalAdvance(QString,...)` 使用 QStackTextEngine、itemize 和 width；QPainter 的 QPointF 主实现使用 QTextEngine 的 itemize/shapeLine。
`QTextEngine::shapeText`（`0x346f50`）继续调用 HarfBuzz 字形处理。
这确实提供了 Qt 内部测量与绘制共用排版引擎的证据。

但 `shape(item)` / `shapeText(item)` 接收的是已拆分的脚本文字段；Qt 私有头文件还显示 QTextEngine 同时保存文本、文档块、格式、预编辑信息、字符到字形映射和布局缓存。
在此替换文本需要同步这些索引及上层布局，而不是简单替换一个 QString。它还缺少可靠的显示标签/用户输入身份，并且不能覆盖 RE/RV 的独立 FreeType 路径。

更底层并非绝对没有原文：Qt 的 QTextItemInt 可带字符指针，QGlyphRun 也有可选 sourceString/stringIndexes 接口。但存在原文不等于可以忽略已确定的字形位置、缓存、格式与选择区域，直接用中文替换。

因此本轮不推荐用 QTextEngine/FreeType 的全局修改替换现有显示层适配。

### 8. Qt Quick 仍需要独立处理

核查到 `QSGInternalTextNode::doAddTextLayout`（`Qt6Quick.dll + 0xe7360`）和 `doAddTextDocument`（`0xe6ae0`）。前者逐行处理 QTextLayout，后者遍历文档块/帧。
更低层 `addGlyphs`（`0xe6460`）接收 QGlyphRun。这些与 QPainter 入口不是同一个 API。
现有纯文本/富文本入口、中英切换时的重布局应保留；不能把只在绘制阶段换文字当成完整布局更新。

## 后续候选改造顺序（暂不执行）

1. **第一阶段只收拢已证实的转发层。** RE 省 3，RV 省 2，QPainter 省 4，合计 9 个候选；目前目录实际为 80 个 Hook，理论可先研究降到 71 个。这个数是待验证目标，不是已经安全删除的数量，也不对应固定比例的 CPU 提升。
2. **安装按功能组决策。** 主入口成功才关闭包装入口；失败保留回退组。诊断区分“主入口生效”“回退入口生效”“未覆盖”，不能以导出存在代替安装成功。
3. **把 UI_Look::draw 装起来并验证覆盖。** 补充分配诊断；用已有嵌套调用计数观察子类是否实际经过父入口。异步、缓存及直接绕过调用仍需单独验证。
4. **统一规则，保持后端薄适配。** 控件角色判定、词典查找、显示副本、宽度和语言代次缓存使用同一规则；Qt Widgets、Qt Quick、RE/RV 各负责各自布局/绘制接口；CV 保持在切碎字符串前处理。
5. **以功能对照为删 Hook 的依据。** 节点/端口、材质树和图标、Persp/No cam、下拉当前值及列表、富文本页面、菜单/工具架、中英往返宽度与刷新、可编辑参数/路径/表达式保护。对照相同版本完整组，不能只看启动调用次数。

## 范围与方法限制

- 反汇编本机 7 个 DLL：libUI、libCV、libSI、libRE、libRV、Qt6Gui、Qt6Quick；查看选定入口和可识别的直接/导入调用，而非完整逆向整个 Houdini。
- `inspect_pe.py` 解析 PE 导出、普通 IAT、x64 .pdata，调用本机 dumpbin；不执行目标 DLL。
- 优化后的函数可能有多个 .pdata 片段，所以反汇编覆盖相邻导出区间；调用边限定为从入口可达的指令，不把后续无关函数直接归到当前入口。
- 尚未解析所有虚表/寄存器间接调用、CFG 分派、延迟导入和跨区间跳转；未穷举内联、插件自绘、其他 Houdini 构建或其他平台。
- DLL 中存在同址折叠的导出别名，多个别名不是多次调用。
- 既有动态调用边仅作为旁证，不代表遍历全部页面，也不能单凭嵌套关系证明处理的是同一字符串。
- 结论是“找到可合并的局部入口，尚未找到满足全部条件的单入口”，不是数学证明所有可能实现都不存在。
- 当前插件源码和安装包保持不变；正式调查文档保存在 `docs/`，原始分析产物在 `build/private-ui-audit/`。

## 调查对象指纹

调查时间：北京时间 2026-09-06 01:41:25 至 01:58:13，约 17 分钟。检查了 76 个选定入口区域；这个数量是调查范围，不是插件 Hook 数量。
以下 SHA256 来自本机目标 DLL，供以后复核地址是否仍对应同一二进制。

| 模块 | SHA256 |
| --- | --- |
| libUI.dll | `e688447a8d2805c6e156d57f65d27f3998e023eacfb3800f828d7b3e009e2c29` |
| libCV.dll | `0a6be15d6e2a098c77a8e4ea5d8beae6a5475efcd5c71f395a91290132829f3c` |
| libSI.dll | `12bc24b5923e849f734fbcc85bc46aabca275d6243ecbc1da2a23114b4242974` |
| libRE.dll | `2199a204b410fcf05504283ce9d931ffcef2c24cd5742f03ef29164634137b03` |
| libRV.dll | `a5fda5a05d569a9a3c598b4bbde814e333efa626267e9bd2e199fbe8d9b2f1df` |
| Qt6Gui.dll | `16a46cd4b324d24c0f2475077f14fddc18bd2bae68c8b39fe7f6fe40009bd52f` |
| Qt6Quick.dll | `d68324199d4071ec7c0e8d20d1c694cbf2e506f15744a0f2cb6c4c906ea80ff7` |
