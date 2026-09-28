// Exercise the actual display adapters with callable originals, without
// starting a Houdini UI. Opt-in mode also tests real Qt entry detours.
#include "../../source/hook.cpp"
#include <QtGui/QImage>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>
#include <cassert>
#include <iostream>
#include <thread>

// MSVC RTTI fixture with the host class identity; no Houdini ABI calls.
class UI_InputFieldLook { public: virtual ~UI_InputFieldLook() = default; };
class DerivedInputLookFixture : public UI_InputFieldLook {};
namespace {
QString measured;
int measuredLength = -2;
int nativeAdvance(const QFontMetrics *font, const QString &text, int length) {
    measured = text;
    measuredLength = length;
    return font->horizontalAdvance(text, length);
}
qreal nativeAdvanceF(const QFontMetricsF *font, const QString &text, int length) {
    measured = text;
    measuredLength = length;
    return font->horizontalAdvance(text, length);
}
QString drawn;
void nativeLayoutDraw(const QTextLayout *layout, QPainter *, const QPointF &,
                      const QList<QTextLayout::FormatRange> &, const QRectF &) {
    drawn = layout->text();
}
bool editableDrawn = false;
QString comboPainted;
int comboSelectionStart = -2;
bool comboLayoutPreserved = false;
void comboSceneDraw(void *, QPointF, QTextLayout *layout, int selectionStart,
                    int, int, int) {
    comboPainted = layout->text();
    comboSelectionStart = selectionStart;
}
void *comboInputPaint(void *self, void *, void *) {
    auto *input = static_cast<QQuickItem *>(self);
    const QString text = input->property("text").toString();
    QTextLayout layout(text, QApplication::font());
    layout.beginLayout();
    layout.createLine().setLineWidth(600);
    layout.endLayout();
    assert(!isQuickTextUpdateLayoutCall());
    hookedQSGInlineAddTextLayout(nullptr, {}, &layout, 7, 6, 0, -1);
    comboLayoutPreserved = layout.text() == text;
    return nullptr;
}

void exerciseQuickComboDisplay() {
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QtQuick.Templates as T
        T.ComboBox {
            id: box
            model: ["Oblique", "Wireframe Over Ghost Surfaces"]
            delegate: T.ItemDelegate { text: modelData }
            currentIndex: 0
            contentItem: T.TextField {
                text: box.editable ? box.editText : box.displayText
                enabled: box.editable
                readOnly: box.down
            }
        }
    )", QUrl());
    std::unique_ptr<QObject> object(component.create());
    if (!object) std::cerr << component.errorString().toStdString();
    assert(object);
    auto *combo = qobject_cast<QQuickItem *>(object.get());
    auto *input = combo->property("contentItem").value<QQuickItem *>();
    assert(input && isNonEditableQuickComboDisplay(input));
    g_quickComboInputUpdatePaintNode = &comboInputPaint;
    g_qsgInlineAddTextLayout = &comboSceneDraw;
    for (int index : {0, 1}) {
        combo->setProperty("currentIndex", index);
        const QString source = combo->property("currentText").toString();
        g_enabled.store(true);
        hookedQQuickComboInputUpdatePaintNode(input, nullptr, nullptr);
        assert(comboPainted == (index == 0 ? QStringLiteral("倾斜") :
                             QStringLiteral("在虚影表面上叠加线框")));
        assert(comboSelectionStart == -1 && comboLayoutPreserved);
        assert(input->property("text").toString() == source);
        assert(combo->property("currentText").toString() == source);
        g_enabled.store(false);
        hookedQQuickComboInputUpdatePaintNode(input, nullptr, nullptr);
        assert(comboPainted == source && comboLayoutPreserved);
    }
    combo->setProperty("editable", true);
    assert(!isNonEditableQuickComboDisplay(input));
    // Even a disabled editable combo still represents user input.
    combo->setEnabled(false);
    assert(!isNonEditableQuickComboDisplay(input));
    g_enabled.store(true);
    hookedQQuickComboInputUpdatePaintNode(input, nullptr, nullptr);
    assert(comboPainted == input->property("text").toString());
    assert(comboLayoutPreserved && g_quickComboDisplayPaintDepth == 0);
    g_quickComboInputUpdatePaintNode = nullptr;
    g_qsgInlineAddTextLayout = nullptr;
    g_enabled.store(false);
    std::cout << "QML combo display: current values translated, editable controls protected, source layout unchanged\n";
}

