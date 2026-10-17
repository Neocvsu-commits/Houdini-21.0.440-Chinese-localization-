# UI_Look::draw 挂接失败诊断

日期：2026-09-06。调查基线：`9b18ac2`，Houdini 22.0.368 / Qt 6.8.3 / Windows x64。
本轮限时 20 分钟，目标是定位安装失败并完善诊断；不合并 Hook，不修改重试策略或翻译行为。

## 已定位的问题

独立 Houdini 诊断进程 PID `11280` 复现了失败：

| 项目 | 实测结果 |
| --- | --- |
| 目标入口 | libUI.dll 的 UI_Look::draw |
| 模块基址 / 目标地址 | `0x11a30000` / `0x11c74270` |
| 符号查找失败次数 | 0 |
| 安装尝试次数 | 3，已达到重试上限 |
| 最后失败阶段 | `detour_allocate_trampoline` |
| 顶层错误 | 8（ERROR_NOT_ENOUGH_MEMORY） |
| 最后一次地址查询次数 | 1022 |
| VirtualQuery 错误 | 0 |
| VirtualAlloc 尝试次数 | 0 |
| 入口代码状态 | 与磁盘原始入口前 32 字节一致，未被 inline 改写 |

**直接原因：在该次安装搜索中，没有可供 Detours 跳板使用的近地址空间。**
本地 Detours x64 要求跳板在目标附近的相对跳转范围内，新区域按 64 KiB 分配。
早期只读地址采样范围为 `0x80000..0x91bf4270`，其中未找到连续 64 KiB 空闲区域，最大的空闲片段为 61,440 字节（60 KiB）。
因此搜索完成后直接返回分配失败，尚未调用 VirtualAlloc，更没有执行目标代码页的 VirtualProtect。
这不是“符号丢失”或“函数机器码不支持”的同义词，也不能用这个错误推断整机物理内存耗尽。

**导致失败持续的因素：地址空间后来可用了，但重试预算已耗尽。**
同一进程的后续采样在 `0x04680000..0x10680000` 发现了 192 MiB 连续空闲区。
60 秒诊断报告中，UI_Look::draw 仍是 `attempts=3`、`inline_retry_limit_reached=true`、`inline_patched=false`。
现有 `source/hooks/hook_lifecycle.h` 设置最多 3 次尝试、尝试间隔至少 10 秒；达到上限后，不会因地址空间释放自动再试。

这些证据说明本次存在“启动期间目标附近空间不足，之后空间释放，但预算已用完”的时序问题。
尚未定位具体哪个 Houdini 子系统拥有或释放这些临时保留区；不能把地址占用归因于某个未经确认的分配器。

## 如何排除其他解释

1. 在独立程序内，以 SEC_IMAGE 映射同一 libUI.dll 到对应地址，只尝试挂接并中止事务；不初始化 DLL、不解析其导入、不执行任何 Houdini 私有函数。原始机器码可成功挂接。
2. 使用现有老进程 PID 19784 的地址查询结果回放搜索，同时只在诊断程序自身进行分配和代码页权限调整，也能成功。
3. 新进程 11280 启动早期的真实插件报告明确落在跳板分配阶段；此时只读采样也没有足够的空闲块。
4. 新进程稍后的地址布局回放中，搜索在 `0x0c670000` 找到可用位置，独立程序完成分配、指令重定位和目标页权限调整，DetourAttach 返回 0。
5. 人为让独立程序的 VirtualAlloc 返回 487，得到顶层错误 8，细分阶段为 `detour_allocate_trampoline`；人为让目标 VirtualProtect 返回 8，细分阶段准确变成 `detour_protect_target`。

回放程序只读取 Houdini 地址区域元数据；所有实际分配、挂接尝试和事务中止均发生在自身进程中。
回放成功不能代替在 Houdini 内执行补挂接及完整界面回归，本轮没有做这一步。
地址采样不是暂停全部线程后的原子快照，区域可随启动过程变化；早晚采样的变化正是本次观察的一部分。

## 新增的诊断能力

`source/detours/detours.h/.cpp` 增加项目内使用的 `DetourGetAttachDiagnostic`，记录当前线程最近一次 AttachEx 的阶段和分配搜索信息。
阶段包括参数/事务验证、操作对象分配、跳板分配、指令重定位、目标页权限修改。
`source/hook.cpp` 在 Attach 返回后立即复制结果；失败时把细分阶段写入现有 `last_error_stage`。

每条 Hook JSON 记录新增 `detour_diagnostic`：

| 字段 | 含义 |
| --- | --- |
| stage | 最近一次 Attach 到达的阶段；需结合 last_error 和 inline_patched 判断是否失败 |
| allocation_attempts | 该次跳板搜索实际调用 VirtualAlloc 的次数 |
| allocation_error | 该次搜索最近一次 VirtualAlloc 失败的错误码；0 不等于分配成功，可能根本未调用 |
| query_count | 该次跳板地址搜索调用 VirtualQuery 的次数 |
| query_error | 该次搜索最近一次 VirtualQuery 失败的错误码 |

成功搜索可能先经过失败候选，因此非零底层错误也不能覆盖最终 Attach 成功结论。
每次 Attach 重置 Detours 线程内数据；每次新的 inline 尝试清除记录中的旧明细，避免前一次失败残留被误认为本次失败。
导入表回退随后产生的错误可能更新 `last_error_stage`，`detour_diagnostic` 仍只描述最近一次 Attach，二者含义不同。

