"""中文节点搜索：按中文标签的拼音（支持模糊音）检索并创建节点。

设计要点
- 索引来自 hou 实时枚举，自定义 HDA 也能被英文名检索到
- 中文标签取自 node_label_zh.json（由插件词典在构建期导出）
- 拼音由随模块分发的 pinyin_table.json 现算，无第三方依赖
- 模糊音：zh/z、ch/c、sh/s 不分，an/ang、en/eng、in/ing 不分，n/l 不分
"""

import json
import os
import re
import sys

_MODULE_DIR = os.path.dirname(os.path.abspath(__file__))
_PINYIN_FILE = os.path.join(_MODULE_DIR, "pinyin_table.json")
_LABEL_FILE = os.path.join(_MODULE_DIR, "node_label_zh.json")

_CJK = re.compile(r"[\u3400-\u4dbf\u4e00-\u9fff]")
_NON_LABEL = re.compile(r"[^0-9a-zA-Z\u3400-\u4dbf\u4e00-\u9fff]+")

_pinyin_table = None
_label_map = None
_log_once = set()


def _log(message, console=False):
    """默认只写日志文件；需要排查时设 HOUDINI_CHINESE_NODESEARCH_DEBUG=1 才输出到控制台。

    往 stderr 打印会让 Houdini 把 Console 窗口弹到前面，日常使用很干扰，
    所以默认静音。
    """
    text = "HoudiniChinese node search: %s" % message
    if console or os.environ.get("HOUDINI_CHINESE_NODESEARCH_DEBUG") == "1":
        try:
            print(text, file=sys.stderr)
        except Exception:
            pass
    try:
        path = os.path.join(os.environ.get("TEMP", "."), "houdini_chinese_nodesearch.log")
        with open(path, "a", encoding="utf-8") as handle:
            handle.write(text + "\n")
    except Exception:
        pass


def _log_first(key, message):
    if key in _log_once:
        return
    _log_once.add(key)
    _log(message)


def _table():
    global _pinyin_table
    if _pinyin_table is None:
        try:
            with open(_PINYIN_FILE, encoding="utf-8") as handle:
                _pinyin_table = json.load(handle)
        except (OSError, ValueError):
            _pinyin_table = {}
    return _pinyin_table


def _labels():
    global _label_map
    if _label_map is None:
        try:
            with open(_LABEL_FILE, encoding="utf-8") as handle:
                _label_map = json.load(handle)
        except (OSError, ValueError):
            _label_map = {}
    return _label_map


def fuzzy_syllable(syllable):
    """把一个音节折叠成模糊音形式。"""
    if syllable.startswith("zh"):
        syllable = "z" + syllable[2:]
    elif syllable.startswith("ch"):
        syllable = "c" + syllable[2:]
    elif syllable.startswith("sh"):
        syllable = "s" + syllable[2:]
    if syllable.endswith("iang"):
        syllable = syllable[:-4] + "ian"
    elif syllable.endswith("uang"):
        syllable = syllable[:-4] + "uan"
    elif syllable.endswith("ang"):
        syllable = syllable[:-3] + "an"
    elif syllable.endswith("eng"):
        syllable = syllable[:-3] + "en"
    elif syllable.endswith("ing"):
        syllable = syllable[:-3] + "in"
    if syllable.startswith("n"):
        syllable = "l" + syllable[1:]
    elif syllable.startswith("l"):
        syllable = "l" + syllable[1:]
    return syllable


_variant_cache = {}


def label_variants(text, limit=6):
    """把一个中文标签展开成若干 (全拼, 模糊全拼, 首字母, 模糊首字母) 变体。"""
    if not text:
        return []
    cached = _variant_cache.get(text)
    if cached is not None:
        return cached
    table = _table()
    beams = [([], [], [])]
    for char in text:
        readings = table.get(char) if _CJK.match(char) else None
        if readings:
            expanded = []
            for syllables, strict, soft in beams:
                for reading in readings[:2]:
                    expanded.append((syllables + [reading],
                                     strict + [reading],
                                     soft + [fuzzy_syllable(reading)]))
            if expanded:
                beams = expanded[: limit * 2]
            continue
        literal = char.lower()
        if literal.isalnum():
            # 混排标签（UV 拼贴 / RBD 破碎）把 ASCII 也编进拼音串
            beams = [(syllables + [literal], strict + [literal], soft + [literal])
                     for syllables, strict, soft in beams]
    variants = []
    seen = set()
    for syllables, strict, soft in beams:
        if not syllables:
            continue
        full = "".join(strict)
        key = full
        if key in seen:
            continue
        seen.add(key)
        variants.append((full,
                         "".join(soft),
                         "".join(word[0] for word in strict),
                         "".join(word[0] for word in soft)))
        if len(variants) >= limit:
            break
    _variant_cache[text] = variants
    return variants