void exerciseEnglishWidthRestore() {
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        Item {
            implicitWidth: label.implicitWidth + 20
            Text {
                id: label
                objectName: "widthLabel"
                text: "Wireframe Over Ghost Surfaces"
                font.pixelSize: 18
                leftPadding: 7
                rightPadding: 9
            }
        }
    )", QUrl());
    g_enabled.store(false);
    std::unique_ptr<QObject> object(component.create());
    assert(object);
    auto *container = qobject_cast<QQuickItem *>(object.get());
    auto *label = container->findChild<QQuickItem *>(QStringLiteral("widthLabel"));
    assert(label);
    assert(installQtQuickTextOwnerHooks());
    assert(installQtQuickLayoutInputHook());
    assert(installQtQuickImplicitSizeHooks());
    const auto relayout = [&] {
        assert(QMetaObject::invokeMethod(label, "forceLayout", Qt::DirectConnection));
    };
    for (const QString &text : {QStringLiteral("Wireframe Over Ghost Surfaces"),
                                QStringLiteral("Oblique")}) {
        g_enabled.store(false);
        label->setProperty("text", text);
        QFont font = label->property("font").value<QFont>();
        font.setPixelSize(text == QStringLiteral("Oblique") ? 23 : 18);
        label->setProperty("font", font);
        relayout();
        const qreal englishWidth = label->implicitWidth();
        for (int cycle = 0; cycle < 3; ++cycle) {
            g_enabled.store(true);
            relayout();
            assert(qAbs(label->implicitWidth() - englishWidth) > 1.0);
            assert(g_translatedQuickSizeItems.contains(label));
            g_enabled.store(false);
            reflowTranslatedQuickSizes();
            assert(qAbs(label->implicitWidth() - englishWidth) < 0.1);
            assert(qAbs(container->implicitWidth() - englishWidth - 20) < 0.1);
            assert(label->property("text").toString() == text);
            assert(g_translatedQuickSizeItems.contains(label));
        }
    }
    // Do not overwrite application-owned explicit width while repairing the
    // natural width used by layout and other controls' bindings.
    label->setWidth(320);
    g_enabled.store(true);
    relayout();
    g_enabled.store(false);
    reflowTranslatedQuickSizes();
    assert(label->width() == 320);
    g_enabled.store(true);
    relayout();
    object.reset();
    g_enabled.store(false);
    reflowTranslatedQuickSizes(); // Destroyed labels are harmless.
    assert(g_translatedQuickSizeItems.isEmpty());
    std::cout << "English width restore: actual QML layout, bound container, repeated toggles, font/text changes and destroyed labels passed\n";

    // MatchText-style rich label: change language on the SAME page, without
    // recreating the component, changing tabs or manually forcing its layout.
    QQmlComponent rich(&engine);
    rich.setData(R"(
        import QtQuick
        Item {
            width: label.implicitWidth
            height: 40
            Text {
                id: label
                objectName: "richToggleLabel"
                anchors.fill: parent
                text: "Oblique"
                textFormat: Text.RichText
                wrapMode: Text.Wrap
                verticalAlignment: Text.AlignVCenter
                font.pixelSize: 18
            }
        }
    )", QUrl());
    g_enabled.store(true);
    std::unique_ptr<QObject> richRoot(rich.create());
    assert(richRoot);
    auto *richLabel = richRoot->findChild<QQuickItem *>(QStringLiteral("richToggleLabel"));
    auto *document = richLabel->findChild<QTextDocument *>();
    assert(document);
    for (bool enabled : {false, true, false, true, false}) {
        g_enabled.store(enabled);
        refreshLanguageDisplay();
        QCoreApplication::processEvents();
        assert(document->toPlainText() == (enabled ? QStringLiteral("倾斜") : QStringLiteral("Oblique")));
        assert(richLabel->property("text").toString() == QStringLiteral("Oblique"));
        assert(richLabel->property("lineCount").toInt() == 1);
        assert(richLabel->property("contentHeight").toReal() <= richLabel->height());
        assert(g_translatedQuickSizeItems.contains(richLabel));
    }
    richRoot.reset();
    reflowTranslatedQuickSizes(true);
    assert(g_translatedQuickSizeItems.isEmpty());
    std::cout << "Same-page rich labels: repeated language switches preserve one line and restore document/layout without page navigation\n";

}
// A host callback may synchronously remove both the item being refreshed and
// another item already queued for traversal. Keep the document alive so this
// exercises our traversal lifetime, not deletion inside Qt's own setHtml().
void exerciseQuickRefreshLifetime() {
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        Item {
            Text { objectName: "first"; text: "Oblique" }
            Text { objectName: "pendingSibling"; text: "Oblique" }
        }
    )", QUrl());
    g_enabled.store(false);
    std::unique_ptr<QObject> root(component.create());
    assert(root);
    QPointer<QQuickItem> first = root->findChild<QQuickItem *>("first");
    QPointer<QQuickItem> sibling = root->findChild<QQuickItem *>("pendingSibling");
    assert(first && sibling);
    auto *document = new QTextDocument(first);
    bool callbackRan = false;
    QObject::connect(document, &QTextDocument::contentsChanged, [&] {
        if (callbackRan) return;
        callbackRan = true;
        document->setParent(nullptr);
        delete first.data();
        delete sibling.data();
    });
    g_enabled.store(true);
    std::unordered_set<QQuickItem *> visited;
    refreshQuickTextItemTree(qobject_cast<QQuickItem *>(root.get()), visited);
    assert(callbackRan && !first && !sibling);
    assert(visited.size() == 2); // Root and first; destroyed sibling is skipped.
    delete document;
    g_enabled.store(false);
    std::cout << "QML refresh lifetime: current and queued sibling destroyed in host callback, traversal safely completed\n";
}

