# HoudiniChinese · Houdini 21.0.440 中文本地化

Houdini 21.0.440（Windows x64）的**显示层**汉化：菜单、参数标签、工具提示、节点检索。
不修改安装目录，不改工程文件，**F3 随时中英切换**。

## 安装

1. 关闭 Houdini
2. 运行 `HoudiniChineseInstaller-21.0.440.exe`；或解压 ZIP 后运行 `install.bat`
3. 重启 Houdini，按 **F3** 切换语言

安装器覆盖前会备份同名包，之后可从 Windows「已安装的应用」卸载并恢复。
安装器未做商业签名，首次运行可能有 SmartScreen 提示。

## 快捷键

| 按键 | 作用 |
|---|---|
| **F3** / 顶部「中/英」 | 中英切换（右键按钮可改快捷键） |
| **F4** | 节点搜索：中文 / 拼音 / 英文 |
| **Shift+Enter** | 搜索面板创建节点后保持打开，可连续创建 |
| **Ctrl+L** | 搜索面板切换「全部分类」 |
| **Tab** | Houdini 原生 Tab 菜单（不接管） |
| **Shift+~** | 扫描当前界面未翻译文本，导出待补清单 |

## 包含内容

| 组件 | 说明 |
|---|---|
| 词典 | `translations/` 下 9 份，约 10.8 万条 |
| 节点搜索 | F4 呼出；支持全拼、首字母、模糊音、中英混输；当前类别无结果时自动跨类别 |
| 参数提示 | 2,787 个节点的参数提示顶部插入中文摘要（覆盖层不随安装包分发，需运行 tools/build_help_overlay21.py 生成） |
| 工具 | 覆盖率审计、词典修订、help 覆盖层生成、工具提示导出 |

## 已知限制

- 只支持 **21.0.440**（Python 3.11 / Qt 6.5.3 / HDK 21000440）
- 第三方节点、自定义端口、后续安装的 shelf 不保证覆盖
- 参数提示摘要覆盖 2,787 / 4,729 个节点（覆盖层需单独生成），未命中的保持英文
- Houdini 启动页是网页内容，不在覆盖范围
- 节点图上与词典重名的名称可能显示译文，但编辑框与场景数据保留原文

## 卸载与禁用

- 安装器：Windows「已安装的应用」卸载
- 手动：退出 Houdini，删除 packages 下的 `HoudiniChinese.json` 与 `HoudiniChinese/`
- 参数提示覆盖层：`python tools/build_help_overlay21.py --uninstall`
- 仅禁用：把 `HoudiniChinese.json` 改名为 `HoudiniChinese.json.disabled`

## 构建

```bat
build21.cmd            :: 编译引擎并打包
test21.cmd             :: 回归测试
build-installer21.cmd  :: 生成安装器（需 Inno Setup 6）
```

依赖：Houdini 21.0.440 SDK、Visual Studio 2022 Build Tools、Python 3.11+。

## 文档

- [功能说明](docs/FEATURES.md)：节点搜索、参数提示、语言切换行为
- [开发与工具](docs/DEVELOPMENT.md)：构建、测试、数据流水线
- [兼容性](docs/COMPATIBILITY.md)
- [第三方与许可](docs/THIRD_PARTY.md)
- [历史记录](docs/archive/)：早期审计与钩子调研

## 许可

源码沿用原项目 **GPL-3.0**；Detours 许可证见 `source/detours/LICENSE.md`。
词条与第三方数据来源见 [docs/THIRD_PARTY.md](docs/THIRD_PARTY.md)。