def normalize(text):
    return _NON_LABEL.sub(" ", text or "").strip().lower()


def soft_form(text):
    """把用户输入的拼音做同样的模糊音折叠（zh->z、ch->c、sh->s、后鼻音归前鼻音）。"""
    text = text.replace("zh", "z").replace("ch", "c").replace("sh", "s")
    for suffix, short in (("iang", "ian"), ("uang", "uan"),
                          ("ang", "an"), ("eng", "en"), ("ing", "in")):
        text = text.replace(suffix, short)
    return text


def word_prefix_match(label, query):
    """多词查询逐词匹配：每个词是标签里某个词的前缀（或子串）。"""
    words = label.split()
    if not words:
        return None
    tier = 1
    index = 0
    for piece in query.split():
        matched = False
        while index < len(words):
            word = words[index]
            index += 1
            if word.startswith(piece):
                matched = True
                break
            if piece in word:
                matched = True
                tier = max(tier, 2)
                break
        if not matched:
            return None
    return tier


def _match_tier(value, query, allow_subsequence=True):
    """返回 (层级, 跨度)，层级越小越精确。"""
    if not value or not query:
        return None
    if value == query:
        return (0, 0)
    index = value.find(query)
    if index == 0:
        return (1, 0)
    if index > 0:
        return (2, index)
    if not allow_subsequence or len(query) < 3:
        return None
    position = -1
    first = -1
    last = -1
    for char in query:
        position = value.find(char, position + 1)
        if position < 0:
            return None
        if first < 0:
            first = position
        last = position
    span = last - first + 1 - len(query)
    # 只接受"漏打几个字"的容错，拒绝跨半个词硬凑出来的命中
    if span > 3:
        return None
    return (3, span)


class Entry:
    __slots__ = ("name", "category", "label_en", "label_zh", "fields")

    def __init__(self, name, category, label_en, label_zh):
        self.name = name
        self.category = category
        self.label_en = label_en
        self.label_zh = label_zh
        self.fields = []


def _prepare(entry):
    """构造检索字段：英文类型名、英文标签、中文标签、拼音、首字母。"""
    fields = []
    name = entry.name.lower()
    label = entry.label_en.lower()
    fields.append((name, 0, False))
    fields.append((label, 1, False))
    if entry.label_zh:
        fields.append((entry.label_zh, 2, False))
        for full, soft, initials, soft_initials in label_variants(entry.label_zh):
            fields.append((full, 3, True))
            fields.append((soft, 4, True))
            fields.append((initials, 5, False))
            fields.append((soft_initials, 6, False))
    entry.fields = fields


def build_index(category=None):
    """枚举节点类型。category 为 None 时包含所有类别。"""
    import hou

    labels = _labels()
    categories = hou.nodeTypeCategories()
    entries = []
    for category_name, category_object in categories.items():
        if category is not None and category_name != category:
            continue
        try:
            node_types = category_object.nodeTypes()
        except Exception:
            continue
        for name, node_type in node_types.items():
            try:
                description = node_type.description()
            except Exception:
                description = ""
            if not description:
                description = name
            label_zh = labels.get(description, "")
            entry = Entry(name, category_name, description, label_zh)
            _prepare(entry)
            entries.append(entry)
    return entries


def _compact(text):
    return re.sub(r"\s+", "", text.lower())


def _token_search(entry, tokens):
    """中英混输：把查询拆成若干 token，要求每个 token 都能在某个字段里命中。"""
    worst = 0
    for token in tokens:
        best = None
        for value, field_rank, allow_sub in entry.fields:
            result = _match_tier(_compact(value), token, allow_sub)
            if result is None:
                continue
            if best is None or result < best:
                best = result
        if best is None:
            return None
        worst = max(worst, best[0])
    return worst