void exerciseTransactionExceptionCleanup() {
    HANDLE wake = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    assert(wake);
    std::thread worker([wake] { assert(WaitForSingleObject(wake, 5000) == WAIT_OBJECT_0); });
    bool caught = false;
    try {
        PendingDetourTransaction transaction;
        assert(transaction.begin() == NO_ERROR);
        assert(DetourUpdateThread(worker.native_handle()) == NO_ERROR);
        throw std::bad_alloc(); // Same unwind path as Detours' internal new.
    } catch (const std::bad_alloc &) {
        caught = true;
    }
    assert(caught);
    SetEvent(wake);
    assert(WaitForSingleObject(worker.native_handle(), 5000) == WAIT_OBJECT_0);
    worker.join();
    CloseHandle(wake);
    PendingDetourTransaction next;
    assert(next.begin() == NO_ERROR);
    assert(next.commit() == NO_ERROR);
    std::cout << "Detours exception cleanup: suspended thread resumed and next transaction accepted\n";
}

void nativeEditableDraw(QGraphicsTextItem *item, QPainter *,
                        const QStyleOptionGraphicsItem *, QWidget *) {
    editableDrawn = true;
    assert(g_inDisplayTranslation);
    assert(displayTranslation(item->toPlainText()).isEmpty());
}

