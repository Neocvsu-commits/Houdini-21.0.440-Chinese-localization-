> 注：本文为历史对照记录，当时的临时工作区已清理，脚本整理进 `tools/`。

# Houdini 21.0.440 汉化 · 与「星空汉化」词条库对照报告

对比对象：<https://github.com/Ker0el/Houdini-zh>（星空汉化，Houdini **22.0.368** 界面文案词条库，CC BY-SA 4.0）
对照基准：本机 `D:\DCC SoftWare\Houdini 21.0.440` + 你的 release 包（7 份词典、108,238 键）

---

## 一、先给结论

**词条覆盖率不是你的短板。**

对方 46,812 条里，你缺 3,766 条；逐条去 H21 本体里找字面证据后，能确认"21 里确实存在、而你没翻"的是 **516 条**（已按证据强度分档导出，可直接导入），其余约 3,250 条基本是 22 版新增。

真正的差距在四个**系统层面**：

| 类别 | 你的现状 | 对方做法 | 影响 |
|---|---|---|---|
| 动态拼接文案 | **0 覆盖**（`templates` 全空） | 71 条正则 + 7 条前缀规则 | 状态栏、撤销历史、标题栏一直是英文 |
| 排除表 | 无 | 1,791 条 skip | 你翻了 732 条标识符/库名，有误伤风险 |
| 词典内部一致性 | 59 键自相冲突 | 单一成品文件 | 靠加载顺序赢回来，打包一改就复发 |
| 术语统一 | 纹理/贴图、噪波/噪声、三维/3D 混用 | README 明写"一个词只有一种译法" | 界面读起来不统一 |

---

## 二、覆盖量对比

| 指标 | 数值 |
|---|---|
| 你的词条（7 份词典去重） | **108,238** |
| 对方词条（`dict/zh_cn.json` 成品） | 46,812 |
| 双方都有 | 43,046（精确 42,852 + 归一化命中 194） |
| 对方有、你没有（精确 + 大小写标点归一） | **3,766** |
| 你有、对方没有 | 62,546 |
| 同一原文、双方译法不同 | 19,505（其中 ≤22 字符短标签约 13,000） |

### 缺口验伪：516 是怎么算出来的

拿 3,766 条逐条去 H21 安装里找字面证据，分三档：

| 档 | 证据 | 条数 |
|---|---|---|
| A | H21 `bin` 下 458 个 dll/exe 里存在**完整 ASCII 字符串** | 61 |
| B | 同样扫描，作为**子串命中且长度 ≥8** | 106 |
| C | H21 自带界面源（`config/**/*.ui`、`python3.11libs/**/*.py`、`toolbar`、`pdg`） | 349 |
| D | 低置信（<8 字符的短词裸字节命中），单独放一个文件 | 62 |
| X | 第三方 JS 文档误命中（codemirror），剔除 | 11 |

合计可并入 **516 条**。

反过来说，约 3,250 条在 21 里找不到任何字面证据，例如 `Enable UV Blending`、`Export Norm Slice ID`、`RizomUV Path`——这些在 21 的二进制里根本不存在，是 22 版新参数。

---

## 三、516 条真缺口长什么样

集中在"界面外壳"，而不是节点参数——这些是最该补的：

**标准按钮/菜单项**（B 档，最像"常规界面"）
- `Save All` 全部保存、`Restore Defaults` 恢复默认、`Import All` 导入全部
- `Add Files` 添加文件、`Add Item` 添加项、`Refresh List` 刷新列表
- `Yes to All` 全是、`No to All` 全否、`License Manager` 许可管理器
- `Multithreading` 多线程、`Polygon Count` 多边形数量、`Version Number` 版本号

**节点状态/标记**（B 档）
- `HDA Locked` HDA 已锁定、`HDA Unlocked` HDA 已解锁、`Node Locked` 节点已锁定
- `Node Needs to Cook` 节点需要计算、`Continuous Cook` 持续计算、`Node Unload` 节点卸载
- `Node Has Data` 节点有数据、`Child Error` 子级错误、`Warning Message` 警告消息
- `Footprint Flag` 足迹标记、`Has Take Data` 有 Take 数据