def search(entries, text, limit=80):
    query = (text or "").strip().lower()
    if not query:
        return entries[:limit]
    compact = _compact(query)
    compact_soft = soft_form(compact)
    has_cjk = bool(_CJK.search(query))
    has_ascii = bool(re.search(r"[a-z0-9]", query))
    pure_cjk = has_cjk and not has_ascii
    mixed = has_cjk and has_ascii          # 例如 "uv贴图"、"wrangle属性"
    multiword = " " in query
    tokens = re.findall(r"[a-z0-9]+|[^\x00-\x7f]+", query) if mixed else []
    scored = []
    for entry in entries:
        best = None
        for value, field_rank, allow_sub in entry.fields:
            if pure_cjk and field_rank != 2:
                continue
            value_compact = _compact(value) if (has_cjk or compact != query) else value
            tries = []
            if field_rank >= 4:
                tries.append((value, compact_soft))
            elif field_rank >= 3:
                tries.append((value, compact))
            else:
                tries.append((value, query))
                if compact != query:
                    tries.append((value, compact))
                if multiword:
                    word_tier = word_prefix_match(value, query)
                    if word_tier is not None:
                        key = (word_tier, field_rank, 0, len(value))
                        if best is None or key < best[0]:
                            best = (key, entry)
            # 中英混输：把字段也去掉空格再比一次（"UV 贴图" ↔ "uv贴图"）
            if has_cjk and value_compact != value:
                tries.append((value_compact, compact))
            for candidate_value, candidate in tries:
                result = _match_tier(candidate_value, candidate, allow_sub)
                if result is None:
                    continue
                key = (result[0], field_rank, result[1], len(candidate_value))
                if best is None or key < best[0]:
                    best = (key, entry)
        if tokens and len(tokens) >= 2:
            tier = _token_search(entry, tokens)
            if tier is not None:
                key = (tier, 9, 0, len(entry.label_zh or entry.label_en))
                if best is None or key < best[0]:
                    best = (key, entry)
        if best is not None:
            scored.append(best)
    scored.sort(key=lambda item: (item[0], item[1].label_zh or item[1].label_en,
                                  item[1].name))
    return [entry for _, entry in scored[:limit]]


def _display(entry):
    if entry.label_zh:
        return "%s   ·   %s   ·   %s" % (entry.label_zh, entry.label_en, entry.category)
    return "%s   ·   %s" % (entry.label_en, entry.category)


def _network_pane():
    import hou

    try:
        pane = hou.ui.paneTabUnderCursor()
    except Exception:
        pane = None
    if isinstance(pane, hou.NetworkEditor) and pane.isCurrentTab():
        return pane
    try:
        pane = hou.paneTabType and hou.ui.paneTabOfType(hou.paneTabType.NetworkEditor)
    except Exception:
        pane = None
    return pane if isinstance(pane, hou.NetworkEditor) else None


def _target_pwd(pane):
    import hou

    if pane is not None:
        try:
            return pane.pwd()
        except Exception:
            pass
    try:
        return hou.pwd()
    except Exception:
        return hou.node("/obj")


def _create(entry, pane):
    import hou

    pwd = _target_pwd(pane)
    node = pwd.createNode(entry.name)
    try:
        node.setSelected(True, clear_all_selected=True)
    except Exception:
        pass
    try:
        if pane is not None:
            pane.setPwd(pwd)
    except Exception:
        pass
    try:
        node.moveToGoodPosition()
    except Exception:
        pass
    try:
        hou.ui.setStatusMessage("已创建 %s" % (entry.label_zh or entry.label_en))
    except Exception:
        pass
    return node


_dialog = None


def _ensure_qt():
    from PySide6 import QtCore, QtGui, QtWidgets

    return QtCore, QtGui, QtWidgets


_last_geometry = None
_GEOMETRY_FILE = os.path.join(
    os.environ.get("APPDATA") or os.environ.get("TEMP", "."),
    "HoudiniChinese", "nodesearch_geometry.json")