void exerciseInlineExperiment() {
    if (!experimentalQtInlineEnabled())
        return;
    using namespace HoudiniChineseHooks;
    refreshHookTargets();
    g_hookRegistry.enableObservation(true);
    g_qtextLayoutDraw = nullptr; // Earlier tests used a callable test double.
    installFontMeasurementHooks();
    assert(installHooks());
    assert(installQTextLayoutDrawHook());
    installStaticTextHooks();
    for (HookId id : {HookId::hookedDrawRect, HookId::hookedQTextLayoutDraw,
                      HookId::hookedDrawStaticText, HookId::hookedFontElide})
        assert(g_hookRegistry.record(id).inlinePatched);

    const std::pair<const char *, const char *> samples[] = {
        {"Oblique", "倾斜"},
        {"Wireframe Over Ghost Surfaces", "在虚影表面上叠加线框"},
        {"cloth", "布料"}, {"redvelvet", "红色天鹅绒"},
        {"softbeigeleather", "柔软米色皮革"},
    };
    for (const auto &sample : samples) {
        const auto hash = dictionaryHashBytes(sample.first);
        g_dictionaryIndex.emplace(hash, g_dictionaryEntries.size());
        g_dictionaryEntries.push_back({hash, sample.first, sample.second});
    }
    HMODULE gui = GetModuleHandleW(L"Qt6Gui.dll");
    // Raw export invocation bypasses all patched import slots. Pixel equality
    // proves this path translates; counters alone would not prove the result.
    const auto draw = reinterpret_cast<DrawRect>(GetProcAddress(gui,
        "?drawText@QPainter@@QEAAXAEBVQRect@@HAEBVQString@@PEAV2@@Z"));
    assert(draw);
    const auto render = [&](const QString &text, bool enabled, bool layoutPath) {
        g_enabled.store(enabled);
        QImage image(600, 60, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        painter.setPen(Qt::black);
        if (layoutPath) {
            QTextLayout layout(text, painter.font(), &image);
            layout.beginLayout();
            layout.createLine().setLineWidth(580);
            layout.endLayout();
            layout.draw(&painter, QPointF(4, 4));
            assert(layout.text() == text);
        } else {
            draw(&painter, QRect(4, 4, 580, 50), Qt::AlignLeft, text, nullptr);
        }
        painter.end();
        return image;
    };
    const QFontMetrics metrics(QApplication::font());
    QLineEdit editor;
    editor.setText(QStringLiteral("Oblique"));
    editor.resize(300, 40);
    const auto renderEditor = [&](bool enabled) {
        g_enabled.store(enabled);
        QImage image(editor.size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        editor.render(&image);
        return image;
    };
    const QImage originalEditor = renderEditor(false);
    assert(renderEditor(true) == originalEditor);
    assert(editor.text() == QStringLiteral("Oblique"));
    for (const auto &sample : samples) {
        const QString source = QString::fromUtf8(sample.first);
        const QString translated = QString::fromUtf8(sample.second);
        for (bool layoutPath : {false, true}) {
            const QImage expected = render(translated, false, layoutPath);
            assert(render(source, true, layoutPath) == expected);
            assert(render(source, false, layoutPath) != expected);
        }
        g_enabled.store(false);
        const QString expected = metrics.elidedText(translated, Qt::ElideRight, 64);
        g_enabled.store(true);
        assert(metrics.elidedText(source, Qt::ElideRight, 64) == expected);
        assert(source == QString::fromUtf8(sample.first));
    }
    g_enabled.store(false);
    std::cout << "Qt inline experiment: raw exports, layout pixels, elision, toggle and source preservation passed\n";
    exerciseQuickComboDisplay();
    exerciseEnglishWidthRestore();
    exerciseQuickRefreshLifetime();
    {
        QTabBar tabs;
        tabs.setExpanding(false);
        tabs.addTab(QStringLiteral("Wireframe Over Ghost Surfaces"));
        tabs.addTab(QStringLiteral("Oblique"));
        tabs.resize(800, 40);
        tabs.show();
        g_enabled.store(false);
        QEvent englishFont(QEvent::FontChange);
        QCoreApplication::sendEvent(&tabs, &englishFont);
        const int englishTabWidth = tabs.tabRect(0).width();
        g_enabled.store(true);
        QEvent chineseFont(QEvent::FontChange);
        QCoreApplication::sendEvent(&tabs, &chineseFont);
        const int chineseTabWidth = tabs.tabRect(0).width();
        g_oneShotShortcutTarget = &tabs;
        g_enabled.store(false);
        refreshLanguageDisplay();
        QCoreApplication::processEvents();
        std::cout << "Tab widths: English=" << englishTabWidth
                  << " Chinese=" << chineseTabWidth
                  << " restored=" << tabs.tabRect(0).width() << std::endl;
        assert(englishTabWidth != chineseTabWidth);
        assert(tabs.tabRect(0).width() == englishTabWidth);
        assert(tabs.tabText(0) == QStringLiteral("Wireframe Over Ghost Surfaces"));
        g_oneShotShortcutTarget = nullptr;
    }
}
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    const QString english = QStringLiteral("File");
    const QString chinese = QStringLiteral("文件");
    const auto hash = dictionaryHashBytes("File");
    g_dictionaryEntries.push_back({hash, "File", "文件"});
    g_dictionaryIndex.emplace(hash, 0);
    g_dictionaryReady.store(true);
    g_matching.add("File", "错误候选"); // Exact lookup must still win.
    g_matching.add("Rough Glass", "粗糙玻璃");
    g_matching.finalize();
    assert(lookupTranslationUncached("File") == chinese);
    assert(lookupTranslationUncached("Rough_Glass") == QStringLiteral("粗糙玻璃"));
    assert(lookupTranslationUncached("Rough Glsss*") == QStringLiteral("粗糙玻璃*"));
    assert(displayTranslation("Rough Glsss") == QStringLiteral("粗糙玻璃"));
    {
        TextScope input(TextRole::UserValue);
        assert(displayTranslation("Rough Glsss").isEmpty());
        assert(displayTranslation("Rough_Glass").isEmpty());
    }
    g_enabled.store(false);
    assert(displayTranslation("Rough Glsss").isEmpty());
    g_enabled.store(true);
    g_fmHAdv = &nativeAdvance;
    g_fmfHAdv = &nativeAdvanceF;
    QFontMetrics font(app.font());
    QFontMetricsF fontF(app.font());
    for (int length : {0, 1, 3}) {
        assert(hookedFontHAdv(&font, english, length) ==
               font.horizontalAdvance(english, length));
        assert(measured == english && measuredLength == length);
        assert(hookedFontFHAdv(&fontF, english, length) ==
               fontF.horizontalAdvance(english, length));
        assert(measured == english && measuredLength == length);
    }
    const int expected = font.horizontalAdvance(chinese) +
                         font.horizontalAdvance(QStringLiteral("中"));
    assert(hookedFontHAdv(&font, english, -1) == expected);
    assert(hookedFontHAdv(&font, english, static_cast<int>(english.size())) == expected);
    g_enabled.store(false);
    assert(hookedFontHAdv(&font, english, -1) == font.horizontalAdvance(english));
    assert(hookedFontFHAdv(&fontF, english, -1) == fontF.horizontalAdvance(english));
    g_enabled.store(true);

    QImage image(400, 100, QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&image);
    QTextLayout layout(english, app.font(), &image);
    layout.beginLayout();
    layout.createLine().setLineWidth(300);
    layout.endLayout();
    g_qtextLayoutDraw = &nativeLayoutDraw;
    hookedQTextLayoutDraw(&layout, &painter, {}, {}, {});
    assert(drawn == chinese && layout.text() == english);
    layout.setPreeditArea(0, QStringLiteral("x"));
    hookedQTextLayoutDraw(&layout, &painter, {}, {}, {});
    assert(drawn == english);
    layout.setPreeditArea(-1, {});
    QTextLayout::FormatRange selection;
    selection.length = 1;
    hookedQTextLayoutDraw(&layout, &painter, {}, {selection}, {});
    assert(drawn == english);

    QLineEdit editor;
    editor.setText(english);
    assert(displayTranslationForDevice(&editor, english).isEmpty());
    QGraphicsTextItem editable(english);
    editable.setTextInteractionFlags(Qt::TextEditorInteraction);
    g_graphicsTextPaint = &nativeEditableDraw;
    hookedGraphicsTextPaint(&editable, &painter, nullptr, nullptr);
    assert(editableDrawn && editable.toPlainText() == english);
    assert(!g_inDisplayTranslation && displayTranslation(english) == chinese);

    // Owner evidence wins over coincidentally equal text elsewhere on screen.
    QLabel label(english);
    label.setAttribute(Qt::WA_InputMethodEnabled, true);
    assert(displayTranslationForDevice(&label, english) == chinese);
    assert(displayQuickLabelTranslation(english) == chinese);
    assert(displayTranslationForDevice(&editor, english).isEmpty());
    // A native input and its sibling labels can share one QWidget surface.
    // Toggling IME support must not revoke an explicit native label owner.
    QWidget nativeSurface;
    for (bool inputActive : {false, true, false, true}) {
        nativeSurface.setAttribute(Qt::WA_InputMethodEnabled, inputActive);
        ScopedAllowHoudiniUiText knownNativeLabel;
        assert(displayTranslationForDevice(&nativeSurface, english) == chinese);
        assert(displayTranslationForDevice(&editor, english).isEmpty());
        { TextScope nativeInputOwner(TextRole::UserValue);
          assert(displayTranslationForDevice(&nativeSurface, english).isEmpty()); }
    }
    // An IME canvas is unknown, not an editor class. Identical text in an
    // unrelated editor must not suppress its translation.
    assert(textRoleForDevice(&nativeSurface) == TextRole::Unknown);
    assert(displayTranslationForDevice(&nativeSurface, english) == chinese);
    QComboBox combo;
    combo.addItem(english);
    assert(displayTranslationForDevice(&combo, english) == chinese);
    combo.setEditable(true);
    combo.setEnabled(false);
    assert(displayTranslationForDevice(&combo, english).isEmpty());
    editor.setReadOnly(true);
    editor.setEnabled(false);
    assert(displayTranslationForDevice(&editor, english).isEmpty());
    QTextEdit document;
    document.setReadOnly(true);
    assert(isProtectedTextWidget(document.viewport()));
    QTextBrowser help;
    assert(displayTranslationForDevice(help.viewport(), english) == chinese);
    // A rendering thread must not veto a known QML label merely because an
    // unrelated QWidget editor has focus on the GUI thread.
    std::thread renderThread([&] {
        assert(displayQuickLabelTranslation(english) == chinese);
        TextScope input(TextRole::UserValue);
        assert(displayQuickLabelTranslation(english).isEmpty());
    });
    renderThread.join();
    {
        TextScope input(TextRole::UserValue);
        ScopedAllowHoudiniUiText nativeLabel;
        assert(displayQuickLabelTranslation(english).isEmpty());
        assert(displayTranslationForDevice(&label, english).isEmpty());
    }
    assert(HoudiniChineseTextContext::current.role == TextRole::Unknown);
    assert(displayTranslationForDevice(&label, english) == chinese);
    // Shared forwarding keeps the owner's protection until the original draw
    // returns, including nested overloads that paint through another device.
    {
        TextScope inputOwner(TextRole::UserValue);
        drawDisplayText(&painter, english, [&](const QString &display) {
            assert(display == english);
            assert(HoudiniChineseTextContext::current.role == TextRole::UserValue);
            assert(displayTranslationForDevice(&label, english).isEmpty());
        });
    }
    assert(HoudiniChineseTextContext::current.role == TextRole::Unknown);
    std::cout << "owner policy: same-text labels, rendering thread, nested protection and read-only values passed\n";

    // Reuse is bounded by object, draw lifetime and thread, including a
    // negative classification. It must never grant permission to an editor.
    UI_InputFieldLook inputLookFixture;
    DerivedInputLookFixture derivedInputLookFixture;
    assert(nativeLookTextRole(&inputLookFixture) == TextRole::UserValue);
    assert(nativeLookTextRole(&derivedInputLookFixture) == TextRole::UserValue);
    assert(!shouldAllowNativeLookText(&inputLookFixture));
    int firstLook = 0, secondLook = 0;
    {
        ScopedNativeLookDecision first(&firstLook, TextRole::DisplayLabel);
        assert(shouldAllowNativeLookText(&firstLook));
        {
            ScopedNativeLookDecision second(&secondLook, TextRole::UserValue);
            assert(!shouldAllowNativeLookText(&secondLook));
        }
        assert(shouldAllowNativeLookText(&firstLook));
        std::thread independent([] { assert(!g_activeNativeLookDecision.object); });
        independent.join();
        TextScope protectedOwner(TextRole::UserValue);
        {
            ScopedAllowHoudiniUiText parentLabel;
            ScopedAllowHoudiniUiText childLabel;
            assert(displayTranslationForDevice(&label, english).isEmpty());
            assert(HoudiniChineseTextContext::current.role == TextRole::UserValue);
        }
    }
    assert(!g_activeNativeLookDecision.object);
    assert(HoudiniChineseTextContext::current.role == TextRole::Unknown);
    assert(!HoudiniChineseTextContext::current.allowNativeText);
    {
        ScopedAllowHoudiniUiText parentLabel;
        { ScopedAllowHoudiniUiText repeatedLabel; }
        assert(HoudiniChineseTextContext::current.role == TextRole::DisplayLabel);
        { TextScope protectedOwner(TextRole::UserValue);
          ScopedAllowHoudiniUiText nestedLabel;
          assert(HoudiniChineseTextContext::current.role == TextRole::UserValue); }
        assert(HoudiniChineseTextContext::current.role == TextRole::DisplayLabel);
    }
    assert(HoudiniChineseTextContext::current.role == TextRole::Unknown);
    std::cout << "Native context reuse: object/thread isolation, nested restore and editor priority passed\n";

    g_enabled.store(false);
    // Disabled mode must return before inspecting or converting native bytes.
    const auto *unreadable = reinterpret_cast<const char *>(std::uintptr_t{1});
    assert(translateTextToUtf8(unreadable).empty());
    assert(translateTextRangeToUtf8(unreadable, nullptr).empty());
    NativeTranslatedTextRange range;
    assert(!translatedUtf8TextRange(unreadable, nullptr, true, range));
    std::cout << "display_hooks_test: passed (no hook installation)\n";
    exerciseTransactionExceptionCleanup();
    exerciseInlineExperiment();
}