**状态栏 / 进度**（A 档，21 二进制里是完整串）
- `Press Esc to Cancel` 按 Esc 取消（正好是对方前缀规则里的那条）
- `DOP First Frame` DOP 第一帧、`Evaluating python` 正在求值 Python
- `Run Hscript command` 运行 Hscript 命令、`Updating Exports` 正在更新导出

**参数/属性标签**（A+B）
- `Unlock Parameters` 解锁参数、`Unlock Geometry` 解锁几何体
- `Cache Dir` 缓存目录、`Cached VEX Code` 已缓存的 VEX 代码、`Compression Settings` 压缩设置
- `Boundary Edges` 边界边、`UV Islands` UV 孤岛、`Sharp Corner` 尖角、`Flat Edges` 平边
- `Voxelize`/`Voxelization` 体素化、`Skylight` 天光、`Restitution` 弹性、`Decouple` 解耦
- `Harden UV Seams` 硬化 UV 接缝、`Rectangularize` 矩形化、`Downscale` 降尺度

**杂项高频词**（A 档）
- `Shortcut` 快捷键、`Progress` 进度、`Keyframe` 关键帧、`Uncooked` 未计算
- `Work Item Info` 工作项信息、`Enable Work Item Selection` 启用工作项选择
- `Casts Shadows` 投射阴影、`CHOP Networks` CHOP 网络、`Foreground` 前景
- `Select Color` 选择颜色、`Fixed Color` 固定颜色、`Custom 1/2/3` 自定义 1/2/3
- `Home Folder` 主文件夹、`Choose Image:` 选择图像：、`Milliseconds` 毫秒

**Python 面板 / USD 工具**（C 档）
- `Start typing to search` 开始输入以搜索、`Rename collection` 重命名集合
- `Import Global Ambient Light` 导入全局环境光、`Select Vertices to Pin` 选择要固定的顶点
- `Remove Bundle Nodes From Selection` 把捆绑中的节点移出选择
- `Avoid using : (colon) or . (dot) characters in the base name` 基础名称里请避免使用 :（冒号）或 .（句点）字符

**粘贴菜单**
- `Paste Expressions / Channels` 粘贴表达式 / 通道、`Paste Expression` 粘贴表达式
- `Paste Absolute References` 粘贴绝对引用、`Paste Relative Reference` 粘贴相对引用

---

## 四、四类结构性问题（比补词条更重要）

### 4.1 动态文案完全没接

你引擎里 `placeholder_templates.h` 已经有完整的 `{name:type}` 占位符机制（带整数/浮点类型校验、括号转义、字段上限），但**7 份词典的 `templates` 全部为空**，`templates_zh.json` 也是空壳。

而这类文案是运行时拼出来的，静态词典永远命中不了：

```
3 nodes cooked        ← 状态栏
12 seconds ago        ← 自动保存提示
Undo Move Points      ← 撤销历史
Edit shelf: my_tools  ← 工具架编辑器
```

对方用 71 条正则 + 7 条前缀规则解决。

**最小改动**：纯数字类规则改写成占位符模板即可，不需要引入正则引擎：

```json
"templates": {
  "{count:integer} nodes cooked": "{count} 个节点已计算",
  "{n:integer} seconds ago": "{n} 秒前",
  "Frame {frame:integer}": "帧 {frame}"
}
```

`Undo ` / `Redo ` / `Unable to undo ` / `Edit shelf: ` 这类前缀规则属于引擎能力，需要在 `lookupTranslation` 未命中后加一层"前缀剥离再查"的 fallback。

### 4.2 缺排除表，误翻了标识符

对方 `skip` 表 1,791 个唯一串里，有 **732 条在你词典里被翻译了**：

```
radius → 半径          height → 高度          index → 索引
normal → 法线          axis → 轴              Which → 选择项
primvars:displayColor → 图元变量：显示颜色      light:filters → 灯光过滤器
Uvoffset → UV 偏移     Valuebr → 右下值        ineditblocklabel → 编辑块内标签
```

