#include "../../source/hook.cpp"
#include <cassert>
#include <cstdio>

class UI_InputField { public: virtual ~UI_InputField() = default; };

namespace {
float chineseWidth = 20.0f;
float fakeWidth(const void *, const char *begin, const char *, const void *) {
    return begin && static_cast<unsigned char>(*begin) > 127 ? chineseWidth : 50.0f;
}
void fakePreferred(void *self, int &width, int &, int &, int &, int &, int &) {
    width = static_cast<int>(hookedUITextStringWidth(self, "File", nullptr, nullptr));
}
bool inputProtected = false;
void fakeInputRender(void *, void *, int, int) {
    inputProtected = displayTranslation(QStringLiteral("File")).isEmpty();
}
UIHelpString *fakeHelp(void *, UIHelpString *result, UI_DeviceEvent *) {
    *result = UIHelpString("File");
    return result;
}
bool fakeMOTHelp(void *, const char *url, UT_String &result) {
    assert(std::strcmp(url, "tool:test_source") == 0);
    result.harden("File");
    return true;
}
QString wrappedText;
void fakeWrappedRender(void *, void *, const UT_StringView &text, int, int, int, int) {
    wrappedText = QString::fromUtf8(text.begin(), text.end()-text.begin());
}
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    const auto hash = dictionaryHashBytes("File");
    g_dictionaryEntries.push_back({hash, "File", "文件"});
    g_dictionaryIndex.emplace(hash, 0);
    g_dictionaryReady.store(true);
    g_enabled.store(true);
    const auto mixedHash = dictionaryHashBytes("Follow Parent Context 窗格按钮开/关");
    g_dictionaryIndex.emplace(mixedHash, g_dictionaryEntries.size());
    g_dictionaryEntries.push_back({mixedHash, "Follow Parent Context 窗格按钮开/关", "跟随父级上下文开关"});
    g_dictionaryEntries.push_back({dictionaryHashBytes("Capture Weights"),
                                   "Capture Weights", "捕获权重"});
    g_dictionaryEntries.push_back({dictionaryHashBytes("Capture Weight Handle"),
                                   "Capture Weight Handle", "捕获权重操纵器"});
    g_dictionaryEntries.push_back({dictionaryHashBytes("Shared A"),
                                   "Shared A", "重复译文"});
    g_dictionaryEntries.push_back({dictionaryHashBytes("Shared B"),
                                   "Shared B", "重复译文"});
    rebuildReverseDictionary();
    assert(displayTranslation(QStringLiteral("Follow Parent Context 窗格按钮开/关"))
           == QStringLiteral("跟随父级上下文开关"));
    assert(displayTranslation(QStringLiteral("User File 测试" )).isEmpty());
    assert(displayTranslation(QStringLiteral("Capture Wei...")) ==
           QStringLiteral("捕获权重"));
    assert(displayTranslation(QStringLiteral("Capture Wei…")) ==
           QStringLiteral("捕获权重"));
    g_wrappedRender21 = fakeWrappedRender;
    {
        ScopedAllowHoudiniUiText scope;
        int owner = 0;
        const UT_StringView original("File");
        hookedWrappedRender21(&owner, nullptr, original, 0, 0, 100, 20);
        assert(wrappedText == QStringLiteral("文件"));
        assert(QString::fromUtf8(original.begin(), original.end()-original.begin()) == QStringLiteral("File"));
        TextScope input(TextRole::UserValue);
        hookedWrappedRender21(&owner, nullptr, original, 0, 0, 100, 20);
        assert(wrappedText == QStringLiteral("File"));
    }
    QStaticText badge(QStringLiteral("<b>File</b> "));
    assert(staticTextTranslation21(badge) == QStringLiteral("<b>文件</b> "));
    assert(badge.text() == QStringLiteral("<b>File</b> "));
    {
        TextScope input(TextRole::UserValue);
        assert(staticTextTranslation21(badge).isEmpty());
    }
    g_uiTextStringWidth = fakeWidth;
    g_uiTextPreferredSizes21 = fakePreferred;
    int self = 0, a=0,b=0,c=0,d=0,e=0,f=0;
    {
        ScopedAllowHoudiniUiText scope;
        hookedUITextPreferredSizes21(&self,a,b,c,d,e,f);
        assert(a == 50); // Short translations must not shrink H21's native row.
        chineseWidth = 90;
        hookedUITextPreferredSizes21(&self,a,b,c,d,e,f);
        assert(a == 90); // Long translations receive sufficient layout width.
        assert(g_nativePreferredSizeDepth21 == 0);
        assert(hookedUITextStringWidth(&self,"File",nullptr,nullptr) == 50);
        // Paint-time prefix offsets still refer to the original English text.
        TextScope input(TextRole::UserValue);
        hookedUITextPreferredSizes21(&self,a,b,c,d,e,f);
        assert(a == 50);
    }
    g_uiFeelRenderMe = fakeInputRender;
    UI_InputField field;
    hookedUIFeelRenderMe(&field,nullptr,0,0);
    assert(inputProtected);
    assert(displayTranslation(QStringLiteral("File")) == QStringLiteral("文件"));
    g_uiHelpFeelGetText = fakeHelp;
    UIHelpString result;
    assert(hookedUIHelpFeelGetText(nullptr,&result,nullptr) == &result);
    assert(QString::fromUtf8(result.c_str()) == QStringLiteral("文件"));
    g_motTooltip21 = fakeMOTHelp;
    UT_String motResult;
    assert(hookedMOTTooltip21(nullptr, "tool:test_source", motResult));
    assert(QString::fromUtf8(motResult.c_str()) == QStringLiteral("文件"));
    g_enabled.store(false);
    assert(hookedMOTTooltip21(nullptr, "tool:test_source", motResult));
    assert(QString::fromUtf8(motResult.c_str()) == QStringLiteral("File"));
    motResult.harden(QStringLiteral("文件").toUtf8().constData());
    translateTooltipCopy21(motResult);
    assert(QString::fromUtf8(motResult.c_str()) == QStringLiteral("File"));
    motResult.harden(QStringLiteral("重复译文").toUtf8().constData());
    translateTooltipCopy21(motResult);
    assert(QString::fromUtf8(motResult.c_str()) == QStringLiteral("重复译文"));
    g_enabled.store(true);
    const auto typeLookup = [](const QString &s) {
        if (s == QStringLiteral("Geometry")) return QStringLiteral("几何体");
        if (s == QStringLiteral("Box")) return QStringLiteral("长方体");
        if (s == QStringLiteral("Bounding Source")) return QStringLiteral("边界源");
        return QString();
    };
    assert(HoudiniChineseMessages21::translate(QStringLiteral("box_object1 (Geometry) node"), typeLookup)
           == QStringLiteral("box_object1（几何体）节点"));
    assert(HoudiniChineseMessages21::translate(QStringLiteral("Save changes to \"测试.hip\" before exiting?"), typeLookup)
           == QStringLiteral("退出前保存对“测试.hip”的修改吗？"));
    assert(HoudiniChineseMessages21::translate(QStringLiteral("arbitrary text (Geometry) node"), typeLookup).isEmpty());
    assert(HoudiniChineseMessages21::translate(QStringLiteral("box_object1 (Unknown) node"), typeLookup).isEmpty());
    assert(HoudiniChineseMessages21::translate(QStringLiteral("Output 1 (output1)"), typeLookup)
           == QStringLiteral("输出 1 (output1)"));
    assert(HoudiniChineseMessages21::translate(QStringLiteral("box_object1 (Geometry)\nOutput 1 (output1)"), typeLookup)
           == QStringLiteral("box_object1（几何体）\n输出 1 (output1)"));
    assert(HoudiniChineseMessages21::translate(QStringLiteral("Input 12 (custom_port)"), typeLookup)
           == QStringLiteral("输入 12 (custom_port)"));
    assert(HoudiniChineseMessages21::translate(QStringLiteral("Bounding Source (input1)"), typeLookup)
           == QStringLiteral("边界源 (input1)"));
    assert(HoudiniChineseMessages21::translate(QStringLiteral("box1 (Box)\nBounding Source (input1)"), typeLookup)
           == QStringLiteral("box1（长方体）\n边界源 (input1)"));
    for (const auto &s : {"Output 1 (output1) suffix", "Custom Output 1 (output1)",
                         "box_object1 (Unknown)\nOutput 1 (output1)", "Output 1 (output1: float)",
                         "Unknown Label (input1)"})
        assert(HoudiniChineseMessages21::translate(QString::fromUtf8(s), typeLookup).isEmpty());
    assert(HoudiniChineseMessages21::translate(QStringLiteral("First Person Navigation: Press Esc or Shift+F to exit."), typeLookup)
           == QStringLiteral("第一人称导航：按 Esc 或 Shift+F 退出。"));
    assert(HoudiniChineseMessages21::translate(QStringLiteral("Drag this to another pane to set its path to /obj/测试."), typeLookup)
           == QStringLiteral("拖到另一个窗格，将该窗格的路径设为 /obj/测试。"));
    {
        TextScope input(TextRole::UserValue);
        assert(displayTranslation(QStringLiteral("Save changes to \"test.hip\" before exiting?")).isEmpty());
    }
    QDialog dialog;
    dialog.setWindowTitle(QStringLiteral("File"));
    refreshDialogTitle21(&dialog);
    assert(dialog.windowTitle() == QStringLiteral("文件"));
    g_enabled.store(false);
    refreshDialogTitle21(&dialog);
    assert(dialog.windowTitle() == QStringLiteral("File"));
    g_enabled.store(true);
    wchar_t label[128]{};
    assert(HoudiniChineseTranslateLabel21(L"<b>File</b>", label, 128) > 0);
    assert(QString::fromWCharArray(label) == QStringLiteral("<b>文件</b>"));
    assert(HoudiniChineseRestoreLabel21(L"<b>文件</b>", label, 128) > 0);
    assert(QString::fromWCharArray(label) == QStringLiteral("<b>File</b>"));
    assert(HoudiniChineseTranslateLabel21(L"<b>File</b>", label, 2) == 0);
    g_enabled.store(false);
    assert(HoudiniChineseTranslateLabel21(L"<b>File</b>", label, 128) == 0);
    std::puts("native21_regression_test: passed (layout, input protection, help ABI, dynamic messages, rich text, titles, HUD bridge)");
}
