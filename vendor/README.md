# 上游数据（vendor）

这里的文件来自上游 HoudiniChinese 更新包，仅保留构建发行包所需的数据与 Python 文件：

```
HoudiniChinese/translations/*.json      5 份基础词典
HoudiniChinese/python3.13libs/*.py      启动脚本、pythonrc、uiready
```

`tools/prepare21.py` 会优先读取本目录（自包含，克隆即可构建），
其次才是环境变量 `HOUDINI_CHINESE_UPSTREAM_ZIP` 指向的更新包。
二进制（DLL）不在此目录，由本仓库源码在本地编译生成。

词典版权与授权见仓库根目录 `LICENSE` 与 `docs/THIRD_PARTY.md`。
