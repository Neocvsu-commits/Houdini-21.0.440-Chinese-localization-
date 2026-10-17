# 第三方与许可

## 源码

| 组件 | 许可 | 说明 |
|---|---|---|
| 本插件源码 | GPL-3.0 | 沿用上游 HoudiniChinese 项目 |
| Microsoft Detours | MIT | `source/detours/LICENSE.md` |
| Qt 头文件（构建期） | LGPL/GPL | `third_party/qt-6.5.3-headers/PROVENANCE.md` |

## 词条与数据

| 来源 | 许可 | 用途 |
|---|---|---|
| 星空汉化 [Houdini-zh](https://github.com/Ker0el/Houdini-zh) | CC BY-SA 4.0 | 部分词条的对照与校对基准 |
| [Stars401/Houdini_ZH_Tooltip](https://github.com/Stars401/Houdini_ZH_Tooltip) | MIT | 参数提示中文摘要（`data/help_summaries_stars401.json`） |
| 上游 HoudiniChinese 作者词典 | 随项目 | `dictionary_help/menus/nodes/...` 基础词典 |

分发时请保留以上署名与许可声明。Houdini 界面英文原文版权归 Side Effects Software Inc.，
本项目只提供中文对照。

## 为什么使用 help 覆盖层

参数提示正文由 Houdini 帮助系统通过 `bookish.OverlayStore` 加载，用户偏好目录下的
`help/nodes/` 会覆盖安装目录 `nodes.zip` 的同名文件。用文件覆盖可以在不改动安装目录、
不依赖钩子的前提下改变提示正文，与引擎的 UI 翻译互补。