def _load_saved_geometry():
    """记住上次拖动/拉伸后的位置和大小，跨会话生效。"""
    try:
        with open(_GEOMETRY_FILE, encoding="utf-8") as handle:
            data = json.load(handle)
        rect = [int(data[key]) for key in ("x", "y", "w", "h")]
        if rect[2] >= 480 and rect[3] >= 320:
            return rect
    except (OSError, ValueError, KeyError, TypeError):
        pass
    return None


def _save_geometry(rect):
    try:
        folder = os.path.dirname(_GEOMETRY_FILE)
        if folder and not os.path.isdir(folder):
            os.makedirs(folder, exist_ok=True)
        with open(_GEOMETRY_FILE, "w", encoding="utf-8") as handle:
            json.dump({"x": rect.x(), "y": rect.y(),
                       "w": rect.width(), "h": rect.height()}, handle)
    except OSError:
        pass


def _position_dialog(dialog, QtCore, QtGui, QtWidgets):
    global _last_geometry
    if _last_geometry is None:
        _last_geometry = _load_saved_geometry()
    if _last_geometry is not None:
        # 可能是 QRect（本次会话缓存）或 [x, y, w, h]（磁盘读取）
        try:
            if isinstance(_last_geometry, (tuple, list)):
                dialog.setGeometry(*[int(value) for value in _last_geometry])
            else:
                dialog.setGeometry(_last_geometry)
            return
        except Exception as error:
            _log("恢复窗口位置失败，改用默认尺寸：%r" % (error,))
            _last_geometry = None
    cursor = QtGui.QCursor.pos()
    screen = QtGui.QGuiApplication.screenAt(cursor)
    if screen is None:
        screen = QtGui.QGuiApplication.primaryScreen()
    geometry = screen.availableGeometry()
    # 按屏幕比例给尺寸，高分屏和笔记本上都够大
    width = int(geometry.width() * 0.40)
    height = int(geometry.height() * 0.46)
    width = max(680, min(1100, width))
    height = max(480, min(760, height))
    x = min(max(geometry.left() + 8, cursor.x() - width // 2), geometry.right() - width - 8)
    y = min(max(geometry.top() + 8, cursor.y() - 24), geometry.bottom() - height - 8)
    dialog.setGeometry(x, y, width, height)


class _DialogFilter:
    """把一个纯 Python 面板对象包装成 Qt 事件过滤器。"""

    def __new__(cls, owner):
        QtCore, _, QtWidgets = _ensure_qt()

        class _Filter(QtCore.QObject):
            def __init__(self):
                QtCore.QObject.__init__(self)
                self._owner = owner

            def eventFilter(self, watched, event):
                try:
                    return self._owner.handle_key(watched, event)
                except Exception:
                    return False

        return _Filter()


class NodeSearchDialog:
    """Qt 检索面板。用工厂函数构造，避免导入时依赖鼠标位置。"""

    def __init__(self, entries, pane, all_provider=None):
        QtCore, QtGui, QtWidgets = _ensure_qt()
        self._qt = (QtCore, QtGui, QtWidgets)
        self._entries = entries
        self._pane = pane
        self._all = entries
        self._all_provider = all_provider
        self._all_cache = None

        dialog = QtWidgets.QDialog(None)
        # 保留原生窗口边框：可拖动、可拉伸，比无边框实用
        dialog.setWindowFlags(QtCore.Qt.WindowType.Tool |
                              QtCore.Qt.WindowType.WindowStaysOnTopHint)
        dialog.setWindowTitle("中文节点搜索 — 中文 / 拼音 / 英文")
        dialog.setMinimumSize(560, 380)
        dialog.setAttribute(QtCore.Qt.WidgetAttribute.WA_DeleteOnClose, False)
        dialog.setStyleSheet(
            "QDialog{background:#2b2b2b;}"
            "QLineEdit{background:#1f1f1f;color:#f0f0f0;border:0;padding:12px 14px;"
            "font-size:13pt;}"
            "QListWidget{background:#232323;color:#e0e0e0;border:0;outline:0;"
            "font-size:10.5pt;}"
            "QListWidget::item{padding:8px 12px;}"
            "QListWidget::item:selected{background:#3d6ea8;color:#fff;}"
            "QCheckBox{color:#bdbdbd;font-size:9.5pt;padding:4px 10px;}"
            "QLabel{color:#9a9a9a;font-size:9pt;padding:4px 10px;}"
        )

        layout = QtWidgets.QVBoxLayout(dialog)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        edit = QtWidgets.QLineEdit(dialog)
        edit.setPlaceholderText("中文 / 拼音 / 首字母 / 英文都能查：长方体、changfangti、cft、box、attr wrangle")
        layout.addWidget(edit)

        listing = QtWidgets.QListWidget(dialog)
        listing.setUniformItemSizes(True)
        layout.addWidget(listing, 1)

        footer = QtWidgets.QHBoxLayout()
        footer.setContentsMargins(0, 0, 0, 0)
        scope = QtWidgets.QCheckBox("全部分类 (Ctrl+L)", dialog)
        scope.stateChanged.connect(lambda _state: self.refresh())
        footer.addWidget(scope)
        status = QtWidgets.QLabel(dialog)
        footer.addWidget(status, 1)
        holder = QtWidgets.QWidget(dialog)
        holder.setLayout(footer)
        layout.addWidget(holder)

        self._dialog = dialog
        self._edit = edit
        self._list = listing
        self._status = status
        self._scope = scope

        edit.textChanged.connect(self.refresh)
        edit.returnPressed.connect(self.accept_current)
        listing.itemDoubleClicked.connect(lambda _item: self.accept_current())
        self._filter = _DialogFilter(self)
        edit.installEventFilter(self._filter)
        listing.installEventFilter(self._filter)
        dialog.installEventFilter(self._filter)

        self.refresh("")

    def _all_entries(self):
        if self._all_cache is None:
            if self._all_provider is not None:
                self._all_cache = self._all_provider()
            else:
                self._all_cache = build_index(None)
        return self._all_cache

    # ---- Qt 事件：上下键进列表、Esc 关闭、Ctrl+L 切范围 ----
    def handle_key(self, watched, event):
        QtCore = self._qt[0]
        if event.type() == QtCore.QEvent.Type.KeyPress:
            key = event.key()
            if key == QtCore.Qt.Key.Key_Escape:
                self.close()
                return True
            if (key == QtCore.Qt.Key.Key_L
                    and event.modifiers() & QtCore.Qt.KeyboardModifier.ControlModifier):
                self._scope.setChecked(not self._scope.isChecked())
                self.refresh()
                return True
            if key in (QtCore.Qt.Key.Key_Down, QtCore.Qt.Key.Key_Up):
                if watched is self._edit:
                    self._list.setFocus()
                    row = self._list.currentRow()
                    self._list.setCurrentRow(max(0, row))
                    return True
                if watched is self._list:
                    row = self._list.currentRow()
                    target = row + (1 if key == QtCore.Qt.Key.Key_Down else -1)
                    if target < 0:
                        self._edit.setFocus()
                        return True
                    if self._list.count() > 0:
                        self._list.setCurrentRow(min(target, self._list.count() - 1))
                    return True
            if key in (QtCore.Qt.Key.Key_Return, QtCore.Qt.Key.Key_Enter) and watched is self._list:
                keep = bool(event.modifiers() & QtCore.Qt.KeyboardModifier.ShiftModifier)
                self.accept_current(keep_open=keep)
                return True
        if event.type() == QtCore.QEvent.Type.WindowDeactivate and watched is self._dialog:
            self.close()
            return True
        return False

    def refresh(self, text=None):
        QtCore, _, QtWidgets = self._qt
        if text is None:
            text = self._edit.text()
        query = text.strip()
        scope_all = self._scope.isChecked()
        entries = self._all_entries() if scope_all else self._entries
        results = search(entries, text, limit=300)
        fallback = False
        if not results and query and not scope_all:
            # 当前类别没有，就在全部分类里找，并提示属于什么类别
            results = search(self._all_entries(), text, limit=300)
            fallback = bool(results)
        self._list.clear()
        for entry in results:
            item = QtWidgets.QListWidgetItem(_display(entry), self._list)
            item.setData(QtCore.Qt.ItemDataRole.UserRole, entry)
            item.setToolTip("%s\n类型名：%s\n类别：%s"
                            % (entry.label_en, entry.name, entry.category))
        if results:
            self._list.setCurrentRow(0)
        total = len(entries)
        if query:
            if fallback:
                self._status.setText("当前类别无匹配，已在全部分类中找到 %d 条 · Enter 创建 · Esc 关闭"
                                     % len(results))
            else:
                self._status.setText("匹配 %d / 候选 %d · ↑↓ 选择 · Enter 创建 · Esc 关闭"
                                     % (len(results), total))
        else:
            self._status.setText("候选 %d · 中文/拼音/英文均可 · Enter 创建 · Esc 关闭" % total)


    def _current_entry(self):
        item = self._list.currentItem()
        if item is None and self._list.count() > 0:
            item = self._list.item(0)
        if item is None:
            return None
        return item.data(self._qt[0].Qt.ItemDataRole.UserRole)

    def accept_current(self, keep_open=False):
        entry = self._current_entry()
        if entry is None:
            return
        import hou
        pwd = _target_pwd(self._pane)
        try:
            category = pwd.childTypeCategory()
            category_name = category.name() if category is not None else None
        except Exception:
            category_name = None
        if category_name and entry.category != category_name:
            # 跨类别候选：不要偷偷创建，直接告诉用户该去哪儿
            self._status.setText("%s 属于 %s 类别，当前网络是 %s；请先进入对应网络"
                                 % (entry.label_zh or entry.label_en,
                                    entry.category, category_name))
            return
        try:
            _create(entry, self._pane)
        except Exception as error:
            self._status.setText("创建失败：%s" % error)
            try:
                hou.ui.setStatusMessage("创建节点失败：%s" % error,
                                        severity=hou.severityType.Error)
            except Exception:
                pass
            return
        if keep_open:
            # Shift+Enter：连续创建，面板留在原地
            self._status.setText("已创建 %s · 继续输入，或 Esc 关闭"
                                 % (entry.label_zh or entry.label_en))
            self._edit.setFocus()
            self._edit.selectAll()
            self.refresh()
            return
        self.close()

    def show(self):
        QtCore, QtGui, QtWidgets = self._qt
        _position_dialog(self._dialog, QtCore, QtGui, QtWidgets)
        self._dialog.show()
        self._dialog.raise_()
        self._dialog.activateWindow()
        self._edit.setFocus()
        self._edit.selectAll()

    def close(self):
        global _dialog, _last_geometry
        try:
            _last_geometry = self._dialog.geometry()
            _save_geometry(_last_geometry)
        except Exception:
            pass
        try:
            self._dialog.close()
            self._dialog.deleteLater()
        except Exception:
            pass
        _dialog = None


def show():
    """打开中文节点搜索面板。"""
    global _dialog
    import hou

    if not hou.isUIAvailable():
        raise RuntimeError("需要 Houdini 图形界面")
    if _dialog is not None:
        try:
            _dialog.close()
        except Exception:
            pass
        _dialog = None

    pane = _network_pane()
    pwd = _target_pwd(pane)
    category = None
    try:
        child = pwd.childTypeCategory()
        category = child.name() if child is not None else None
    except Exception:
        category = None

    entries = build_index(category)
    if not entries:
        entries = build_index(None)
    _log("打开面板：类别=%s，候选=%d" % (category, len(entries)))
    _dialog = NodeSearchDialog(entries, pane,
                               all_provider=lambda: build_index(None))
    _dialog.show()
    return _dialog


def install_menu_button():
    """在中/英按钮旁边插一个 🔍 按钮（Qt 层注入，不改 C++）。"""
    if os.environ.get("HOUDINI_CHINESE_NODESEARCH_BUTTON", "1") == "0":
        return False
    global _menu_button
    if _menu_button is not None:
        return True
    try:
        from PySide6 import QtWidgets
        app = QtWidgets.QApplication.instance()
        if app is None:
            return False
        root = app.activeWindow() or app.primaryScreen()
        if not isinstance(root, QtWidgets.QWidget):
            return False
        anchor = None
        for widget in root.findChildren(QtWidgets.QWidget):
            getter = getattr(widget, "text", None)
            if not callable(getter):
                continue
            try:
                if getter().strip() in ("中/英", "中 / 英", "Chinese"):
                    anchor = widget
                    break
            except Exception:
                continue
        if anchor is None:
            _log("没找到中/英按钮，跳过菜单栏 🔍 按钮")
            return False
        parent = anchor.parentWidget()
        layout = parent.layout() if parent is not None else None
        if layout is None:
            _log("中/英按钮没有可用布局，跳过菜单栏 🔍 按钮")
            return False
        button = QtWidgets.QToolButton(parent)
        button.setText("🔍")
        button.setToolTip("中文节点搜索（F4）")
        button.setAutoRaise(True)
        button.clicked.connect(show)
        index = layout.indexOf(anchor)
        layout.insertWidget(index + 1 if index >= 0 else 0, button)
        _menu_button = button
        _log("已在菜单栏添加 🔍 按钮")
        return True
    except Exception as error:
        _log("菜单栏按钮安装失败：%r" % (error,))
        return False


def _tab_enabled():
    """Tab 默认还给 Houdini 原生菜单；需要接管时设 HOUDINI_CHINESE_NODESEARCH_TAB=1。"""
    return os.environ.get("HOUDINI_CHINESE_NODESEARCH_TAB", "0") == "1"


def _keys_enabled():
    return os.environ.get("HOUDINI_CHINESE_NODESEARCH_KEYS", "1") != "0"


def make_tab_filter():
    """构造 Qt 事件过滤器。

    installEventFilter 只接受 QObject，所以必须在拿到 PySide 之后再动态派生子类；
    纯 Python 类会抛 TypeError（这正是之前 Tab 一直没生效的原因）。
    """
    QtCore, QtGui, QtWidgets = _ensure_qt()

    class _Filter(QtCore.QObject):
        def __init__(self):
            QtCore.QObject.__init__(self)
            self._installed = False

        def _install(self):
            app = QtWidgets.QApplication.instance()
            if app is None:
                _log("安装事件过滤器失败：没有 QApplication")
                return
            if not self._installed:
                app.installEventFilter(self)
                self._installed = True
                _log("事件过滤器已安装：F4 呼出面板，Tab 归原生菜单")

        def eventFilter(self, watched, event):
            try:
                if event.type() != QtCore.QEvent.Type.KeyPress:
                    return False
                if not _keys_enabled():
                    return False
                key = event.key()
                tab = key == QtCore.Qt.Key.Key_Tab
                f4 = key == QtCore.Qt.Key.Key_F4
                if not (tab or f4):
                    return False
                if tab and not _tab_enabled():
                    return False
                if event.modifiers() != QtCore.Qt.KeyboardModifier.NoModifier:
                    return False
                _log_first("hotkey_seen", "收到 %s 按键（事件过滤器生效）"
                           % ("F4" if f4 else "Tab"))
                if _dialog is not None:
                    return False
                if QtWidgets.QApplication.activeModalWidget() is not None:
                    _log_first("hotkey_modal", "有模态对话框，按键交给宿主处理")
                    return False
                pane = _network_pane()
                if pane is None:
                    _log_first("hotkey_no_pane", "按键被按下，但当前不在节点网络视图")
                    return False
                _log_first("hotkey_open", "%s 命中节点网络，打开拼音面板"
                           % ("F4" if f4 else "Tab"))
                show()
                return True
            except Exception as error:
                _log_first("hotkey_error", "按键处理异常：%r" % (error,))
                return False

    return _Filter()


def install():
    """尽量安装 Tab 键拦截；失败只打印原因，不影响插件其它功能。"""
    global _tab_filter
    if _tab_filter is not None:
        return True
    if not _keys_enabled():
        _log("快捷键接管已关闭（HOUDINI_CHINESE_NODESEARCH_KEYS=0）")
        return False
    if not _tab_enabled():
        _log("Tab 交给 Houdini 原生菜单，只接管 F4")
    try:
        import hou
        if not hou.isUIAvailable():
            _log("没有图形界面，跳过 Tab 接管")
            return False
        handler = make_tab_filter()
        _tab_filter = handler
        from PySide6 import QtCore
        QtCore.QTimer.singleShot(1500, handler._install)
        QtCore.QTimer.singleShot(2500, install_menu_button)
        _log("已请求接管 F4（1.5 秒后安装事件过滤器）")
        return True
    except Exception as error:
        _log("Tab 接管失败：%r" % (error,))
        _tab_filter = None
        return False


_tab_filter = None


_menu_button = None