这些既是界面文字、又是查询键（属性名、参数内部名、USD primvar 名）。你的引擎已有"编辑框和场景数据保留原文"的保护，但几何体电子表格、属性列表这类只读展示位仍会显示译文。

**建议**：不是照抄对方的 skip，而是挑出真标识符（小写单词、含 `:` 或 `_`、`primvars:*`、`light:*`）从词典剔除，保留 `SideFX Labs → SideFX Labs 工具集` 这种真标题。

同时提醒：**对方也有过度汉化**。`Acos → 反余弦`、`BD → 后向差分`、`FS → 前向步长`、`Fit01 → 映射到 0–1` 是 VEX 函数名，翻了反而妨碍查文档。这一块你的保守策略是对的。

### 4.3 59 个键自相冲突（潜伏 bug）

同一原文在两份词典里译法不同。引擎按 `_wcsicmp` 文件名升序加载、后者覆盖（`g_translations.insert_or_assign`）。目前 `dictionary_zz_houdini21_zh.json` 排最后，所以 README 声称的修复**确实生效**：

| 键 | 旧值（仍在库里） | 生效值 |
|---|---|---|
| `Box` | 盒（nodes_parameters） | **长方体**（zz） |
| `Main` | 主（nodes_parameters） | **主要**（zz） |
| `Delta Mush` | Delta Mush（nodes_parameters） | **平滑变形（Delta Mush）**（zz） |
| `Follow Parent Context pane button on/off` | 雾噪声会让雾更可信…（**help 词典，明显错配**） | 开启或关闭窗格的"跟随父级上下文"（zz） |

问题是：**旧的错误条目还躺在库里**。任何人调整打包顺序、重命名文件、或单独发布某份词典，这些错译就会重新出现——尤其 help 词典里那条"雾噪声"是致命错配。

建议直接在源头删掉旧条目，别只靠 zz 覆盖。（已确认：加载时 `insert_or_assign` 先合并成唯一表，才喂给精确查表和 `matching.h` 的模糊索引，所以运行时不会出现"同一键两个值"的歧义——风险纯粹来自"哪份文件最后加载"。）

### 4.4 术语没统一

你词典内部（同一批文件内）就在混用：

| 术语组 | 用量 |
|---|---|
| 纹理 / 贴图 | 1,325 / 915 |
| 噪波 / 噪声 | 511 / 433 |
| 三维 / 3D | 170 / 222 |
| 撤销 / 取消 | 57 / 224 |

对方 README 里"一个词只有一种译法""成组的值必须对称"两条，正是针对这个。建议先定一张 30 词左右的术语表，再批量替换。

---

## 五、19,505 条译文差异：要不要改？

抽查结论：**大部分是等价风格差异，不是错**。

```
EN : Load Sampled Points From Disk
你 : 从磁盘加载采样点            对方: 从磁盘载入采样点
EN : At Most One Surface Face per Tet
你 : 每个四面体最多一个表面面片    对方: 每个 Tet 至多一个表面面
EN : Schedule Work Items as Jobs
你 : 将工作项作为作业调度         对方: 将工作项作为任务调度
```

你的偏"名词短语"、更书面；对方偏"动词短语"、更短。两种都成立，不建议大规模替换。

但有 **68 条你这边译文等于英文原文**（等于没翻），对方有真译文，属于可捡的漏：

```
Acos → 反余弦   Epsilon → 极小值   Deadline → 截止时间
Detail ID → 细节 ID   Delta Mush → Delta Mush 平滑
```

（其中 `Acos`/`BD`/`FS`/`Fit01` 是 VEX 函数名，见 4.2——这类**不建议并**。）

另外你库里还有少量机翻痕迹值得单独修：

```
1 Float Default → 单浮点默认值        （应为"1 浮点默认值"）
2 Float Default → 双浮点默认值
3 Float Default → 三浮点默认值
```

---

## 六、建议动作顺序

