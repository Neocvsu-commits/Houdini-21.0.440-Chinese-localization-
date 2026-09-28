"""Run via the isolated interaction probe; the helper DLL is never shipped."""
helper = ctypes.WinDLL(str(root/'build/tests/help_probe21.dll'))
helper.readTooltip21.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_wchar), ctypes.c_int]
helper.readTooltip21.restype = ctypes.c_int
button = next(w for w in QtWidgets.QApplication.allWidgets()
              if w.objectName() == 'houdini_chinese_language_button')
from hutil.qt import tooltip

expected_help = {
    'tool:capturegeometry': (
        'Uses one or more geometry objects as the skin for a hierarchy of bones or a set of metaball objects.',
        '使用一个或多个几何体对象作为蒙皮，由骨骼层级或一组元球对象驱动。'),
    'tool:feconstrainpointstotarget': (
        'Constrain a selection of points to animated target positions.',
        '将所选点约束到随动画变化的目标位置。'),
    'tool:sop_groom_guideprocess_straighten': (
        'Straightens the hair by bending each segment back so that it’s in the same direction as the previous segment.',
        '调整毛发各段的弯曲，使其方向与前一段一致，从而拉直毛发。'),
}
data = tooltip.tipData('tool:sop_groom_guideprocess_straighten')
original_html = data.html
original_url = data.base_url.toString()
tooltip.tooltip_window.display(data, QtCore.QRect(900, 200, 100, 30))
help_results = {}

def sample_help(language, chinese):
    rows = {}
    for url, (english, translated) in expected_help.items():
        buffer = ctypes.create_unicode_buffer(8192)
        assert helper.readTooltip21(url.encode(), buffer, len(buffer))
        assert buffer.value == (translated if chinese else english), (url, buffer.value)
        rows[url] = buffer.value
    assert data.html == original_html and data.base_url.toString() == original_url
    # Windows may hide ToolTip windows when the user focuses another app.
    # Reopen from the same source data when checking a hidden card.
    if not tooltip.tooltip_window.isVisible():
        tooltip.tooltip_window.display(data, QtCore.QRect(900, 200, 100, 30))
    document = tooltip.tooltip_window.document().toPlainText()
    expected = expected_help['tool:sop_groom_guideprocess_straighten'][int(chinese)]
    assert document == expected, document
    rows['visible_help_document'] = document
    help_results[language] = rows
    (output/'native-help.json').write_text(json.dumps(help_results, ensure_ascii=False, indent=2), encoding='utf8')
    print('HOVER_CHECK_PASS', language, flush=True)

def check_english():
    sample_help('en', False)
    button.click()
    QtCore.QTimer.singleShot(300, lambda: sample_help('zh_again', True))

def check_chinese():
    sample_help('zh', True)
    button.click()
    QtCore.QTimer.singleShot(300, check_english)

# Help loading can enter a nested Qt event loop. Schedule each next step only
# after the preceding API call returns, so timers cannot race inside getTooltip.
if not tr('File'):
    button.click()
QtCore.QTimer.singleShot(300, check_chinese)
