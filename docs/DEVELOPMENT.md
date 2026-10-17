# 开发与工具

## 目录

```
source/             C++ 引擎（hook.cpp、hooks/、translation/、ui/）
python3.11libs/     拼音节点搜索 + 数据（pinyin_table.json、node_label_zh.json）
translations/       本机覆盖词典（dictionary_zz/zzz_*）
data/               第三方参数摘要数据
tools/              构建、打包、审计、数据生成
tests/              C++ 与 Python 回归测试
docs/               文档；docs/archive/ 为历史记录
```

## 构建与测试

```bat
build21.cmd             :: prepare21.py → 编译 loader/detours/引擎 → 打包
deploy-engine21.cmd     :: 把新引擎装进现装包（需先完全退出 Houdini，自动备份）
test21.cmd              :: Python 回归
test-native21.cmd       :: 原生钩子回归
build-installer21.cmd   :: Inno Setup 6 生成单文件安装器
python tools\package21.py
```

`tools/prepare21.py` 从源码词典与 Python 文件生成发行包，并接入拼音搜索模块与
help 覆盖层所需数据；`build21.cmd` 会自动调用它。上游数据已内置在 `vendor/upstream/`，
克隆后无需外部更新包即可构建。

## 在另一台机器复刻

前置：**Houdini 21.0.440**（提供 SDK 头文件/库和 `nodes.zip`）、
**Visual Studio 2022 Build Tools**（C++ 桌面开发）、**Python 3.11+**；
只有生成安装器时才需要 **Inno Setup 6**。

```bat
git clone https://github.com/Neocvsu-commits/Houdini-21.0.440-Chinese-localization-.git
cd Houdini-21.0.440-Chinese-localization-
set HFS=D:\你的路径\Houdini 21.0.440
build21.cmd
:: 完全退出 Houdini 后
deploy-engine21.cmd
python tools\build_help_overlay21.py --install --hfs "%HFS%"
python tools\clear_help_cache21.py
:: 重新出包（可选）
python -X utf8 tools\package21.py
build-installer21.cmd
```

引擎二进制不在仓库里，必须在本地编译；`HFS` 未设置时 `prepare21.py` 会退回到
开发机默认路径并报错，请显式指定。

## 数据流水线

| 任务 | 命令 | 产物 |
|---|---|---|
| 术语/语义修订 | `python tools\build_fixes21.py` | `translations/dictionary_zzz_fixes_zh.json` |
| 节点标签 + 拼音表 | `python tools\build_node_labels21.py` | `python3.11libs/node_label_zh.json`、`pinyin_table.json` |
| help 覆盖层 | `python tools\build_help_overlay21.py [--install\|--uninstall]` | `build/help_overlay` → 用户 `help/nodes/` |
| 清理帮助缓存 | `python tools\clear_help_cache21.py` | 删除 `config/Help/cache/{nodes,shelf}` |
| 界面文案审计 | `python tools\audit_ui_strings21.py` | `build/ui_audit/uncovered.tsv` |
| 工具提示导出 | `hython tools\dump_shelf_tooltips21.py` | `build/shelf_tooltips.json` |
| 一键收集 | `collect-tooltips21.cmd` | 导出 + 审计 + 摘要 |

审计来源：`.ui/.gad`、`python3.11libs`、`config/Help/cache`、`bin/*.dll`（候选池）。
当前界面文案覆盖率约 **97.6%**，剩余多为截断片段与控件标识符。

## 新增内容的入口

以后要补内容，只改这几处，不用碰 C++：

| 想加什么 | 改哪里 | 怎么生效 |
|---|---|---|
| 界面词条 | 新建 `translations/dictionary_zzz_*.json`（`translations` 字段，按文件名最后加载） | 放入用户包 `translations/`，重启 Houdini |
| 参数提示摘要 | `data/help_summaries_local.json`（英文原文 → 中文摘要，覆盖 Stars401 同名项） | `build_help_overlay21.py --install`，重启 + 清缓存 |
| 工具提示 | 优先用 `collect-tooltips21.cmd` 导出 → 补译进上面的词条文件 | 重启 Houdini |
| 节点中文名 / 拼音 | 先补词条，再跑 `build_node_labels21.py` | 覆盖 `python3.11libs/` 两个 JSON，重启 |
| 术语修订 | `tools/build_fixes21.py` 里的 `REWRITES` / `SEMANTIC` | 重新生成 `zzz_fixes`，重启 |

约定：`translations/dictionary_zzz_*` 永远最后加载，可以安全地覆盖上游词典；
`data/help_summaries_local.json` 是人工补充入口，`_` 开头的键会被忽略。

## 覆盖层生成规则

- 只在 `@parameters` 段内处理：缩进 ≥4 空格、大写开头、不以冒号结尾的说明行
- 匹配顺序：整句 → 去空白后前缀互验 → 首句；数据来自
  `data/help_summaries_stars401.json`、`data/help_summaries_local.json` 与本插件词典
  （仅取 ≤80 字短译文）
- 摘要后补一个空行，使其单独成段
- 已存在 `【` 的行不重复插入；写出前校验非空且无 `\x00`

## 测试现状

`pytest tests/python`：46 通过 / 9 失败。失败集中在 `test_hook_catalog` 与
`test_package_assets` 的既有钩子目录校验，与本次功能无关；新增的
`test_nodesearch.py` 13 项全部通过。

## 回滚

- 引擎二进制：`HoudiniChinese-backups\engine-<时间戳>\` 覆盖回 `packages/HoudiniChinese/`
- help 覆盖层：`--uninstall` 或删除 `help/nodes/`
- 词典：删除 `translations/dictionary_zzz_*.json` 即可恢复旧译文