诊断只在安装阶段更新固定字段；没有增加绘制期间查词、日志、地址扫描、后台定时器或新的安装重试。
JSON 仍通过原有显式诊断请求生成。

## 验证与产物

- 完整引擎编译通过。
- `tests/diagnostics.bat` 通过，包含错误明细序列化和重试时清除旧明细的检查。
- `tests/hook_registry.bat` 通过。
- 独立程序的正常挂接及两种错误 8 故障注入通过。
- 独立 Houdini 确认加载新诊断引擎，并输出 1、5、15、30、60 秒报告。
- 未修改 Hook 清单、入口选择、重试上限、输入保护、词典处理或中英刷新逻辑。

独立包：`dist/HoudiniChinese-Houdini-22.0.368-hook-install-diagnostic.zip`。
独立构建：`build/out-hook-install-audit/`。
正式部署目录未被覆盖；标准 ZIP 保持原有 `stable-1b60edb` 副本，另有已保存的 `9b18ac2` 版本包。
本次诊断包不标为新的界面验收稳定版。

本机原始证据（build 目录被 Git 忽略，清理后可能不存在）：

- `build/hook-install-audit/runtime/hooks-5s.json`、`hooks-60s.json`：真实安装状态。
- `build/hook-install-audit/memory-11280.json`：早期无 64 KiB 空闲区的采样。
- `build/hook-install-audit/memory-11280-late.json`：后期出现 192 MiB 空闲区的采样。
- `build/hook-install-audit/probe-live-layout-11280.txt`：后续地址搜索回放成功。
- `build/hook-install-audit/probe-allocate.txt`、`probe-protect.txt`：两种错误的故障注入。
- `build/hook-install-audit/allocation_probe.cpp`、`memory_probe.py`：独立回放和只读采样工具。

## 下一步建议，尚未实施

对“跳板空间不足”这一可恢复失败，研究在 UI 启动完成后安排一次有界补挂接机会；先判断近地址空间是否出现合适候选，再进入需要暂停宿主线程的安装事务。
不能只把永久重试频率调高，也不能无限放宽预算。
重试状态应区分暂时资源不足与符号/ABI/指令不支持；保留现有回退入口，成功后再核验真实调用和输入保护。
在宿主内补挂接可能改变实际覆盖路径，仍需节点、材质、视口及输入场景对照，不能以独立程序成功代替验收。

本轮只定位原因并增加诊断，不实施补挂接策略，不合并 Hook。

---

## 后续实施（2026-09-06 补充）

已实施第一版根因修复：

- `source/detours/detours.cpp` 的 `DETOUR_REGION_SIZE` 由 `0x10000`（64 KiB）改为 `0x1000`（4 KiB）。
- 同步修正 `detour_alloc_trampoline` 和 `detour_free_trampoline` 的硬编码 64 KiB 掩码，改为按 `DETOUR_REGION_SIZE` 对齐。
- 这样启动早期即使只有 60 KiB 空闲区，也可能满足 `RegionSize >= DETOUR_REGION_SIZE`，不再必须等待 192 MiB 空闲区出现。

尚未实施：

- 模块级跳板池复用；
- 有界启动后补挂接；
- 真实 Houdini UI 场景回归验证。

需验证：

- `tests\diagnostics.bat` / `tests\hook_registry.bat`；
- 独立 Houdini 中 `UI_Look::draw` 是否在早期即安装成功；
- 确认 4 KiB Region 下的 `VirtualProtect`、释放和指令缓存刷新正常。

## 对齐修正与独立宿主验证（2026-09-06 03:46）

上节 4 KiB 修改不能单独作为修复结论：MEM_RESERVE 的地址必须按
GetSystemInfo().dwAllocationGranularity 对齐，不能把提交大小当作地址步长。
现在保留 4 KiB 跳板区域，将候选地址对齐、搜索步长、可用空间预检查统一改用
Windows 实际分配粒度，并增加搜索上界检查。区域保护和区域头回查仍使用区域大小。
未使用现有进程中其他组件的保留页，也未修改目标函数的 ABI 或删除备用 Hook。

实验包在 uiready 后进行 2、15、45、120 秒四次有界只读检查；找到可用空间时，
最多增加一次挂载事务。成功即结束，不持续轮询。实验仍由环境开关启用。

独立 Houdini 22.0.368，PID 26008 的 hooks-50s.json 已确认：
- UI_Look::draw：inline_patched=true，last_error=0，attempts=4。
- late_look_recovery：installed，checks=3，extra_attempts=1。
- 实际分配尝试 1 次，分配错误 0；正常启动的三次尝试仍在早期空间不足时失败。
- 该时点 observed_calls=0，不能把挂载成功当成真实绘制覆盖已经验证。

本次回归通过：4 KiB 区域在 Windows 分配粒度边界上的双向申请、拒绝不可独立
预留的尾部空隙、多区域分配和释放；重试策略、注册表、诊断序列化；Qt 实际绘制、
输入保护、中英宽度恢复及同页刷新。

测试包：dist/HoudiniChinese-Houdini-22.0.368-look-fix-experiment.zip。
正式部署未覆盖；源码改动尚未提交。

随后 hooks-125s.json 确认真实绘制调用 7,244 次，inline_patched=true、last_error=0。因此本次同时验证了安装和实际入口执行；仍不代表所有 UI 场景已完成覆盖验收。

复现测试包：将 HOUDINI_CHINESE_OUT 指向 build/out-look-fix，运行 tools/build.bat，再运行 tools/prepare_look_experiment.py。后者保存实验包启动通知与诊断脚本，仅用于独立实验包，默认正式构建不启用延迟补挂载。