| 优先级 | 动作 | 工作量 | 产物 |
|---|---|---|---|
| P0 | 并入 516 条真缺口 | 10 分钟 | `dictionary_imported_xingkong_zh.json`（已生成） |
| P0 | 删掉 59 处冲突的旧条目（尤其 help 里"雾噪声"那条） | 半小时 | — |
| P1 | 把纯数字动态规则写成 `templates` | 半小时 + 测试 | 可参照 `xingkong_regex_rules.txt` |
| P1 | 挑出真标识符从词典剔除 | 1–2 小时 | 用 `our_translations_xingkong_skips.tsv` 筛 |
| P2 | 统一术语（先定 30 词表） | 半天 | — |
| P2 | 给引擎加前缀规则 fallback | 改 `hook.cpp` | 补丁 |
| P3 | 用 Shift+~ 在真实会话里跑一遍捕获 | 自己用一遍 | `untranslated_captured.pending.json` |

最后一条最值钱：外部词库再全也不如你自己机器上跑一遍 Shift+~ 扫描，它抓的是**当前版本、当前界面**真正显示的未翻译串。目前 `C:\Users\84747\Documents\houdini21.0\packages\HoudiniChinese\translations\` 下还没有这个文件，说明这个功能还没正式用过。

---

## 七、授权提醒

对方词条按 **CC BY-SA 4.0** 发布（署名 + 相同方式共享），你的项目是 **GPL-3.0**。CC BY-SA 与 GPL 混用在法律上是有争议的组合，尤其"相同方式共享"的传染范围。

务实做法：

- 把导入的词条放**单独文件** `dictionary_imported_xingkong_zh.json`（已按此方式生成，description 里写了来源），与原有 GPL 词典分开
- README 加一句：部分词条来自星空汉化 Houdini-zh（CC BY-SA 4.0）
- 或者更保守：只把这份当**校对基准**，人工确认后用自己的话重写进主词典

---

## 八、产物清单

| 文件 | 说明 |
|---|---|
| `dictionary_imported_xingkong_zh.json` | **516 条**已验证缺口（A 61 + B 106 + C 349），可直接放入 `translations/` |
| `dictionary_imported_xingkong_lowconf_zh.json` | 62 条低置信短词（D 档），人工挑选后再用 |
| `gaps_tiered.tsv` | 全部 516+62 条 + 证据来源与档位 |
| `review_short_labels.tsv` | 约 13,000 条短标签的"我方 / 星空"译文对照，用于审校 |
| `our_translations_xingkong_skips.tsv` | 732 条被对方标"别翻"而你翻了，用于筛标识符 |
| `xingkong_regex_rules.txt` | 对方 71 条正则 + 7 条前缀规则原文，写模板时参照 |

### 接入方式

放进 `translations/dictionary_imported_xingkong_zh.json` 后，`tools/prepare21.py` 需加一行（现有逻辑只从更新 ZIP + 两份本地词典取文件）：

```python
imported = ROOT / 'translations' / 'dictionary_imported_xingkong_zh.json'
shutil.copy2(imported, PACKAGE / 'HoudiniChinese/translations' / imported.name)
```

`dictionary_manifest.json` 会自动 glob `*_zh.json`，不用手改。加载顺序上 `dictionary_imported_*` 排在 `dictionary_help_*` 之后、`dictionary_menus_*` 之前，不会覆盖 zz 的修复值（516 条本身已排除与现有键重复的条目）。

---

## 九、方法学与局限

- 缺口判定：精确键匹配 + NFKC 归一（去 `&`、`…`→`...`、压空白、去尾部标点、小写）双重命中
- H21 存在性验证：ASCII 串提取（UTF-16LE 在 Houdini 二进制里几乎不存在，`Houdini.exe` 仅 23 条完整串），A 档要求 ≥8 字符完整串，B 档放宽为子串、C 档在自带界面源里找
- **未覆盖的验证源**：HDA/OTL 内部字符串、`otls` 目录、帮助文档正文。因此 516 是**下限**
- B 档子串命中意味着该词出现在更长的字符串里，不等于一定有条独立标签；但 106 条抽查后基本都是标准界面词
- 未做性能回归测试，也未与插件运行时行为联调
- 复现脚本留在 `_compare/` 下：`compare.py`（词条 diff）、`h21full.py`（二进制完整串）、`h21text.py`（短键+文本源）、`finalize4.py`（分档出补丁）
