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
test21.cmd              :: Python 回归
test-native21.cmd       :: 原生钩子回归
build-installer21.cmd   :: Inno Setup 6 生成单文件安装器
python tools\package21.py
```

`tools/prepare21.py` 从源码词典与 Python 文件生成发行包，并接入拼音搜索模块与
help 覆盖层所需数据；`build21.cmd` 会自动调用它。

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

## 覆盖层生成规则

- 只处理「缩进 ≥8 空格 + 大写开头 + 不以冒号结尾」的描述行，避免把参数名当描述
- 匹配顺序：整句 → 去空白前缀互验 → 首句；数据来自 `data/help_summaries_stars401.json`
  与本插件词典（仅取 ≤80 字短译文）
- 已存在 `【` 的行不重复插入；写出前校验非空且无 `\x00`

## 测试现状

`pytest tests/python`：46 通过 / 9 失败。失败集中在 `test_hook_catalog` 与
`test_package_assets` 的既有钩子目录校验，与本次功能无关；新增的
`test_nodesearch.py` 13 项全部通过。

## 回滚

- 引擎二进制：`HoudiniChinese-backups\engine-<时间戳>\` 覆盖回 `packages/HoudiniChinese/`
- help 覆盖层：`--uninstall` 或删除 `help/nodes/`
- 词典：删除 `translations/dictionary_zzz_*.json` 即可恢复旧译文
