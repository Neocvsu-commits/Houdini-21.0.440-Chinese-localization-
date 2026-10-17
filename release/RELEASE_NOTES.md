## 更新内容

- **F4 拼音节点搜索**：中文、英文、全拼、首字母、模糊音（`zh/z`、`ch/c`、`sh/s`、`ang/an`、`n/l`）与中英混输（如 `uv贴图`）；Tab 保持 Houdini 原生菜单。
- **参数提示中文摘要**：2,787 个节点的参数提示顶部增加一行中文摘要（覆盖层不随安装包分发，见下方"可选步骤"）。
- **引擎修正**：切回英文时用会话级反查缓存还原；参数提示头部 `Parameter:` → `参数：`；提示页脚中文化；容忍提示末尾的 `(type:parm)` 参数标识。
- **词典**：10 份、合计 108,972 条；新增术语修订、覆盖率补充、星空汉化验证缺口 417 条。
- **工具**：覆盖层生成/卸载、帮助缓存清理、界面文案审计、工具提示导出。

## 安装

1. 关闭 Houdini
2. 运行 `HoudiniChineseInstaller-21.0.440.exe`；或解压 `HoudiniChinese-21.0.440.zip` 后运行 `install.bat`
3. 启动 Houdini，按 **F3** 或顶部「中/英」切换语言

### 可选步骤：参数提示摘要

```powershell
python tools\build_help_overlay21.py --install
python tools\clear_help_cache21.py
```

需要本机有 Houdini 21.0.440 与 Python 3.11+；完成后重启 Houdini。

## 校验

- `HoudiniChineseInstaller-21.0.440.exe`
  SHA-256：`42082e98e770c06feba07754c5402a35218ee44bb8ce5e59ae6983667cb6e53d`

## 兼容性

只支持 Windows x64 的 Houdini **21.0.440**（Python 3.11 / Qt 6.5.3 / HDK 21000440）。
不修改 Houdini 安装目录，不改工程文件。
