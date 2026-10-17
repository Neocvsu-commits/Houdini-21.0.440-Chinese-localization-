// Houdini pure display-layer translation engine.
// Source text remains in the host control/model; hooks replace only temporary
// paint and measurement arguments. Runtime capabilities are resolved from the
// Qt and Houdini DLLs already loaded by the host.
#include <QtCore/QCoreApplication>
#include <QtCore/QAbstractNativeEventFilter>
#include <QtCore/QByteArray>
#include <QtCore/QEvent>
#include <QtCore/QEventLoop>
#include <QtCore/QAbstractItemModel>
#include <QtCore/QModelIndex>
#include <QtCore/QPersistentModelIndex>
#include <QtCore/QSet>
#include <QtCore/QSignalBlocker>
#include <QtCore/QTimer>
#include <QtCore/QString>
#include <QtCore/QPointer>
#include <QtCore/QThread>
#include <QtCore/QVariant>
#include <QtCore/QList>
#include <QtCore/QHash>
#include <QtCore/QUrl>
#include <QtGui/QPainter>
#include <QtGui/QFontMetrics>
#include <QtGui/QFontMetricsF>
#include <QtGui/QTextOption>
#include <QtGui/QStaticText>
#include <QtGui/QTextLayout>
#include <QtGui/QTextCursor>
#include <QtGui/QTextDocument>
#include <QtGui/QPalette>
#include <QtGui/QWindow>
#include <QtGui/QDesktopServices>
#include <QtGui/QAccessible>
#include <QtGui/QPixmap>
#include <QtWidgets/QApplication>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QCommonStyle>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QStackedWidget>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QTabBar>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QToolBox>
#include <QtWidgets/QTreeView>
#include <QtWidgets/QWizardPage>
#include <QtWidgets/QWidget>
#include <QtWidgets/QGraphicsSimpleTextItem>
#include <QtWidgets/QGraphicsTextItem>
#include <QtWidgets/QStyleOptionGraphicsItem>
#include <QtWidgets/QStyle>
#include <QtWidgets/QStylePainter>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QAbstractSpinBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QPlainTextEdit>
#include <QtQml/QJSValue>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>

#include "hooks/hook_lifecycle.h"
#include "translation/display_text.h"
#include "translation/matching.h"
#include "translation/display_text_layout.h"
#include "translation/text_context.h"
#include "ui/hotkey_settings.h"
#include "ui/menu_buttons.h"

#include "capture/native_capture.h"
#include "ui/input_events.h"

#include "hooks/hook_registry.h"
#include "hooks/hook_diagnostics.h"
#include "hooks/import_hook.h"


#include "hooks/native_dispatch_hook.h"
#include "hooks/native_rtti.h"

#include "ui/plugin_config.h"
#include "translation/quick_text_width.h"

#include "translation/translation_json.h"
#include "translation/placeholder_templates.h"
#include "translation/messages21.h"


#include "capture/untranslated_capture.h"

#include "detours/detours.h"

#include <UT/UT_HDKVersion.h>
#include <SYS/SYS_Version.h>
#include <UT/UT_StringHolder.h>
#include <UT/UT_DeepString.h>
#include <UI/UI_DeviceEvent.h>

#include <cwchar>
#include <cstring>
#include <array>
#include <vector>
#include <algorithm>
#include <atomic>
#include <memory>
#include <utility>
#include <type_traits>
#include <unordered_map>
#include <mutex>
#include <unordered_set>
#include <string>

namespace {

// H21 exports this method as protected (I), while H22 exports it as public (Q).
// Access changes the MSVC export name, not this signature's calling convention.
constexpr const char *kUiTextWidthSymbol = HC_UI_WIDTH_SYMBOL;
// H21 uses source-string offsets while wrapping/painting. Translating those
// measurements shrinks preferred rows and changes their vertical alignment.
// Expand widths only in preferred-size queries and never shrink English bounds.
thread_local unsigned g_nativePreferredSizeDepth21 = 0;

// Release builds intentionally perform no file I/O. Keep failure annotations
// at their call sites without evaluating their arguments or producing logs.
#define hookLog(...) ((void)0)

HoudiniChineseHooks::HookRegistry g_hookRegistry;
std::atomic<bool> g_hookRegistryReady{false};
#define HC_OBSERVE(Id) HoudiniChineseHooks::HookObservation hookObservation(\
    g_hookRegistry, HoudiniChineseHooks::HookId::Id)

// 英文原文 -> 中文 （精确匹配）
std::unordered_map<std::string, std::string> g_translations;
std::unordered_map<std::string, std::string> g_templateDefinitions;
struct DictionaryEntry final {
    uint64_t hash = 0;
    std::string source;
    std::string translation;
};
std::vector<DictionaryEntry> g_dictionaryEntries;
std::unordered_map<uint64_t, size_t> g_dictionaryIndex;
// Exact, unambiguous Chinese -> English entries are used only to undo a
// translated MOT tooltip that Houdini cached before the language was turned
// off. Ambiguous translations are deliberately omitted.
std::unordered_map<std::string, std::string> g_reverseDictionary;
// 会话级反查缓存：把本次运行翻译过的（显示文本 -> 原文）成对记录下来。
// 歧义译文（同一句中文来自多个英文原文）无法进入静态反查表，切回英文时就会
// 残留中文；凡是本会话显示过的文案都能在这里精确还原。
std::mutex g_sessionReverseMutex;
std::unordered_map<std::string, std::string> g_sessionReverse;

void recordSessionReverse(const QString &source, const QString &display) {
    if (source.isEmpty() || display.isEmpty() || source == display)
        return;
    const QByteArray sourceBytes = source.toUtf8();
    const QByteArray displayBytes = display.toUtf8();
    if (sourceBytes.size() > 4096 || displayBytes.size() > 512)
        return;
    std::lock_guard<std::mutex> lock(g_sessionReverseMutex);
    if (g_sessionReverse.size() > 50000)
        g_sessionReverse.clear();
    g_sessionReverse[std::string(displayBytes.constData(),
                                 static_cast<size_t>(displayBytes.size()))] =
        std::string(sourceBytes.constData(), static_cast<size_t>(sourceBytes.size()));
}

QString findSessionReverse(const QByteArray &display) {
    std::lock_guard<std::mutex> lock(g_sessionReverseMutex);
    const auto found = g_sessionReverse.find(
        std::string(display.constData(), static_cast<size_t>(display.size())));
    if (found == g_sessionReverse.end())
        return {};
    return QString::fromUtf8(found->second.data(),
                             static_cast<qsizetype>(found->second.size()));
}
HoudiniChinesePlaceholders::Catalog g_placeholderTemplates;
HoudiniChineseMatching::Catalog g_matching;
thread_local HoudiniChineseLookupCache::DirectMapped<> g_lookupCache;
std::atomic_bool g_enabled{true};
std::atomic_bool g_dictionaryReady{false};
bool displayWorkRequested() noexcept {
    return g_enabled.load(std::memory_order_acquire) ||
           HoudiniChineseCapture::isEnabled();
}
thread_local bool g_inDisplayTranslation = false;
struct DisplayTranslationGuard final {
    const bool previous = g_inDisplayTranslation;
    DisplayTranslationGuard() { g_inDisplayTranslation = true; }
    ~DisplayTranslationGuard() { g_inDisplayTranslation = previous; }
};

uint64_t dictionaryHashBytes(const std::string &text) {
    uint64_t hash = HoudiniChineseUtf8::kFnvOffset;
    for (const unsigned char value : text) {
        hash ^= value;
        hash *= HoudiniChineseUtf8::kFnvPrime;
    }
    return hash;
}

QString findDictionaryExact(const QString &key) {
    const HoudiniChineseUtf8::Stats stats =
        HoudiniChineseUtf8::stats(key, 0, key.size());
    const auto found = g_dictionaryIndex.find(stats.hash);
    if (found == g_dictionaryIndex.end())
        return {};
    for (size_t index = found->second;
         index < g_dictionaryEntries.size() &&
         g_dictionaryEntries[index].hash == stats.hash;
         ++index) {
        const DictionaryEntry &entry = g_dictionaryEntries[index];
        if (entry.source.size() != stats.length)
            continue;
        if (HoudiniChineseUtf8::equals(key, 0, key.size(), entry.source.data(),
                                       entry.source.size())) {
            return QString::fromUtf8(
                entry.translation.c_str(),
                static_cast<qsizetype>(entry.translation.size()));
        }
    }
    return {};
}

void rebuildReverseDictionary() {
    g_reverseDictionary.clear();
    g_reverseDictionary.reserve(g_dictionaryEntries.size());
    std::unordered_set<std::string> ambiguous;
    ambiguous.reserve(g_dictionaryEntries.size() / 8);
    for (const DictionaryEntry &entry : g_dictionaryEntries) {
        if (entry.translation.empty() || entry.translation == entry.source ||
            ambiguous.find(entry.translation) != ambiguous.end())
            continue;
        const auto inserted = g_reverseDictionary.emplace(entry.translation,
                                                           entry.source);
        if (!inserted.second && inserted.first->second != entry.source) {
            g_reverseDictionary.erase(inserted.first);
            ambiguous.emplace(entry.translation);
        }
    }
}

QString findDictionarySourceExact(const QString &translation) {
    const QByteArray bytes = translation.toUtf8();
    const QString session = findSessionReverse(bytes);
    if (!session.isEmpty())
        return session;
    const auto found = g_reverseDictionary.find(std::string(
        bytes.constData(), static_cast<std::size_t>(bytes.size())));
    if (found == g_reverseDictionary.end())
        return {};
    return QString::fromUtf8(found->second.data(),
                             static_cast<qsizetype>(found->second.size()));
}

// Houdini can split a shelf label and apply an ellipsis before UI_Text sees
// it (for example, "Mirror" + "Capture Wei..."). Recover only short Latin
// display labels and choose the shortest unique dictionary completion. This
// stays conservative when several equally short entries share the prefix.
QString findDictionaryEllipsisCompletion(const QString &text) {
    QString prefix = text;
    if (prefix.endsWith(QStringLiteral("...")))
        prefix.chop(3);
    else if (prefix.endsWith(QChar(0x2026)))
        prefix.chop(1);
    else
        return {};
    prefix = prefix.trimmed();
    if (prefix.size() < 7 || prefix.size() > 60)
        return {};
    for (QChar ch : prefix) {
        if (!((ch >= QLatin1Char('A') && ch <= QLatin1Char('Z')) ||
              (ch >= QLatin1Char('a') && ch <= QLatin1Char('z')) ||
              ch == QLatin1Char(' ') || ch == QLatin1Char('-')))
            return {};
    }
    const QByteArray bytes = prefix.toUtf8();
    const std::string_view start(bytes.constData(),
                                 static_cast<std::size_t>(bytes.size()));
    const DictionaryEntry *best = nullptr;
    bool ambiguous = false;
    for (const DictionaryEntry &entry : g_dictionaryEntries) {
        if (entry.source.size() <= start.size() ||
            entry.source.compare(0, start.size(), start) != 0)
            continue;
        if (!best || entry.source.size() < best->source.size()) {
            best = &entry;
            ambiguous = false;
        } else if (entry.source.size() == best->source.size() &&
                   entry.translation != best->translation) {
            ambiguous = true;
        }
    }
    return best && !ambiguous
        ? QString::fromUtf8(best->translation.c_str(),
                            static_cast<qsizetype>(best->translation.size()))
        : QString();
}
bool g_hooksInstalled = false;
HMODULE g_moduleHandle = nullptr;

QKeySequence g_toggleShortcut = HoudiniChineseConfig::defaultToggleShortcut();
bool g_hotkeyDialogOpen = false;
QPointer<HoudiniChineseHotkeyDialog> g_hotkeyDialog;
QString g_configPath;
QSet<int> g_swallowedShortcutKeys;
bool g_oneShotCaptureRunning = false;

// SI_Tree is Houdini's native hierarchy control (it is not a QTreeView).
// During the one-shot scan its render path supplies live SI_TreeNode pointers;
// the scanner then walks their private parent/child model without expanding UI.
constexpr std::size_t kMaxVisibleSITreeNodes = 65536;
std::array<std::atomic<void *>, kMaxVisibleSITreeNodes> g_visibleSITreeNodes{};
std::atomic_size_t g_visibleSITreeNodeCount{0};
std::atomic_bool g_collectVisibleSITreeNodes{false};

using SITreeDoRenderNodeFn = void (*)(void *, void *, void *, void *, void *,
                                      int, int, int, int, int, int);
SITreeDoRenderNodeFn g_siTreeDoRenderNode = nullptr;
HoudiniChineseNativeTree::Api g_siTreeNodeApi;
HoudiniChineseNativeTree::Api g_siTreeNodeFunctions;

// UI_Feel is the common base of Houdini's private controls. A one-shot scan
// records the feels rendered by libUI, climbs each to its root, then walks the
// complete child hierarchy. This reaches folded groups, inactive switcher
// pages and unopened static submenu branches without changing UI state.
constexpr std::size_t kMaxVisibleUIFeels = 65536;
constexpr std::size_t kMaxVisibleUILooks = 65536;
constexpr std::size_t kInvalidVtableSlot = static_cast<std::size_t>(-1);
std::array<std::atomic<void *>, kMaxVisibleUIFeels> g_visibleUIFeels{};
std::atomic_size_t g_visibleUIFeelCount{0};
std::array<std::atomic<void *>, kMaxVisibleUILooks> g_visibleUILooks{};
std::atomic_size_t g_visibleUILookCount{0};
std::atomic_bool g_collectVisibleUIFeels{false};
std::atomic_uint32_t g_activeNativeCaptureWriters{0};

class ActiveNativeCaptureWriter final {
  public:
    ActiveNativeCaptureWriter() noexcept {
        g_activeNativeCaptureWriters.fetch_add(1, std::memory_order_seq_cst);
    }
    ~ActiveNativeCaptureWriter() noexcept {
        g_activeNativeCaptureWriters.fetch_sub(1, std::memory_order_seq_cst);
    }

    ActiveNativeCaptureWriter(const ActiveNativeCaptureWriter &) = delete;
    ActiveNativeCaptureWriter &
    operator=(const ActiveNativeCaptureWriter &) = delete;
};

class ScopedNativeCollection final {
  public:
    explicit ScopedNativeCollection(std::atomic_bool &flag) noexcept
        : flag_(flag) {
        flag_.store(true, std::memory_order_seq_cst);
    }
    ~ScopedNativeCollection() noexcept { stop(); }

    ScopedNativeCollection(const ScopedNativeCollection &) = delete;
    ScopedNativeCollection &operator=(const ScopedNativeCollection &) = delete;

    void stop() noexcept { flag_.store(false, std::memory_order_seq_cst); }

  private:
    std::atomic_bool &flag_;
};

bool waitForNativeCaptureWriters() noexcept {
    const ULONGLONG deadline = GetTickCount64() + 1000;
    while (g_activeNativeCaptureWriters.load(std::memory_order_seq_cst) != 0) {
        if (GetTickCount64() >= deadline)
            return false;
        SwitchToThread();
    }
    return true;
}

template <std::size_t Capacity>
void recordVisibleNativePointer(
    void *pointer, std::atomic_bool &collecting, std::atomic_size_t &count,
    std::array<std::atomic<void *>, Capacity> &storage) noexcept {
    if (!pointer || !collecting.load(std::memory_order_seq_cst))
        return;
    ActiveNativeCaptureWriter writer;
    if (!collecting.load(std::memory_order_seq_cst))
        return;
    const std::size_t slot = count.fetch_add(1, std::memory_order_acq_rel);
    if (slot < storage.size())
        storage[slot].store(pointer, std::memory_order_release);
}

using UIFeelRenderMeFn = void (*)(void *, void *, int, int);
using UILookDrawFn = void (*)(void *, void *, int, int, int, int, void *);
using UIFeelParentFn = void *(*)(void *);
using UIFeelChildCountFn = int (*)(void *);
using UIFeelChildFn = void *(*)(void *, int);
using NativeCStringFn = const char *(*)(void *);
using NativeFeelPointerFn = void *(*)(void *);
using UIStringChooserEntryCountFn = int (*)(void *);
using UIStringChooserEntryFn = const UT_StringHolder *(*)(void *, int);

UIFeelRenderMeFn g_uiFeelRenderMe = nullptr;
UILookDrawFn g_uiLookDraw = nullptr;
UIFeelParentFn g_uiFeelParent = nullptr;
UIFeelChildCountFn g_uiFeelChildCount = nullptr;
UIFeelChildFn g_uiFeelChild = nullptr;
std::size_t g_uiFeelClassNameSlot = kInvalidVtableSlot;
std::size_t g_uiItemListEntryCountSlot = kInvalidVtableSlot;
std::size_t g_uiItemListEntrySlot = kInvalidVtableSlot;
std::size_t g_uiItemListSubmenuSlot = kInvalidVtableSlot;
std::size_t g_uiTextGetTextSlot = kInvalidVtableSlot;
std::size_t g_uiFormattedTextGetTextSlot = kInvalidVtableSlot;
NativeCStringFn g_uiMenuItemGetLabel = nullptr;
NativeCStringFn g_uiLabeledFeelGetLabel = nullptr;
NativeCStringFn g_uiGroupFeelGetName = nullptr;
NativeCStringFn g_uiHelpTipGetText = nullptr;
NativeCStringFn g_uiHelpTipGetTitle = nullptr;
NativeCStringFn g_uiCollapseButtonGetLabel = nullptr;
NativeFeelPointerFn g_uiDynamicMenuStripGetList = nullptr;
NativeFeelPointerFn g_uiMenuFeelGetDynamicMenu = nullptr;
NativeFeelPointerFn g_uiMenuFeelGetStaticMenu = nullptr;
NativeFeelPointerFn g_uiMenuBarItemGetMenu = nullptr;
NativeFeelPointerFn g_uiParentMenuItemGetMenuWindow = nullptr;
NativeFeelPointerFn g_uiMenuWindowGetFirstMenu = nullptr;
NativeFeelPointerFn g_uiTextFeelGetLook = nullptr;
NativeFeelPointerFn g_uiChooserGetQuery = nullptr;
void **g_uiStringChooserQueryVtable = nullptr;
UIStringChooserEntryCountFn g_uiStringChooserEntryCount = nullptr;
UIStringChooserEntryFn g_uiStringChooserEntry = nullptr;
HoudiniChineseNativeControl::FeelApi g_uiFeelApi;
HoudiniChineseNativeControl::ItemListApi g_uiItemListApi;

void forceMainWindowRepaint();
void refreshLanguageDisplay();
void toggleLanguage();
void openHotkeyDialog();
void captureUntranslatedOnce();
void redrawAllHoudiniWindows();
void rememberQtWindow(QWindow *window);
bool setHelpFooterChinese(bool chinese);
bool setParameterHeaderChinese(bool chinese);

bool isSupportedGuiHost() {
    wchar_t path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (!length || length >= MAX_PATH)
        return false;
    const wchar_t *name = wcsrchr(path, L'\\');
    name = name ? name + 1 : path;
    return _wcsicmp(name, L"houdini.exe") == 0 ||
           _wcsicmp(name, L"houdinifx.exe") == 0 ||
           _wcsicmp(name, L"houdinicore.exe") == 0;
}

bool isCompatibleRuntime() noexcept {
    return HoudiniChineseCompatibility::isCompatibleRuntime(
        HDK_API_VERSION, UTgetHDKAPIVersion(), QT_VERSION_MAJOR, QT_VERSION_STR,
        qVersion(), GetModuleHandleW(L"Qt6Core.dll") != nullptr,
        GetModuleHandleW(L"Qt6Gui.dll") != nullptr,
        GetModuleHandleW(L"Qt6Widgets.dll") != nullptr);
}

struct NativeFunctionRange final {
    std::uintptr_t begin = 0;
    std::uintptr_t end = 0;

    explicit operator bool() const noexcept {
        return begin != 0 && end > begin;
    }

    bool contains(const void *address) const noexcept {
        const auto value = reinterpret_cast<std::uintptr_t>(address);
        return value >= begin && value < end;
    }
};

NativeFunctionRange resolveNativeFunctionRange(HMODULE module,
                                               const char *symbol) noexcept {
    if (!module || !symbol)
        return {};
    void *address = reinterpret_cast<void *>(GetProcAddress(module, symbol));
    if (!address)
        return {};
    DWORD64 imageBase = 0;
    const PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(
        reinterpret_cast<DWORD64>(address), &imageBase, nullptr);
    if (!function || imageBase == 0 ||
        function->EndAddress <= function->BeginAddress) {
        return {};
    }
    return {
        static_cast<std::uintptr_t>(imageBase + function->BeginAddress),
        static_cast<std::uintptr_t>(imageBase + function->EndAddress),
    };
}

template <std::size_t RangeCount>
bool stackContainsAnyFunction(
    const std::array<NativeFunctionRange, RangeCount> &ranges) noexcept {
    void *frames[20] = {};
    const USHORT count = CaptureStackBackTrace(1, 20, frames, nullptr);
    for (USHORT frame = 0; frame < count; ++frame) {
        for (const NativeFunctionRange &range : ranges) {
            if (range.contains(frames[frame]))
                return true;
        }
    }
    return false;
}

bool loadTranslationsFromJson(const std::wstring &path,
                              bool *hasUsableTranslations = nullptr) {
    if (hasUsableTranslations)
        *hasUsableTranslations = false;
    std::string json;
    {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                                  nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return false;
        struct CloseFile {
            HANDLE value;
            ~CloseFile() { CloseHandle(value); }
        } held{file};
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 ||
            size.QuadPart > 128LL * 1024 * 1024)
            return false;
        json.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        if (!ReadFile(file, json.data(), static_cast<DWORD>(json.size()),
                      &read, nullptr) || read != json.size())
            return false;
    }

    HoudiniChineseJson::DictionaryDocument parsed;
    if (!HoudiniChineseJson::parseDictionaryDocument(json, parsed))
        return false;
    bool usableTranslationFound = false;
    for (auto &entry : parsed.translations) {
        const std::string &key = entry.first;
        std::string &value = entry.second;
        if (!key.empty() && !value.empty()) {
            usableTranslationFound = true;
            g_translations.insert_or_assign(key, std::move(value));
        }
    }
    if (hasUsableTranslations)
        *hasUsableTranslations =
            parsed.hasTranslations && usableTranslationFound;
    for (auto &entry : parsed.templates) {
        const std::string &key = entry.first;
        std::string &value = entry.second;
        if (!key.empty() && !value.empty())
            g_templateDefinitions.insert_or_assign(key, std::move(value));
    }
    return true;
}

bool isEastAsianCodePoint(uint32_t value) noexcept {
    return (value >= 0x1100 && value <= 0x11FF) ||
           (value >= 0x2E80 && value <= 0x303F) ||
           (value >= 0x3040 && value <= 0x30FF) ||
           (value >= 0x3130 && value <= 0x318F) ||
           (value >= 0x31F0 && value <= 0x31FF) ||
           (value >= 0x3400 && value <= 0x4DBF) ||
           (value >= 0x4E00 && value <= 0x9FFF) ||
           (value >= 0xA960 && value <= 0xA97F) ||
           (value >= 0xAC00 && value <= 0xD7AF) ||
           (value >= 0xD7B0 && value <= 0xD7FF) ||
           (value >= 0xF900 && value <= 0xFAFF) ||
           (value >= 0xFF00 && value <= 0xFFEF) ||
           (value >= 0x20000 && value <= 0x323AF) ||
           (value >= 0x2F800 && value <= 0x2FA1F);
}

bool containsEastAsianText(const QString &text) {
    bool found = false;
    HoudiniChineseUtf8::forEachCodePoint(
        text, 0, text.size(), [&found](uint32_t value) {
            found = found || isEastAsianCodePoint(value);
        });
    return found;
}

// Every display/measurement adapter shares this ordered lookup pipeline.
QString lookupTranslationUncached(const QString &trimmed) {
    const QString exact = findDictionaryExact(trimmed);
    if (!exact.isEmpty())
        return exact;
    if constexpr (SYS_VERSION_MAJOR_INT == 21) {
        const QString message = HoudiniChineseMessages21::translate(trimmed, findDictionaryExact);
        if (!message.isEmpty()) return message;
        const QString completed = findDictionaryEllipsisCompletion(trimmed);
        if (!completed.isEmpty()) return completed;
    }
    const HoudiniChineseDisplayText::LookupKey fallback =
        HoudiniChineseDisplayText::normalizedFallbackKey(trimmed);
    if (fallback.modified || fallback.text != trimmed) {
        const QString found = findDictionaryExact(fallback.text);
        if (!found.isEmpty())
            return fallback.modified ? found + QLatin1Char('*') : found;
    }
    const auto equivalent = g_matching.equivalent(fallback.text);
    if (equivalent.decided)
        return fallback.modified && !equivalent.translation.isEmpty()
            ? equivalent.translation + QLatin1Char('*') : equivalent.translation;
    const QString templated = g_placeholderTemplates.lookup(fallback.text);
    if (!templated.isEmpty())
        return fallback.modified ? templated + QLatin1Char('*') : templated;
    const QString fuzzy = g_matching.fuzzy(fallback.text);
    return fallback.modified && !fuzzy.isEmpty() ? fuzzy + QLatin1Char('*') : fuzzy;
}

QString lookupTranslation(const QString &trimmed) {
    return g_lookupCache.lookup(
        trimmed, [&trimmed] { return lookupTranslationUncached(trimmed); });
}

QList<QPointer<QWindow>> g_nodeInfoWindows;
std::array<QPointer<QWindow>, 128> g_knownQtWindows{};
std::size_t g_nextKnownQtWindow = 0;
QPointer<QObject> g_oneShotShortcutTarget;
using TextRole = HoudiniChineseTextContext::Role;
using TextScope = HoudiniChineseTextContext::Scope;

class ScopedAllowHoudiniUiText final {
  public:
    explicit ScopedAllowHoudiniUiText(TextRole role = TextRole::DisplayLabel)
        : scope_(role, true) {}

  private:
    TextScope scope_;
};

bool isHoudiniEditorTextCall() {
    HMODULE ui = GetModuleHandleW(L"libUI.dll");
    if (!ui)
        return false;

    // UI_Text is Houdini's private editable-text renderer. Query each real
    // x64 unwind range instead of assuming unrelated exports remain adjacent
    // between daily builds.
    static const std::array<NativeFunctionRange, 10> ranges = {
        resolveNativeFunctionRange(
            ui, "?getPreferredSizes@UI_Text@@UEAAXAEAH00000@Z"),
        resolveNativeFunctionRange(ui,
                                   "?paint@UI_Text@@UEAAXAEAVQStylePainter@@"
                                   "AEBVQRect@@PEAX@Z"),
        resolveNativeFunctionRange(
            ui, "?paintLineWithColoring@UI_Text@@AEBAXAEAVQStylePainter@@"
                "AEBVQPointF@@AEBVUT_StringView@@H_NAEBVQPalette@@"
                "AEBVQFontMetricsF@@3@Z"),
        resolveNativeFunctionRange(
            ui, "?paintLineWithSelection@UI_Text@@AEBAXAEAVQStylePainter@@"
                "AEBVQPointF@@AEBVUT_StringView@@H_NAEBVQPalette@@"
                "AEBVQFontMetricsF@@3@Z"),
        resolveNativeFunctionRange(
            ui, "?paintText@UI_Text@@QEBAXAEAVQStylePainter@@"
                "AEBVQRect@@PEBD2@Z"),
        resolveNativeFunctionRange(
            ui, "?paintWrappedText@UI_Text@@AEBAXAEAVQStylePainter@@"
                "AEBVQRect@@AEBVUT_StringView@@AEBVQPalette@@"
                "AEBVQFontMetricsF@@_N@Z"),
        resolveNativeFunctionRange(
            ui, kUiTextWidthSymbol),
        resolveNativeFunctionRange(
            ui, "?render@UI_Text@@UEAAXPEAVRE_Render@@HHHHPEAX@Z"),
        resolveNativeFunctionRange(
            ui, "?renderText@UI_Text@@QEAAXPEAVRE_Render@@PEBD1HHHH@Z"),
        resolveNativeFunctionRange(
            ui, "?renderWrappedText@UI_Text@@AEAAXPEAVRE_Render@@"
                "AEBVUT_StringView@@HHHH@Z"),
    };
    return stackContainsAnyFunction(ranges);
}

QString displayTranslation(const QString &text) {
    if (HoudiniChineseTextContext::current.role == TextRole::UserValue) {
        HoudiniChineseHooks::noteTranslationOutcome(HoudiniChineseHooks::TranslationOutcome::Protected);
        return {};
    }
    if (g_inDisplayTranslation)
        return {};
    DisplayTranslationGuard guard;
    const bool translationEnabled = g_enabled.load(std::memory_order_acquire);
    const bool captureEnabled = HoudiniChineseCapture::isEnabled();
    if (!translationEnabled && !captureEnabled)
        return {};
    const QString menuText = text.trimmed();
    if (text.isEmpty() || menuText.size() <= 1)
        return {};
    if (!g_dictionaryReady.load(std::memory_order_acquire))
        return {};
    if (containsEastAsianText(menuText) && findDictionaryExact(menuText).isEmpty() &&
        HoudiniChineseMessages21::translate(menuText, findDictionaryExact).isEmpty())
        return {};
    const QString translated = lookupTranslation(menuText);
    // In normal release operation an untranslated string is returned unchanged,
    // so editor-stack checks cannot affect the result. Only pay for that
    // classification on translation hits, or when development capture is active.
    if (translated.isEmpty() && !captureEnabled) {
        HoudiniChineseHooks::noteTranslationOutcome(HoudiniChineseHooks::TranslationOutcome::LookupMiss);
        return {};
    }
    if (!translationEnabled && !translated.isEmpty())
        return {};
    // UI_Text owns Houdini's editable parameter strings. They are user data,
    // not UI labels, so keep them verbatim in both focused and unfocused states.
    if (HoudiniChineseTextContext::current.role == TextRole::Unknown &&
        !HoudiniChineseTextContext::current.allowNativeText && isHoudiniEditorTextCall()) {
        HoudiniChineseHooks::noteTranslationOutcome(HoudiniChineseHooks::TranslationOutcome::Protected);
        return {};
    }
    HoudiniChineseHooks::noteTranslationOutcome(translated.isEmpty()
        ? HoudiniChineseHooks::TranslationOutcome::LookupMiss
        : HoudiniChineseHooks::TranslationOutcome::Translated);
    if (translated.isEmpty())
        HoudiniChineseCapture::submit(menuText);
    if (translationEnabled && !translated.isEmpty()) {
        const QString display =
            HoudiniChineseDisplayText::preserveOuterWhitespace(text, translated);
        recordSessionReverse(text, display);
        return display;
    }
    return {};
}

// Callers that have already proved the text belongs to a display-only Qt
// Quick label do not need the comparatively expensive Houdini UI_Text stack
// classification. Explicit input-owner protection still runs in displayTranslation.
QString displayQuickLabelTranslation(const QString &text) {
    ScopedAllowHoudiniUiText guard;
    return displayTranslation(text);
}

// User-entered text is data, not a UI label. Protect all standard editable
// widgets and Houdini's private input-method based editors.
bool isProtectedTextWidget(const QWidget *w) {
    return HoudiniChineseTextContext::widgetRole(w) == TextRole::UserValue;
}

TextRole textRoleForDevice(QPaintDevice *device) {
    if (!displayWorkRequested())
        return TextRole::Unknown;
    auto *app = QCoreApplication::instance();
    if (!app || QThread::currentThread() != app->thread())
        return TextRole::Unknown; // Never inspect QWidget on a render thread.
    // A shared native canvas is not an editor class. Only concrete Qt
    // editor types can assign UserValue here; native ownership is established
    // from UI_Look RTTI. IME flags are used only by the capture inventory.
    return HoudiniChineseTextContext::widgetRole(
        device ? dynamic_cast<QWidget *>(device) : nullptr, false);
}

QString displayTranslationForDevice(QPaintDevice *device, const QString &text) {
    TextScope scope(textRoleForDevice(device));
    return displayTranslation(text);
}

// Keep owner protection alive while the host forwards into another paint
// overload. Resolve the device once and share source/translation lifetimes;
// every ABI adapter below only forwards its own geometry arguments.
template <typename Draw>
void drawDisplayText(QPainter *painter, const QString &source, Draw &&draw) {
    TextScope scope(textRoleForDevice(painter ? painter->device() : nullptr));
    const QString translated = displayTranslation(source);
    std::forward<Draw>(draw)(translated.isEmpty() ? source : translated);
}

// Font-metric hooks measure the translated display string without modifying
// the source. One CJK glyph of padding prevents labels touching their edge.
using FmHAdv = int(__cdecl *)(const QFontMetrics *, const QString &, int);
using FmBRectRaw = void *(__cdecl *)(const QFontMetrics *, QRect *,
                                     const QString &);
using FmBRectRectRaw = void *(__cdecl *)(const QFontMetrics *, QRect *,
                                         const QRect &, int, const QString &,
                                         int, int *);
using FmBRectXYWHRaw = void *(__cdecl *)(const QFontMetrics *, QRect *, int,
                                         int, int, int, int, const QString &,
                                         int, int *);
using FmSizeRaw = void *(__cdecl *)(const QFontMetrics *, QSize *, int,
                                    const QString &, int, int *);
using FmTBRectRaw = void *(__cdecl *)(const QFontMetrics *, QRect *,
                                      const QString &);
using FmElideRaw = void *(__cdecl *)(const QFontMetrics *, QString *,
                                     const QString &, Qt::TextElideMode, int,
                                     int);

using FmFHAdv = qreal(__cdecl *)(const QFontMetricsF *, const QString &, int);
using FmFBRectRaw = void *(__cdecl *)(const QFontMetricsF *, QRectF *,
                                      const QString &);
using FmFBRectRectRaw = void *(__cdecl *)(const QFontMetricsF *, QRectF *,
                                          const QRectF &, int, const QString &,
                                          int, int *);
using FmFSizeRaw = void *(__cdecl *)(const QFontMetricsF *, QSizeF *, int,
                                     const QString &, int, int *);
using FmFTBRectRaw = void *(__cdecl *)(const QFontMetricsF *, QRectF *,
                                       const QString &);
using FmFElideRaw = void *(__cdecl *)(const QFontMetricsF *, QString *,
                                      const QString &, Qt::TextElideMode, qreal,
                                      int);

FmHAdv g_fmHAdv = nullptr;
FmBRectRaw g_fmBRect = nullptr;
FmBRectRectRaw g_fmBRectRect = nullptr;
FmBRectXYWHRaw g_fmBRectXYWH = nullptr;
FmSizeRaw g_fmSize = nullptr;
FmTBRectRaw g_fmTBRect = nullptr;
FmElideRaw g_fmElide = nullptr;
FmFHAdv g_fmfHAdv = nullptr;
FmFBRectRaw g_fmfBRect = nullptr;
FmFBRectRectRaw g_fmfBRectRect = nullptr;
FmFSizeRaw g_fmfSize = nullptr;
FmFTBRectRaw g_fmfTBRect = nullptr;
FmFElideRaw g_fmfElide = nullptr;
bool g_fontHooksInstalled = false;

// Qt Quick's QML FontMetrics type wraps QFontMetricsF behind its own exported
// invokable methods.  Controls such as Houdini's ButtonStrip call
// FontMetrics.advanceWidth() directly to size delegates, so hooking only the
// public QFontMetricsF imports can leave those QML widths based on English.
using QuickFontAdvanceFn = qreal(__fastcall *)(const void *, const QString &);
using QuickFontRectRaw = void *(__fastcall *)(const void *, QRectF *,
                                              const QString &);
using QuickFontElideRaw = void *(__fastcall *)(const void *, QString *,
                                               const QString &,
                                               Qt::TextElideMode, qreal, int);
QuickFontAdvanceFn g_quickFontAdvance = nullptr;
QuickFontRectRaw g_quickFontBoundingRect = nullptr;
QuickFontRectRaw g_quickFontTightBoundingRect = nullptr;
QuickFontElideRaw g_quickFontElidedText = nullptr;
bool g_quickFontMetricHooksInstalled = false;

// QQuickText calculates its natural width and publishes it through these two
// QQuickItem setters. Houdini's MatchText/Loader chain ultimately sizes strip
// buttons from that implicit width, so this is the last common size boundary
// after both plain and rich text layout.
using QuickItemSetImplicitWidthFn = void(__fastcall *)(QQuickItem *, qreal);
using QuickItemSetImplicitSizeFn = void(__fastcall *)(QQuickItem *, qreal,
                                                      qreal);
QuickItemSetImplicitWidthFn g_quickItemSetImplicitWidth = nullptr;
QuickItemSetImplicitSizeFn g_quickItemSetImplicitSize = nullptr;
bool g_quickImplicitSizeHooksInstalled = false;

QString measureText(const QString &text) { return displayTranslation(text); }

QString measureQuickText(const QString &text) {
    return displayQuickLabelTranslation(text);
}

int cjkPad(const QFontMetrics *fm) {
    if (g_fmHAdv) {
        const int w = g_fmHAdv(fm, QStringLiteral("中"), -1);
        if (w > 0)
            return w;
    }
    return fm->height();
}
qreal cjkPadF(const QFontMetricsF *fm) {
    if (g_fmfHAdv) {
        const qreal w = g_fmfHAdv(fm, QStringLiteral("中"), -1);
        if (w > 0)
            return w;
    }
    return fm->height();
}

int hookedFontHAdv(const QFontMetrics *fm, const QString &text, int len) {
    HC_OBSERVE(hookedFontHAdv);
    // Prefix lengths are source-string offsets (cursor/partial measurement).
    // A whole-string translation cannot preserve those offsets.
    if (len >= 0 && len < text.size())
        return g_fmHAdv ? g_fmHAdv(fm, text, len) : 0;
    QString t = text;
    QString tr = measureText(text);
    if (!tr.isEmpty())
        t = tr;
    const int effectiveLength = tr.isEmpty() ? len : -1;
    if (g_fmHAdv)
        return g_fmHAdv(fm, t, effectiveLength) +
               (tr.isEmpty() ? 0 : cjkPad(fm));
    return 0;
}
void *hookedFontBRect(const QFontMetrics *fm, QRect *result,
                      const QString &text) {
    HC_OBSERVE(hookedFontBRect);
    QString t = text;
    QString tr = measureText(text);
    if (!tr.isEmpty())
        t = tr;
    if (g_fmBRect)
        g_fmBRect(fm, result, t);
    if (!tr.isEmpty())
        result->setWidth(result->width() + cjkPad(fm));
    return result;
}
void *hookedFontBRectRect(const QFontMetrics *fm, QRect *result, const QRect &r,
                          int flags, const QString &text, int tabstops,
                          int *tabarray) {
    HC_OBSERVE(hookedFontBRectRect);
    QString t = text;
    QString tr = measureText(text);
    if (!tr.isEmpty())
        t = tr;
    if (g_fmBRectRect)
        g_fmBRectRect(fm, result, r, flags, t, tabstops, tabarray);
    if (!tr.isEmpty())
        result->setWidth(result->width() + cjkPad(fm));
    return result;
}
void *hookedFontBRectXYWH(const QFontMetrics *fm, QRect *result, int x, int y,
                          int w, int h, int flags, const QString &text,
                          int tabstops, int *tabarray) {
    HC_OBSERVE(hookedFontBRectXYWH);
    QString t = text;
    QString tr = measureText(text);
    if (!tr.isEmpty())
        t = tr;
    if (g_fmBRectXYWH)
        g_fmBRectXYWH(fm, result, x, y, w, h, flags, t, tabstops, tabarray);
    if (!tr.isEmpty())
        result->setWidth(result->width() + cjkPad(fm));
    return result;
}
void *hookedFontSize(const QFontMetrics *fm, QSize *result, int flags,
                     const QString &text, int tabstops, int *tabarray) {
    HC_OBSERVE(hookedFontSize);
    QString t = text;
    QString tr = measureText(text);
    if (!tr.isEmpty())
        t = tr;
    if (g_fmSize)
        g_fmSize(fm, result, flags, t, tabstops, tabarray);
    if (!tr.isEmpty())
        result->setWidth(result->width() + cjkPad(fm));
    return result;
}
void *hookedFontTBRect(const QFontMetrics *fm, QRect *result,
                       const QString &text) {
    HC_OBSERVE(hookedFontTBRect);
    QString t = text;
    QString tr = measureText(text);
    if (!tr.isEmpty())
        t = tr;
    if (g_fmTBRect)
        g_fmTBRect(fm, result, t);
    if (!tr.isEmpty())
        result->setWidth(result->width() + cjkPad(fm));
    return result;
}
void *hookedFontElide(const QFontMetrics *fm, QString *result,
                      const QString &text, Qt::TextElideMode mode, int width,
                      int flags) {
    HC_OBSERVE(hookedFontElide);
    QString t = text;
    QString tr = measureText(text);
    if (!tr.isEmpty())
        t = tr;
    if (g_fmElide)
        g_fmElide(fm, result, t, mode, width, flags);
    else
        *result = t;
    return result;
}

qreal hookedFontFHAdv(const QFontMetricsF *fm, const QString &text, int len) {
    HC_OBSERVE(hookedFontFHAdv);
    if (len >= 0 && len < text.size())
        return g_fmfHAdv ? g_fmfHAdv(fm, text, len) : 0;
    QString t = text;
    QString tr = measureText(text);
    if (!tr.isEmpty())
        t = tr;
    const int effectiveLength = tr.isEmpty() ? len : -1;
    if (g_fmfHAdv)
        return g_fmfHAdv(fm, t, effectiveLength) +
               (tr.isEmpty() ? 0 : cjkPadF(fm));
    return 0;
}
void *hookedFontFBRect(const QFontMetricsF *fm, QRectF *result,
                       const QString &text) {
    HC_OBSERVE(hookedFontFBRect);
    QString t = text;
    QString tr = measureText(text);
    if (!tr.isEmpty())
        t = tr;
    if (g_fmfBRect)
        g_fmfBRect(fm, result, t);
    if (!tr.isEmpty())
        result->setWidth(result->width() + cjkPadF(fm));
    return result;
}
void *hookedFontFBRectRect(const QFontMetricsF *fm, QRectF *result,
                           const QRectF &r, int flags, const QString &text,
                           int tabstops, int *tabarray) {
    HC_OBSERVE(hookedFontFBRectRect);
    QString t = text;
    QString tr = measureText(text);
    if (!tr.isEmpty())
        t = tr;
    if (g_fmfBRectRect)
        g_fmfBRectRect(fm, result, r, flags, t, tabstops, tabarray);
    if (!tr.isEmpty())
        result->setWidth(result->width() + cjkPadF(fm));
    return result;
}
void *hookedFontFSize(const QFontMetricsF *fm, QSizeF *result, int flags,
                      const QString &text, int tabstops, int *tabarray) {
    HC_OBSERVE(hookedFontFSize);
    QString t = text;
    QString tr = measureText(text);
    if (!tr.isEmpty())
        t = tr;
    if (g_fmfSize)
        g_fmfSize(fm, result, flags, t, tabstops, tabarray);
    if (!tr.isEmpty())
        result->setWidth(result->width() + cjkPadF(fm));
    return result;
}
void *hookedFontFTBRect(const QFontMetricsF *fm, QRectF *result,
                        const QString &text) {
    HC_OBSERVE(hookedFontFTBRect);
    QString t = text;
    QString tr = measureText(text);
    if (!tr.isEmpty())
        t = tr;
    if (g_fmfTBRect)
        g_fmfTBRect(fm, result, t);
    if (!tr.isEmpty())
        result->setWidth(result->width() + cjkPadF(fm));
    return result;
}
void *hookedFontFElide(const QFontMetricsF *fm, QString *result,
                       const QString &text, Qt::TextElideMode mode, qreal width,
                       int flags) {
    HC_OBSERVE(hookedFontFElide);
    QString t = text;
    QString tr = measureText(text);
    if (!tr.isEmpty())
        t = tr;
    if (g_fmfElide)
        g_fmfElide(fm, result, t, mode, width, flags);
    else
        *result = t;
    return result;
}

// QPainter::drawText overloads used by Houdini's Qt 6 build.
using DrawPoint = void (*)(QPainter *, const QPoint &, const QString &);
using DrawPointF = void (*)(QPainter *, const QPointF &, const QString &);
using DrawPointF2 = void (*)(QPainter *, const QPointF &, const QString &, int,
                             int);
using DrawRect = void (*)(QPainter *, const QRect &, int, const QString &,
                          QRect *);
using DrawRectFOption = void (*)(QPainter *, const QRectF &, const QString &,
                                 const QTextOption &);
using DrawRectFAlign = void (*)(QPainter *, const QRectF &, int,
                                const QString &, QRectF *);
using DrawXY = void (*)(QPainter *, int, int, const QString &);
using DrawXYWH = void (*)(QPainter *, int, int, int, int, int, const QString &,
                          QRect *);

DrawPoint g_drawPoint = nullptr;
DrawPointF g_drawPointF = nullptr;
DrawPointF2 g_drawPointF2 = nullptr;
DrawRect g_drawRect = nullptr;
DrawRectFOption g_drawRectFOption = nullptr;
DrawRectFAlign g_drawRectFAlign = nullptr;
DrawXY g_drawXY = nullptr;
DrawXYWH g_drawXYWH = nullptr;

// Houdini's native UI frequently calls QStyle::drawItemText directly from
// libUI.dll. Those labels and list entries never pass through the public
// QPainter::drawText imports that are hooked below. Intercept this final style
// painting boundary and substitute only the QString argument; the widget,
// model and Houdini resource remain untouched.
using StyleDrawItemText = void (*)(const QStyle *, QPainter *, const QRect &,
                                   int, const QPalette &, bool, const QString &,
                                   QPalette::ColorRole);
StyleDrawItemText g_styleDrawItemText = nullptr;
StyleDrawItemText g_styleDrawItemTextVtableOriginal = nullptr;

void hookedStyleDrawItemText(const QStyle *self, QPainter *painter,
                             const QRect &rect, int flags,
                             const QPalette &palette, bool enabled,
                             const QString &text, QPalette::ColorRole role) {
    HC_OBSERVE(hookedStyleDrawItemText);
    TextScope paintScope(textRoleForDevice(painter ? painter->device() : nullptr));
    QString translated;
    {
        // QStyle is a shared paint sink, not proof that its caller is a label.
        // Keep explicit widget/owner roles and the fallback for unknown owners.
        ScopedAllowHoudiniUiText allowUiText(TextRole::Unknown);
        translated = displayTranslation(text);
    }

    StyleDrawItemText original = g_styleDrawItemTextVtableOriginal
                                     ? g_styleDrawItemTextVtableOriginal
                                     : g_styleDrawItemText;
    if (original)
        original(self, painter, rect, flags, palette, enabled,
                 translated.isEmpty() ? text : translated, role);
}

// QTextLayout::draw is the actual text painting path used by Qt item views and
// rich text controls. Keep model text untouched and rebuild only the temporary
// layout passed to the paint operation.
using QTextLayoutDrawFn = void (*)(const QTextLayout *, QPainter *,
                                   const QPointF &,
                                   const QList<QTextLayout::FormatRange> &,
                                   const QRectF &);
QTextLayoutDrawFn g_qtextLayoutDraw = nullptr;

void hookedQTextLayoutDraw(const QTextLayout *self, QPainter *painter,
                           const QPointF &pos,
                           const QList<QTextLayout::FormatRange> &selections,
                           const QRectF &clip) {
    HC_OBSERVE(hookedQTextLayoutDraw);
    TextScope paintScope(textRoleForDevice(painter ? painter->device() : nullptr));
    if (!self || !painter || !g_qtextLayoutDraw)
        return;
    // Selection ranges use offsets in the source string. Translating a selected
    // layout could invalidate those ranges, so preserve it verbatim.
    if (!selections.isEmpty() || !HoudiniChineseTextLayout::canTranslate(self)) {
        g_qtextLayoutDraw(self, painter, pos, selections, clip);
        return;
    }

    const QString source = self->text();
    const QString translated = displayTranslation(source);
    if (translated.isEmpty()) {
        g_qtextLayoutDraw(self, painter, pos, selections, clip);
        return;
    }
    auto layout = HoudiniChineseTextLayout::makeDisplayLayout(
        self, translated, painter->device());
    g_qtextLayoutDraw(layout ? layout.get() : self, painter, pos, {}, clip);
}

void hookedDrawPoint(QPainter *p, const QPoint &pt, const QString &text) {
    HC_OBSERVE(hookedDrawPoint);
    drawDisplayText(p, text, [&](const QString &display) {
        g_drawPoint(p, pt, display);
    });
}
void hookedDrawPointF(QPainter *p, const QPointF &pt, const QString &text) {
    HC_OBSERVE(hookedDrawPointF);
    drawDisplayText(p, text, [&](const QString &display) {
        g_drawPointF(p, pt, display);
    });
}
void hookedDrawPointF2(QPainter *p, const QPointF &pt, const QString &text,
                       int flags, int just) {
    HC_OBSERVE(hookedDrawPointF2);
    drawDisplayText(p, text, [&](const QString &display) {
        g_drawPointF2(p, pt, display, flags, just);
    });
}
void hookedDrawRect(QPainter *p, const QRect &r, int flags, const QString &text,
                    QRect *br) {
    HC_OBSERVE(hookedDrawRect);
    drawDisplayText(p, text, [&](const QString &display) {
        g_drawRect(p, r, flags, display, br);
    });
}
void hookedDrawRectFOption(QPainter *p, const QRectF &r, const QString &text,
                           const QTextOption &opt) {
    HC_OBSERVE(hookedDrawRectFOption);
    drawDisplayText(p, text, [&](const QString &display) {
        g_drawRectFOption(p, r, display, opt);
    });
}
void hookedDrawRectFAlign(QPainter *p, const QRectF &r, int flags,
                          const QString &text, QRectF *br) {
    HC_OBSERVE(hookedDrawRectFAlign);
    drawDisplayText(p, text, [&](const QString &display) {
        g_drawRectFAlign(p, r, flags, display, br);
    });
}
void hookedDrawXY(QPainter *p, int x, int y, const QString &text) {
    HC_OBSERVE(hookedDrawXY);
    drawDisplayText(p, text, [&](const QString &display) {
        g_drawXY(p, x, y, display);
    });
}
void hookedDrawXYWH(QPainter *p, int x, int y, int w, int h, int flags,
                    const QString &text, QRect *br) {
    HC_OBSERVE(hookedDrawXYWH);
    drawDisplayText(p, text, [&](const QString &display) {
        g_drawXYWH(p, x, y, w, h, flags, display, br);
    });
}

// Network editor labels may paint through QGraphics text items instead of the
// public QPainter::drawText imports. Paint a temporary translated item while
// preserving the original item's data and alignment.
using GraphicsSimpleTextPaint = void (*)(QGraphicsSimpleTextItem *, QPainter *,
                                         const QStyleOptionGraphicsItem *,
                                         QWidget *);
using GraphicsTextPaint = void (*)(QGraphicsTextItem *, QPainter *,
                                   const QStyleOptionGraphicsItem *, QWidget *);

GraphicsSimpleTextPaint g_graphicsSimpleTextPaint = nullptr;
GraphicsTextPaint g_graphicsTextPaint = nullptr;

void hookedGraphicsSimpleTextPaint(QGraphicsSimpleTextItem *self,
                                   QPainter *painter,
                                   const QStyleOptionGraphicsItem *option,
                                   QWidget *widget) {
    HC_OBSERVE(hookedGraphicsSimpleTextPaint);
    TextScope textScope(TextRole::DisplayLabel);
    if (!self || !painter || !g_graphicsSimpleTextPaint) {
        if (g_graphicsSimpleTextPaint)
            g_graphicsSimpleTextPaint(self, painter, option, widget);
        return;
    }
    const QString display = displayTranslation(self->text());
    if (display.isEmpty()) {
        g_graphicsSimpleTextPaint(self, painter, option, widget);
        return;
    }
    QGraphicsSimpleTextItem translatedItem(display);
    translatedItem.setFont(self->font());
    translatedItem.setBrush(self->brush());
    translatedItem.setPen(self->pen());
    // Preserve the original text item's horizontal center.
    const qreal sourceWidth = self->boundingRect().width();
    const qreal displayWidth = translatedItem.boundingRect().width();
    painter->save();
    painter->translate((sourceWidth - displayWidth) * 0.5, 0.0);
    translatedItem.paint(painter, option, widget);
    painter->restore();
}

void hookedGraphicsTextPaint(QGraphicsTextItem *self, QPainter *painter,
                             const QStyleOptionGraphicsItem *option,
                             QWidget *widget) {
    HC_OBSERVE(hookedGraphicsTextPaint);
    TextScope textScope(self && self->textInteractionFlags().testFlag(Qt::TextEditable)
        ? TextRole::UserValue : TextRole::DisplayLabel);
    if (!self || !painter || !g_graphicsTextPaint) {
        if (g_graphicsTextPaint)
            g_graphicsTextPaint(self, painter, option, widget);
        return;
    }
    if (self->textInteractionFlags().testFlag(Qt::TextEditable)) {
        // The inner QTextLayout/QPainter paths must also leave editor content
        // untouched, including when this graphics item does not have focus.
        DisplayTranslationGuard guard;
        g_graphicsTextPaint(self, painter, option, widget);
        return;
    }
    const QString display = displayTranslation(self->toPlainText());
    if (display.isEmpty()) {
        g_graphicsTextPaint(self, painter, option, widget);
        return;
    }
    QGraphicsTextItem translatedItem(display);
    translatedItem.setFont(self->font());
    translatedItem.setDefaultTextColor(self->defaultTextColor());
    const qreal sourceWidth = self->boundingRect().width();
    const qreal displayWidth = translatedItem.boundingRect().width();
    painter->save();
    painter->translate((sourceWidth - displayWidth) * 0.5, 0.0);
    translatedItem.paint(painter, option, widget);
    painter->restore();
}

// Network editor static-text paint path.
using DrawStaticTextPF = void (*)(QPainter *, const QPointF &,
                                  const QStaticText &);
DrawStaticTextPF g_drawStaticTextPF = nullptr;

QString staticTextTranslation21(const QStaticText &st) {
    const QString source = st.text();
    // H21 viewer state badges are QStaticText("<b>Select</b> "). Only replace
    // text nodes; preserve bold tags, attributes and the source object itself.
    if (st.textFormat() != Qt::PlainText && Qt::mightBeRichText(source))
        return HoudiniChineseRichText::translateTextNodes(source, displayTranslation);
    return displayTranslation(source);
}

void hookedDrawStaticText(QPainter *p, const QPointF &pos,
                          const QStaticText &st) {
    HC_OBSERVE(hookedDrawStaticText);
    TextScope paintScope(textRoleForDevice(p ? p->device() : nullptr));
    if (!p || !g_drawStaticTextPF) {
        if (g_drawStaticTextPF)
            g_drawStaticTextPF(p, pos, st);
        return;
    }
    const QString tr = SYS_VERSION_MAJOR_INT == 21
        ? staticTextTranslation21(st) : displayTranslation(st.text());
    if (tr.isEmpty()) {
        g_drawStaticTextPF(p, pos, st);
        return;
    }
    QStaticText out(tr);
    out.setTextFormat(st.textFormat());
    out.setTextWidth(st.textWidth());
    out.setTextOption(st.textOption());
    p->save();
    g_drawStaticTextPF(p, pos, out);
    p->restore();
}

// ---------------------------------------------------------------------------
// IAT hook：替换所有已加载模块导入表里指向 drawText 的槽
// ---------------------------------------------------------------------------
size_t g_hookSlotCount = 0;

// Opt-in experiment: keep the release IAT path unless explicitly requested
// at process startup. Install before publishing any IAT replacements, so a
// failed Detours transaction cannot leave an active handler with a null original.
bool experimentalQtInlineEnabled() {
    static const bool enabled = [] {
        wchar_t value[4]{};
        return GetEnvironmentVariableW(L"HOUDINI_CHINESE_EXPERIMENT_QT_INLINE",
                                       value, 4) == 1 && value[0] == L'1';
    }();
    return enabled;
}

template <typename Function>
bool tryExperimentalQtInline(Function &original, Function replacement);

std::vector<HMODULE> loadedModules() {
    std::vector<HMODULE> modules(256);
    DWORD needed = 0;
    for (;;) {
        if (!EnumProcessModules(
                GetCurrentProcess(), modules.data(),
                static_cast<DWORD>(modules.size() * sizeof(HMODULE)), &needed))
            return {};
        if (needed <= modules.size() * sizeof(HMODULE)) {
            modules.resize(needed / sizeof(HMODULE));
            return modules;
        }
        modules.resize((needed / sizeof(HMODULE)) + 64);
    }
}

bool g_importScanSucceeded = true;
bool replaceImportInModule(HMODULE module, size_t &patchedSlotCount,
                           void *original, void *replacement) {
    const auto result = HoudiniChineseImports::patchModule(module, original, replacement);
    patchedSlotCount += result.patched;
    if (!result.complete())
        g_importScanSucceeded = false;
    g_hookRegistry.importResult(original, reinterpret_cast<std::uintptr_t>(module),
                                result.patched + result.alreadyPatched, result.error);
    return result.patched != 0;
}

// ---------------------------------------------------------------------------
// 安装 drawText 钩子（Qt6：GetModuleHandleW(L"Qt6Gui.dll")）
// ---------------------------------------------------------------------------
bool installHooks() {
    hookLog("installHooks: entered");
    if (g_hooksInstalled)
        return true;
    HMODULE qtGui = GetModuleHandleW(L"Qt6Gui.dll");
    if (!qtGui) {
        hookLog("installHooks: Qt6Gui.dll NOT loaded");
        return false;
    }
    bool installed = false;
    const std::vector<HMODULE> modules = loadedModules();
    auto hookOne = [&](const char *sym, auto &original, auto replacement) {
        using Function = std::remove_reference_t<decltype(original)>;
        // Publish the callable original before any render thread can enter a
        // patched import. Returning it after patching left a null-call window.
        if (!original)
            original = reinterpret_cast<Function>(GetProcAddress(qtGui, sym));
        if (!original) {
            hookLog("installHooks: SYMBOL NOT FOUND: %s", sym);
            return;
        }
        if (tryExperimentalQtInline(original, replacement)) {
            installed = true;
            return;
        }
        void *orig = reinterpret_cast<void *>(original);
        void *hookFn = reinterpret_cast<void *>(replacement);
        bool ok = false;
        for (HMODULE module : modules) {
            if (module != g_moduleHandle &&
                replaceImportInModule(module, g_hookSlotCount, orig, hookFn))
                ok = true;
        }
        installed = installed || ok;
        hookLog("installHooks: symbol=%s orig=%p slots=%zu ok=%d", sym, orig,
                g_hookSlotCount, ok ? 1 : 0);
    };
    hookOne("?drawText@QPainter@@QEAAXAEBVQPoint@@AEBVQString@@@Z",
            g_drawPoint, &hookedDrawPoint);
    hookOne("?drawText@QPainter@@QEAAXAEBVQPointF@@AEBVQString@@@Z",
            g_drawPointF, &hookedDrawPointF);
    hookOne("?drawText@QPainter@@QEAAXAEBVQPointF@@AEBVQString@@HH@Z",
            g_drawPointF2, &hookedDrawPointF2);
    hookOne("?drawText@QPainter@@QEAAXAEBVQRect@@HAEBVQString@@PEAV2@@Z",
            g_drawRect, &hookedDrawRect);
    hookOne(
        "?drawText@QPainter@@QEAAXAEBVQRectF@@AEBVQString@@AEBVQTextOption@@@Z",
        g_drawRectFOption, &hookedDrawRectFOption);
    hookOne("?drawText@QPainter@@QEAAXAEBVQRectF@@HAEBVQString@@PEAV2@@Z",
            g_drawRectFAlign, &hookedDrawRectFAlign);
    hookOne("?drawText@QPainter@@QEAAXHHAEBVQString@@@Z",
            g_drawXY, &hookedDrawXY);
    hookOne("?drawText@QPainter@@QEAAXHHHHHAEBVQString@@PEAVQRect@@@Z",
            g_drawXYWH, &hookedDrawXYWH);
    if (!installed)
        return false;
    g_hooksInstalled = true;
    hookLog("HOOKS INSTALLED: %zu slots patched", g_hookSlotCount);
    return true;
}

// QTextLayout::draw is resolved once. Late importers are handled by the shared
// bounded module scanner below.
bool installQTextLayoutDrawHook() {
    if (g_qtextLayoutDraw)
        return true;
    HMODULE qtGui = GetModuleHandleW(L"Qt6Gui.dll");
    if (!qtGui)
        return false;
    g_qtextLayoutDraw = reinterpret_cast<QTextLayoutDrawFn>(GetProcAddress(
        qtGui, "?draw@QTextLayout@@QEBAXPEAVQPainter@@AEBVQPointF@@AEBV?$QList@"
               "UFormatRange@QTextLayout@@@@AEBVQRectF@@@Z"));
    if (!g_qtextLayoutDraw)
        return false;
    if (tryExperimentalQtInline(g_qtextLayoutDraw, &hookedQTextLayoutDraw))
        return true;
    for (HMODULE mod : loadedModules()) {
        if (mod == g_moduleHandle)
            continue;
        replaceImportInModule(mod, g_hookSlotCount,
                              reinterpret_cast<void *>(g_qtextLayoutDraw),
                              reinterpret_cast<void *>(&hookedQTextLayoutDraw));
    }
    return true;
}

bool installStyleItemTextHook() {
    HMODULE qtWidgets = GetModuleHandleW(L"Qt6Widgets.dll");
    if (!qtWidgets)
        return false;
    if (!g_styleDrawItemText) {
        void *orig = reinterpret_cast<void *>(GetProcAddress(
            qtWidgets, "?drawItemText@QStyle@@UEBAXPEAVQPainter@@AEBVQRect@@"
                       "HAEBVQPalette@@_NAEBVQString@@W4ColorRole@4@@Z"));
        if (!orig) {
            hookLog("StyleItemTextHook: symbol not found");
            return false;
        }
        g_styleDrawItemText = reinterpret_cast<StyleDrawItemText>(orig);
    }

    // drawItemText is virtual. Determine its ABI slot from a standard
    // QCommonStyle, then patch only that slot in Houdini's existing style.
    // This retains UI_QtPlutoStyle and avoids QApplication::setStyle(), which
    // rebuilds Houdini's private style ownership and can terminate the process.
    if (!QApplication::instance() || !QApplication::style())
        return false;

    QStyle *style = QApplication::style();
    auto **vtable = *reinterpret_cast<void ***>(style);
    static void **lastPatchedTable = nullptr;
    static size_t lastPatchedSlot = 0;
    if (vtable && vtable == lastPatchedTable &&
        vtable[lastPatchedSlot] == reinterpret_cast<void *>(&hookedStyleDrawItemText))
        return true;
    QCommonStyle probeStyle;
    auto **probeVtable = *reinterpret_cast<void ***>(&probeStyle);
    if (!vtable || !probeVtable)
        return false;

    MEMORY_BASIC_INFORMATION tableMbi{};
    if (VirtualQuery(probeVtable, &tableMbi, sizeof(tableMbi)) !=
        sizeof(tableMbi))
        return false;
    const auto regionEnd = reinterpret_cast<uintptr_t>(tableMbi.BaseAddress) +
                           static_cast<uintptr_t>(tableMbi.RegionSize);
    const auto tableStart = reinterpret_cast<uintptr_t>(probeVtable);
    const size_t available =
        regionEnd > tableStart
            ? static_cast<size_t>((regionEnd - tableStart) / sizeof(void *))
            : 0;
    const size_t limit = (std::min)(available, size_t{256});
    size_t drawItemTextSlot = limit;
    for (size_t i = 0; i < limit; ++i) {
        if (!HoudiniChineseDispatch::executable(probeVtable[i]))
            break;
        if (probeVtable[i] == reinterpret_cast<void *>(g_styleDrawItemText)) {
            drawItemTextSlot = i;
            break;
        }
    }
    if (drawItemTextSlot == limit) {
        hookLog("StyleItemTextHook: probe slot not found");
        return false;
    }

    void **slot = &vtable[drawItemTextSlot];
    if (*slot == reinterpret_cast<void *>(&hookedStyleDrawItemText))
        return true;

    MEMORY_BASIC_INFORMATION codeMbi{};
    const bool executable =
        *slot &&
        VirtualQuery(*slot, &codeMbi, sizeof(codeMbi)) == sizeof(codeMbi) &&
        (codeMbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
                            PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
    if (!executable) {
        hookLog("StyleItemTextHook: target is not executable class=%s slot=%zu",
                style->metaObject() ? style->metaObject()->className() : "?",
                drawItemTextSlot);
        return false;
    }

    void *expected = *slot;
    // Previously patched style instances may still be alive. Never redirect
    // their hook to a different style class's implementation. Unknown styles
    // retain the common QPainter/import translation paths.
    if (g_styleDrawItemTextVtableOriginal &&
        expected != reinterpret_cast<void *>(g_styleDrawItemTextVtableOriginal))
        return false;
    DWORD oldProtection = 0;
    if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &oldProtection))
        return false;
    if (!g_styleDrawItemTextVtableOriginal)
        g_styleDrawItemTextVtableOriginal = reinterpret_cast<StyleDrawItemText>(expected);
    void *observed = InterlockedCompareExchangePointer(
        reinterpret_cast<PVOID volatile *>(slot),
        reinterpret_cast<void *>(&hookedStyleDrawItemText), expected);
    DWORD ignored = 0;
    if (!VirtualProtect(slot, sizeof(void *), oldProtection, &ignored)) {
        auto &record = g_hookRegistry.record(HoudiniChineseHooks::HookId::hookedStyleDrawItemText);
        const DWORD error = GetLastError();
        record.lastError = error ? error : ERROR_GEN_FAILURE;
        record.errorStage = "vtable_protection_restore";
    }
    if (observed != expected)
        return observed == reinterpret_cast<void *>(&hookedStyleDrawItemText);
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void *));
    ++g_hookSlotCount;
    g_hookRegistry.record(HoudiniChineseHooks::HookId::hookedStyleDrawItemText).virtualPatched = true;
    lastPatchedTable = vtable;
    lastPatchedSlot = drawItemTextSlot;
    hookLog("StyleItemTextHook: vtable patched class=%s slot=%zu original=%p",
            style->metaObject() ? style->metaObject()->className() : "?",
            drawItemTextSlot,
            reinterpret_cast<void *>(g_styleDrawItemTextVtableOriginal));
    return true;
}

bool isExecutableAddress(const void *address) {
    return HoudiniChineseDispatch::executable(address);
}

std::atomic_flag g_detourTransactionLock = ATOMIC_FLAG_INIT;
HoudiniChineseHooks::RetryPolicy g_inlineHookRetries;

class DetourTransactionGuard {
  public:
    DetourTransactionGuard() {
        while (g_detourTransactionLock.test_and_set(std::memory_order_acquire))
            SwitchToThread();
    }
    ~DetourTransactionGuard() {
        g_detourTransactionLock.clear(std::memory_order_release);
    }
};

// DetourAttach/DetourUpdateThread allocate internally and may throw. Always
// abort an unfinished transaction before releasing thread handles or the lock.
class PendingDetourTransaction final {
  public:
    LONG begin() {
        const LONG error = DetourTransactionBegin();
        active_ = error == NO_ERROR;
        return error;
    }
    LONG commit() {
        const LONG error = DetourTransactionCommit();
        active_ = false; // Commit also aborts the transaction on reported failure.
        return error;
    }
    ~PendingDetourTransaction() { if (active_) DetourTransactionAbort(); }
    PendingDetourTransaction() = default;
    PendingDetourTransaction(const PendingDetourTransaction &) = delete;
    PendingDetourTransaction &operator=(const PendingDetourTransaction &) = delete;
  private:
    bool active_ = false;
};

template <typename Function>
bool attachInlineHook(Function &original, Function replacement, bool lateAttempt = false) {
    const void *target = reinterpret_cast<void *>(original);
    struct OriginalGuard {
        Function &value;
        bool installed = false;
        ~OriginalGuard() { if (!installed) value = nullptr; }
    } originalGuard{original};
    if (!isExecutableAddress(reinterpret_cast<void *>(original))) {
        g_hookRegistry.inlineResult(target, false, ERROR_INVALID_ADDRESS);
        original = nullptr;
        return false;
    }
    DetourTransactionGuard transactionGuard;
    const bool permitted = lateAttempt
        ? g_inlineHookRetries.beginLate(target, GetTickCount64())
        : g_inlineHookRetries.begin(target, GetTickCount64());
    if (!permitted) {
        if (g_inlineHookRetries.attempts(target) >= HoudiniChineseHooks::RetryPolicy::kMaxAttempts)
            g_hookRegistry.retryLimited(target);
        original = nullptr;
        return false;
    }

    g_hookRegistry.beginInline(target);
    DWORD installError = ERROR_SUCCESS;
    const char *installStage = "thread_snapshot";
    std::vector<HANDLE> threads;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    struct ThreadHandles {
        std::vector<HANDLE> &threads;
        HANDLE &snapshot;
        ~ThreadHandles() {
            for (HANDLE thread : threads) CloseHandle(thread);
            if (snapshot != INVALID_HANDLE_VALUE) CloseHandle(snapshot);
        }
    } handles{threads, snapshot};
    bool ready = snapshot != INVALID_HANDLE_VALUE;
    if (!ready) installError = GetLastError();
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    if (ready && Thread32First(snapshot, &entry)) {
        do {
            if (entry.th32OwnerProcessID != GetCurrentProcessId() ||
                entry.th32ThreadID == GetCurrentThreadId())
                continue;
            HANDLE thread =
                OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                               THREAD_SET_CONTEXT | THREAD_QUERY_INFORMATION,
                           FALSE, entry.th32ThreadID);
            if (thread) {
                try { threads.push_back(thread); }
                catch (...) { CloseHandle(thread); throw; }
            } else if (GetLastError() != ERROR_INVALID_PARAMETER) {
                // A vanished thread can be skipped. A live/inaccessible thread
                // cannot: its instruction pointer would escape Detours' fixup.
                installError = GetLastError();
                installStage = "thread_open";
                ready = false;
            }
        } while (Thread32Next(snapshot, &entry));
        const DWORD enumerationError = GetLastError();
        if (enumerationError != ERROR_NO_MORE_FILES) {
            installError = enumerationError;
            installStage = "thread_enumeration";
            ready = false;
        }
    } else {
        installError = GetLastError();
        installStage = "thread_enumeration";
        ready = false;
    }
    if (snapshot != INVALID_HANDLE_VALUE) {
        CloseHandle(snapshot);
        snapshot = INVALID_HANDLE_VALUE;
    }

    bool installed = false;
    if (ready) {
        installStage = "transaction_begin";
        PendingDetourTransaction pending;
        installError = pending.begin();
        if (installError == NO_ERROR) {
            installStage = "thread_suspend";
            installError = DetourUpdateThread(GetCurrentThread());
            ready = installError == NO_ERROR;
            for (HANDLE thread : threads) {
                const LONG error = DetourUpdateThread(thread);
                if (error != NO_ERROR) {
                    DWORD exitCode = STILL_ACTIVE;
                    if (!GetExitCodeThread(thread, &exitCode) || exitCode == STILL_ACTIVE) {
                        ready = false;
                        installError = error;
                    }
                }
            }
            if (ready) {
                installStage = "detour_attach";
                installError = DetourAttach(reinterpret_cast<PVOID *>(&original),
                                            reinterpret_cast<PVOID>(replacement));
                DETOUR_ATTACH_DIAGNOSTIC detail{};
                DetourGetAttachDiagnostic(&detail);
                g_hookRegistry.forTarget(target, [&detail](auto &entry) {
                    entry.detourStage = detail.stage ? detail.stage : "";
                    entry.detourAllocationError = detail.allocationError;
                    entry.detourQueryError = detail.queryError;
                    entry.detourAllocationAttempts = detail.allocationAttempts;
                    entry.detourQueryCount = detail.queryCount;
                });
                if (installError != NO_ERROR && detail.stage)
                    installStage = detail.stage;
                ready = installError == NO_ERROR;
            }
            if (ready) {
                installStage = "transaction_commit";
                installError = pending.commit();
                installed = installError == NO_ERROR;
            }
        }
    }
    originalGuard.installed = installed;
    g_hookRegistry.inlineResult(target, installed,
        installed ? ERROR_SUCCESS : (installError ? installError : ERROR_GEN_FAILURE),
        installStage);
    if (!installed)
        original = nullptr;
    return installed;
}

template <typename Function>
bool resolveAndAttachInlineHook(HMODULE module, const char *symbol,
                                Function &original, Function replacement) {
    if (original)
        return true;
    if (!module || !symbol)
        return false;
    original = reinterpret_cast<Function>(GetProcAddress(module, symbol));
    return attachInlineHook(original, replacement);
}

template <typename Function>
bool tryExperimentalQtInline(Function &original, Function replacement) {
    if (!experimentalQtInlineEnabled() || !original)
        return false;
    const Function target = original;
    const bool installed = attachInlineHook(original, replacement);
    if (!installed)
        original = target; // Existing import fallback must remain callable.
    hookLog("QtInlineExperiment: target=%p installed=%d",
            reinterpret_cast<void *>(target), installed ? 1 : 0);
    return installed;
}

struct NativeDispatchImport final {
    void *target;
    void *replacement;
};
// Written and scanned only on the GUI thread, including deferred installation.
std::vector<NativeDispatchImport> g_nativeDispatchImports;

template <typename Function>
bool resolveAndAttachImportedHook(HMODULE module, const char *symbol,
                                  Function &original, Function replacement) {
    if (original)
        return true;
    if (!module || !symbol)
        return false;
    if (resolveAndAttachInlineHook(module, symbol, original, replacement))
        return true;

    // Keep the original RV import-hook coverage when a low-address Houdini DLL
    // has no room for a Detours trampoline. Store the raw export, never a
    // trampoline, so the deferred scan can also patch DLLs loaded afterwards.
    const auto target = reinterpret_cast<Function>(GetProcAddress(module, symbol));
    if (!isExecutableAddress(reinterpret_cast<void *>(target)) ||
        !isExecutableAddress(reinterpret_cast<void *>(replacement)))
        return false;
    const NativeDispatchImport entry{reinterpret_cast<void *>(target),
                                     reinterpret_cast<void *>(replacement)};
    g_nativeDispatchImports.push_back(entry);
    g_hookRegistry.registerImport(target);
    original = target; // Publish before any importer can call the replacement.
    for (HMODULE importer : loadedModules()) {
        if (importer != g_moduleHandle)
            replaceImportInModule(importer, g_hookSlotCount, entry.target,
                                  entry.replacement);
    }
    // Registration also covers exports whose importing DLL is not loaded yet.
    hookLog("NativeImportFallback: registered %s", symbol);
    return true;
}

template <typename Function>
bool resolveAndAttachNativeVirtualHook(HMODULE module, const char *symbol,
                                       const char *vtableSymbol,
                                       Function &original, Function replacement) {
    if (original)
        return true;
    if (!module)
        return false;
    if (resolveAndAttachInlineHook(module, symbol, original, replacement))
        return true;

    // Detours can run out of address space within +/-2 GB of Houdini's low
    // DLL addresses even when the machine has ample RAM. Keep the baseline
    // virtual and imported entry points usable when no trampoline can be made.
    const auto target = reinterpret_cast<Function>(GetProcAddress(module, symbol));
    auto **table = reinterpret_cast<void **>(GetProcAddress(module, vtableSymbol));
    // Allocate bookkeeping before publishing the vtable replacement.
    // An allocation failure must not leave imports permanently unregistered.
    g_nativeDispatchImports.reserve(g_nativeDispatchImports.size() + 1);
    DWORD restoreError = ERROR_SUCCESS;
    const bool virtualInstalled = HoudiniChineseDispatch::patchVtable(
        table, target, replacement, original, g_hookSlotCount, &restoreError);
    g_hookRegistry.virtualResult(target, virtualInstalled);
    if (restoreError) {
        g_hookRegistry.forTarget(reinterpret_cast<void *>(target), [restoreError](auto &record) {
            record.lastError = restoreError;
            record.errorStage = "vtable_protection_restore";
        });
    }
    if (!virtualInstalled)
        return false;
    const NativeDispatchImport entry{reinterpret_cast<void *>(target),
                                     reinterpret_cast<void *>(replacement)};
    g_nativeDispatchImports.push_back(entry);
    g_hookRegistry.registerImport(target);
    for (HMODULE importer : loadedModules()) {
        if (importer != g_moduleHandle)
            replaceImportInModule(importer, g_hookSlotCount, entry.target,
                                  entry.replacement);
    }
    hookLog("NativeVirtualFallback: installed %s", symbol);
    return true;
}

bool findNativeVtableSlot(HMODULE module, const char *vtableSymbol,
                          const char *methodSymbol, std::size_t &slot) {
    slot = kInvalidVtableSlot;
    if (!module)
        return false;
    void **vtable =
        reinterpret_cast<void **>(GetProcAddress(module, vtableSymbol));
    void *method =
        reinterpret_cast<void *>(GetProcAddress(module, methodSymbol));
    if (!vtable || !method)
        return false;

    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(vtable, &info, sizeof(info)) != sizeof(info) ||
        info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {
        return false;
    }
    const auto regionEnd = reinterpret_cast<std::uintptr_t>(info.BaseAddress) +
                           static_cast<std::uintptr_t>(info.RegionSize);
    const auto tableStart = reinterpret_cast<std::uintptr_t>(vtable);
    const std::size_t available =
        regionEnd > tableStart ? static_cast<std::size_t>(
                                     (regionEnd - tableStart) / sizeof(void *))
                               : 0;
    const std::size_t limit = (std::min)(available, std::size_t{256});
    for (std::size_t index = 0; index < limit; ++index) {
        if (!isExecutableAddress(vtable[index]))
            break;
        if (vtable[index] == method) {
            slot = index;
            return true;
        }
    }
    return false;
}

int nativeTraversalExceptionFilter(DWORD code) noexcept {
    // These probes touch only private objects whose lifetime is controlled by
    // Houdini. Recover from a stale/unreadable pointer, but never swallow C++
    // exceptions or unrelated faults raised by code inside a control getter.
    return code == EXCEPTION_ACCESS_VIOLATION ||
                   code == EXCEPTION_IN_PAGE_ERROR ||
                   code == EXCEPTION_DATATYPE_MISALIGNMENT
               ? EXCEPTION_EXECUTE_HANDLER
               : EXCEPTION_CONTINUE_SEARCH;
}

bool guardedNativeHasBase(void *object, const char *decoratedTypeName) {
    if (!object || !decoratedTypeName)
        return false;
    __try {
        return HoudiniChineseNativeRtti::hasBase(object, decoratedTypeName);
    } __except (nativeTraversalExceptionFilter(GetExceptionCode())) {
        return false;
    }
}

bool guardedNativeIsExactType(void *object, const char *decoratedTypeName) {
    if (!object || !decoratedTypeName)
        return false;
    __try {
        return HoudiniChineseNativeRtti::typeNameEquals(
            HoudiniChineseNativeRtti::dynamicTypeName(object),
            decoratedTypeName);
    } __except (nativeTraversalExceptionFilter(GetExceptionCode())) {
        return false;
    }
}

void *guardedNativePointerCall(NativeFeelPointerFn function, void *self) {
    if (!function || !self)
        return nullptr;
    __try {
        return function(self);
    } __except (nativeTraversalExceptionFilter(GetExceptionCode())) {
        return nullptr;
    }
}

const char *guardedNativeCStringCall(NativeCStringFn function, void *self) {
    if (!function || !self)
        return nullptr;
    __try {
        return function(self);
    } __except (nativeTraversalExceptionFilter(GetExceptionCode())) {
        return nullptr;
    }
}

std::size_t guardedNativeCStringLength(const char *text,
                                       std::size_t limit = 65536) {
    if (!text)
        return 0;
    __try {
        std::size_t length = 0;
        while (length < limit && text[length])
            ++length;
        return length < limit ? length : 0;
    } __except (nativeTraversalExceptionFilter(GetExceptionCode())) {
        return 0;
    }
}

void *guardedUIFeelParent(void *feel) {
    if (!g_uiFeelParent || !feel)
        return nullptr;
    __try {
        return g_uiFeelParent(feel);
    } __except (nativeTraversalExceptionFilter(GetExceptionCode())) {
        return nullptr;
    }
}

int guardedUIFeelChildCount(void *feel) {
    if (!g_uiFeelChildCount || !feel)
        return 0;
    __try {
        const int count = g_uiFeelChildCount(feel);
        return count >= 0 && count <= 1000000 ? count : 0;
    } __except (nativeTraversalExceptionFilter(GetExceptionCode())) {
        return 0;
    }
}

void *guardedUIFeelChild(void *feel, int index) {
    if (!g_uiFeelChild || !feel || index < 0)
        return nullptr;
    __try {
        return g_uiFeelChild(feel, index);
    } __except (nativeTraversalExceptionFilter(GetExceptionCode())) {
        return nullptr;
    }
}

const char *guardedVirtualCString(void *object, std::size_t slot) {
    if (!object || slot == kInvalidVtableSlot)
        return nullptr;
    __try {
        void **vtable = *reinterpret_cast<void ***>(object);
        if (!vtable)
            return nullptr;
        auto function = reinterpret_cast<NativeCStringFn>(vtable[slot]);
        if (!isExecutableAddress(reinterpret_cast<void *>(function)))
            return nullptr;
        return function(object);
    } __except (nativeTraversalExceptionFilter(GetExceptionCode())) {
        return nullptr;
    }
}

const char *guardedUIFeelClassName(void *feel) {
    return guardedVirtualCString(feel, g_uiFeelClassNameSlot);
}

bool guardedIsStringChooserQuery(void *query) {
    if (!query || !g_uiStringChooserQueryVtable)
        return false;
    __try {
        return *reinterpret_cast<void ***>(query) ==
               g_uiStringChooserQueryVtable;
    } __except (nativeTraversalExceptionFilter(GetExceptionCode())) {
        return false;
    }
}

int guardedStringChooserEntryCount(void *query) {
    if (!query || !g_uiStringChooserEntryCount)
        return 0;
    __try {
        const int count = g_uiStringChooserEntryCount(query);
        return count >= 0 && count <= 1000000 ? count : 0;
    } __except (nativeTraversalExceptionFilter(GetExceptionCode())) {
        return 0;
    }
}

const UT_StringHolder *guardedStringChooserEntry(void *query, int index) {
    if (!query || index < 0 || !g_uiStringChooserEntry)
        return nullptr;
    __try {
        return g_uiStringChooserEntry(query, index);
    } __except (nativeTraversalExceptionFilter(GetExceptionCode())) {
        return nullptr;
    }
}

const char *guardedStringChooserEntryText(void *query, int index) {
    __try {
        const UT_StringHolder *entry = guardedStringChooserEntry(query, index);
        return entry ? entry->c_str() : nullptr;
    } __except (nativeTraversalExceptionFilter(GetExceptionCode())) {
        return nullptr;
    }
}

void *guardedSITreeParent(void *node) {
    if (!node || !g_siTreeNodeFunctions.getParent)
        return nullptr;
    __try {
        return g_siTreeNodeFunctions.getParent(node);
    } __except (nativeTraversalExceptionFilter(GetExceptionCode())) {
        return nullptr;
    }
}

int guardedSITreeChildCount(void *node) {
    if (!node || !g_siTreeNodeFunctions.getChildCount)
        return 0;
    __try {
        const int count = g_siTreeNodeFunctions.getChildCount(node);
        return count >= 0 && count <= 1000000 ? count : 0;
    } __except (nativeTraversalExceptionFilter(GetExceptionCode())) {
        return 0;
    }
}

void *guardedSITreeChild(void *node, int index) {
    if (!node || index < 0 || !g_siTreeNodeFunctions.getChild)
        return nullptr;
    __try {
        return g_siTreeNodeFunctions.getChild(node, index);
    } __except (nativeTraversalExceptionFilter(GetExceptionCode())) {
        return nullptr;
    }
}

const char *guardedSITreeText(void *node) {
    if (!node || !g_siTreeNodeFunctions.getText)
        return nullptr;
    __try {
        return g_siTreeNodeFunctions.getText(node);
    } __except (nativeTraversalExceptionFilter(GetExceptionCode())) {
        return nullptr;
    }
}

int guardedUIItemListEntryCount(void *list) {
    if (!list || g_uiItemListEntryCountSlot == kInvalidVtableSlot)
        return 0;
    __try {
        void **vtable = *reinterpret_cast<void ***>(list);
        if (!vtable)
            return 0;
        using Function = int (*)(void *);
        auto function =
            reinterpret_cast<Function>(vtable[g_uiItemListEntryCountSlot]);
        if (!isExecutableAddress(reinterpret_cast<void *>(function)))
            return 0;
        const int count = function(list);
        return count >= 0 && count <= 1000000 ? count : 0;
    } __except (nativeTraversalExceptionFilter(GetExceptionCode())) {
        return 0;
    }
}

const char *guardedUIItemListEntry(void *list, int index) {
    if (!list || index < 0 || g_uiItemListEntrySlot == kInvalidVtableSlot) {
        return nullptr;
    }
    __try {
        void **vtable = *reinterpret_cast<void ***>(list);
        if (!vtable)
            return nullptr;
        using Function = const char *(*)(void *, int);
        auto function =
            reinterpret_cast<Function>(vtable[g_uiItemListEntrySlot]);
        if (!isExecutableAddress(reinterpret_cast<void *>(function)))
            return nullptr;
        return function(list, index);
    } __except (nativeTraversalExceptionFilter(GetExceptionCode())) {
        return nullptr;
    }
}

void *guardedUIItemListSubmenu(void *list, int index) {
    if (!list || index < 0 || g_uiItemListSubmenuSlot == kInvalidVtableSlot) {
        return nullptr;
    }
    __try {
        void **vtable = *reinterpret_cast<void ***>(list);
        if (!vtable)
            return nullptr;
        using Function = void *(*)(void *, int);
        auto function =
            reinterpret_cast<Function>(vtable[g_uiItemListSubmenuSlot]);
        if (!isExecutableAddress(reinterpret_cast<void *>(function)))
            return nullptr;
        return function(list, index);
    } __except (nativeTraversalExceptionFilter(GetExceptionCode())) {
        return nullptr;
    }
}

struct NativeLookTextCacheEntry final {
    void **vtable = nullptr;
    TextRole role = TextRole::Unknown;
};

thread_local std::array<NativeLookTextCacheEntry, 128> g_nativeLookTextCache{};
thread_local std::size_t g_nativeLookTextCacheSize = 0;

void **guardedNativeVtable(void *object) {
    if (!object)
        return nullptr;
    __try {
        return *reinterpret_cast<void ***>(object);
    } __except (nativeTraversalExceptionFilter(GetExceptionCode())) {
        return nullptr;
    }
}

// Reuse only the exact object's decision within its active draw. Child
// objects and draws outside UI_Look still run the existing classification.
struct NativeLookDecision {
    const void *object = nullptr;
    TextRole role = TextRole::Unknown;
};
thread_local NativeLookDecision g_activeNativeLookDecision;
class ScopedNativeLookDecision final {
  public:
    ScopedNativeLookDecision(const void *object, TextRole role)
        : previous_(g_activeNativeLookDecision) {
        g_activeNativeLookDecision = {object, role};
    }
    ~ScopedNativeLookDecision() { g_activeNativeLookDecision = previous_; }
    ScopedNativeLookDecision(const ScopedNativeLookDecision &) = delete;
    ScopedNativeLookDecision &operator=(const ScopedNativeLookDecision &) = delete;
  private:
    NativeLookDecision previous_;
};

TextRole nativeLookTextRole(void *look) {
    if (look && g_activeNativeLookDecision.object == look)
        return g_activeNativeLookDecision.role;
    void **vtable = guardedNativeVtable(look);
    if (!vtable)
        return TextRole::Unknown;
    for (std::size_t index = 0; index < g_nativeLookTextCacheSize; ++index) {
        if (g_nativeLookTextCache[index].vtable == vtable)
            return g_nativeLookTextCache[index].role;
    }

    // UI_InputFieldLook is not derived from UI_Text. Only derived UI_Text
    // looks represent labels/buttons/menu text; exclude exact UI_Text because
    // that base look is also used by editor-like controls and already has a
    // dedicated paint hook with device-owner protection.
    const bool derivedUiText = guardedNativeHasBase(look, ".?AVUI_Text@@") &&
                               !guardedNativeIsExactType(look, ".?AVUI_Text@@");
    const bool display = derivedUiText ||
                       guardedNativeHasBase(look, ".?AVUI_FormattedText@@") ||
                       guardedNativeHasBase(look, ".?AVUI_HelpTipLook@@");

    const std::size_t slot =
        g_nativeLookTextCacheSize < g_nativeLookTextCache.size()
            ? g_nativeLookTextCacheSize++
            : (reinterpret_cast<std::uintptr_t>(vtable) >> 4) %
                  g_nativeLookTextCache.size();
    // UI_InputFieldLook has its own paint/render exports and RTTI. Protect
    // it and subclasses regardless of focus, without protecting sibling looks.
    const TextRole role = guardedNativeHasBase(look, ".?AVUI_InputFieldLook@@")
        ? TextRole::UserValue : display ? TextRole::DisplayLabel : TextRole::Unknown;
    g_nativeLookTextCache[slot] = {vtable, role};
    return role;
}

bool shouldAllowNativeLookText(void *look) {
    return nativeLookTextRole(look) == TextRole::DisplayLabel;
}

void hookedUILookDraw(void *look, void *render, int x, int y, int width,
                      int height, void *data) {
    HC_OBSERVE(hookedUILookDraw);
    recordVisibleNativePointer(look, g_collectVisibleUIFeels,
                               g_visibleUILookCount, g_visibleUILooks);
    if (!g_uiLookDraw)
        return;
    const TextRole role = nativeLookTextRole(look);
    ScopedNativeLookDecision decision(look, role);
    TextScope owner(role, role == TextRole::DisplayLabel);
    g_uiLookDraw(look, render, x, y, width, height, data);
}

void hookedUIFeelRenderMe(void *feel, void *render, int x, int y) {
    HC_OBSERVE(hookedUIFeelRenderMe);
    recordVisibleNativePointer(feel, g_collectVisibleUIFeels,
                               g_visibleUIFeelCount, g_visibleUIFeels);
    // H21 input controls can bypass UI_InputFieldLook and draw UI_Text directly.
    // Protect their owner boundary, including unfocused node-name fields.
    const bool input21 = SYS_VERSION_MAJOR_INT == 21 &&
        (guardedNativeHasBase(feel, ".?AVUI_InputField@@") ||
         guardedNativeHasBase(feel, ".?AVUI_Textport@@") ||
         guardedNativeHasBase(feel, ".?AVUI_TextDisplay@@"));
    TextScope owner(input21 ? TextRole::UserValue : TextRole::Unknown);
    g_uiFeelRenderMe(feel, render, x, y);
}

bool installNativeLookDrawHook(bool lateAttempt = false) {
    if (g_uiLookDraw)
        return true;
    HMODULE ui = GetModuleHandleW(L"libUI.dll");
    if (!ui)
        return false;

    findNativeVtableSlot(ui, "??_7UI_FormattedText@@6B@",
                         "?getText@UI_FormattedText@@UEBAPEBDXZ",
                         g_uiFormattedTextGetTextSlot);
    g_uiHelpTipGetText = reinterpret_cast<NativeCStringFn>(
        GetProcAddress(ui, "?getText@UI_HelpTipLook@@QEBAPEBDXZ"));
    g_uiHelpTipGetTitle = reinterpret_cast<NativeCStringFn>(
        GetProcAddress(ui, "?getTitle@UI_HelpTipLook@@QEBAPEBDXZ"));
    g_uiCollapseButtonGetLabel = reinterpret_cast<NativeCStringFn>(
        GetProcAddress(ui, "?getLabel@UI_CollapseButtonLook@@QEBAPEBDXZ"));
    g_uiLookDraw = reinterpret_cast<UILookDrawFn>(
        GetProcAddress(ui, "?draw@UI_Look@@QEAAXPEAVRE_Render@@HHHHPEAX@Z"));
    if (!g_uiLookDraw)
        return false;
    const bool installed = attachInlineHook(g_uiLookDraw, &hookedUILookDraw, lateAttempt);
    hookLog("NativeLookDrawHook: installed=%d", installed ? 1 : 0);
    return installed;
}

bool installNativeControlTraversalHook() {
    if (g_uiFeelRenderMe)
        return static_cast<bool>(g_uiFeelApi);

    HMODULE ui = GetModuleHandleW(L"libUI.dll");
    if (!ui)
        return false;

    g_uiFeelParent = reinterpret_cast<UIFeelParentFn>(
        GetProcAddress(ui, "?getParent@UI_Feel@@QEBAPEAV1@XZ"));
    g_uiFeelChildCount = reinterpret_cast<UIFeelChildCountFn>(
        GetProcAddress(ui, "?getNumChildren@UI_Feel@@QEBAHXZ"));
    g_uiFeelChild = reinterpret_cast<UIFeelChildFn>(
        GetProcAddress(ui, "?getChild@UI_Feel@@QEBAPEAV1@H@Z"));
    const bool hasClassNameSlot = findNativeVtableSlot(
        ui, "??_7UI_Feel@@6BUI_Object@@@", "?className@UI_Feel@@UEBAPEBDXZ",
        g_uiFeelClassNameSlot);
    if (!g_uiFeelParent || !g_uiFeelChildCount || !g_uiFeelChild ||
        !hasClassNameSlot) {
        return false;
    }

    g_uiFeelApi = {&guardedUIFeelParent, &guardedUIFeelChildCount,
                   &guardedUIFeelChild, &guardedUIFeelClassName};

    const bool hasEntryCountSlot = findNativeVtableSlot(
        ui, "??_7UI_StringList@@6B@", "?getNumEntries@UI_StringList@@UEBAHXZ",
        g_uiItemListEntryCountSlot);
    const bool hasEntrySlot = findNativeVtableSlot(
        ui, "??_7UI_StringList@@6B@", "?getEntry@UI_StringList@@UEBAPEBDH@Z",
        g_uiItemListEntrySlot);
    const bool hasSubmenuSlot = findNativeVtableSlot(
        ui, "??_7UI_NestedStringMenu@@6B@",
        "?getMenu@UI_NestedStringMenu@@UEAAPEAVUI_ItemList@@H@Z",
        g_uiItemListSubmenuSlot);
    if (hasEntryCountSlot && hasEntrySlot && hasSubmenuSlot) {
        g_uiItemListApi = {&guardedUIItemListEntryCount,
                           &guardedUIItemListEntry, &guardedUIItemListSubmenu};
    }

    findNativeVtableSlot(ui, "??_7UI_Text@@6B@", "?getText@UI_Text@@UEBAPEBDXZ",
                         g_uiTextGetTextSlot);
    g_uiMenuItemGetLabel = reinterpret_cast<NativeCStringFn>(
        GetProcAddress(ui, "?getLabel@UI_ParentMenuItem@@QEBAPEBDXZ"));
    g_uiLabeledFeelGetLabel = reinterpret_cast<NativeCStringFn>(
        GetProcAddress(ui, "?getLabel@UI_LabeledFeel@@QEBAPEBDXZ"));
    g_uiGroupFeelGetName = reinterpret_cast<NativeCStringFn>(
        GetProcAddress(ui, "?getGroupName@UI_GroupFeel@@QEBAPEBDXZ"));
    g_uiDynamicMenuStripGetList =
        reinterpret_cast<NativeFeelPointerFn>(GetProcAddress(
            ui,
            "?getMenuItemList@UI_DynamicMenuStrip@@IEAAPEAVUI_ItemList@@XZ"));
    g_uiMenuFeelGetDynamicMenu =
        reinterpret_cast<NativeFeelPointerFn>(GetProcAddress(
            ui, "?getDynamicMenu@UI_MenuFeel@@QEAAPEAVUI_ItemList@@XZ"));
    g_uiMenuFeelGetStaticMenu = reinterpret_cast<NativeFeelPointerFn>(
        GetProcAddress(ui, "?getStaticMenu@UI_MenuFeel@@QEAAPEAVUI_Menu@@XZ"));
    g_uiMenuBarItemGetMenu = reinterpret_cast<NativeFeelPointerFn>(
        GetProcAddress(ui, "?getMenu@UI_MenuBarItem@@QEBAPEBVUI_Menu@@XZ"));
    g_uiParentMenuItemGetMenuWindow =
        reinterpret_cast<NativeFeelPointerFn>(GetProcAddress(
            ui, "?getMenu@UI_ParentMenuItem@@QEBAPEAVUI_MenuWindow@@XZ"));
    g_uiMenuWindowGetFirstMenu = reinterpret_cast<NativeFeelPointerFn>(
        GetProcAddress(ui, "?getFirstMenu@UI_MenuWindow@@QEAAPEAVUI_Menu@@XZ"));
    g_uiTextFeelGetLook = reinterpret_cast<NativeFeelPointerFn>(
        GetProcAddress(ui, "?textLook@UI_TextFeel@@AEBAPEBVUI_Text@@XZ"));
    g_uiChooserGetQuery = reinterpret_cast<NativeFeelPointerFn>(GetProcAddress(
        ui, "?getQueryObject@UI_Chooser@@QEBAPEAVUI_ChooserQuery@@XZ"));
    g_uiStringChooserQueryVtable = reinterpret_cast<void **>(
        GetProcAddress(ui, "??_7UI_StringChooserQuery@@6BUI_ChooserQuery@@@"));
    g_uiStringChooserEntryCount = reinterpret_cast<UIStringChooserEntryCountFn>(
        GetProcAddress(ui, "?getNumEntries@UI_StringChooserQuery@@QEBAHXZ"));
    g_uiStringChooserEntry =
        reinterpret_cast<UIStringChooserEntryFn>(GetProcAddress(
            ui,
            "?getEntry@UI_StringChooserQuery@@QEBAAEBVUT_StringHolder@@H@Z"));

    g_uiFeelRenderMe = reinterpret_cast<UIFeelRenderMeFn>(
        GetProcAddress(ui, "?renderMe@UI_Feel@@QEAAXPEAVRE_Render@@HH@Z"));
    if (!g_uiFeelRenderMe)
        return false;
    return attachInlineHook(g_uiFeelRenderMe, &hookedUIFeelRenderMe);
}

void hookedSITreeDoRenderNode(void *tree, void *render, void *paintState,
                              void *styleOption, void *node, int a, int b,
                              int c, int d, int e, int f) {
    HC_OBSERVE(hookedSITreeDoRenderNode);
    recordVisibleNativePointer(node, g_collectVisibleSITreeNodes,
                               g_visibleSITreeNodeCount, g_visibleSITreeNodes);

    // SI_Tree renders ordinary rows through an exact UI_Text instance.  That
    // same exact type is also used by editable native fields, so the generic
    // UI_Text hook must not opt it in globally.  The tree renderer is the
    // display-only owner boundary: keep its complete row render (including
    // width measurement) inside the translated-text scope.  This preserves
    // gallery/tree/list labels without ever widening the editable-value path.
    ScopedAllowHoudiniUiText allowTreeRowText;
    g_siTreeDoRenderNode(tree, render, paintState, styleOption, node, a, b, c,
                         d, e, f);
}

bool installSITreeTraversalHook() {
    if (g_siTreeDoRenderNode)
        return static_cast<bool>(g_siTreeNodeApi);

    HMODULE si = GetModuleHandleW(L"libSI.dll");
    if (!si)
        return false;

    g_siTreeNodeFunctions.getParent =
        reinterpret_cast<HoudiniChineseNativeTree::Api::GetParentFn>(
            GetProcAddress(si, "?getParent@SI_TreeNode@@QEBAPEAV1@XZ"));
    g_siTreeNodeFunctions.getChildCount =
        reinterpret_cast<HoudiniChineseNativeTree::Api::GetChildCountFn>(
            GetProcAddress(si, "?getNumChildNodes@SI_TreeNode@@QEAAHXZ"));
    g_siTreeNodeFunctions.getChild =
        reinterpret_cast<HoudiniChineseNativeTree::Api::GetChildFn>(
            GetProcAddress(si, "?getChildNode@SI_TreeNode@@QEAAPEAV1@H@Z"));
    // This wrapper dispatches through the concrete node's vtable, so derived
    // node labels are returned instead of SI_TreeNode's empty base value.
    g_siTreeNodeFunctions.getText =
        reinterpret_cast<HoudiniChineseNativeTree::Api::GetTextFn>(
            GetProcAddress(si, "?getFullDescription@SI_TreeNode@@UEAAPEBDXZ"));
    if (!g_siTreeNodeFunctions)
        return false;
    g_siTreeNodeApi = {
        &guardedSITreeParent,
        &guardedSITreeChildCount,
        &guardedSITreeChild,
        &guardedSITreeText,
    };

    g_siTreeDoRenderNode =
        reinterpret_cast<SITreeDoRenderNodeFn>(GetProcAddress(
            si, HC_TREE_RENDER_SYMBOL));
    if (!g_siTreeDoRenderNode)
        return false;
    return attachInlineHook(g_siTreeDoRenderNode, &hookedSITreeDoRenderNode);
}

// The r(...) field in Houdini's .ui resources is returned by
// UI_HelpFeel::getHelpText as a temporary UT_StringHolder. Translate that
// return value only; the cached help field on the source gadget is unchanged.
// MSVC x64 passes the hidden return buffer in RDX and UI_DeviceEvent in R8.
#if defined(HC_HOUDINI_21)
using UIHelpString = UT_DeepString;
#else
using UIHelpString = UT_StringHolder;
#endif
using UIHelpFeelGetTextFn = UIHelpString *(*)(void *, UIHelpString *,
                                                 UI_DeviceEvent *);
UIHelpFeelGetTextFn g_uiHelpFeelGetText = nullptr;
bool g_uiHelpFeelGetTextHookInstalled = false;

UIHelpString *hookedUIHelpFeelGetText(void *self, UIHelpString *result,
                                         UI_DeviceEvent *event) {
    HC_OBSERVE(hookedUIHelpFeelGetText);
    if (!g_uiHelpFeelGetText || !result)
        return result;
    UIHelpString *returned = g_uiHelpFeelGetText(self, result, event);
    UIHelpString *value = returned ? returned : result;
    const QString source = QString::fromUtf8(value->c_str());
    const QString translated = displayTranslation(source);
    if (!translated.isEmpty()) {
        const QByteArray utf8 = translated.toUtf8();
        *value = UIHelpString(utf8.constData());
    }
    return returned ? returned : result;
}

bool installUIHelpFeelGetTextHook() {
    if (g_uiHelpFeelGetTextHookInstalled)
        return true;
    HMODULE ui = GetModuleHandleW(L"libUI.dll");
    if (!ui)
        return false;
    g_uiHelpFeelGetText = reinterpret_cast<UIHelpFeelGetTextFn>(
        GetProcAddress(ui, HC_UI_HELP_SYMBOL));
    if (!g_uiHelpFeelGetText)
        return false;
    g_uiHelpFeelGetTextHookInstalled =
        attachInlineHook(g_uiHelpFeelGetText, &hookedUIHelpFeelGetText);
    hookLog("UIHelpFeelGetTextHook: installed=%d",
            g_uiHelpFeelGetTextHookInstalled ? 1 : 0);
    return g_uiHelpFeelGetTextHookInstalled;
}

// ---------------------------------------------------------------------------
// Buttons live in a single Qt child in the measured Desktop/Take blank area.
// All private UI access below is read-only, bounded and on the GUI thread.
// No menu paint, selection, mouse-event or enable/disable hook is installed.
// ---------------------------------------------------------------------------
using NativeMapToWindowFn = void (*)(const void *, int *, int *);
using NativeDimensionFn = int (*)(const void *);
using NativeVisibleFn = bool (*)(const void *);
using MainQtWindowFn = QWidget *(*)();
using MainNativeWindowFn = void *(*)();
NativeMapToWindowFn g_menuMapToWindow = nullptr;
NativeDimensionFn g_menuWidth = nullptr;
NativeDimensionFn g_menuHeight = nullptr;
NativeVisibleFn g_menuVisible = nullptr;
MainQtWindowFn g_mainQtWindow = nullptr;
MainNativeWindowFn g_mainNativeWindow = nullptr;
UIFeelParentFn g_menuParent = nullptr;
UIFeelChildCountFn g_menuCount = nullptr;
UIFeelChildFn g_menuChild = nullptr;
int g_menuNativeWindowHeight = 0;
bool g_menuButtonsInstalled = false;
std::atomic_bool g_languageRefreshScheduled{false};
QPointer<HoudiniChineseButtons::Pair> g_menuButtons;

void updateMenuButtonState() {
    if (g_menuButtons) {
        g_menuButtons->setState(g_enabled.load(std::memory_order_acquire),
            g_dictionaryReady.load(std::memory_order_acquire));
    }
}

void *guardedMenuRoot() {
    __try {
        return HoudiniChineseNativeRtti::nonVirtualBase(
            g_mainNativeWindow(), ".?AVUI_Feel@@");
    } __except (nativeTraversalExceptionFilter(GetExceptionCode())) {
        return nullptr;
    }
}
void *guardedMenuParent(void *node) {
    __try { return node ? g_menuParent(node) : nullptr; }
    __except (nativeTraversalExceptionFilter(GetExceptionCode())) { return nullptr; }
}
int guardedMenuCount(void *node) {
    __try {
        const int count = node ? g_menuCount(node) : 0;
        return count >= 0 && count <= 100000 ? count : 0;
    } __except (nativeTraversalExceptionFilter(GetExceptionCode())) { return 0; }
}
void *guardedMenuChild(void *node, int index) {
    __try { return node ? g_menuChild(node, index) : nullptr; }
    __except (nativeTraversalExceptionFilter(GetExceptionCode())) { return nullptr; }
}
QRect guardedMenuRect(void *node) {
    __try {
        if (!node || !g_menuVisible(node))
            return {};
        int x = 0, y = 0;
        g_menuMapToWindow(node, &x, &y);
        const int w = g_menuWidth(node), h = g_menuHeight(node);
        return QRect(x, g_menuNativeWindowHeight - y - h, w, h);
    } __except (nativeTraversalExceptionFilter(GetExceptionCode())) { return {}; }
}

QRect readMenuRowSpace() {
    QWidget *parent = g_mainQtWindow();
    void *root = guardedMenuRoot();
    if (!parent || !root)
        return {};
    const QRect nativeWindow = guardedMenuRect(root);
    if (nativeWindow.width() <= 0 || nativeWindow.height() <= 0)
        return {};
    g_menuNativeWindowHeight = nativeWindow.height();
    const HoudiniChineseMenuRow::Api api{&guardedMenuParent, &guardedMenuCount,
        &guardedMenuChild, &guardedNativeIsExactType, &guardedMenuRect};
    const QRect space = HoudiniChineseMenuRow::availableSpace(root, api);
    if (space.isEmpty())
        return {};
    // Native device units and Qt logical pixels can differ on high-DPI setups.
    const qreal sx = qreal(parent->width()) / nativeWindow.width();
    const qreal sy = qreal(parent->height()) / nativeWindow.height();
    return QRect(qRound(space.x() * sx), qRound(space.y() * sy),
                 qRound(space.width() * sx), qRound(space.height() * sy));
}

void ensureMenuButtons() {
    QWidget *parent = g_mainQtWindow();
    if (!parent)
        return;
    if (!g_menuButtons || g_menuButtons->parent() != parent) {
        if (g_menuButtons)
            g_menuButtons->deleteLater();
        g_menuButtons = new HoudiniChineseButtons::Pair(parent,
            [](HoudiniChineseButtons::Action action) {
                switch (action) {
                case HoudiniChineseButtons::Action::ToggleLanguage:
                    toggleLanguage();
                    break;
                case HoudiniChineseButtons::Action::EditShortcut:
                    openHotkeyDialog();
                    break;
                }
            }, &readMenuRowSpace);
    }
    updateMenuButtonState();
}

bool installMenuButtons() {
    if (g_menuButtonsInstalled)
        return true;
    HMODULE ui = GetModuleHandleW(L"libUI.dll");
    HMODULE re = GetModuleHandleW(L"libRE.dll");
    if (!ui || !re)
        return false;
    g_menuMapToWindow = reinterpret_cast<NativeMapToWindowFn>(
        GetProcAddress(ui, "?mapFromFeelToWindow@UI_Feel@@QEBAXPEAH0@Z"));
    g_menuWidth = reinterpret_cast<NativeDimensionFn>(
        GetProcAddress(ui, "?getDevW@UI_Feel@@QEBAHXZ"));
    g_menuHeight = reinterpret_cast<NativeDimensionFn>(
        GetProcAddress(ui, "?getDevH@UI_Feel@@QEBAHXZ"));
    g_menuVisible = reinterpret_cast<NativeVisibleFn>(
        GetProcAddress(ui, "?isVisible@UI_Feel@@QEBA_NXZ"));
    g_mainQtWindow = reinterpret_cast<MainQtWindowFn>(
        GetProcAddress(re, "?mainQtWindow@RE_Window@@SAPEAVQWidget@@XZ"));
    g_mainNativeWindow = reinterpret_cast<MainNativeWindowFn>(
        GetProcAddress(ui, "?getMainWindow@UI_Window@@SAPEAV1@XZ"));
    g_menuParent = reinterpret_cast<UIFeelParentFn>(
        GetProcAddress(ui, "?getParent@UI_Feel@@QEBAPEAV1@XZ"));
    g_menuCount = reinterpret_cast<UIFeelChildCountFn>(
        GetProcAddress(ui, "?getNumChildren@UI_Feel@@QEBAHXZ"));
    g_menuChild = reinterpret_cast<UIFeelChildFn>(
        GetProcAddress(ui, "?getChild@UI_Feel@@QEBAPEAV1@H@Z"));
    if (!g_menuMapToWindow || !g_menuWidth || !g_menuHeight || !g_menuVisible ||
        !g_mainQtWindow || !g_mainNativeWindow || !g_menuParent || !g_menuCount || !g_menuChild)
        return false;
    new HoudiniChineseButtons::OwnerWatch(QCoreApplication::instance(), []() -> QObject * {
        ensureMenuButtons();
        return g_mainQtWindow() ? g_menuButtons.data() : nullptr;
    });
    g_menuButtonsInstalled = true;
    return true;
}

qreal __fastcall hookedQuickFontAdvance(const void *self, const QString &text) {
    HC_OBSERVE(hookedQuickFontAdvance);
    if (!g_quickFontAdvance)
        return 0.0;
    const QString translated = measureQuickText(text);
    return g_quickFontAdvance(self, translated.isEmpty() ? text : translated);
}

void *__fastcall hookedQuickFontBoundingRect(const void *self, QRectF *result,
                                             const QString &text) {
    HC_OBSERVE(hookedQuickFontBoundingRect);
    if (!g_quickFontBoundingRect)
        return result;
    const QString translated = measureQuickText(text);
    return g_quickFontBoundingRect(self, result,
                                   translated.isEmpty() ? text : translated);
}

void *__fastcall hookedQuickFontTightBoundingRect(const void *self,
                                                  QRectF *result,
                                                  const QString &text) {
    HC_OBSERVE(hookedQuickFontTightBoundingRect);
    if (!g_quickFontTightBoundingRect)
        return result;
    const QString translated = measureQuickText(text);
    return g_quickFontTightBoundingRect(
        self, result, translated.isEmpty() ? text : translated);
}

void *__fastcall hookedQuickFontElidedText(const void *self, QString *result,
                                           const QString &text,
                                           Qt::TextElideMode mode, qreal width,
                                           int flags) {
    HC_OBSERVE(hookedQuickFontElidedText);
    if (!g_quickFontElidedText)
        return result;
    const QString translated = measureQuickText(text);
    return g_quickFontElidedText(self, result,
                                 translated.isEmpty() ? text : translated, mode,
                                 width, flags);
}

bool isQuickTextObject(const QObject *object) {
    if (!object)
        return false;
    for (const QMetaObject *meta = object->metaObject(); meta;
         meta = meta->superClass()) {
        if (qstrcmp(meta->className(), "QQuickText") == 0)
            return true;
    }
    return false;
}

bool quickTextDocumentAlreadyTranslated(const QQuickItem *item,
                                        const QString &translated) {
    if (!item || translated.isEmpty())
        return false;
    const QObjectList &children = item->children();
    for (QObject *child : children) {
        const auto *document = qobject_cast<const QTextDocument *>(child);
        if (document && document->toPlainText() == translated)
            return true;
    }
    return false;
}

// GUI-thread-only weak inventory of labels whose natural size participated
// in translation. Do not retain an old numeric width: text, font, padding and
// available space can change while Chinese is active.
QHash<QQuickItem *, QPointer<QQuickItem>> g_translatedQuickSizeItems;

void reflowQuickDisplayLabel(QQuickItem *item) {
    if (!item || !isQuickTextObject(item))
        return;
    const QPointer<QQuickItem> safeItem(item);
    const auto children = item->children();
    for (QObject *child : children) {
        if (auto *document = qobject_cast<QTextDocument *>(child)) {
            document->setHtml(item->property("text").toString());
            break;
        }
    }
    if (safeItem)
        QMetaObject::invokeMethod(safeItem, "forceLayout", Qt::DirectConnection);
}

void rememberTranslatedQuickSize(QQuickItem *item) {
    if (g_translatedQuickSizeItems.value(item) == item)
        return;
    if (g_translatedQuickSizeItems.size() % 64 == 0) {
        for (auto it = g_translatedQuickSizeItems.begin();
             it != g_translatedQuickSizeItems.end();) {
            if (it.value().isNull()) it = g_translatedQuickSizeItems.erase(it);
            else ++it;
        }
    }
    g_translatedQuickSizeItems.insert(item, QPointer<QQuickItem>(item));
}

void reflowTranslatedQuickSizes(bool settled = false) {
    auto *app = QCoreApplication::instance();
    if (!app || QThread::currentThread() != app->thread())
        return;
    // Keep weak owners across BOTH phases and both languages. Releasing them
    // on the first English pass leaves the deferred pass with no text items,
    // and a later Chinese toggle cannot rebuild their rich documents either.
    for (auto it = g_translatedQuickSizeItems.begin();
         it != g_translatedQuickSizeItems.end();) {
        if (it.value().isNull()) it = g_translatedQuickSizeItems.erase(it);
        else ++it;
    }
    const auto items = g_translatedQuickSizeItems.values();
    for (const auto &item : items) {
        if (item) reflowQuickDisplayLabel(item);
    }
    if (!settled) return;
    for (const auto &item : items) {
        if (!item) continue;
        // Width bindings/Row/Loader positioning must settle before the text
        // commits wrap and vertical alignment at its final rectangle.
        QList<QPointer<QQuickItem>> ancestors;
        for (auto *parent = item->parentItem(); parent && ancestors.size() < 64;
             parent = parent->parentItem())
            ancestors.append(parent);
        for (auto it = ancestors.crbegin(); it != ancestors.crend(); ++it)
            if (*it) (*it)->ensurePolished();
        if (!item) continue;
        item->ensurePolished();
        if (!item) continue;
        QMetaObject::invokeMethod(item, "forceLayout", Qt::DirectConnection);
        if (!item) continue;
        item->update();
        if (auto *window = item->window()) window->requestUpdate();
    }
}

qreal translatedQuickTextImplicitWidth(QQuickItem *item, qreal sourceWidth) {
    auto *app = QGuiApplication::instance();
    if (!app || QThread::currentThread() != app->thread() ||
        !isQuickTextObject(item)) {
        return sourceWidth;
    }

    const QString source = item->property("text").toString();
    const QString translated = displayQuickLabelTranslation(source);
    if (translated.isEmpty())
        return sourceWidth;
    // Also remember labels already sized by the translated QTextDocument.
    // They need the same English re-layout even if no extra delta is applied.
    rememberTranslatedQuickSize(item);
    if (quickTextDocumentAlreadyTranslated(item, translated)) {
        return sourceWidth;
    }

    const QVariant fontValue = item->property("font");
    if (!fontValue.canConvert<QFont>())
        return sourceWidth;
    const QFontMetricsF metrics(qvariant_cast<QFont>(fontValue));
    DisplayTranslationGuard rawMetrics;
    const qreal sourceTextWidth = metrics.horizontalAdvance(source);
    const qreal translatedTextWidth = metrics.horizontalAdvance(translated);
    const qreal horizontalPadding =
        (std::max)(qreal{0.0}, item->property("leftPadding").toReal()) +
        (std::max)(qreal{0.0}, item->property("rightPadding").toReal());
    return static_cast<qreal>(HoudiniChineseQuickText::adjustedImplicitWidth(
        sourceWidth, sourceTextWidth, translatedTextWidth, horizontalPadding));
}

void __fastcall hookedQuickItemSetImplicitWidth(QQuickItem *self, qreal width) {
    HC_OBSERVE(hookedQuickItemSetImplicitWidth);
    if (g_quickItemSetImplicitWidth) {
        g_quickItemSetImplicitWidth(
            self, translatedQuickTextImplicitWidth(self, width));
    }
}

void __fastcall hookedQuickItemSetImplicitSize(QQuickItem *self, qreal width,
                                               qreal height) {
    HC_OBSERVE(hookedQuickItemSetImplicitSize);
    if (g_quickItemSetImplicitSize) {
        g_quickItemSetImplicitSize(
            self, translatedQuickTextImplicitWidth(self, width), height);
    }
}

void scheduleLanguageRefresh() {
    if (auto *app = QCoreApplication::instance()) {
        if (!g_languageRefreshScheduled.exchange(true,
                                                 std::memory_order_acq_rel)) {
            QTimer::singleShot(0, app, [] {
                g_languageRefreshScheduled.store(false,
                                                 std::memory_order_release);
                refreshLanguageDisplay();
            });
        }
    } else {
        forceMainWindowRepaint();
    }
}

// A visible Houdini gadget help tip owns a rendered copy of getHelpText()'s
// result. Repainting cannot change that copy. Re-enter libUI's own help-tip
// lifecycle with its saved locate event so the active tip is rebuilt through
// getHelpText() immediately, without synthesizing mouse input.
void rebuildActiveHoudiniHelpTip() {
    using RemoveActiveHelpFn = void (*)(void *);
    using DisplayHelpMsgFn = void (*)(void *, void *);
    static void **currentHelper = nullptr;
    static void *locateEvent = nullptr;
    static RemoveActiveHelpFn removeActiveHelp = nullptr;
    static DisplayHelpMsgFn displayHelpMsg = nullptr;
    if (HMODULE ui = GetModuleHandleW(L"libUI.dll")) {
        if (!currentHelper) {
            currentHelper = reinterpret_cast<void **>(
                GetProcAddress(ui, "?ourCurrentHelper@UI_HelpFeel@@0PEAV1@EA"));
        }
        if (!locateEvent) {
            locateEvent = GetProcAddress(
                ui, "?ourLocateEvent@UI_HelpFeel@@0VUI_DeviceEvent@@A");
        }
        if (!removeActiveHelp) {
            removeActiveHelp =
                reinterpret_cast<RemoveActiveHelpFn>(GetProcAddress(
                    ui, "?removeActiveHelp@UI_HelpFeel@@SAXPEAVUI_Feel@@@Z"));
        }
        if (!displayHelpMsg) {
            displayHelpMsg = reinterpret_cast<DisplayHelpMsgFn>(GetProcAddress(
                ui, "?displayHelpMsg@UI_HelpFeel@@IEAAXPEAVUI_Event@@@Z"));
        }
    }
    if (!currentHelper || !locateEvent ||
        !removeActiveHelp || !displayHelpMsg) {
        return;
    }

    __try {
        void *helper = *currentHelper;
        if (!helper)
            return;
        removeActiveHelp(nullptr);
        displayHelpMsg(helper, locateEvent);
    } __except (nativeTraversalExceptionFilter(GetExceptionCode())) {
        // A help tip can disappear between the exported global read and the
        // private lifecycle calls. Language switching must remain harmless.
        return;
    }
}

void toggleLanguage() {
    const bool enabled = !g_enabled.load(std::memory_order_acquire);
    g_enabled.store(enabled, std::memory_order_release);
    setHelpFooterChinese(enabled);
    setParameterHeaderChinese(enabled);
    updateMenuButtonState();
    rebuildActiveHoudiniHelpTip();
    scheduleLanguageRefresh();
}

void openHotkeyDialog() {
    if (g_hotkeyDialog && g_hotkeyDialog->isVisible()) {
        g_hotkeyDialog->raise();
        g_hotkeyDialog->activateWindow();
        return;
    }
    // Set the guard before constructing/showing the editor. This guarantees
    // that the key release completing a newly recorded shortcut cannot also
    // reach the global language-toggle handler.
    g_hotkeyDialogOpen = true;
    HoudiniChineseHotkeyDialog *dialog = nullptr;
    try {
        dialog = new HoudiniChineseHotkeyDialog(
            QApplication::activeWindow(), g_toggleShortcut, g_configPath);
    } catch (...) {
        g_hotkeyDialogOpen = false;
        return; // A failed optional dialog must not escape the Qt callback.
    }
    g_hotkeyDialog = dialog;
    QObject::connect(dialog, &QObject::destroyed, [] {
        g_hotkeyDialog = nullptr;
        g_hotkeyDialogOpen = false;
        updateMenuButtonState();
    });
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

class NativeShortcutEventFilter final : public QAbstractNativeEventFilter {
  public:
    bool nativeEventFilter(const QByteArray &, void *message,
                           qintptr *result) override {
        const auto *nativeMessage = static_cast<const MSG *>(message);
        if (!nativeMessage)
            return false;

        // QQuickWindow does not consistently forward this reserved shortcut
        // through QApplication's QObject event filter. Catch the physical
        // Windows key at the same native boundary already used by Houdini's
        // private menu controls. OEM_3 is the ` / ~ key on the active Windows
        // layout; Shift selects the one-shot capture chord.
        const bool oneShotKey = (nativeMessage->message == WM_KEYDOWN ||
                                 nativeMessage->message == WM_SYSKEYDOWN) &&
                                nativeMessage->wParam == VK_OEM_3 &&
                                (GetKeyState(VK_SHIFT) & 0x8000) != 0 &&
                                (GetKeyState(VK_CONTROL) & 0x8000) == 0 &&
                                (GetKeyState(VK_MENU) & 0x8000) == 0;
        if (oneShotKey && !g_hotkeyDialogOpen && !QApplication::activeModalWidget()) {
            captureKeyAccepted_ = true;
            // Bit 30 is set by Windows only when this is an auto-repeat.  Do
            // not retain a separate "key is down" flag: capture starts a
            // nested event loop and an injected or redirected key-up can then
            // be delivered to another Houdini top-level window, leaving such
            // a flag permanently armed and disabling all later scans.
            const bool autoRepeat =
                (static_cast<quintptr>(nativeMessage->lParam) &
                 (quintptr{1} << 30)) != 0;
            if (!autoRepeat && !g_oneShotCaptureRunning) {
                g_oneShotShortcutTarget = QGuiApplication::focusObject();
                rememberQtWindow(QGuiApplication::focusWindow());
                QTimer::singleShot(0, QCoreApplication::instance(),
                                   [] { captureUntranslatedOnce(); });
            }
            if (result)
                *result = 0;
            return true;
        }
        const bool oneShotKeyUp = (nativeMessage->message == WM_KEYUP ||
                                   nativeMessage->message == WM_SYSKEYUP) &&
                                  nativeMessage->wParam == VK_OEM_3;
        // Modifiers may be released first. Consume a release only if we took
        // its press; this flag never prevents a later press from starting work.
        if (oneShotKeyUp && captureKeyAccepted_) {
            captureKeyAccepted_ = false;
            if (result)
                *result = 0;
            return true;
        }

        return false;
    }

  private:
    bool captureKeyAccepted_ = false;
};

void installNativeShortcutEventFilter() {
    static NativeShortcutEventFilter filter;
    static bool installed = false;
    if (!installed && QCoreApplication::instance()) {
        QCoreApplication::instance()->installNativeEventFilter(&filter);
        installed = true;
    }
}

using QSGInlineAddTextLayoutFn = void(__fastcall *)(void *, QPointF,
                                                    QTextLayout *, int, int,
                                                    int, int);
QSGInlineAddTextLayoutFn g_qsgInlineAddTextLayout = nullptr;
using QSGInlineAddTextDocumentFn = void(__fastcall *)(void *, QPointF,
                                                      QTextDocument *, int,
                                                      int);
QSGInlineAddTextDocumentFn g_qsgInlineAddTextDocument = nullptr;
using QQuickTextUpdateLayoutFn = void(__fastcall *)(void *);
using QQuickTextUpdatePaintNodeFn = void *(__fastcall *)(void *, void *,
                                                         void *);
QQuickTextUpdateLayoutFn g_quickTextUpdateLayout = nullptr;
QQuickTextUpdatePaintNodeFn g_quickTextUpdatePaintNode = nullptr;
QQuickTextUpdatePaintNodeFn g_quickComboInputUpdatePaintNode = nullptr;
thread_local int g_quickDisplayTextScopeDepth = 0;
thread_local int g_quickComboDisplayPaintDepth = 0;

bool isNonEditableQuickComboDisplay(const QQuickItem *input) {
    return HoudiniChineseTextContext::nonEditableComboDisplay(input);
}

void *__fastcall hookedQQuickComboInputUpdatePaintNode(void *self, void *oldNode,
                                                       void *updateData) {
    HC_OBSERVE(hookedQQuickComboInputUpdatePaintNode);
    if (!g_quickComboInputUpdatePaintNode)
        return nullptr;
    const bool display = experimentalQtInlineEnabled() && displayWorkRequested() &&
        isNonEditableQuickComboDisplay(static_cast<QQuickItem *>(self));
    TextScope textScope(display ? TextRole::DisplayLabel : TextRole::UserValue);
    struct Scope {
        bool active;
        explicit Scope(bool value) : active(value) {
            if (active) ++g_quickComboDisplayPaintDepth;
        }
        ~Scope() { if (active) --g_quickComboDisplayPaintDepth; }
    } scope(display);
    return g_quickComboInputUpdatePaintNode(self, oldNode, updateData);
}

class ScopedQuickDisplayText final {
  public:
    ScopedQuickDisplayText() { ++g_quickDisplayTextScopeDepth; }
    ~ScopedQuickDisplayText() { --g_quickDisplayTextScopeDepth; }
  private:
    TextScope textScope_{TextRole::DisplayLabel};
};

bool isQuickDisplayTextCall() noexcept {
    return g_quickDisplayTextScopeDepth > 0 || g_quickComboDisplayPaintDepth > 0;
}

void __fastcall hookedQQuickTextUpdateLayout(void *self) {
    HC_OBSERVE(hookedQQuickTextUpdateLayout);
    if (!g_quickTextUpdateLayout)
        return;
    ScopedQuickDisplayText scope;
    g_quickTextUpdateLayout(self);
}

void *__fastcall hookedQQuickTextUpdatePaintNode(void *self, void *oldNode,
                                                 void *updateData) {
    HC_OBSERVE(hookedQQuickTextUpdatePaintNode);
    if (!g_quickTextUpdatePaintNode)
        return nullptr;
    ScopedQuickDisplayText scope;
    return g_quickTextUpdatePaintNode(self, oldNode, updateData);
}

bool installQtQuickTextOwnerHooks() {
    HMODULE quick = GetModuleHandleW(L"Qt6Quick.dll");
    if (!quick)
        return false;
    // These are the two QQuickText owner boundaries. Entry detours catch
    // internal direct and virtual dispatch inside Qt6Quick; a stack-range
    // check can miss tail calls and also costs a backtrace on every hit.
    const bool layoutInstalled = resolveAndAttachInlineHook(
        quick, "?updateLayout@QQuickTextPrivate@@QEAAXXZ",
        g_quickTextUpdateLayout, hookedQQuickTextUpdateLayout);
    const bool paintInstalled = resolveAndAttachInlineHook(
        quick,
        "?updatePaintNode@QQuickText@@MEAAPEAVQSGNode@@PEAV2@"
        "PEAUUpdatePaintNodeData@QQuickItem@@@Z",
        g_quickTextUpdatePaintNode, hookedQQuickTextUpdatePaintNode);
    bool comboInstalled = true;
    if (experimentalQtInlineEnabled()) {
        comboInstalled = resolveAndAttachInlineHook(
            quick,
            "?updatePaintNode@QQuickTextInput@@MEAAPEAVQSGNode@@PEAV2@"
            "PEAUUpdatePaintNodeData@QQuickItem@@@Z",
            g_quickComboInputUpdatePaintNode, hookedQQuickComboInputUpdatePaintNode);
    }
    return layoutInstalled && paintInstalled && comboInstalled;
}

QString displayQuickSceneGraphTranslation(const QString &text) {
    const QString trimmed = text.trimmed();
    const bool translationEnabled = g_enabled.load(std::memory_order_acquire);
    const bool captureEnabled = HoudiniChineseCapture::isEnabled();
    if ((!translationEnabled && !captureEnabled) || text.isEmpty() ||
        trimmed.size() <= 1 || containsEastAsianText(trimmed) ||
        !g_dictionaryReady.load(std::memory_order_acquire)) {
        return {};
    }
    // Stack walking is intentionally after the cached lookup. Most scene
    // graph strings are misses and should not pay for owner classification on
    // every frame. During an explicit scan, misses are classified too.
    if (lookupTranslation(trimmed).isEmpty() && !captureEnabled) {
        HoudiniChineseHooks::noteTranslationOutcome(HoudiniChineseHooks::TranslationOutcome::LookupMiss);
        return {};
    }
    if (!isQuickDisplayTextCall()) {
        HoudiniChineseHooks::noteTranslationOutcome(HoudiniChineseHooks::TranslationOutcome::Protected);
        return {};
    }
    return displayQuickLabelTranslation(text);
}

void __fastcall hookedQSGInlineAddTextLayout(void *node, QPointF position,
                                             QTextLayout *layout,
                                             int selectionStart,
                                             int selectionCount, int lineStart,
                                             int lineCount) {
    HC_OBSERVE(hookedQSGInlineAddTextLayout);
    if (!g_qsgInlineAddTextLayout)
        return;

    const bool wholeLayout =
        layout && lineStart == 0 &&
        (lineCount == -1 || lineCount == layout->lineCount());
    // TextInput passes cursor-position-based selection arguments even when
    // nothing is selected. The combo owner scope already checks selectedText
    // and preeditText; normalize only our temporary translated draw below.
    const bool comboDisplay = g_quickComboDisplayPaintDepth > 0;
    const bool selected = selectionStart != -1 || selectionCount != -1;
    if (layout && wholeLayout && (!selected || comboDisplay) &&
        HoudiniChineseTextLayout::canTranslate(layout)) {
        const QString translated =
            displayQuickSceneGraphTranslation(layout->text());
        if (!translated.isEmpty()) {
            auto display = HoudiniChineseTextLayout::makeDisplayLayout(layout, translated);
            if (display) {
                g_qsgInlineAddTextLayout(
                    node, position, display.get(), comboDisplay ? -1 : selectionStart,
                    comboDisplay ? -1 : selectionCount, lineStart,
                    lineCount == -1 ? -1 : display->lineCount());
                return;
            }
        }
    }

    g_qsgInlineAddTextLayout(node, position, layout, selectionStart,
                             selectionCount, lineStart, lineCount);
}

std::unique_ptr<QTextDocument>
makeQuickDisplayDocument(const QTextDocument *source, const QString &text) {
    if (!source || text.isEmpty())
        return {};

    std::unique_ptr<QTextDocument> result(source->clone());
    if (!result)
        return {};

    QTextCursor sourceCursor(const_cast<QTextDocument *>(source));
    sourceCursor.movePosition(QTextCursor::Start);
    const QTextCharFormat charFormat = sourceCursor.charFormat();
    const QTextBlockFormat blockFormat = sourceCursor.blockFormat();

    QTextCursor targetCursor(result.get());
    targetCursor.select(QTextCursor::Document);
    targetCursor.setBlockFormat(blockFormat);
    targetCursor.insertText(text, charFormat);
    return result;
}

void __fastcall hookedQSGInlineAddTextDocument(void *node, QPointF position,
                                               QTextDocument *document,
                                               int selectionStart,
                                               int selectionCount) {
    HC_OBSERVE(hookedQSGInlineAddTextDocument);
    if (!g_qsgInlineAddTextDocument)
        return;

    const bool selected = selectionStart != -1 || selectionCount != -1;
    if (document && !selected) {
        const QString source = document->toPlainText();
        const QString translated = displayQuickSceneGraphTranslation(source);
        if (!translated.isEmpty()) {
            auto display = makeQuickDisplayDocument(document, translated);
            if (display) {
                g_qsgInlineAddTextDocument(node, position, display.get(),
                                           selectionStart, selectionCount);
                return;
            }
        }
    }

    g_qsgInlineAddTextDocument(node, position, document, selectionStart,
                               selectionCount);
}

bool installQtQuickTextLayoutHook() {
    if (g_qsgInlineAddTextLayout && g_qsgInlineAddTextDocument)
        return true;
    HMODULE quick = GetModuleHandleW(L"Qt6Quick.dll");
    if (!quick)
        return false;
    constexpr const char *layoutSymbol =
        "?doAddTextLayout@QSGInternalTextNode@@MEAAXVQPointF@@"
        "PEAVQTextLayout@@HHHH@Z";
    constexpr const char *documentSymbol =
        "?doAddTextDocument@QSGInternalTextNode@@MEAAXVQPointF@@"
        "PEAVQTextDocument@@HH@Z";
    bool layoutInstalled = g_qsgInlineAddTextLayout != nullptr;
    if (!layoutInstalled) {
        g_qsgInlineAddTextLayout = reinterpret_cast<QSGInlineAddTextLayoutFn>(
            GetProcAddress(quick, layoutSymbol));
        layoutInstalled = attachInlineHook(g_qsgInlineAddTextLayout,
                                           hookedQSGInlineAddTextLayout);
    }
    bool documentInstalled = g_qsgInlineAddTextDocument != nullptr;
    if (!documentInstalled) {
        g_qsgInlineAddTextDocument =
            reinterpret_cast<QSGInlineAddTextDocumentFn>(
                GetProcAddress(quick, documentSymbol));
        documentInstalled = attachInlineHook(g_qsgInlineAddTextDocument,
                                             hookedQSGInlineAddTextDocument);
    }
    hookLog("installQtQuickTextLayoutHook: layout=%p/%d document=%p/%d",
            reinterpret_cast<void *>(g_qsgInlineAddTextLayout),
            layoutInstalled ? 1 : 0,
            reinterpret_cast<void *>(g_qsgInlineAddTextDocument),
            documentInstalled ? 1 : 0);
    return layoutInstalled && documentInstalled;
}

using QTextLayoutSetTextFn = void(__fastcall *)(QTextLayout *, const QString &);
QTextLayoutSetTextFn g_qtextLayoutSetText = nullptr;
using QTextDocumentSetTextFn = void(__fastcall *)(QTextDocument *,
                                                  const QString &);
QTextDocumentSetTextFn g_qtextDocumentSetHtml = nullptr;
QTextDocumentSetTextFn g_qtextDocumentSetPlainText = nullptr;
bool g_quickLayoutInputHooksInstalled = false;

bool isQuickTextUpdateLayoutCall() {
    // Combo TextInput owns editing state: never replace its stored layout text.
    return g_quickDisplayTextScopeDepth > 0;
}

QString translateQuickRichText(const QString &source) {
    return HoudiniChineseRichText::translateTextNodes(
        source,
        [](const QString &text) { return displayQuickLabelTranslation(text); });
}

void __fastcall hookedQTextLayoutSetText(QTextLayout *self,
                                         const QString &source) {
    HC_OBSERVE(hookedQTextLayoutSetText);
    if (!g_qtextLayoutSetText)
        return;
    if (!isQuickTextUpdateLayoutCall() || (source.contains(QLatin1Char('<')) &&
                                           source.contains(QLatin1Char('>')))) {
        g_qtextLayoutSetText(self, source);
        return;
    }

    const QString translated = displayQuickLabelTranslation(source);
    if (translated.isEmpty()) {
        g_qtextLayoutSetText(self, source);
        return;
    }

    g_qtextLayoutSetText(self, translated);
}

void __fastcall hookedQTextDocumentSetHtml(QTextDocument *self,
                                           const QString &source) {
    HC_OBSERVE(hookedQTextDocumentSetHtml);
    if (!g_qtextDocumentSetHtml)
        return;
    // QQuickText uses QTextDocument for Text.RichText, including Houdini's
    // Python-generated SuperTooltip HTML. Translate text nodes before the
    // document lays them out, while preserving tags, attributes, CSS and the
    // QML source property. This keeps paint and width measurement consistent.
    if (!self || !isQuickTextObject(self->parent())) {
        g_qtextDocumentSetHtml(self, source);
        return;
    }
    const bool markedUp =
        source.contains(QLatin1Char('<')) && source.contains(QLatin1Char('>'));
    const QString translated = markedUp ? translateQuickRichText(source)
                                        : displayQuickLabelTranslation(source);
    g_qtextDocumentSetHtml(self, translated.isEmpty() ? source : translated);
}

void __fastcall hookedQTextDocumentSetPlainText(QTextDocument *self,
                                                const QString &source) {
    HC_OBSERVE(hookedQTextDocumentSetPlainText);
    if (!g_qtextDocumentSetPlainText)
        return;
    if (!self || !isQuickTextObject(self->parent())) {
        g_qtextDocumentSetPlainText(self, source);
        return;
    }
    const QString translated = displayQuickLabelTranslation(source);
    g_qtextDocumentSetPlainText(self,
                                translated.isEmpty() ? source : translated);
}

bool installQtQuickLayoutInputHook() {
    if (g_quickLayoutInputHooksInstalled)
        return true;
    HMODULE gui = GetModuleHandleW(L"Qt6Gui.dll");
    HMODULE quick = GetModuleHandleW(L"Qt6Quick.dll");
    if (!gui || !quick)
        return false;

    const bool ownerHooksInstalled = installQtQuickTextOwnerHooks();
    const bool layoutInstalled = resolveAndAttachInlineHook(
        gui, "?setText@QTextLayout@@QEAAXAEBVQString@@@Z", g_qtextLayoutSetText,
        hookedQTextLayoutSetText);
    const bool htmlInstalled = resolveAndAttachInlineHook(
        gui, "?setHtml@QTextDocument@@QEAAXAEBVQString@@@Z",
        g_qtextDocumentSetHtml, hookedQTextDocumentSetHtml);
    const bool plainInstalled = resolveAndAttachInlineHook(
        gui, "?setPlainText@QTextDocument@@QEAAXAEBVQString@@@Z",
        g_qtextDocumentSetPlainText, hookedQTextDocumentSetPlainText);
    g_quickLayoutInputHooksInstalled = layoutInstalled && htmlInstalled &&
                                       plainInstalled && ownerHooksInstalled;
    hookLog("installQtQuickLayoutInputHook: layout=%d html=%d plain=%d "
            "ownerHooks=%d",
            layoutInstalled ? 1 : 0, htmlInstalled ? 1 : 0,
            plainInstalled ? 1 : 0, ownerHooksInstalled ? 1 : 0);
    return g_quickLayoutInputHooksInstalled;
}

using UITextPaintFn = void (*)(void *, QStylePainter *, const QRect &, void *);
using UILookRenderFn = void (*)(void *, void *, int, int, int, int, void *);
using UITextPaintRangeFn = void (*)(void *, QStylePainter *, const QRect &,
                                    const char *, const char *);
using UITextRenderRangeFn = void (*)(void *, void *, const char *, const char *,
                                     int, int, int, int);
using UITextStringWidthFn = float (*)(const void *, const char *, const char *,
                                      const void *);
using RERenderPutStringFn = void (*)(void *, void *, const char *, const char *,
                                     void *);
using RERenderPutCStringFn = void (*)(void *, const char *);
using RERenderPutRangeFn = void (*)(void *, const char *, const char *);
using REFontStringWidthFn = float (*)(void *, const char *, const char *,
                                      const void *);
using REFontStringRefWidthFn = float (*)(void *, const UT_StringRef &,
                                         const void *);
using BRHelpOpenTooltipWindowFn = void (*)(const void *, const char *,
                                           const char *, int, int, int, int,
                                           const char *, const char *,
                                           const char *, bool);
UITextPaintFn g_uiTextPaint = nullptr;
UITextPaintFn g_uiTableTextPaint = nullptr;
UITextPaintFn g_uiTableIconTextPaint = nullptr;
UILookRenderFn g_uiHelpTipLookRender = nullptr;
UITextPaintRangeFn g_uiTextPaintRange = nullptr;
UITextRenderRangeFn g_uiTextRenderRange = nullptr;
UITextStringWidthFn g_uiTextStringWidth = nullptr;
RERenderPutStringFn g_reRenderPutString = nullptr;
RERenderPutCStringFn g_reRenderPutCString = nullptr;
RERenderPutRangeFn g_reRenderPutRange = nullptr;
REFontStringWidthFn g_reFontStringWidth = nullptr;
REFontStringRefWidthFn g_reFontStringRefWidth = nullptr;
BRHelpOpenTooltipWindowFn g_brHelpOpenTooltipWindow = nullptr;
bool g_uiTableTextPaintInstalled = false;
bool g_uiTableIconTextPaintInstalled = false;
bool g_nativeDirectTextLookHooksInstalled = false;
bool g_uiTextRangeHooksInstalled = false;
bool g_reTextHooksInstalled = false;

struct NativeTranslatedTextRange final {
    QByteArray utf8;
    const char *begin = nullptr;
    const char *end = nullptr;
};

bool translatedUtf8TextRange(const char *begin, const char *end,
                             bool allowHoudiniUiText,
                             NativeTranslatedTextRange &result) {
    // Bypass decoding/allocation when both display translation and explicit
    // capture are off. Capture still receives native strings in English mode.
    if (!displayWorkRequested() || !begin)
        return false;
    std::size_t length = 0;
    if (end) {
        const auto first = reinterpret_cast<std::uintptr_t>(begin);
        const auto last = reinterpret_cast<std::uintptr_t>(end);
        if (last < first || last - first > 65536)
            return false;
        length = static_cast<std::size_t>(last - first);
    } else {
        length = guardedNativeCStringLength(begin);
    }
    if (length == 0)
        return false;

    const QString source =
        QString::fromUtf8(begin, static_cast<qsizetype>(length));
    QString translated;
    if (allowHoudiniUiText) {
        ScopedAllowHoudiniUiText allowUiText;
        translated = displayTranslation(source);
    } else {
        translated = displayTranslation(source);
    }
    if (translated.isEmpty())
        return false;

    result.utf8 = translated.toUtf8();
    result.begin = result.utf8.constData();
    result.end = result.begin + result.utf8.size();
    return true;
}

bool translatedNativeTextRange(void *self, const char *begin, const char *end,
                               NativeTranslatedTextRange &result) {
    if (!self)
        return false;
    const bool allowUiText =
        HoudiniChineseTextContext::current.allowNativeText || shouldAllowNativeLookText(self);
    if (!allowUiText) {
        HoudiniChineseHooks::noteTranslationOutcome(HoudiniChineseHooks::TranslationOutcome::Protected);
        return false;
    }
    return translatedUtf8TextRange(begin, end, true, result);
}

// H21 shelf captions must be translated before wrapping and ellipsis. Its
// paint signature has no trailing bool (unlike the H22 editor-stack symbol).
using WrappedPaint21Fn = void (*)(const void *, QStylePainter *, const QRect &,
    const UT_StringView &, const QPalette &, const QFontMetricsF &);
using WrappedRender21Fn = void (*)(void *, void *, const UT_StringView &, int, int, int, int);
WrappedPaint21Fn g_wrappedPaint21 = nullptr;
WrappedRender21Fn g_wrappedRender21 = nullptr;
void hookedWrappedPaint21(const void *self, QStylePainter *painter, const QRect &rect,
    const UT_StringView &text, const QPalette &palette, const QFontMetricsF &metrics) {
    HC_OBSERVE(hookedWrappedPaint21);
    NativeTranslatedTextRange translated;
    // This overload is the display-only wrapping sink used by shelf captions.
    // Translate before it chooses line breaks or ellipsis. TextRole::UserValue
    // is still checked first by displayTranslation, so editable data remains
    // protected even if a future control reuses this sink.
    if (self && translatedUtf8TextRange(text.begin(), text.end(), true, translated)) {
        const UT_StringView display(translated.begin, translated.end);
        TextScope protectNested(TextRole::UserValue);
        g_wrappedPaint21(self, painter, rect, display, palette, metrics);
    } else g_wrappedPaint21(self, painter, rect, text, palette, metrics);
}
void hookedWrappedRender21(void *self, void *render, const UT_StringView &text,
    int x, int y, int width, int height) {
    HC_OBSERVE(hookedWrappedRender21);
    NativeTranslatedTextRange translated;
    if (self && translatedUtf8TextRange(text.begin(), text.end(), true, translated)) {
        const UT_StringView display(translated.begin, translated.end);
        TextScope protectNested(TextRole::UserValue);
        g_wrappedRender21(self, render, display, x, y, width, height);
    } else g_wrappedRender21(self, render, text, x, y, width, height);
}

void hookedUITextPaintRange(void *self, QStylePainter *painter,
                            const QRect &rect, const char *begin,
                            const char *end) {
    HC_OBSERVE(hookedUITextPaintRange);
    if (!g_uiTextPaintRange)
        return;
    NativeTranslatedTextRange translated;
    if (translatedNativeTextRange(self, begin, end, translated)) {
        g_uiTextPaintRange(self, painter, rect, translated.begin,
                           translated.end);
        return;
    }
    g_uiTextPaintRange(self, painter, rect, begin, end);
}

void hookedUITextRenderRange(void *self, void *render, const char *begin,
                             const char *end, int x, int y, int width,
                             int height) {
    HC_OBSERVE(hookedUITextRenderRange);
    if (!g_uiTextRenderRange)
        return;
    NativeTranslatedTextRange translated;
    if (translatedNativeTextRange(self, begin, end, translated)) {
        g_uiTextRenderRange(self, render, translated.begin, translated.end, x,
                            y, width, height);
        return;
    }
    g_uiTextRenderRange(self, render, begin, end, x, y, width, height);
}

float hookedUITextStringWidth(const void *self, const char *begin,
                              const char *end, const void *transform) {
    HC_OBSERVE(hookedUITextStringWidth);
    if (!g_uiTextStringWidth)
        return 0.0f;
    if (SYS_VERSION_MAJOR_INT == 21 && g_nativePreferredSizeDepth21 == 0) {
        TextScope preserveMetrics(TextRole::UserValue);
        return g_uiTextStringWidth(self, begin, end, transform);
    }
    NativeTranslatedTextRange translated;
    if (translatedNativeTextRange(const_cast<void *>(self), begin, end,
                                  translated)) {
        if constexpr (SYS_VERSION_MAJOR_INT == 21) {
            TextScope preserveNested(TextRole::UserValue);
            return (std::max)(g_uiTextStringWidth(self, begin, end, transform),
                g_uiTextStringWidth(self, translated.begin, translated.end, transform));
        }
        return g_uiTextStringWidth(self, translated.begin, translated.end,
                                   transform);
    }
    return g_uiTextStringWidth(self, begin, end, transform);
}

using UITextPreferredSizesFn = void (*)(void *, int &, int &, int &, int &, int &, int &);
UITextPreferredSizesFn g_uiTextPreferredSizes21 = nullptr;
void callPreferredSizes21(UITextPreferredSizesFn original, void *self,
                         int &a, int &b, int &c, int &d, int &e, int &f) {
    struct LayoutScope {
        LayoutScope() { ++g_nativePreferredSizeDepth21; }
        ~LayoutScope() { --g_nativePreferredSizeDepth21; }
    } layout;
    ScopedAllowHoudiniUiText owner(TextRole::Unknown);
    original(self, a, b, c, d, e, f);
}
void hookedUITextPreferredSizes21(void *self, int &a, int &b, int &c,
                                  int &d, int &e, int &f) {
    callPreferredSizes21(g_uiTextPreferredSizes21,self,a,b,c,d,e,f);
}

#define HC_PREFERRED21(Name) \
    UITextPreferredSizesFn g_preferred##Name##21 = nullptr; \
    void hookedPreferred##Name##21(void *self, int &a,int &b,int &c,int &d,int &e,int &f) { \
        callPreferredSizes21(g_preferred##Name##21,self,a,b,c,d,e,f); \
    }
HC_PREFERRED21(MenuBarItemLook)
HC_PREFERRED21(MenuItemLookBase)
HC_PREFERRED21(MenuText)
HC_PREFERRED21(MenuButtonLook)
HC_PREFERRED21(IconText)
#undef HC_PREFERRED21

void installWrappedTextHooks21() {
    if constexpr (SYS_VERSION_MAJOR_INT == 21) {
        HMODULE ui = GetModuleHandleW(L"libUI.dll");
        if (!ui) return;
        // Retry independently of the older range hooks. A transient thread
        // suspension failure must not be hidden by their installed flag.
        // attachInlineHook retains the shared three-attempt retry bound.
        resolveAndAttachInlineHook(ui,
            "?paintWrappedText@UI_Text@@AEBAXAEAVQStylePainter@@AEBVQRect@@AEBVUT_StringView@@AEBVQPalette@@AEBVQFontMetricsF@@@Z",
            g_wrappedPaint21, hookedWrappedPaint21);
        resolveAndAttachInlineHook(ui,
            "?renderWrappedText@UI_Text@@AEAAXPEAVRE_Render@@AEBVUT_StringView@@HHHH@Z",
            g_wrappedRender21, hookedWrappedRender21);
    }
}

bool installUITextRangeHooks() {
    if (g_uiTextRangeHooksInstalled)
        return true;
    HMODULE ui = GetModuleHandleW(L"libUI.dll");
    if (!ui)
        return false;

    if constexpr (SYS_VERSION_MAJOR_INT == 21) {
        resolveAndAttachNativeVirtualHook(ui,
            "?getPreferredSizes@UI_Text@@UEAAXAEAH00000@Z",
            "??_7UI_Text@@6B@",
            g_uiTextPreferredSizes21, hookedUITextPreferredSizes21);
#define HC_INSTALL_PREFERRED21(Name) \
        resolveAndAttachNativeVirtualHook(ui, \
            "?getPreferredSizes@UI_" #Name "@@UEAAXAEAH00000@Z", \
            "??_7UI_" #Name "@@6B@", \
            g_preferred##Name##21, hookedPreferred##Name##21);
        HC_INSTALL_PREFERRED21(MenuBarItemLook)
        HC_INSTALL_PREFERRED21(MenuItemLookBase)
        HC_INSTALL_PREFERRED21(MenuText)
        HC_INSTALL_PREFERRED21(MenuButtonLook)
        HC_INSTALL_PREFERRED21(IconText)
#undef HC_INSTALL_PREFERRED21
    }

    // Disassembly of libUI 22.0 shows these non-virtual overloads are the
    // common paint/render/measurement sinks for ActionButtonLook,
    // FloaterText, FolderText, GroupHeaderLook, Histogram, IconText, Label,
    // LabeledLook, MenuText, RadioText, the table text/header/title variants,
    // ToggleButtonLook and XCFLook. Hooking the sink entries covers all of
    // those libUI-internal direct and virtual call paths without maintaining a
    // fragile list of every outer class.
    const bool paintInstalled = resolveAndAttachImportedHook(
        ui, "?paintText@UI_Text@@QEBAXAEAVQStylePainter@@AEBVQRect@@PEBD2@Z",
        g_uiTextPaintRange, hookedUITextPaintRange);
    const bool renderInstalled = resolveAndAttachImportedHook(
        ui, "?renderText@UI_Text@@QEAAXPEAVRE_Render@@PEBD1HHHH@Z",
        g_uiTextRenderRange, hookedUITextRenderRange);
    const bool widthInstalled = resolveAndAttachImportedHook(
        ui, kUiTextWidthSymbol,
        g_uiTextStringWidth, hookedUITextStringWidth);
    g_uiTextRangeHooksInstalled =
        paintInstalled && renderInstalled && widthInstalled;
    hookLog("UITextRangeHooks: paint=%d render=%d width=%d",
            paintInstalled ? 1 : 0, renderInstalled ? 1 : 0,
            widthInstalled ? 1 : 0);
    return g_uiTextRangeHooksInstalled;
}

void hookedRERenderPutString(void *self, void *font, const char *begin,
                             const char *end, void *transform) {
    HC_OBSERVE(hookedRERenderPutString);
    if (!g_reRenderPutString)
        return;
    NativeTranslatedTextRange translated;
    if (translatedUtf8TextRange(begin, end, false, translated)) {
        g_reRenderPutString(self, font, translated.begin, translated.end,
                            transform);
        return;
    }
    g_reRenderPutString(self, font, begin, end, transform);
}

void hookedRERenderPutCString(void *self, const char *text) {
    HC_OBSERVE(hookedRERenderPutCString);
    if (!g_reRenderPutCString)
        return;
    NativeTranslatedTextRange translated;
    g_reRenderPutCString(self,
        translatedUtf8TextRange(text, nullptr, false, translated)
            ? translated.begin : text);
}

void hookedRERenderPutRange(void *self, const char *begin, const char *end) {
    HC_OBSERVE(hookedRERenderPutRange);
    if (!g_reRenderPutRange)
        return;
    NativeTranslatedTextRange translated;
    if (translatedUtf8TextRange(begin, end, false, translated)) {
        g_reRenderPutRange(self, translated.begin, translated.end);
        return;
    }
    g_reRenderPutRange(self, begin, end);
}

float hookedREFontStringRefWidth(void *self, const UT_StringRef &text,
                                 const void *transform) {
    HC_OBSERVE(hookedREFontStringRefWidth);
    if (!g_reFontStringRefWidth)
        return 0.0f;
    if (SYS_VERSION_MAJOR_INT == 21 && g_nativePreferredSizeDepth21 == 0) {
        TextScope preserveMetrics(TextRole::UserValue);
        return g_reFontStringRefWidth(self, text, transform);
    }
    NativeTranslatedTextRange translated;
    if (translatedUtf8TextRange(text.begin(), text.end(), false, translated)) {
        const UT_StringRef replacement(translated.begin,
                                       static_cast<exint>(translated.utf8.size()));
        if constexpr (SYS_VERSION_MAJOR_INT == 21) {
            TextScope preserveNested(TextRole::UserValue);
            return (std::max)(g_reFontStringRefWidth(self, text, transform),
                g_reFontStringRefWidth(self, replacement, transform));
        }
        return g_reFontStringRefWidth(self, replacement, transform);
    }
    return g_reFontStringRefWidth(self, text, transform);
}

float hookedREFontStringWidth(void *self, const char *begin, const char *end,
                              const void *transform) {
    HC_OBSERVE(hookedREFontStringWidth);
    if (!g_reFontStringWidth)
        return 0.0f;
    if (SYS_VERSION_MAJOR_INT == 21 && g_nativePreferredSizeDepth21 == 0) {
        TextScope preserveMetrics(TextRole::UserValue);
        return g_reFontStringWidth(self, begin, end, transform);
    }
    NativeTranslatedTextRange translated;
    if (translatedUtf8TextRange(begin, end, false, translated)) {
        if constexpr (SYS_VERSION_MAJOR_INT == 21) {
            TextScope preserveNested(TextRole::UserValue);
            return (std::max)(g_reFontStringWidth(self, begin, end, transform),
                g_reFontStringWidth(self, translated.begin, translated.end, transform));
        }
        return g_reFontStringWidth(self, translated.begin, translated.end,
                                   transform);
    }
    return g_reFontStringWidth(self, begin, end, transform);
}

bool installRETextHooks() {
    if (g_reTextHooksInstalled)
        return true;
    HMODULE re = GetModuleHandleW(L"libRE.dll");
    if (!re)
        return false;
    // UI_FormattedText and several legacy looks bypass UI_Text and emit their
    // UTF-8 range directly through RE_RenderUI. This is the final native text
    // boundary. Prefer its implementation entry; preserve imported callers
    // when a nearby Detours trampoline cannot be allocated.
    const bool renderInstalled = resolveAndAttachImportedHook(
        re,
        "?putString@RE_RenderUI@@QEAAXAEAVRE_Font@@PEBD1PEAVtransform@UT_"
        "Unicode@@@Z",
        g_reRenderPutString, hookedRERenderPutString);
    const bool widthInstalled = resolveAndAttachImportedHook(
        re, "?getStringWidth@RE_Font@@QEAAMPEBD0PEBVtransform@UT_Unicode@@@Z",
        g_reFontStringWidth, hookedREFontStringWidth);
    // DM_VPortAgent::doRenderCurrentName draws the viewport's view/camera
    // selectors through the current-font overload and measures UT_StringRef.
    // These wrappers call the range sink inside libRE, bypassing its IAT.
    const bool cstringInstalled = resolveAndAttachImportedHook(
        re, "?putString@RE_RenderUI@@QEAAXPEBD@Z",
        g_reRenderPutCString, hookedRERenderPutCString);
    const bool rangeInstalled = resolveAndAttachImportedHook(
        re, "?putString@RE_RenderUI@@QEAAXPEBD0@Z",
        g_reRenderPutRange, hookedRERenderPutRange);
    const bool refWidthInstalled = resolveAndAttachImportedHook(
        re, "?getStringWidth@RE_Font@@QEAAMAEBVUT_StringRef@@"
            "PEBVtransform@UT_Unicode@@@Z",
        g_reFontStringRefWidth, hookedREFontStringRefWidth);
    g_reTextHooksInstalled = renderInstalled && widthInstalled &&
                            cstringInstalled && rangeInstalled && refWidthInstalled;
    hookLog("RETextHooks: render=%d width=%d", renderInstalled ? 1 : 0,
            widthInstalled ? 1 : 0);
    return g_reTextHooksInstalled;
}

void hookedUIHelpTipLookRender(void *self, void *render, int x, int y,
                               int width, int height, void *data) {
    HC_OBSERVE(hookedUIHelpTipLookRender);
    if (!g_uiHelpTipLookRender)
        return;
    // UI_HelpTipLook overrides UI_Look::draw/render and paints its title,
    // formatted body and footer through child look objects. Keep the complete
    // render call inside the display-only scope so those downstream text hooks
    // cannot mistake tooltip copy for an editable UI_Text value.
    ScopedAllowHoudiniUiText guard;
    g_uiHelpTipLookRender(self, render, x, y, width, height, data);
}

bool installUIHelpTipLookRenderHook() {
    HMODULE ui = GetModuleHandleW(L"libUI.dll");
    return resolveAndAttachInlineHook(
        ui, "?render@UI_HelpTipLook@@UEAAXPEAVRE_Render@@HHHHPEAX@Z",
        g_uiHelpTipLookRender, hookedUIHelpTipLookRender);
}

// 工具提示的标题不走 UI_Text，而是由 UI_HelpTipLook::getTitle 直接返回给绘制层，
// 所以在取词这一层翻译；返回的缓冲区是 thread_local，生命周期覆盖绘制调用。
thread_local std::string g_helpTipTitleBuffer;

const char *hookedUIHelpTipGetTitle(void *self) {
    HC_OBSERVE(hookedUIHelpTipGetTitle);
    if (!g_uiHelpTipGetTitle)
        return nullptr;
    const char *raw = g_uiHelpTipGetTitle(self);
    if (!raw || !*raw || !g_enabled.load(std::memory_order_acquire))
        return raw;
    const QString translated = displayTranslation(QString::fromUtf8(raw));
    if (translated.isEmpty())
        return raw;
    const QByteArray bytes = translated.toUtf8();
    g_helpTipTitleBuffer.assign(bytes.constData(),
                                static_cast<size_t>(bytes.size()));
    return g_helpTipTitleBuffer.c_str();
}

bool installUIHelpTipTitleHook() {
    HMODULE ui = GetModuleHandleW(L"libUI.dll");
    if (!g_uiHelpTipGetTitle)
        g_uiHelpTipGetTitle = reinterpret_cast<NativeCStringFn>(
            GetProcAddress(ui, "?getTitle@UI_HelpTipLook@@QEBAPEBDXZ"));
    if (!g_uiHelpTipGetTitle)
        return false;
    const bool installed =
        attachInlineHook(g_uiHelpTipGetTitle, hookedUIHelpTipGetTitle);
    hookLog("HelpTipTitleHook: target=%p installed=%d",
            reinterpret_cast<void *>(g_uiHelpTipGetTitle),
            installed ? 1 : 0);
    return installed;
}

// 工具提示页脚是 "Press %s for more help." 拼出来的（libUI 里只有这一个格式串），
// 拼完的整句不会再经过任何可钩的取词函数。这里直接把这个唯一的格式串
// 换成中文；原文 24 字节，中文 22 字节，放得下，切回英文时原样写回。
namespace {
constexpr size_t kHelpFooterBytes = 24; // "Press %s for more help." + NUL
unsigned char *g_helpFooterLiteral = nullptr;
char g_helpFooterOriginal[kHelpFooterBytes] = {};
bool g_helpFooterChinese = false;

bool locateHelpFooterLiteral() {
    if (g_helpFooterLiteral)
        return true;
    HMODULE ui = GetModuleHandleW(L"libUI.dll");
    if (!ui)
        return false;
    const auto *base = reinterpret_cast<const unsigned char *>(ui);
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return false;
    const auto *nt =
        reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return false;
    static const char needle[] = "Press %s for more help.";
    const auto *section = IMAGE_FIRST_SECTION(nt);
    const unsigned char *hit = nullptr;
    size_t found = 0;
    // 只扫描已初始化的只读数据段：可执行段不会有这个字面量，
    // 扫描未映射页会直接崩溃。
    for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index) {
        const DWORD characteristics = section[index].Characteristics;
        if ((characteristics & IMAGE_SCN_MEM_EXECUTE) ||
            !(characteristics & IMAGE_SCN_CNT_INITIALIZED_DATA))
            continue;
        const unsigned char *begin = base + section[index].VirtualAddress;
        const size_t length = section[index].Misc.VirtualSize;
        for (size_t offset = 0; offset + kHelpFooterBytes <= length; ++offset) {
            if (begin[offset] != 'P' ||
                std::memcmp(begin + offset, needle, kHelpFooterBytes - 1) != 0)
                continue;
            hit = begin + offset;
            if (++found > 1)
                return false; // 多个候选就不动，宁可不翻
        }
    }
    if (!hit)
        return false;
    g_helpFooterLiteral = const_cast<unsigned char *>(hit);
    std::memcpy(g_helpFooterOriginal, g_helpFooterLiteral, kHelpFooterBytes);
    hookLog("HelpFooter: literal at %p", static_cast<const void *>(hit));
    return true;
}

} // namespace

bool setHelpFooterChinese(bool chinese) {
    if (!locateHelpFooterLiteral())
        return false;
    if (g_helpFooterChinese == chinese)
        return true;
    static const char kChinese[] = "按 %s 查看帮助。";
    static_assert(sizeof(kChinese) <= kHelpFooterBytes, "footer text too long");
    DWORD previous = 0;
    if (!VirtualProtect(g_helpFooterLiteral, kHelpFooterBytes, PAGE_READWRITE,
                        &previous))
        return false;
    if (chinese) {
        std::memset(g_helpFooterLiteral, 0, kHelpFooterBytes);
        std::memcpy(g_helpFooterLiteral, kChinese, sizeof(kChinese) - 1);
    } else {
        std::memcpy(g_helpFooterLiteral, g_helpFooterOriginal, kHelpFooterBytes);
    }
    DWORD ignored = 0;
    VirtualProtect(g_helpFooterLiteral, kHelpFooterBytes, previous, &ignored);
    FlushInstructionCache(GetCurrentProcess(), g_helpFooterLiteral,
                          kHelpFooterBytes);
    g_helpFooterChinese = chinese;
    return true;
}

// 参数提示头是 libPRM.dll 里的 "Parameter: %s" 格式串（全库唯一）。
// 原位换成 "参数：%s"，切回英文时写回原文。
namespace {
constexpr size_t kParmHeaderBytes = 14; // "Parameter: %s" + NUL
unsigned char *g_parmHeaderLiteral = nullptr;
char g_parmHeaderOriginal[kParmHeaderBytes] = {};
bool g_parmHeaderChinese = false;

bool locateParmHeaderLiteral() {
    if (g_parmHeaderLiteral)
        return true;
    HMODULE prm = GetModuleHandleW(L"libPRM.dll");
    if (!prm)
        return false;
    const auto *base = reinterpret_cast<const unsigned char *>(prm);
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return false;
    const auto *nt =
        reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return false;
    static const char needle[] = "Parameter: %s";
    const auto *section = IMAGE_FIRST_SECTION(nt);
    const unsigned char *hit = nullptr;
    size_t found = 0;
    for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index) {
        const DWORD characteristics = section[index].Characteristics;
        if ((characteristics & IMAGE_SCN_MEM_EXECUTE) ||
            !(characteristics & IMAGE_SCN_CNT_INITIALIZED_DATA))
            continue;
        const unsigned char *begin = base + section[index].VirtualAddress;
        const size_t length = section[index].Misc.VirtualSize;
        for (size_t offset = 0; offset + kParmHeaderBytes <= length; ++offset) {
            if (begin[offset] != 'P' ||
                std::memcmp(begin + offset, needle, kParmHeaderBytes - 1) != 0)
                continue;
            hit = begin + offset;
            if (++found > 1)
                return false;
        }
    }
    if (!hit)
        return false;
    g_parmHeaderLiteral = const_cast<unsigned char *>(hit);
    std::memcpy(g_parmHeaderOriginal, g_parmHeaderLiteral, kParmHeaderBytes);
    hookLog("ParmHeader: literal at %p", static_cast<const void *>(hit));
    return true;
}
} // namespace

bool setParameterHeaderChinese(bool chinese) {
    if (!locateParmHeaderLiteral())
        return false;
    if (g_parmHeaderChinese == chinese)
        return true;
    static const char kChinese[] = "参数：%s";
    static_assert(sizeof(kChinese) <= kParmHeaderBytes, "header text too long");
    DWORD previous = 0;
    if (!VirtualProtect(g_parmHeaderLiteral, kParmHeaderBytes, PAGE_READWRITE,
                        &previous))
        return false;
    if (chinese) {
        std::memset(g_parmHeaderLiteral, 0, kParmHeaderBytes);
        std::memcpy(g_parmHeaderLiteral, kChinese, sizeof(kChinese) - 1);
    } else {
        std::memcpy(g_parmHeaderLiteral, g_parmHeaderOriginal, kParmHeaderBytes);
    }
    DWORD ignored = 0;
    VirtualProtect(g_parmHeaderLiteral, kParmHeaderBytes, previous, &ignored);
    FlushInstructionCache(GetCurrentProcess(), g_parmHeaderLiteral,
                          kParmHeaderBytes);
    g_parmHeaderChinese = chinese;
    return true;
}


const char *translatedTooltipArgument(const char *source,
                                      NativeTranslatedTextRange &storage) {
    if (!source)
        return nullptr;
    return translatedUtf8TextRange(source, nullptr, true, storage)
               ? storage.begin
               : source;
}

void hookedBRHelpOpenTooltipWindow(const void *self, const char *url,
                                   const char *help, int x, int y, int width,
                                   int height, const char *title,
                                   const char *hotkeySymbol,
                                   const char *hotkeyText, bool useFixedFont) {
    HC_OBSERVE(hookedBRHelpOpenTooltipWindow);
    if (!g_brHelpOpenTooltipWindow)
        return;

    // Shelf/tool help cards are browser-backed windows. They do not paint
    // through UI_HelpTipLook, UI_Text, QPainter, or RE_RenderUI: libBR first
    // serializes these UTF-8 fields and sends the result to its embedded
    // browser. Translate only the three visible prose fields at that final
    // display boundary. The URL/caching key and hotkey glyph identifier are
    // control data and must remain byte-for-byte unchanged.
    std::array<NativeTranslatedTextRange, 3> translated;
    g_brHelpOpenTooltipWindow(
        self, url, translatedTooltipArgument(help, translated[0]), x, y, width,
        height, translatedTooltipArgument(title, translated[1]), hotkeySymbol,
        translatedTooltipArgument(hotkeyText, translated[2]), useFixedFont);
}

bool installBRHelpTooltipHook() {
    HMODULE browser = GetModuleHandleW(L"libBR.dll");
    return resolveAndAttachInlineHook(
        browser, "?openTooltipWindow@BR_HelpManager@@UEBAXPEBD0HHHH000_N@Z",
        g_brHelpOpenTooltipWindow, hookedBRHelpOpenTooltipWindow);
}

// H21 native shelf tips obtain a complete summary from MOT, then split it into
// words for UI_FormattedText. Translating after that split cannot match prose.
// Only the caller-owned output copy is changed; URL and source help stay intact.
using MOTTooltip21Fn = bool (*)(void *, const char *, UT_String &);
using MOTParmTooltip21Fn = bool (*)(void *, const void *, UT_String &);
using MOTParsedTooltip21Fn = bool (*)(void *, const char *, const char *, UT_String &);
MOTTooltip21Fn g_motTooltip21 = nullptr;
MOTParmTooltip21Fn g_motParmTooltip21 = nullptr;
MOTParsedTooltip21Fn g_motParsedTooltip21 = nullptr;
void translateTooltipCopy21(UT_String &result) {
    const QString text = QString::fromUtf8(result.c_str());
    if (!g_enabled.load(std::memory_order_acquire)) {
        const QString source = findDictionarySourceExact(text);
        if (!source.isEmpty()) result.harden(source.toUtf8().constData());
        return;
    }
    const QString translated = displayTranslation(text);
    if (!translated.isEmpty()) result.harden(translated.toUtf8().constData());
}
bool hookedMOTTooltip21(void *self, const char *url, UT_String &result) {
    HC_OBSERVE(hookedMOTTooltip21);
    const bool ok = g_motTooltip21(self, url, result);
    if (ok) translateTooltipCopy21(result);
    return ok;
}
bool hookedMOTParmTooltip21(void *self, const void *parm, UT_String &result) {
    HC_OBSERVE(hookedMOTParmTooltip21);
    const bool ok = g_motParmTooltip21(self, parm, result);
    if (ok) translateTooltipCopy21(result);
    return ok;
}
bool hookedMOTParsedTooltip21(void *self, const char *key, const char *help, UT_String &result) {
    HC_OBSERVE(hookedMOTParsedTooltip21);
    const bool ok = g_motParsedTooltip21(self, key, help, result);
    if (ok) translateTooltipCopy21(result);
    return ok;
}
bool installMOTTooltipHooks21() {
    if constexpr (SYS_VERSION_MAJOR_INT != 21) return false;
    HMODULE mot = GetModuleHandleW(L"libMOT.dll");
    const bool url = resolveAndAttachInlineHook(mot,
        "?getTooltip@MOT_HelpManager@@UEAA_NPEBDAEAVUT_String@@@Z",
        g_motTooltip21, hookedMOTTooltip21);
    const bool parm = resolveAndAttachInlineHook(mot,
        "?getTooltip@MOT_HelpManager@@UEAA_NPEBVPRM_Parm@@AEAVUT_String@@@Z",
        g_motParmTooltip21, hookedMOTParmTooltip21);
    const bool parsed = resolveAndAttachInlineHook(mot,
        "?getParsedTooltip@MOT_HelpManager@@UEAA_NPEBD0AEAVUT_String@@@Z",
        g_motParsedTooltip21, hookedMOTParsedTooltip21);
    return url && parm && parsed;
}

#define HC_DECLARE_NATIVE_TEXT_PAINT_HOOK(Name)                                \
    UITextPaintFn g_ui##Name##Paint = nullptr;                                 \
    void hookedUI##Name##Paint(void *self, QStylePainter *painter,             \
                               const QRect &rect, void *data) {                \
        HC_OBSERVE(hookedUI##Name##Paint);                                    \
        if (!g_ui##Name##Paint)                                                \
            return;                                                            \
        ScopedAllowHoudiniUiText guard;                                        \
        g_ui##Name##Paint(self, painter, rect, data);                          \
    }

HC_DECLARE_NATIVE_TEXT_PAINT_HOOK(Label)
HC_DECLARE_NATIVE_TEXT_PAINT_HOOK(FolderText)
HC_DECLARE_NATIVE_TEXT_PAINT_HOOK(FormattedText)
HC_DECLARE_NATIVE_TEXT_PAINT_HOOK(IconText)
HC_DECLARE_NATIVE_TEXT_PAINT_HOOK(MenuButtonLook)
HC_DECLARE_NATIVE_TEXT_PAINT_HOOK(TableListHeader)

#undef HC_DECLARE_NATIVE_TEXT_PAINT_HOOK

#define HC_DECLARE_NATIVE_TEXT_RENDER_HOOK(Name)                               \
    UILookRenderFn g_ui##Name##Render = nullptr;                               \
    void hookedUI##Name##Render(void *self, void *render, int x, int y,        \
                                int width, int height, void *data) {           \
        HC_OBSERVE(hookedUI##Name##Render);                                   \
        if (!g_ui##Name##Render)                                               \
            return;                                                            \
        ScopedAllowHoudiniUiText guard;                                        \
        g_ui##Name##Render(self, render, x, y, width, height, data);           \
    }

HC_DECLARE_NATIVE_TEXT_RENDER_HOOK(FolderText)
HC_DECLARE_NATIVE_TEXT_RENDER_HOOK(FormattedText)
HC_DECLARE_NATIVE_TEXT_RENDER_HOOK(IconText)
HC_DECLARE_NATIVE_TEXT_RENDER_HOOK(MenuButtonLook)
HC_DECLARE_NATIVE_TEXT_RENDER_HOOK(TableListHeader)
HC_DECLARE_NATIVE_TEXT_RENDER_HOOK(CollapseButtonLook)
HC_DECLARE_NATIVE_TEXT_RENDER_HOOK(ColorMenuItemLook)
HC_DECLARE_NATIVE_TEXT_RENDER_HOOK(Overlap)

#undef HC_DECLARE_NATIVE_TEXT_RENDER_HOOK

// Keep the stable baseline's complete UI_Text paint/render coverage. Exact
// UI_Text objects are also used as display labels by libSI, libDM, libDTUI,
// libFUSE, libFUI and libIMGUI; filtering the exact type here silently drops
// those clients. Keep unknown ownership here: device roles protect actual
// editors, explicit owner scopes identify labels, and only unresolved native
// calls retain their existing editor-stack protection.
UILookRenderFn g_uiTextRender = nullptr;
void hookedUITextRender(void *self, void *render, int x, int y, int width,
                        int height, void *data) {
    HC_OBSERVE(hookedUITextRender);
    if (!g_uiTextRender)
        return;
    ScopedAllowHoudiniUiText guard(TextRole::Unknown);
    g_uiTextRender(self, render, x, y, width, height, data);
}

void paintTranslatedUIText(void *self, QStylePainter *painter,
                           const QRect &rect, void *data,
                           UITextPaintFn originalPaint) {
    if (!originalPaint || !self) {
        if (originalPaint)
            originalPaint(self, painter, rect, data);
        return;
    }
    TextScope paintScope(textRoleForDevice(painter ? painter->device() : nullptr));
    ScopedAllowHoudiniUiText guard(TextRole::Unknown);
    originalPaint(self, painter, rect, data);
}

void hookedUITextPaint(void *self, QStylePainter *painter, const QRect &rect,
                       void *data) {
    HC_OBSERVE(hookedUITextPaint);
    if (!g_uiTextPaint)
        return;
    paintTranslatedUIText(self, painter, rect, data, g_uiTextPaint);
}

void hookedUITableTextPaint(void *self, QStylePainter *painter,
                            const QRect &rect, void *data) {
    HC_OBSERVE(hookedUITableTextPaint);
    paintTranslatedUIText(self, painter, rect, data, g_uiTableTextPaint);
}

void hookedUITableIconTextPaint(void *self, QStylePainter *painter,
                                const QRect &rect, void *data) {
    HC_OBSERVE(hookedUITableIconTextPaint);
    paintTranslatedUIText(self, painter, rect, data, g_uiTableIconTextPaint);
}

bool installUITextPaintHook() {
    HMODULE ui = GetModuleHandleW(L"libUI.dll");
    if (!ui)
        return false;
    return resolveAndAttachNativeVirtualHook(
        ui, "?paint@UI_Text@@UEAAXAEAVQStylePainter@@AEBVQRect@@PEAX@Z",
        "??_7UI_Text@@6B@",
        g_uiTextPaint, hookedUITextPaint);
}

void installUITableTextPaintHooks() {
    HMODULE ui = GetModuleHandleW(L"libUI.dll");
    if (!ui)
        return;
    if (!g_uiTableTextPaintInstalled) {
        g_uiTableTextPaintInstalled = resolveAndAttachNativeVirtualHook(
            ui,
            "?paint@?$UI_TableListEntryBase@VUI_Text@@@@UEAAXAEAVQStylePainter@"
            "@AEBVQRect@@PEAX@Z",
            "??_7?$UI_TableListEntryBase@VUI_Text@@@@6BUI_Text@@@",
            g_uiTableTextPaint, hookedUITableTextPaint);
    }
    if (!g_uiTableIconTextPaintInstalled) {
        g_uiTableIconTextPaintInstalled = resolveAndAttachNativeVirtualHook(
            ui,
            "?paint@?$UI_TableListEntryBase@VUI_IconText@@@@"
            "UEAAXAEAVQStylePainter@@AEBVQRect@@PEAX@Z",
            "??_7?$UI_TableListEntryBase@VUI_IconText@@@@6BUI_IconText@@@",
            g_uiTableIconTextPaint, hookedUITableIconTextPaint);
    }
}

void installNativeDirectTextLookHooks() {
    if (g_nativeDirectTextLookHooksInstalled)
        return;
    HMODULE ui = GetModuleHandleW(L"libUI.dll");
    if (!ui)
        return;
    bool installed = true;

#define HC_ATTACH_NATIVE_TEXT_PAINT(Name, Symbol)                              \
    installed = resolveAndAttachNativeVirtualHook(                             \
                    ui, Symbol, "??_7UI_" #Name "@@6B@", g_ui##Name##Paint,    \
                                           hookedUI##Name##Paint) &&           \
                installed

    HC_ATTACH_NATIVE_TEXT_PAINT(
        Label, "?paint@UI_Label@@UEAAXAEAVQStylePainter@@AEBVQRect@@PEAX@Z");
    HC_ATTACH_NATIVE_TEXT_PAINT(
        FolderText,
        "?paint@UI_FolderText@@UEAAXAEAVQStylePainter@@AEBVQRect@@PEAX@Z");
    HC_ATTACH_NATIVE_TEXT_PAINT(
        FormattedText,
        "?paint@UI_FormattedText@@UEAAXAEAVQStylePainter@@AEBVQRect@@PEAX@Z");
    HC_ATTACH_NATIVE_TEXT_PAINT(
        IconText,
        "?paint@UI_IconText@@UEAAXAEAVQStylePainter@@AEBVQRect@@PEAX@Z");
    HC_ATTACH_NATIVE_TEXT_PAINT(
        MenuButtonLook,
        "?paint@UI_MenuButtonLook@@MEAAXAEAVQStylePainter@@AEBVQRect@@PEAX@Z");
    HC_ATTACH_NATIVE_TEXT_PAINT(
        TableListHeader,
        "?paint@UI_TableListHeader@@UEAAXAEAVQStylePainter@@AEBVQRect@@PEAX@Z");

#undef HC_ATTACH_NATIVE_TEXT_PAINT

#define HC_ATTACH_NATIVE_TEXT_RENDER(Name, Symbol)                             \
    installed = resolveAndAttachNativeVirtualHook(                             \
                    ui, Symbol, "??_7UI_" #Name "@@6B@", g_ui##Name##Render,   \
                                           hookedUI##Name##Render) &&          \
                installed

    HC_ATTACH_NATIVE_TEXT_RENDER(
        Text, "?render@UI_Text@@UEAAXPEAVRE_Render@@HHHHPEAX@Z");
    HC_ATTACH_NATIVE_TEXT_RENDER(
        FolderText, "?render@UI_FolderText@@UEAAXPEAVRE_Render@@HHHHPEAX@Z");
    HC_ATTACH_NATIVE_TEXT_RENDER(
        FormattedText,
        "?render@UI_FormattedText@@UEAAXPEAVRE_Render@@HHHHPEAX@Z");
    HC_ATTACH_NATIVE_TEXT_RENDER(
        IconText, "?render@UI_IconText@@UEAAXPEAVRE_Render@@HHHHPEAX@Z");
    HC_ATTACH_NATIVE_TEXT_RENDER(
        MenuButtonLook,
        "?render@UI_MenuButtonLook@@UEAAXPEAVRE_Render@@HHHHPEAX@Z");
    HC_ATTACH_NATIVE_TEXT_RENDER(
        TableListHeader,
        "?render@UI_TableListHeader@@UEAAXPEAVRE_Render@@HHHHPEAX@Z");
    HC_ATTACH_NATIVE_TEXT_RENDER(
        CollapseButtonLook,
        "?render@UI_CollapseButtonLook@@UEAAXPEAVRE_Render@@HHHHPEAX@Z");
    HC_ATTACH_NATIVE_TEXT_RENDER(
        ColorMenuItemLook,
        "?render@UI_ColorMenuItemLook@@UEAAXPEAVRE_Render@@HHHHPEAX@Z");
    HC_ATTACH_NATIVE_TEXT_RENDER(
        Overlap, "?render@UI_Overlap@@UEAAXPEAVRE_Render@@HHHHPEAX@Z");

#undef HC_ATTACH_NATIVE_TEXT_RENDER
    // These are virtual methods implemented and dispatched inside libUI.dll.
    // Inline hooks cover internal direct calls too. If unavailable, retain
    // virtual dispatch plus imports; never rely on imports alone for libUI.
    g_nativeDirectTextLookHooksInstalled = installed;
    hookLog("NativeDirectTextLookHooks: installed=%d", installed ? 1 : 0);
}

// QGraphics text items are painted through virtual dispatch inside
// Qt6Widgets.dll. Hook their implementation entries so those internal calls
// cannot bypass an import-table-only replacement.
bool g_nodeTextHooksInstalled = false;
bool installNodeTextHooks() {
    if (g_nodeTextHooksInstalled)
        return true;
    HMODULE qtWidgets = GetModuleHandleW(L"Qt6Widgets.dll");
    if (!qtWidgets)
        return false;
    const bool simpleInstalled = resolveAndAttachImportedHook(
        qtWidgets,
        "?paint@QGraphicsSimpleTextItem@@UEAAXPEAVQPainter@@"
        "PEBVQStyleOptionGraphicsItem@@PEAVQWidget@@@Z",
        g_graphicsSimpleTextPaint, hookedGraphicsSimpleTextPaint);
    const bool textInstalled = resolveAndAttachImportedHook(
        qtWidgets,
        "?paint@QGraphicsTextItem@@UEAAXPEAVQPainter@@"
        "PEBVQStyleOptionGraphicsItem@@PEAVQWidget@@@Z",
        g_graphicsTextPaint, hookedGraphicsTextPaint);
    g_nodeTextHooksInstalled = simpleInstalled && textInstalled;
    hookLog("NodeTextHooks: simple=%d text=%d", simpleInstalled ? 1 : 0,
            textInstalled ? 1 : 0);
    return g_nodeTextHooksInstalled;
}

// Hook QPainter::drawStaticText for network editor titles and names.
void installStaticTextHooks() {
    if (g_drawStaticTextPF)
        return;
    HMODULE qtGui = GetModuleHandleW(L"Qt6Gui.dll");
    if (!qtGui)
        return;
    if (!g_drawStaticTextPF) {
        g_drawStaticTextPF = reinterpret_cast<DrawStaticTextPF>(GetProcAddress(
            qtGui,
            "?drawStaticText@QPainter@@QEAAXAEBVQPointF@@AEBVQStaticText@@@Z"));
    }
    if (!g_drawStaticTextPF)
        return;
    if (tryExperimentalQtInline(g_drawStaticTextPF, &hookedDrawStaticText))
        return;
    for (HMODULE mod : loadedModules()) {
        if (mod != g_moduleHandle)
            replaceImportInModule(
                mod, g_hookSlotCount,
                reinterpret_cast<void *>(g_drawStaticTextPF),
                reinterpret_cast<void *>(&hookedDrawStaticText));
    }
}

// Resolve Qt 6 font-metric symbols and patch their import slots.
// ---------------------------------------------------------------------------
// 节点文字（真正入口）：网络编辑器文字经 RV_TextRender::addText / renderText
// （libRV.dll 定义，libDM.dll 导入），接收 const char* 文本。在文本压进字体
// 几何前替换成中文。本质与八猴 Font::CompiledString 同类 —— 节点文字的 hook 点。
// ---------------------------------------------------------------------------
std::string translateTextToUtf8(const char *text) {
    if (!displayWorkRequested() || !text || !*text)
        return {};
    const QString q = QString::fromUtf8(text);
    const QString t = displayTranslation(q);
    if (t.isEmpty())
        return {};
    const QByteArray utf8 = t.toUtf8();
    return std::string(utf8.constData(), static_cast<size_t>(utf8.size()));
}

using RVAddTextFn = void (*)(void *, const char *, void *, float, float, float,
                             void *, float);
using RVAddText2Fn = void (*)(void *, const char *, void *, float, float,
                              void *, float);
using RVRenderFn = void (*)(void *, void *, const char *, float, float, float,
                            void *, float, void *);
RVAddTextFn g_rvAddText = nullptr;
RVAddText2Fn g_rvAddText2 = nullptr;
RVRenderFn g_rvRender = nullptr;
bool g_nodeRenderSymbolsResolved = false;

void hookedRVAddText(void *self, const char *text, void *font, float a, float b,
                     float c, void *color, float d) {
    HC_OBSERVE(hookedRVAddText);
    if (!g_rvAddText)
        return;
    const std::string tr = translateTextToUtf8(text);
    g_rvAddText(self, tr.empty() ? text : tr.c_str(), font, a, b, c, color, d);
}
void hookedRVAddText2(void *self, const char *text, void *font, float a,
                      float b, void *color, float d) {
    HC_OBSERVE(hookedRVAddText2);
    if (!g_rvAddText2)
        return;
    const std::string tr = translateTextToUtf8(text);
    g_rvAddText2(self, tr.empty() ? text : tr.c_str(), font, a, b, color, d);
}
void hookedRVRender(void *render, void *font, const char *text, float a,
                    float b, float c, void *color, float d, void *rect) {
    HC_OBSERVE(hookedRVRender);
    if (!g_rvRender)
        return;
    const std::string tr = translateTextToUtf8(text);
    g_rvRender(render, font, tr.empty() ? text : tr.c_str(), a, b, c, color, d,
               rect);
}

void installNodeRenderTextHooks() {
    if (g_nodeRenderSymbolsResolved)
        return;
    HMODULE rv = GetModuleHandleW(L"libRV.dll");
    if (!rv) {
        hookLog("installNodeRenderTextHooks: libRV.dll not loaded");
        return;
    }
    // RV_TextRender calls these overloads both through other modules' imports
    // and directly from libRV itself. Detour the implementation entries so
    // neither a same-DLL call nor a virtual caller can bypass translation.
    const bool addInstalled = resolveAndAttachImportedHook(
        rv,
        "?addText@RV_TextRender@@QEAAXPEBDAEAVRE_Font@@MMMAEBVUT_Color@@M@Z",
        g_rvAddText, hookedRVAddText);
    const bool add2Installed = resolveAndAttachImportedHook(
        rv, "?addText@RV_TextRender@@QEAAXPEBDAEAVRE_Font@@MMAEBVUT_Color@@M@Z",
        g_rvAddText2, hookedRVAddText2);
    const bool renderInstalled = resolveAndAttachImportedHook(
        rv,
        "?renderText@RV_TextRender@@SAXPEAVRV_Render@@AEAVRE_Font@@"
        "PEBDMMMAEBVUT_Color@@MAEBV?$UT_Rect@VUT_DimRectImpl@@@@@Z",
        g_rvRender, hookedRVRender);
    g_nodeRenderSymbolsResolved =
        addInstalled && add2Installed && renderInstalled;
    hookLog("installNodeRenderTextHooks: addText=%d addText2=%d render=%d",
            addInstalled ? 1 : 0, add2Installed ? 1 : 0,
            renderInstalled ? 1 : 0);
}

// Houdini's node graph is rendered by CV_PaintBuffer, not Qt or
// RV_TextRender.  libOPUI imports these functions from libCV, so replace only
// the temporary UTF-8 paint argument and leave every graph/model string intact.
using CVTextFn = void (*)(void *, int, int, bool, const char *, void *,
                          double *, void *);
using CVTextInBoxFn = void (*)(void *, const void *, bool, bool, const char *,
                               bool, int, int, void *, double *, void *);
using CVTextWrappedInBoxFn = void (*)(void *, const void *, int, int,
                                      const char *, void *, double *, void *);
using CVFloatTextMultiLineFn = void (*)(void *, double, double, double, int,
                                        const char *, const char *, double,
                                        void *, double *, void *);
using CVFloatTextWrappedInBoxFn = void (*)(void *, const void *, int,
                                           const char *, const char *, double,
                                           void *, double *, void *);

CVTextFn g_cvText = nullptr;
CVTextInBoxFn g_cvTextInBox = nullptr;
CVTextWrappedInBoxFn g_cvTextWrappedInBox = nullptr;
CVFloatTextMultiLineFn g_cvFloatTextMultiLine = nullptr;
CVFloatTextWrappedInBoxFn g_cvFloatTextWrappedInBox = nullptr;
bool g_cvTextSymbolsResolved = false;

QString utf8RangeToQString(const char *begin, const char *end) {
    if (!begin)
        return {};
    if (!end) {
        const std::size_t length =
            guardedNativeCStringLength(begin, 1024 * 1024);
        return length == 0
                   ? QString()
                   : QString::fromUtf8(begin, static_cast<qsizetype>(length));
    }
    const uintptr_t beginAddress = reinterpret_cast<uintptr_t>(begin);
    const uintptr_t endAddress = reinterpret_cast<uintptr_t>(end);
    if (endAddress < beginAddress || endAddress - beginAddress > 1024 * 1024)
        return {};
    const uintptr_t length = endAddress - beginAddress;
    return QString::fromUtf8(begin, static_cast<qsizetype>(length));
}

std::string translateTextRangeToUtf8(const char *begin, const char *end) {
    if (!displayWorkRequested())
        return {};
    const QString source = utf8RangeToQString(begin, end);
    if (source.isEmpty())
        return {};
    const QString translated = displayTranslation(source);
    if (translated.isEmpty())
        return {};
    const QByteArray utf8 = translated.toUtf8();
    return std::string(utf8.constData(), static_cast<size_t>(utf8.size()));
}

void hookedCVText(void *self, int x, int y, bool invert, const char *text,
                  void *color, double *scale, void *bounds) {
    HC_OBSERVE(hookedCVText);
    if (!g_cvText)
        return;
    const std::string translated = translateTextToUtf8(text);
    g_cvText(self, x, y, invert, translated.empty() ? text : translated.c_str(),
             color, scale, bounds);
}

void hookedCVTextInBox(void *self, const void *rect, bool invert,
                       bool singleLine, const char *text, bool clip,
                       int horizontal, int vertical, void *color, double *scale,
                       void *bounds) {
    HC_OBSERVE(hookedCVTextInBox);
    if (!g_cvTextInBox)
        return;
    const std::string translated = translateTextToUtf8(text);
    g_cvTextInBox(self, rect, invert, singleLine,
                  translated.empty() ? text : translated.c_str(), clip,
                  horizontal, vertical, color, scale, bounds);
}

void hookedCVTextWrappedInBox(void *self, const void *rect, int horizontal,
                              int vertical, const char *text, void *color,
                              double *scale, void *bounds) {
    HC_OBSERVE(hookedCVTextWrappedInBox);
    if (!g_cvTextWrappedInBox)
        return;
    const std::string translated = translateTextToUtf8(text);
    g_cvTextWrappedInBox(self, rect, horizontal, vertical,
                         translated.empty() ? text : translated.c_str(), color,
                         scale, bounds);
}

void hookedCVFloatTextMultiLine(void *self, double x, double y, double z,
                                int vertical, const char *begin,
                                const char *end, double size, void *color,
                                double *scale, void *bounds) {
    HC_OBSERVE(hookedCVFloatTextMultiLine);
    if (!g_cvFloatTextMultiLine)
        return;
    const std::string translated = translateTextRangeToUtf8(begin, end);
    const char *displayBegin = translated.empty() ? begin : translated.data();
    const char *displayEnd =
        translated.empty() ? end : translated.data() + translated.size();
    g_cvFloatTextMultiLine(self, x, y, z, vertical, displayBegin, displayEnd,
                           size, color, scale, bounds);
}

void hookedCVFloatTextWrappedInBox(void *self, const void *rect, int vertical,
                                   const char *begin, const char *end,
                                   double size, void *color, double *scale,
                                   void *bounds) {
    HC_OBSERVE(hookedCVFloatTextWrappedInBox);
    if (!g_cvFloatTextWrappedInBox)
        return;
    const std::string translated = translateTextRangeToUtf8(begin, end);
    const char *displayBegin = translated.empty() ? begin : translated.data();
    const char *displayEnd =
        translated.empty() ? end : translated.data() + translated.size();
    g_cvFloatTextWrappedInBox(self, rect, vertical, displayBegin, displayEnd,
                              size, color, scale, bounds);
}

void installCVPaintBufferTextHooks() {
    if (g_cvTextSymbolsResolved)
        return;
    HMODULE cv = GetModuleHandleW(L"libCV.dll");
    if (!cv)
        return;
    // libCV has direct calls from wrapped-box helpers to text()/textWrapped-
    // InBox(). Entry detours cover those same-DLL paths as well as libOPUI's
    // imported and virtual callers.
    const bool textInstalled = resolveAndAttachImportedHook(
        cv,
        "?text@CV_PaintBuffer@@QEAAXHH_NPEBDPEAVUT_Color@@PEANPEAV?$UT_Rect@"
        "VUT_DimRectImpl@@@@@Z",
        g_cvText, hookedCVText);
    const bool textInBoxInstalled = resolveAndAttachImportedHook(
        cv,
        "?textInBox@CV_PaintBuffer@@QEAAXAEBV?$UT_Rect@VUT_DimRectImpl@@@@_"
        "N1PEBD1HHPEAVUT_Color@@PEANPEAV2@@Z",
        g_cvTextInBox, hookedCVTextInBox);
    const bool wrappedInstalled = resolveAndAttachImportedHook(
        cv,
        "?textWrappedInBox@CV_PaintBuffer@@QEAAXAEBV?$UT_Rect@VUT_DimRectImpl@@"
        "@@W4CV_HorizontalAlignment@@W4CV_VerticalAlignment@@PEBDPEAVUT_Color@@"
        "PEANPEAV2@@Z",
        g_cvTextWrappedInBox, hookedCVTextWrappedInBox);
    const bool multiLineInstalled = resolveAndAttachImportedHook(
        cv,
        "?floatTextMultiLine@CV_PaintBuffer@@QEAAXNNNW4CV_VerticalAlignment@@"
        "PEBD1NPEAVUT_Color@@PEANPEAV?$UT_BoundingRectT@N@@@Z",
        g_cvFloatTextMultiLine, hookedCVFloatTextMultiLine);
    const bool floatWrappedInstalled = resolveAndAttachImportedHook(
        cv,
        "?floatTextWrappedInBox@CV_PaintBuffer@@QEAAXAEBV?$UT_BoundingRectT@N@@"
        "W4CV_VerticalAlignment@@PEBD2NPEAVUT_Color@@PEANPEAV2@@Z",
        g_cvFloatTextWrappedInBox, hookedCVFloatTextWrappedInBox);
    g_cvTextSymbolsResolved = textInstalled && textInBoxInstalled &&
                              wrappedInstalled && multiLineInstalled &&
                              floatWrappedInstalled;
    hookLog("CVPaintBufferTextHooks: text=%d box=%d wrapped=%d multi=%d "
            "floatWrapped=%d",
            textInstalled ? 1 : 0, textInBoxInstalled ? 1 : 0,
            wrappedInstalled ? 1 : 0, multiLineInstalled ? 1 : 0,
            floatWrappedInstalled ? 1 : 0);
}

size_t g_fontHookSlotCount = 0;

bool fontSymbolsAreValid() {
    const void *ptrs[] = {
        reinterpret_cast<void *>(g_fmHAdv),
        reinterpret_cast<void *>(g_fmBRect),
        reinterpret_cast<void *>(g_fmBRectRect),
        reinterpret_cast<void *>(g_fmBRectXYWH),
        reinterpret_cast<void *>(g_fmSize),
        reinterpret_cast<void *>(g_fmTBRect),
        reinterpret_cast<void *>(g_fmElide),
        reinterpret_cast<void *>(g_fmfHAdv),
        reinterpret_cast<void *>(g_fmfBRect),
        reinterpret_cast<void *>(g_fmfBRectRect),
        reinterpret_cast<void *>(g_fmfSize),
        reinterpret_cast<void *>(g_fmfTBRect),
        reinterpret_cast<void *>(g_fmfElide),
    };
    if (!g_fmHAdv)
        return false;
    for (const void *p : ptrs) {
        if (!p)
            continue;
        if (!isExecutableAddress(p))
            return false;
    }
    return true;
}

void patchFontModules() {
    if (!g_fontHooksInstalled)
        return;
    struct SymbolHook final {
        void *original;
        void *replacement;
    };
    const SymbolHook hooks[] = {
        {reinterpret_cast<void *>(g_fmHAdv),
         reinterpret_cast<void *>(&hookedFontHAdv)},
        {reinterpret_cast<void *>(g_fmBRect),
         reinterpret_cast<void *>(&hookedFontBRect)},
        {reinterpret_cast<void *>(g_fmBRectRect),
         reinterpret_cast<void *>(&hookedFontBRectRect)},
        {reinterpret_cast<void *>(g_fmBRectXYWH),
         reinterpret_cast<void *>(&hookedFontBRectXYWH)},
        {reinterpret_cast<void *>(g_fmSize),
         reinterpret_cast<void *>(&hookedFontSize)},
        {reinterpret_cast<void *>(g_fmTBRect),
         reinterpret_cast<void *>(&hookedFontTBRect)},
        {reinterpret_cast<void *>(g_fmElide),
         reinterpret_cast<void *>(&hookedFontElide)},
        {reinterpret_cast<void *>(g_fmfHAdv),
         reinterpret_cast<void *>(&hookedFontFHAdv)},
        {reinterpret_cast<void *>(g_fmfBRect),
         reinterpret_cast<void *>(&hookedFontFBRect)},
        {reinterpret_cast<void *>(g_fmfBRectRect),
         reinterpret_cast<void *>(&hookedFontFBRectRect)},
        {reinterpret_cast<void *>(g_fmfSize),
         reinterpret_cast<void *>(&hookedFontFSize)},
        {reinterpret_cast<void *>(g_fmfTBRect),
         reinterpret_cast<void *>(&hookedFontFTBRect)},
        {reinterpret_cast<void *>(g_fmfElide),
         reinterpret_cast<void *>(&hookedFontFElide)},
    };
    const std::vector<HMODULE> modules = loadedModules();
    for (HMODULE module : modules) {
        if (module == g_moduleHandle)
            continue;
        for (const SymbolHook &hook : hooks) {
            if (hook.original)
                replaceImportInModule(module, g_fontHookSlotCount,
                                      hook.original, hook.replacement);
        }
    }
}

void installFontMeasurementHooks() {
    if (g_fontHooksInstalled)
        return;
    HMODULE qtGui = GetModuleHandleW(L"Qt6Gui.dll");
    if (!qtGui)
        return;
    g_fmHAdv = reinterpret_cast<FmHAdv>(GetProcAddress(
        qtGui, "?horizontalAdvance@QFontMetrics@@QEBAHAEBVQString@@H@Z"));
    g_fmBRect = reinterpret_cast<FmBRectRaw>(GetProcAddress(
        qtGui, "?boundingRect@QFontMetrics@@QEBA?AVQRect@@AEBVQString@@@Z"));
    g_fmBRectRect = reinterpret_cast<FmBRectRectRaw>(
        GetProcAddress(qtGui, "?boundingRect@QFontMetrics@@QEBA?AVQRect@@AEBV2@"
                              "HAEBVQString@@HPEAH@Z"));
    g_fmBRectXYWH = reinterpret_cast<FmBRectXYWHRaw>(GetProcAddress(
        qtGui,
        "?boundingRect@QFontMetrics@@QEBA?AVQRect@@HHHHHAEBVQString@@HPEAH@Z"));
    g_fmSize = reinterpret_cast<FmSizeRaw>(GetProcAddress(
        qtGui, "?size@QFontMetrics@@QEBA?AVQSize@@HAEBVQString@@HPEAH@Z"));
    g_fmTBRect = reinterpret_cast<FmTBRectRaw>(GetProcAddress(
        qtGui,
        "?tightBoundingRect@QFontMetrics@@QEBA?AVQRect@@AEBVQString@@@Z"));
    g_fmElide = reinterpret_cast<FmElideRaw>(
        GetProcAddress(qtGui, "?elidedText@QFontMetrics@@QEBA?AVQString@@AEBV2@"
                              "W4TextElideMode@Qt@@HH@Z"));
    g_fmfHAdv = reinterpret_cast<FmFHAdv>(GetProcAddress(
        qtGui, "?horizontalAdvance@QFontMetricsF@@QEBANAEBVQString@@H@Z"));
    g_fmfBRect = reinterpret_cast<FmFBRectRaw>(GetProcAddress(
        qtGui, "?boundingRect@QFontMetricsF@@QEBA?AVQRectF@@AEBVQString@@@Z"));
    g_fmfBRectRect = reinterpret_cast<FmFBRectRectRaw>(
        GetProcAddress(qtGui, "?boundingRect@QFontMetricsF@@QEBA?AVQRectF@@"
                              "AEBV2@HAEBVQString@@HPEAH@Z"));
    g_fmfSize = reinterpret_cast<FmFSizeRaw>(GetProcAddress(
        qtGui, "?size@QFontMetricsF@@QEBA?AVQSizeF@@HAEBVQString@@HPEAH@Z"));
    g_fmfTBRect = reinterpret_cast<FmFTBRectRaw>(GetProcAddress(
        qtGui,
        "?tightBoundingRect@QFontMetricsF@@QEBA?AVQRectF@@AEBVQString@@@Z"));
    g_fmfElide = reinterpret_cast<FmFElideRaw>(
        GetProcAddress(qtGui, "?elidedText@QFontMetricsF@@QEBA?AVQString@@"
                              "AEBV2@W4TextElideMode@Qt@@NH@Z"));

    g_fontHooksInstalled = fontSymbolsAreValid();
    if (g_fontHooksInstalled) {
        // Intercept before elision loses the dictionary key (e.g. material
        // icon labels). The original model/node string is never assigned to.
        tryExperimentalQtInline(g_fmElide, &hookedFontElide);
        tryExperimentalQtInline(g_fmfElide, &hookedFontFElide);
        patchFontModules();
        hookLog("FONT HOOKS INSTALLED: measure ready(slots=%zu)",
                g_fontHookSlotCount);
    } else {
        hookLog("FONT HOOKS: symbols invalid, measurement disabled");
    }
}

bool installQtQuickFontMetricHooks() {
    if (g_quickFontMetricHooksInstalled)
        return true;
    HMODULE quick = GetModuleHandleW(L"Qt6Quick.dll");
    if (!quick)
        return false;

    const bool advanceInstalled = resolveAndAttachInlineHook(
        quick, "?advanceWidth@QQuickFontMetrics@@QEBANAEBVQString@@@Z",
        g_quickFontAdvance, hookedQuickFontAdvance);
    const bool boundingInstalled = resolveAndAttachInlineHook(
        quick,
        "?boundingRect@QQuickFontMetrics@@QEBA?AVQRectF@@"
        "AEBVQString@@@Z",
        g_quickFontBoundingRect, hookedQuickFontBoundingRect);
    const bool tightInstalled = resolveAndAttachInlineHook(
        quick,
        "?tightBoundingRect@QQuickFontMetrics@@QEBA?AVQRectF@@"
        "AEBVQString@@@Z",
        g_quickFontTightBoundingRect, hookedQuickFontTightBoundingRect);
    const bool elideInstalled = resolveAndAttachInlineHook(
        quick,
        "?elidedText@QQuickFontMetrics@@QEBA?AVQString@@AEBV2@"
        "W4TextElideMode@Qt@@NH@Z",
        g_quickFontElidedText, hookedQuickFontElidedText);
    g_quickFontMetricHooksInstalled = advanceInstalled && boundingInstalled &&
                                      tightInstalled && elideInstalled;
    hookLog("installQtQuickFontMetricHooks: advance=%d bounds=%d "
            "tight=%d elide=%d",
            advanceInstalled ? 1 : 0, boundingInstalled ? 1 : 0,
            tightInstalled ? 1 : 0, elideInstalled ? 1 : 0);
    return g_quickFontMetricHooksInstalled;
}

bool installQtQuickImplicitSizeHooks() {
    if (g_quickImplicitSizeHooksInstalled)
        return true;
    HMODULE quick = GetModuleHandleW(L"Qt6Quick.dll");
    if (!quick)
        return false;

    const bool widthInstalled = resolveAndAttachInlineHook(
        quick, "?setImplicitWidth@QQuickItem@@QEAAXN@Z",
        g_quickItemSetImplicitWidth, hookedQuickItemSetImplicitWidth);
    const bool sizeInstalled = resolveAndAttachInlineHook(
        quick, "?setImplicitSize@QQuickItem@@IEAAXNN@Z",
        g_quickItemSetImplicitSize, hookedQuickItemSetImplicitSize);
    g_quickImplicitSizeHooksInstalled = widthInstalled && sizeInstalled;
    hookLog("installQtQuickImplicitSizeHooks: width=%d size=%d",
            widthInstalled ? 1 : 0, sizeInstalled ? 1 : 0);
    return g_quickImplicitSizeHooksInstalled;
}

HoudiniChineseHooks::ModuleScanState g_moduleScanState;

void patchKnownImportsInModule(HMODULE module) {
    if (!module || module == g_moduleHandle)
        return;
    for (const NativeDispatchImport &entry : g_nativeDispatchImports)
        replaceImportInModule(module, g_hookSlotCount, entry.target,
                              entry.replacement);
    const auto patch = [module](size_t &patchedSlotCount, void *original,
                                void *replacement) {
        if (original)
            replaceImportInModule(module, patchedSlotCount, original,
                                  replacement);
    };

    patch(g_hookSlotCount, reinterpret_cast<void *>(g_drawPoint),
          reinterpret_cast<void *>(&hookedDrawPoint));
    patch(g_hookSlotCount, reinterpret_cast<void *>(g_drawPointF),
          reinterpret_cast<void *>(&hookedDrawPointF));
    patch(g_hookSlotCount, reinterpret_cast<void *>(g_drawPointF2),
          reinterpret_cast<void *>(&hookedDrawPointF2));
    patch(g_hookSlotCount, reinterpret_cast<void *>(g_drawRect),
          reinterpret_cast<void *>(&hookedDrawRect));
    patch(g_hookSlotCount, reinterpret_cast<void *>(g_drawRectFOption),
          reinterpret_cast<void *>(&hookedDrawRectFOption));
    patch(g_hookSlotCount, reinterpret_cast<void *>(g_drawRectFAlign),
          reinterpret_cast<void *>(&hookedDrawRectFAlign));
    patch(g_hookSlotCount, reinterpret_cast<void *>(g_drawXY),
          reinterpret_cast<void *>(&hookedDrawXY));
    patch(g_hookSlotCount, reinterpret_cast<void *>(g_drawXYWH),
          reinterpret_cast<void *>(&hookedDrawXYWH));
    patch(g_hookSlotCount, reinterpret_cast<void *>(g_qtextLayoutDraw),
          reinterpret_cast<void *>(&hookedQTextLayoutDraw));
    patch(g_hookSlotCount, reinterpret_cast<void *>(g_drawStaticTextPF),
          reinterpret_cast<void *>(&hookedDrawStaticText));
    patch(g_fontHookSlotCount, reinterpret_cast<void *>(g_fmHAdv),
          reinterpret_cast<void *>(&hookedFontHAdv));
    patch(g_fontHookSlotCount, reinterpret_cast<void *>(g_fmBRect),
          reinterpret_cast<void *>(&hookedFontBRect));
    patch(g_fontHookSlotCount, reinterpret_cast<void *>(g_fmBRectRect),
          reinterpret_cast<void *>(&hookedFontBRectRect));
    patch(g_fontHookSlotCount, reinterpret_cast<void *>(g_fmBRectXYWH),
          reinterpret_cast<void *>(&hookedFontBRectXYWH));
    patch(g_fontHookSlotCount, reinterpret_cast<void *>(g_fmSize),
          reinterpret_cast<void *>(&hookedFontSize));
    patch(g_fontHookSlotCount, reinterpret_cast<void *>(g_fmTBRect),
          reinterpret_cast<void *>(&hookedFontTBRect));
    patch(g_fontHookSlotCount, reinterpret_cast<void *>(g_fmElide),
          reinterpret_cast<void *>(&hookedFontElide));
    patch(g_fontHookSlotCount, reinterpret_cast<void *>(g_fmfHAdv),
          reinterpret_cast<void *>(&hookedFontFHAdv));
    patch(g_fontHookSlotCount, reinterpret_cast<void *>(g_fmfBRect),
          reinterpret_cast<void *>(&hookedFontFBRect));
    patch(g_fontHookSlotCount, reinterpret_cast<void *>(g_fmfBRectRect),
          reinterpret_cast<void *>(&hookedFontFBRectRect));
    patch(g_fontHookSlotCount, reinterpret_cast<void *>(g_fmfSize),
          reinterpret_cast<void *>(&hookedFontFSize));
    patch(g_fontHookSlotCount, reinterpret_cast<void *>(g_fmfTBRect),
          reinterpret_cast<void *>(&hookedFontFTBRect));
    patch(g_fontHookSlotCount, reinterpret_cast<void *>(g_fmfElide),
          reinterpret_cast<void *>(&hookedFontFElide));
}

bool moduleScanKey(HMODULE module, HoudiniChineseHooks::ModuleKey &key) {
    HoudiniChineseImports::ModuleIdentity identity;
    if (!HoudiniChineseImports::readModuleIdentity(module, identity))
        return false;
    key = {identity.base, identity.size, identity.timestamp};
    return true;
}

void patchNewImportModules();
void rememberCurrentImportModules() {
    // Take and patch a fresh snapshot. Merely remembering modules here could
    // skip DLLs loaded between installation and the startup bookkeeping pass.
    patchNewImportModules();
}

void patchNewImportModules() {
    const std::vector<HMODULE> modules = loadedModules();
    if (modules.empty()) // Enumeration failure is not an unload notification.
        return;
    std::vector<HoudiniChineseHooks::ModuleKey> keys;
    keys.reserve(modules.size());
    std::unordered_set<std::uintptr_t> presentBases;
    presentBases.reserve(modules.size());
    for (HMODULE module : modules) {
        presentBases.insert(reinterpret_cast<std::uintptr_t>(module));
        HoudiniChineseHooks::ModuleKey key;
        if (!moduleScanKey(module, key))
            continue; // An unreadable third-party image must not block others.
        keys.push_back(key);
    }
    g_moduleScanState.reconcile(keys, presentBases);
    g_hookRegistry.forgetAbsentImporters([&presentBases](std::uintptr_t base) {
        return presentBases.find(base) != presentBases.end();
    });
    const auto now = GetTickCount64();
    for (const auto &key : keys) {
        HMODULE module = reinterpret_cast<HMODULE>(key.base);
        if (module == g_moduleHandle ||
            !g_moduleScanState.needsScan(key, g_hookRegistry.generation(), now))
            continue;
        g_importScanSucceeded = true;
        try { patchKnownImportsInModule(module); }
        catch (...) { g_importScanSucceeded = false; }
        g_moduleScanState.recordResult(key, g_hookRegistry.generation(),
                                       g_importScanSucceeded, now);
    }
}

bool isNodeInfoViewWindow(const QWindow *window) {
    return window && window->metaObject() &&
           qstrcmp(window->metaObject()->className(), "NodeInfoViewWindow") ==
               0;
}

void rememberNodeInfoViewWindow(QWindow *window) {
    if (!isNodeInfoViewWindow(window))
        return;
    for (const QPointer<QWindow> &known : g_nodeInfoWindows) {
        if (known == window)
            return;
    }
    g_nodeInfoWindows.append(QPointer<QWindow>(window));
}

void rememberQtWindow(QWindow *window) {
    if (!window)
        return;

    bool quickWindow = false;
    for (const QMetaObject *meta = window->metaObject(); meta;
         meta = meta->superClass()) {
        if (QByteArrayView(meta->className()) ==
            QByteArrayView("QQuickWindow")) {
            quickWindow = true;
            break;
        }
    }
    if (!quickWindow)
        return;

    for (const QPointer<QWindow> &known : g_knownQtWindows) {
        if (known == window)
            return;
    }
    for (QPointer<QWindow> &known : g_knownQtWindows) {
        if (!known) {
            known = window;
            return;
        }
    }
    g_knownQtWindows[g_nextKnownQtWindow++ % g_knownQtWindows.size()] = window;
}

void requestNodeInfoViewUpdates() {
    for (auto it = g_nodeInfoWindows.begin(); it != g_nodeInfoWindows.end();) {
        if (!*it) {
            it = g_nodeInfoWindows.erase(it);
            continue;
        }
        (*it)->requestUpdate();
        ++it;
    }
}

// Qt Quick does not react to QApplication's ApplicationFontChange event.
// QQuickWindow instead listens to QGuiApplication::fontDatabaseChanged. On the
// next scene-graph synchronization Qt marks every QQuickTextInterface dirty,
// rebuilds its layout, and clears the glyph caches. Triggering that existing
// signal path keeps invalidation on Qt Quick's synchronization boundary and
// avoids calling private QQuickText objects from the hook DLL.
void invalidateQtQuickTextLayouts() {
    if (auto *app = QGuiApplication::instance()) {
        QMetaObject::invokeMethod(app, "fontDatabaseChanged",
                                  Qt::DirectConnection);
    }
}

// Refresh newly shown or relaid-out widgets without enumerating Houdini's
// private widget tree.
QList<QPointer<QWidget>> g_translatedDialogs21;
void refreshDialogTitle21(QWidget *widget) {
    if (!widget || !widget->isWindow()) return;
    if (widget->property("hc21_title_busy").toBool()) return;
    const QString current = widget->windowTitle();
    QString source = widget->property("hc21_title_source").toString();
    const QString previous = widget->property("hc21_title_display").toString();
    if (source.isEmpty() || current != previous) source = current;
    const QString translation = findDictionaryExact(source);
    if (translation.isEmpty()) return;
    if (!g_translatedDialogs21.contains(widget)) g_translatedDialogs21.append(widget);
    const QString display = g_enabled.load() ? translation : source;
    widget->setProperty("hc21_title_source", source);
    widget->setProperty("hc21_title_display", display);
    widget->setProperty("hc21_title_busy", true);
    if (current != display) widget->setWindowTitle(display);
    widget->setProperty("hc21_title_busy", false);
}

void refreshNativeWindowTitle21(QWindow *window) {
    if (!window || window->property("hc21_title_busy").toBool()) return;
    const QString current = window->title();
    QString source = window->property("hc21_title_source").toString();
    if (source.isEmpty() || current != window->property("hc21_title_display").toString())
        source = current;
    const QString translation = findDictionaryExact(source);
    if (translation.isEmpty()) return;
    const QString display = g_enabled.load() ? translation : source;
    window->setProperty("hc21_title_source", source);
    window->setProperty("hc21_title_display", display);
    window->setProperty("hc21_title_busy", true);
    if (current != display) window->setTitle(display);
    window->setProperty("hc21_title_busy", false);
}

class RefreshEventFilter final : public QObject {
  public:
    using QObject::QObject;

    bool eventFilter(QObject *object, QEvent *event) override {
        const QEvent::Type type = event->type();
        if constexpr (SYS_VERSION_MAJOR_INT == 21) {
            if (type == QEvent::Show || type == QEvent::WindowTitleChange)
                refreshDialogTitle21(qobject_cast<QWidget *>(object));
        }
        const bool shortcutInputBlocked =
            g_hotkeyDialogOpen || QApplication::activeModalWidget();
        const auto language = languageShortcut_.handle(
            event, g_toggleShortcut, shortcutInputBlocked);
        if (language.activate)
            toggleLanguage();
        if (language.consumed)
            return true;
        // Menu buttons own their mouse events. This filter handles only
        // global shortcuts and ordinary display-refresh bookkeeping.
        if (type == QEvent::KeyRelease) {
            const auto *key = static_cast<QKeyEvent *>(event);
            if (g_swallowedShortcutKeys.remove(key->key())) {
                event->accept();
                return true;
            }
        }
        if (type == QEvent::ShortcutOverride || type == QEvent::KeyPress) {
            const auto *key = static_cast<QKeyEvent *>(event);
            if (!shortcutInputBlocked &&
                HoudiniChineseConfig::isOneShotCaptureShortcut(
                    key->keyCombination())) {
                event->accept();
                if (type == QEvent::KeyPress) {
                    g_swallowedShortcutKeys.insert(key->key());
                    if (!key->isAutoRepeat()) {
                        g_oneShotShortcutTarget = object;
                        QTimer::singleShot(0, QCoreApplication::instance(),
                                           [] { captureUntranslatedOnce(); });
                    }
                }
                return true;
            }
        }
        if (type == QEvent::Show || type == QEvent::Hide) {
            if (auto *window = qobject_cast<QWindow *>(object)) {
                rememberQtWindow(window);
                if constexpr (SYS_VERSION_MAJOR_INT == 21) {
                    refreshNativeWindowTitle21(window);
                }
                if (isNodeInfoViewWindow(window)) {
                    rememberNodeInfoViewWindow(window);
                    if (type == QEvent::Show) {
                        window->requestUpdate();
                        const QPointer<QWindow> safeWindow(window);
                        QTimer::singleShot(0, window, [safeWindow] {
                            if (safeWindow)
                                safeWindow->requestUpdate();
                        });
                    }
                }
            }
        }
        if (type == QEvent::Show || type == QEvent::Polish ||
            type == QEvent::LayoutRequest || type == QEvent::ActionAdded) {
            if (auto *widget = qobject_cast<QWidget *>(object)) {
                if (widget->isVisible())
                    widget->update();
            }
        }
        return false;
    }

  private:
    HoudiniChineseShortcut::State languageShortcut_;
};

// Invalidate visible top-level windows and their children at the Win32 layer.
BOOL CALLBACK ForceRepaintWindow(HWND hwnd, LPARAM) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(hwnd))
        return TRUE;
    // Houdini's node-info panels, transient editors and several private UI
    // surfaces are owned top-level windows.  They must be invalidated too;
    // excluding owned windows left them painted in the previous language.
    InvalidateRect(hwnd, nullptr, TRUE);
    RedrawWindow(hwnd, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_FRAME);
    return TRUE;
}

void forceMainWindowRepaint() { EnumWindows(ForceRepaintWindow, 0); }

using NativeSizeChangedFn = void (*)(void *);
void invalidateNativeFeelSize(void *feel, NativeSizeChangedFn sizeChanged) {
    __try {
        sizeChanged(feel);
    } __except (nativeTraversalExceptionFilter(GetExceptionCode())) {
        return;
    }
}

void invalidateNativeLayoutSizes() {
    HMODULE ui = GetModuleHandleW(L"libUI.dll");
    if (!ui || !g_uiFeelApi || !g_mainNativeWindow)
        return;
    const auto sizeChanged = reinterpret_cast<NativeSizeChangedFn>(
        GetProcAddress(ui, "?sizeChanged@UI_Feel@@QEAAXXZ"));
    if (!sizeChanged)
        return;
    void *root = guardedMenuRoot();
    if (!root)
        return;
    std::vector<void *> feels;
    HoudiniChineseNativeControl::captureFromVisibleFeels(
        &root, 1, g_uiFeelApi,
        [&](void *feel, const char *) { feels.push_back(feel); }, 100000);
    // Invalidate each child's computed min/max/preferred size before its
    // container. relayoutAllWindows only invalidates window placement and
    // otherwise continues to consume these cached UI_Feel measurements.
    for (auto it = feels.rbegin(); it != feels.rend(); ++it)
        invalidateNativeFeelSize(*it, sizeChanged);
}

// Native shelf/pane tabs retain UI_Feel layout sizes across redraws. Request
// layout through the host's window manager, without resizing desktop windows
// or traversing private object memory. The engine loader gates this ABI to the
// matching Houdini build; older/missing exports leave the redraw fallback intact.
void relayoutAllHoudiniWindows() {
    if (!QCoreApplication::instance() ||
        QThread::currentThread() != QCoreApplication::instance()->thread())
        return;
    using RelayoutAllWindowsFn = void (*)(bool);
    static RelayoutAllWindowsFn relayoutAll = nullptr;
    if (!relayoutAll) {
        if (HMODULE libUI = GetModuleHandleW(L"libUI.dll")) {
            relayoutAll = reinterpret_cast<RelayoutAllWindowsFn>(
                GetProcAddress(libUI, "?relayoutAllWindows@UI_Window@@SAX_N@Z"));
        }
    }
    if (!relayoutAll)
        return;
    __try {
        relayoutAll(false);
    } __except (nativeTraversalExceptionFilter(GetExceptionCode())) {
        hookLog("language layout: native relayout failed");
        return;
    }
    hookLog("language layout: native relayout requested (enabled=%d)",
            g_enabled.load() ? 1 : 0);
}

// Houdini's native panels are UI_Window instances managed by libUI, not Qt
// widget children.  libUI publicly exports this static redraw entry point; it
// drives Houdini's own UI_EVENT_REDRAW path and therefore reaches private
// windows such as the node-info panel.
void redrawAllHoudiniWindows() {
    using RedrawAllWindowsFn = void (*)();
    static RedrawAllWindowsFn redrawAll = nullptr;
    if (!redrawAll) {
        if (HMODULE libUI = GetModuleHandleW(L"libUI.dll")) {
            redrawAll = reinterpret_cast<RedrawAllWindowsFn>(
                GetProcAddress(libUI, "?redrawAllWindows@UI_Window@@SAXXZ"));
        }
    }
    if (!redrawAll)
        return;
    __try {
        redrawAll();
    } __except (nativeTraversalExceptionFilter(GetExceptionCode())) {
        // A redraw request is best effort and must never take down the host.
        return;
    }
}

// ---------------------------------------------------------------------------
// Shift+~ one-shot untranslated UI scan.
//
// Paint interception alone sees only the current frame. The explicit walk
// below also reaches hidden pages, collapsed tree branches, complete item
// models and lazily populated menus/combo popups. All temporary expansion is
// restored before the result is shown, and capture is disabled again as soon
// as the final asynchronous repaint has passed through the hooks.
// ---------------------------------------------------------------------------
void captureUiCandidate(const QString &text) {
    const QString trimmed = text.trimmed();
    if (trimmed.size() <= 1 || containsEastAsianText(trimmed) ||
        !g_dictionaryReady.load(std::memory_order_acquire)) {
        return;
    }
    if (!lookupTranslation(trimmed).isEmpty())
        return;
    const HoudiniChineseDisplayText::LookupKey fallback =
        HoudiniChineseDisplayText::normalizedFallbackKey(trimmed);
    HoudiniChineseCapture::submit(fallback.text);
}

void captureNativeCString(const char *text) {
    const std::size_t length = guardedNativeCStringLength(text);
    if (length == 0)
        return;
    captureUiCandidate(QString::fromUtf8(text, static_cast<qsizetype>(length)));
}

void captureNativeLookText(void *look) {
    if (!look)
        return;
    if (guardedNativeHasBase(look, ".?AVUI_Text@@")) {
        captureNativeCString(guardedVirtualCString(look, g_uiTextGetTextSlot));
    }
    if (guardedNativeHasBase(look, ".?AVUI_FormattedText@@")) {
        captureNativeCString(
            guardedVirtualCString(look, g_uiFormattedTextGetTextSlot));
    }
    if (guardedNativeHasBase(look, ".?AVUI_HelpTipLook@@")) {
        captureNativeCString(
            guardedNativeCStringCall(g_uiHelpTipGetTitle, look));
        captureNativeCString(
            guardedNativeCStringCall(g_uiHelpTipGetText, look));
    }
    if (guardedNativeHasBase(look, ".?AVUI_CollapseButtonLook@@")) {
        captureNativeCString(
            guardedNativeCStringCall(g_uiCollapseButtonGetLabel, look));
    }
}

void captureNativeUIFeelHierarchies() {
    if (!g_uiFeelApi)
        return;

    g_visibleUIFeelCount.store(0, std::memory_order_release);
    g_visibleUILookCount.store(0, std::memory_order_release);
    ScopedNativeCollection collection(g_collectVisibleUIFeels);
    redrawAllHoudiniWindows();
    forceMainWindowRepaint();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::UpdateRequest);
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 250);
    collection.stop();
    if (!waitForNativeCaptureWriters())
        return;

    const std::size_t lookCount =
        (std::min)(g_visibleUILookCount.load(std::memory_order_acquire),
                   g_visibleUILooks.size());
    std::unordered_set<void *> uniqueLooks;
    uniqueLooks.reserve(lookCount);
    for (std::size_t index = 0; index < lookCount; ++index) {
        void *look = g_visibleUILooks[index].load(std::memory_order_acquire);
        if (look && uniqueLooks.insert(look).second)
            captureNativeLookText(look);
    }

    const std::size_t count =
        (std::min)(g_visibleUIFeelCount.load(std::memory_order_acquire),
                   g_visibleUIFeels.size());
    std::vector<void *> feelSeeds;
    feelSeeds.reserve(count + 128);
    std::unordered_set<void *> queuedFeels;
    queuedFeels.reserve(count + 128);
    // Main-window traversal does not require a renderMe trampoline. This is
    // the same read-only, ABI-checked root already used by the menu buttons.
    // Keep the main hierarchy available when low-address inline hooks fail.
    if (g_mainNativeWindow) {
        if (void *root = guardedMenuRoot()) {
            queuedFeels.insert(root);
            feelSeeds.push_back(root);
        }
    }
    for (std::size_t index = 0; index < count; ++index) {
        void *feel = g_visibleUIFeels[index].load(std::memory_order_acquire);
        if (feel && queuedFeels.insert(feel).second)
            feelSeeds.push_back(feel);
    }

    std::vector<void *> itemLists;
    itemLists.reserve(128);
    std::unordered_set<void *> queuedItemLists;
    queuedItemLists.reserve(128);
    std::vector<void *> stringChooserQueries;
    stringChooserQueries.reserve(128);
    std::unordered_set<void *> queuedStringChooserQueries;
    queuedStringChooserQueries.reserve(128);
    const auto queueFeel = [&](void *feel) {
        if (feel && queuedFeels.insert(feel).second)
            feelSeeds.push_back(feel);
    };
    const auto queueItemList = [&](void *list) {
        if (list && queuedItemLists.insert(list).second)
            itemLists.push_back(list);
    };
    const auto queueStringChooserQuery = [&](void *query) {
        if (query && guardedIsStringChooserQuery(query) &&
            queuedStringChooserQueries.insert(query).second) {
            stringChooserQueries.push_back(query);
        }
    };

    std::size_t totalRoots = 0;
    std::size_t totalNodes = 0;
    bool truncated = false;
    const auto captureFeel = [&](void *feel, const char *rawClassName) {
        const std::size_t classNameLength =
            guardedNativeCStringLength(rawClassName, 512);
        if (classNameLength == 0)
            return;
        const QByteArray className(rawClassName,
                                   static_cast<qsizetype>(classNameLength));

        const bool isTriggerButton =
            guardedNativeHasBase(feel, ".?AVUI_TriggerButton@@");
        const bool isLabeledFeel =
            guardedNativeHasBase(feel, ".?AVUI_LabeledFeel@@");

        if (className.endsWith("MenuItem") || isTriggerButton ||
            className == "UI_MenuBarItem" || className == "UI_TriggerButton") {
            captureNativeCString(
                guardedNativeCStringCall(g_uiMenuItemGetLabel, feel));
        } else if (isLabeledFeel) {
            captureNativeCString(
                guardedNativeCStringCall(g_uiLabeledFeelGetLabel, feel));
        } else if (className == "UI_GroupFeel") {
            captureNativeCString(
                guardedNativeCStringCall(g_uiGroupFeelGetName, feel));
        } else if (className == "UI_TextFeel") {
            void *look = guardedNativePointerCall(g_uiTextFeelGetLook, feel);
            captureNativeCString(
                guardedVirtualCString(look, g_uiTextGetTextSlot));
        } else if (className == "UI_Chooser") {
            queueStringChooserQuery(
                guardedNativePointerCall(g_uiChooserGetQuery, feel));
        }

        if (className == "UI_ParentMenuItem") {
            void *menuWindow =
                guardedNativePointerCall(g_uiParentMenuItemGetMenuWindow, feel);
            queueFeel(menuWindow);
            queueFeel(guardedNativePointerCall(g_uiMenuWindowGetFirstMenu,
                                               menuWindow));
        }
        if (className == "UI_MenuBarItem") {
            queueFeel(guardedNativePointerCall(g_uiMenuBarItemGetMenu, feel));
        } else if (className == "UI_MenuFeel") {
            queueFeel(
                guardedNativePointerCall(g_uiMenuFeelGetStaticMenu, feel));
            queueItemList(
                guardedNativePointerCall(g_uiMenuFeelGetDynamicMenu, feel));
        } else if (className == "UI_DynamicMenuStrip") {
            queueItemList(
                guardedNativePointerCall(g_uiDynamicMenuStripGetList, feel));
        }
    };

    // A menu item can lead to a separately owned UI_MenuWindow rather than a
    // normal UI_Feel child. Process newly discovered menu roots in batches;
    // the queued set prevents cycles back into the window hierarchy.
    std::size_t seedOffset = 0;
    while (seedOffset < feelSeeds.size() && totalNodes < 1000000) {
        const std::size_t end = feelSeeds.size();
        std::vector<void *> batch(
            feelSeeds.begin() + static_cast<std::ptrdiff_t>(seedOffset),
            feelSeeds.begin() + static_cast<std::ptrdiff_t>(end));
        seedOffset = end;
        const auto result =
            HoudiniChineseNativeControl::captureFromVisibleFeels(
                batch.data(), batch.size(), g_uiFeelApi, captureFeel,
                1000000 - totalNodes);
        totalRoots += result.roots;
        totalNodes += result.nodes;
        truncated = truncated || result.truncated;
    }
    truncated = truncated || seedOffset < feelSeeds.size();

    HoudiniChineseNativeControl::ItemListResult itemResult;
    if (!itemLists.empty() && g_uiItemListApi) {
        itemResult = HoudiniChineseNativeControl::captureItemLists(
            itemLists.data(), itemLists.size(), g_uiItemListApi,
            [](const char *text) { captureNativeCString(text); });
    }
    std::size_t chooserEntries = 0;
    for (void *query : stringChooserQueries) {
        const int entryCount = guardedStringChooserEntryCount(query);
        for (int index = 0; index < entryCount && chooserEntries < 1000000;
             ++index, ++chooserEntries) {
            captureNativeCString(guardedStringChooserEntryText(query, index));
        }
    }
    hookLog("NativeFeelCapture: visible=%zu looks=%zu roots=%zu nodes=%zu "
            "lists=%zu entries=%zu chooser_entries=%zu truncated=%d",
            feelSeeds.size(), uniqueLooks.size(), totalRoots, totalNodes,
            itemResult.lists, itemResult.entries, chooserEntries,
            (truncated || itemResult.truncated) ? 1 : 0);
}

void captureNativeSITreeHierarchies() {
    if (!g_siTreeDoRenderNode || !g_siTreeNodeApi)
        return;

    g_visibleSITreeNodeCount.store(0, std::memory_order_release);
    ScopedNativeCollection collection(g_collectVisibleSITreeNodes);
    redrawAllHoudiniWindows();
    forceMainWindowRepaint();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::UpdateRequest);
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 250);
    collection.stop();
    if (!waitForNativeCaptureWriters())
        return;

    const std::size_t count =
        (std::min)(g_visibleSITreeNodeCount.load(std::memory_order_acquire),
                   g_visibleSITreeNodes.size());
    std::vector<void *> visibleNodes;
    visibleNodes.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        if (void *node =
                g_visibleSITreeNodes[index].load(std::memory_order_acquire)) {
            visibleNodes.push_back(node);
        }
    }

    const HoudiniChineseNativeTree::Result result =
        HoudiniChineseNativeTree::captureFromVisibleNodes(
            visibleNodes.data(), visibleNodes.size(), g_siTreeNodeApi,
            [](const char *text) { captureNativeCString(text); });
    hookLog(
        "SITreeCapture: visible=%zu roots=%zu nodes=%zu texts=%zu truncated=%d",
        visibleNodes.size(), result.roots, result.nodes, result.texts,
        result.truncated ? 1 : 0);
}

bool isProtectedQuickTextObject(const QObject *object) {
    return HoudiniChineseTextContext::quickRole(object) == TextRole::UserValue;
}

void captureTextVariant(const QVariant &value, int depth,
                        qsizetype &remaining) {
    if (!value.isValid() || depth > 8 || remaining <= 0)
        return;
    --remaining;

    const QMetaType type = value.metaType();
    if (type == QMetaType::fromType<QString>()) {
        captureUiCandidate(value.toString());
        return;
    }
    if (type == QMetaType::fromType<QStringList>()) {
        for (const QString &text : value.toStringList())
            captureUiCandidate(text);
        return;
    }
    if (type == QMetaType::fromType<QVariantList>()) {
        for (const QVariant &entry : value.toList())
            captureTextVariant(entry, depth + 1, remaining);
        return;
    }
    if (type == QMetaType::fromType<QVariantMap>()) {
        static constexpr const char *textKeys[] = {
            "text", "label",    "title",   "description",
            "help", "helpText", "toolTip", "placeholderText",
        };
        const QVariantMap map = value.toMap();
        for (const char *key : textKeys)
            captureTextVariant(map.value(QString::fromLatin1(key)), depth + 1,
                               remaining);
        return;
    }
    if (type == QMetaType::fromType<QVariantHash>()) {
        const QVariantHash hash = value.toHash();
        for (auto it = hash.constBegin(); it != hash.constEnd(); ++it) {
            const QString &key = it.key();
            if (key == QStringLiteral("text") ||
                key == QStringLiteral("label") ||
                key == QStringLiteral("title") ||
                key == QStringLiteral("description") ||
                key == QStringLiteral("help") ||
                key == QStringLiteral("helpText") ||
                key == QStringLiteral("toolTip") ||
                key == QStringLiteral("placeholderText")) {
                captureTextVariant(it.value(), depth + 1, remaining);
            }
        }
        return;
    }
    if (type != QMetaType::fromType<QJSValue>())
        return;

    const QVariant converted = value.value<QJSValue>().toVariant();
    if (converted.metaType() != QMetaType::fromType<QJSValue>())
        captureTextVariant(converted, depth + 1, remaining);
}

void captureCommonObjectText(QObject *object, qsizetype &remaining) {
    if (!object)
        return;
    static constexpr const char *properties[] = {
        "label",           "title",          "windowTitle",
        "description",     "help",           "helpText",
        "toolTip",         "statusTip",      "whatsThis",
        "placeholderText", "accessibleName", "accessibleDescription",
        "subTitle",        "message",
    };
    const auto *widget = qobject_cast<const QWidget *>(object);
    if (!isProtectedQuickTextObject(object) &&
        !(widget && (isProtectedTextWidget(widget)))) {
        captureTextVariant(object->property("text"), 0, remaining);
        captureTextVariant(object->property("displayText"), 0, remaining);
    }
    for (const char *name : properties)
        captureTextVariant(object->property(name), 0, remaining);

    // QML preference controls keep unopened combo/strip entries in JavaScript
    // arrays. Reading those backing properties follows the control hierarchy
    // without opening a popup or changing the selected value.
    captureTextVariant(object->property("options"), 0, remaining);
    captureTextVariant(object->property("labels"), 0, remaining);
}

void collectObjectTree(QObject *object, std::vector<QWidget *> &widgets,
                       std::vector<QAction *> &actions,
                       std::unordered_set<QObject *> &visitedObjects) {
    // Avoid QApplication::allWidgets()/topLevelWidgets(): both return a Qt
    // container by value across the plug-in boundary, and both crash dumps
    // ended while that temporary container was being destroyed. QObject's
    // child list is returned by reference and still reaches hidden pages,
    // collapsed controls, menus, models and actions owned by each root.
    constexpr std::size_t kMaxObjects = 1000000;
    std::vector<QObject *> pending;
    pending.push_back(object);
    while (!pending.empty() && visitedObjects.size() < kMaxObjects) {
        QObject *current = pending.back();
        pending.pop_back();
        if (!current || !visitedObjects.insert(current).second)
            continue;
        if (auto *widget = qobject_cast<QWidget *>(current))
            widgets.push_back(widget);
        if (auto *action = qobject_cast<QAction *>(current))
            actions.push_back(action);
        const QObjectList &children = current->children();
        for (auto iterator = children.crbegin(); iterator != children.crend();
             ++iterator) {
            pending.push_back(*iterator);
        }
    }
}

void collectQuickItemTree(QQuickItem *item, std::vector<QWidget *> &widgets,
                          std::vector<QAction *> &actions,
                          std::unordered_set<QObject *> &visitedObjects,
                          std::unordered_set<QQuickItem *> &visitedQuickItems) {
    // A QQuickItem's visual parent and QObject owner need not be the same.
    // Walk both graphs: QObject ownership finds helper objects and models,
    // while childItems() is the authoritative scene hierarchy used to paint
    // labels, hidden preference rows and folded groups.
    constexpr std::size_t kMaxQuickItems = 1000000;
    std::vector<QQuickItem *> pending;
    pending.push_back(item);
    while (!pending.empty() && visitedQuickItems.size() < kMaxQuickItems) {
        QQuickItem *current = pending.back();
        pending.pop_back();
        if (!current || !visitedQuickItems.insert(current).second)
            continue;
        collectObjectTree(current, widgets, actions, visitedObjects);
        const QList<QQuickItem *> children = current->childItems();
        for (auto iterator = children.crbegin(); iterator != children.crend();
             ++iterator) {
            pending.push_back(*iterator);
        }
    }
}

void collectQuickWindowTree(
    QQuickWindow *window, std::vector<QWidget *> &widgets,
    std::vector<QAction *> &actions,
    std::unordered_set<QObject *> &visitedObjects,
    std::unordered_set<QQuickItem *> &visitedQuickItems) {
    if (!window)
        return;
    collectObjectTree(window, widgets, actions, visitedObjects);
    collectQuickItemTree(window->contentItem(), widgets, actions,
                         visitedObjects, visitedQuickItems);
}

void refreshQuickTextItemTree(
    QQuickItem *item, std::unordered_set<QQuickItem *> &visitedQuickItems) {
    constexpr std::size_t kMaxQuickItems = 1000000;
    std::vector<QPointer<QQuickItem>> pending;
    pending.push_back(item);
    while (!pending.empty() && visitedQuickItems.size() < kMaxQuickItems) {
        const QPointer<QQuickItem> current = pending.back();
        pending.pop_back();
        if (!current || !visitedQuickItems.insert(current).second)
            continue;

        if (isQuickTextObject(current)) {
            const QString source = current->property("text").toString();
            const QString translated = displayQuickLabelTranslation(source);
            if (current && !translated.isEmpty()) {
                // Qt 6.8.3 QQuickText::ensureDoc() parents its rich-text
                // document directly to the QQuickText. Re-submit the unchanged
                // public source so the document hook rebuilds translated size.
                const QObjectList &children = current->children();
                for (QObject *child : children) {
                    if (auto *document = qobject_cast<QTextDocument *>(child)) {
                        document->setHtml(source);
                        break;
                    }
                }
                if (!current) continue;
                QMetaObject::invokeMethod(current, "forceLayout",
                                          Qt::DirectConnection);
            }
        }

        if (!current) continue; // Layout/document callbacks can delete this item.
        const QList<QQuickItem *> children = current->childItems();
        for (auto iterator = children.crbegin(); iterator != children.crend();
             ++iterator) {
            pending.push_back(*iterator);
        }
    }
}

bool g_quickTextRefreshScheduled = false;

void scheduleExistingQtQuickTextRefresh() {
    if (g_quickTextRefreshScheduled || !g_quickFontMetricHooksInstalled ||
        !g_quickLayoutInputHooksInstalled ||
        !g_quickImplicitSizeHooksInstalled || !g_qsgInlineAddTextLayout ||
        !g_qsgInlineAddTextDocument) {
        return;
    }
    auto *app = QGuiApplication::instance();
    if (!app)
        return;

    g_quickTextRefreshScheduled = true;
    QTimer::singleShot(0, app, [] {
        invalidateQtQuickTextLayouts();
        std::unordered_set<QQuickItem *> visitedQuickItems;
        visitedQuickItems.reserve(8192);
        QList<QPointer<QWindow>> windows;
        for (auto *window : QGuiApplication::allWindows()) windows.append(window);
        for (const auto &window : windows) {
            if (auto *quickWindow = qobject_cast<QQuickWindow *>(window.data())) {
                const QPointer<QQuickWindow> safeWindow(quickWindow);
                refreshQuickTextItemTree(quickWindow->contentItem(),
                                         visitedQuickItems);
                if (safeWindow) safeWindow->requestUpdate();
            }
        }
    });
}

void collectWidgetRoot(QWidget *widget, std::vector<QWidget *> &widgets,
                       std::vector<QAction *> &actions,
                       std::unordered_set<QObject *> &visitedObjects) {
    if (!widget)
        return;
    while (widget->parentWidget())
        widget = widget->parentWidget();
    collectObjectTree(widget, widgets, actions, visitedObjects);
}

struct NativeWidgetRootScan final {
    DWORD processId = 0;
    std::vector<QWidget *> *widgets = nullptr;
    std::vector<QAction *> *actions = nullptr;
    std::unordered_set<QObject *> *visitedObjects = nullptr;
};

BOOL CALLBACK collectNativeWidgetRoot(HWND window, LPARAM parameter) {
    auto *scan = reinterpret_cast<NativeWidgetRootScan *>(parameter);
    if (!scan || !scan->widgets || !scan->actions || !scan->visitedObjects) {
        return TRUE;
    }
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId != scan->processId)
        return TRUE;
    if (QWidget *widget = QWidget::find(
            static_cast<WId>(reinterpret_cast<quintptr>(window)))) {
        collectWidgetRoot(widget, *scan->widgets, *scan->actions,
                          *scan->visitedObjects);
    }
    return TRUE;
}

void collectKnownWidgetTrees(QApplication *app, std::vector<QWidget *> &widgets,
                             std::vector<QAction *> &actions,
                             std::unordered_set<QObject *> &visitedObjects) {
    if (!app)
        return;
    collectWidgetRoot(app->activeWindow(), widgets, actions, visitedObjects);
    collectWidgetRoot(app->activeModalWidget(), widgets, actions,
                      visitedObjects);
    collectWidgetRoot(app->activePopupWidget(), widgets, actions,
                      visitedObjects);
    collectWidgetRoot(app->focusWidget(), widgets, actions, visitedObjects);
    collectObjectTree(g_oneShotShortcutTarget.data(), widgets, actions,
                      visitedObjects);
    rememberQtWindow(QGuiApplication::focusWindow());
    std::unordered_set<QQuickItem *> visitedQuickItems;
    visitedQuickItems.reserve(8192);

    // This is the public, process-wide Qt Quick root inventory.  It catches
    // independent QML windows such as Houdini 22's Preferences dialog even
    // when the shortcut was received by the main native Houdini window.
    const QWindowList allWindows = QGuiApplication::allWindows();
    for (QWindow *window : allWindows) {
        if (auto *quickWindow = qobject_cast<QQuickWindow *>(window)) {
            rememberQtWindow(quickWindow);
            collectQuickWindowTree(quickWindow, widgets, actions,
                                   visitedObjects, visitedQuickItems);
        }
    }
    for (const QPointer<QWindow> &window : g_knownQtWindows) {
        if (auto *quickWindow = qobject_cast<QQuickWindow *>(window.data())) {
            collectQuickWindowTree(quickWindow, widgets, actions,
                                   visitedObjects, visitedQuickItems);
        }
    }
    NativeWidgetRootScan scan{
        GetCurrentProcessId(),
        &widgets,
        &actions,
        &visitedObjects,
    };
    EnumWindows(collectNativeWidgetRoot, reinterpret_cast<LPARAM>(&scan));
}

void captureActionText(QAction *action, QSet<QAction *> &visitedActions,
                       std::vector<QPointer<QMenu>> &pendingMenus) {
    if (!action || visitedActions.contains(action))
        return;
    visitedActions.insert(action);
    captureUiCandidate(action->text());
    captureUiCandidate(action->iconText());
    captureUiCandidate(action->toolTip());
    captureUiCandidate(action->statusTip());
    captureUiCandidate(action->whatsThis());
    if (QMenu *menu = action->menu())
        pendingMenus.emplace_back(menu);
}

void captureModelTree(QAbstractItemModel *model, qsizetype &remaining) {
    if (!model || remaining <= 0)
        return;
    constexpr int roles[] = {
        Qt::DisplayRole,
        Qt::ToolTipRole,
        Qt::StatusTipRole,
        Qt::WhatsThisRole,
    };
    std::vector<QPersistentModelIndex> pendingParents{
        QPersistentModelIndex{},
    };
    QSet<QPersistentModelIndex> visitedBranches;
    while (!pendingParents.empty() && remaining > 0) {
        const QModelIndex parent = pendingParents.back();
        pendingParents.pop_back();
        const int rows = (std::max)(0, model->rowCount(parent));
        const int columns = (std::max)(1, model->columnCount(parent));
        for (int row = 0; row < rows && remaining > 0; ++row) {
            for (int column = 0; column < columns && remaining > 0; ++column) {
                const QModelIndex index = model->index(row, column, parent);
                if (!index.isValid())
                    continue;
                --remaining;
                for (const int role : roles)
                    captureUiCandidate(model->data(index, role).toString());
            }
            const QModelIndex branch = model->index(row, 0, parent);
            const QPersistentModelIndex persistentBranch(branch);
            if (branch.isValid() && model->hasChildren(branch) &&
                !visitedBranches.contains(persistentBranch)) {
                visitedBranches.insert(persistentBranch);
                pendingParents.push_back(persistentBranch);
            }
        }
    }
}

void captureWholeModel(QAbstractItemModel *model,
                       QSet<QAbstractItemModel *> &visitedModels) {
    if (!model || visitedModels.contains(model))
        return;
    visitedModels.insert(model);
    constexpr int roles[] = {
        Qt::DisplayRole,
        Qt::ToolTipRole,
        Qt::StatusTipRole,
        Qt::WhatsThisRole,
    };
    const int columns = (std::max)(1, model->columnCount());
    const int rows = (std::max)(1, model->rowCount());
    for (int column = 0; column < columns; ++column) {
        for (const int role : roles)
            captureUiCandidate(
                model->headerData(column, Qt::Horizontal, role).toString());
    }
    for (int row = 0; row < rows; ++row) {
        for (const int role : roles)
            captureUiCandidate(
                model->headerData(row, Qt::Vertical, role).toString());
    }
    // A hard ceiling protects Houdini from malformed/cyclic third-party
    // models while remaining far above a normal UI inventory.
    qsizetype remaining = 1000000;
    captureModelTree(model, remaining);
}

void collectExpandedTreeIndexes(QTreeView *view, QAbstractItemModel *model,
                                QList<QPersistentModelIndex> &expanded,
                                qsizetype &remaining) {
    if (!view || !model || remaining <= 0)
        return;
    std::vector<QPersistentModelIndex> pendingParents{
        QPersistentModelIndex{},
    };
    QSet<QPersistentModelIndex> visitedBranches;
    while (!pendingParents.empty() && remaining > 0) {
        const QModelIndex parent = pendingParents.back();
        pendingParents.pop_back();
        const int rows = (std::max)(0, model->rowCount(parent));
        for (int row = 0; row < rows && remaining > 0; ++row) {
            --remaining;
            const QModelIndex index = model->index(row, 0, parent);
            if (!index.isValid())
                continue;
            if (view->isExpanded(index))
                expanded.append(QPersistentModelIndex(index));
            const QPersistentModelIndex persistentIndex(index);
            if (model->hasChildren(index) &&
                !visitedBranches.contains(persistentIndex)) {
                visitedBranches.insert(persistentIndex);
                pendingParents.push_back(persistentIndex);
            }
        }
    }
}

void renderWidgetForCapture(QWidget *widget) {
    if (!widget || widget->width() <= 0 || widget->height() <= 0 ||
        isProtectedTextWidget(widget)) {
        return;
    }
    widget->ensurePolished();
    if (widget->layout())
        widget->layout()->activate();
    constexpr int tileSize = 1536;
    for (int y = 0; y < widget->height(); y += tileSize) {
        for (int x = 0; x < widget->width(); x += tileSize) {
            const QRect source(x, y, (std::min)(tileSize, widget->width() - x),
                               (std::min)(tileSize, widget->height() - y));
            QPixmap pixmap(source.size());
            pixmap.fill(Qt::transparent);
            widget->render(&pixmap, QPoint(), source,
                           QWidget::DrawWindowBackground |
                               QWidget::DrawChildren);
        }
    }
}

void captureAccessibleTree(QAccessibleInterface *interface,
                           QSet<QObject *> &visitedObjects,
                           qsizetype &remaining) {
    if (!interface || remaining <= 0)
        return;
    constexpr QAccessible::Text kinds[] = {
        QAccessible::Name, QAccessible::Description, QAccessible::Value,
        QAccessible::Help, QAccessible::Accelerator,
    };
    std::vector<QAccessibleInterface *> pending{interface};
    QSet<QAccessibleInterface *> queued{interface};
    while (!pending.empty() && remaining > 0) {
        QAccessibleInterface *current = pending.back();
        pending.pop_back();
        if (!current)
            continue;
        QObject *object = current->object();
        if (object && visitedObjects.contains(object))
            continue;
        if (object)
            visitedObjects.insert(object);
        --remaining;
        for (const QAccessible::Text kind : kinds)
            captureUiCandidate(current->text(kind));

        const int childCount = (std::max)(0, current->childCount());
        const qsizetype available = (std::max)(
            qsizetype{0}, remaining - static_cast<qsizetype>(pending.size()));
        const int limit = static_cast<int>(
            (std::min)(static_cast<qsizetype>(childCount), available));
        for (int index = limit - 1; index >= 0; --index) {
            QAccessibleInterface *child = current->child(index);
            if (child && !queued.contains(child)) {
                queued.insert(child);
                pending.push_back(child);
            }
        }
    }
}

void expandAndCaptureMenu(QMenu *menu, QSet<QMenu *> &visitedMenus,
                          QSet<QAction *> &visitedActions,
                          std::vector<QPointer<QMenu>> &pendingMenus) {
    if (!menu || visitedMenus.contains(menu))
        return;
    visitedMenus.insert(menu);
    captureUiCandidate(menu->title());

    // Show the menu through its public control path so aboutToShow handlers can
    // build dynamic entries. WA_DontShowOnScreen prevents a forest of popups
    // from flashing while preserving the same population/layout lifecycle.
    const bool wasVisible = menu->isVisible();
    const bool wasOffscreen = menu->testAttribute(Qt::WA_DontShowOnScreen);
    const QPointer<QMenu> guardedMenu(menu);
    if (!wasVisible) {
        menu->setAttribute(Qt::WA_DontShowOnScreen, true);
        menu->popup(QPoint(-32000, -32000));
    }
    if (!guardedMenu)
        return;
    menu = guardedMenu.data();
    menu->ensurePolished();
    menu->adjustSize();
    std::vector<QWidget *> childWidgets;
    std::vector<QAction *> childActions;
    std::unordered_set<QObject *> childObjects;
    collectObjectTree(menu, childWidgets, childActions, childObjects);
    for (QAction *action : childActions)
        captureActionText(action, visitedActions, pendingMenus);
    renderWidgetForCapture(menu);
    if (!wasVisible) {
        menu->hide();
        if (guardedMenu)
            guardedMenu->setAttribute(Qt::WA_DontShowOnScreen, wasOffscreen);
    }
}

void expandAndCaptureCombo(QComboBox *combo,
                           QSet<QAbstractItemModel *> &visitedModels) {
    if (!combo)
        return;
    captureUiCandidate(combo->placeholderText());
    for (int index = 0; index < combo->count(); ++index) {
        captureUiCandidate(combo->itemText(index));
        captureUiCandidate(combo->itemData(index, Qt::ToolTipRole).toString());
        captureUiCandidate(
            combo->itemData(index, Qt::StatusTipRole).toString());
        captureUiCandidate(
            combo->itemData(index, Qt::WhatsThisRole).toString());
    }
    captureWholeModel(combo->model(), visitedModels);

    if (!combo->view())
        return;
    QWidget *popup = combo->view()->window();
    const bool wasOffscreen =
        popup && popup->testAttribute(Qt::WA_DontShowOnScreen);
    if (popup)
        popup->setAttribute(Qt::WA_DontShowOnScreen, true);
    const QSignalBlocker comboBlocker(combo);
    combo->showPopup();
    combo->view()->doItemsLayout();
    renderWidgetForCapture(combo->view());
    combo->hidePopup();
    if (popup)
        popup->setAttribute(Qt::WA_DontShowOnScreen, wasOffscreen);
}

void captureWidgetInventory(QWidget *widget,
                            QSet<QAbstractItemModel *> &visitedModels,
                            std::vector<QPointer<QMenu>> &pendingMenus) {
    if (!widget)
        return;
    qsizetype objectTextRemaining = 1000000;
    captureCommonObjectText(widget, objectTextRemaining);

    if (auto *label = qobject_cast<QLabel *>(widget))
        captureUiCandidate(label->text());
    if (auto *button = qobject_cast<QAbstractButton *>(widget))
        captureUiCandidate(button->text());
    if (auto *group = qobject_cast<QGroupBox *>(widget))
        captureUiCandidate(group->title());
    if (auto *progress = qobject_cast<QProgressBar *>(widget))
        captureUiCandidate(progress->format());
    if (auto *status = qobject_cast<QStatusBar *>(widget))
        captureUiCandidate(status->currentMessage());
    if (auto *wizardPage = qobject_cast<QWizardPage *>(widget)) {
        captureUiCandidate(wizardPage->title());
        captureUiCandidate(wizardPage->subTitle());
    }
    if (auto *textEdit = qobject_cast<QTextEdit *>(widget)) {
        if (textEdit->isReadOnly())
            captureUiCandidate(textEdit->toPlainText());
    }
    if (auto *plainEdit = qobject_cast<QPlainTextEdit *>(widget)) {
        if (plainEdit->isReadOnly())
            captureUiCandidate(plainEdit->toPlainText());
    }
    if (auto *tabBar = qobject_cast<QTabBar *>(widget)) {
        for (int index = 0; index < tabBar->count(); ++index) {
            captureUiCandidate(tabBar->tabText(index));
            captureUiCandidate(tabBar->tabToolTip(index));
            captureUiCandidate(tabBar->tabWhatsThis(index));
        }
    }
    if (auto *toolBox = qobject_cast<QToolBox *>(widget)) {
        const QSignalBlocker blocker(toolBox);
        const int previous = toolBox->currentIndex();
        for (int index = 0; index < toolBox->count(); ++index) {
            captureUiCandidate(toolBox->itemText(index));
            captureUiCandidate(toolBox->itemToolTip(index));
            toolBox->setCurrentIndex(index);
            renderWidgetForCapture(toolBox->widget(index));
        }
        toolBox->setCurrentIndex(previous);
    }
    if (auto *tabs = qobject_cast<QTabWidget *>(widget)) {
        const QSignalBlocker blocker(tabs);
        const int previous = tabs->currentIndex();
        for (int index = 0; index < tabs->count(); ++index) {
            captureUiCandidate(tabs->tabText(index));
            captureUiCandidate(tabs->tabToolTip(index));
            captureUiCandidate(tabs->tabWhatsThis(index));
            tabs->setCurrentIndex(index);
            renderWidgetForCapture(tabs->widget(index));
        }
        tabs->setCurrentIndex(previous);
    }
    if (auto *stack = qobject_cast<QStackedWidget *>(widget)) {
        const QSignalBlocker blocker(stack);
        const int previous = stack->currentIndex();
        for (int index = 0; index < stack->count(); ++index) {
            stack->setCurrentIndex(index);
            renderWidgetForCapture(stack->widget(index));
        }
        stack->setCurrentIndex(previous);
    }
    if (auto *view = qobject_cast<QAbstractItemView *>(widget))
        captureWholeModel(view->model(), visitedModels);
    if (auto *tree = qobject_cast<QTreeView *>(widget)) {
        QList<QPersistentModelIndex> expanded;
        qsizetype remaining = 1000000;
        collectExpandedTreeIndexes(tree, tree->model(), expanded, remaining);
        const QSignalBlocker blocker(tree);
        tree->expandAll();
        tree->doItemsLayout();
        renderWidgetForCapture(tree->viewport());
        tree->collapseAll();
        for (const QPersistentModelIndex &index : expanded) {
            if (index.isValid())
                tree->setExpanded(index, true);
        }
    }
    if (auto *combo = qobject_cast<QComboBox *>(widget))
        expandAndCaptureCombo(combo, visitedModels);
    if (auto *menu = qobject_cast<QMenu *>(widget))
        pendingMenus.emplace_back(menu);
}

void showOneShotCaptureResult(
    const HoudiniChineseCapture::OneShotResult &result) {
    QString message;
    QMessageBox::Icon icon = QMessageBox::Information;
    if (!result.ready) {
        icon = QMessageBox::Critical;
        message = QStringLiteral("一次性嗅探无法启动：采集缓冲区未初始化。");
    } else if (!result.saved) {
        icon = QMessageBox::Critical;
        message = QStringLiteral("嗅探已完成，但结果文件写入失败：\n") +
                  QDir::toNativeSeparators(result.outputPath);
    } else {
        message = QStringLiteral(
                      "一次性嗅探完成。\n\n本次新增：%1 条\n文件合计：%2 条")
                      .arg(static_cast<qulonglong>(result.added))
                      .arg(static_cast<qulonglong>(result.total));
        if (result.dropped)
            message +=
                QStringLiteral("\n缓冲区丢弃：%1 条").arg(result.dropped);
        message += QStringLiteral("\n\n保存到：\n") +
                   QDir::toNativeSeparators(result.outputPath);
    }
    QMessageBox box(icon, QStringLiteral("HoudiniChinese 未翻译词条嗅探"),
                    message, QMessageBox::Ok, QApplication::activeWindow());
    QAbstractButton *openFolderButton = nullptr;
    const QString outputDirectory =
        result.outputPath.isEmpty()
            ? QString()
            : QFileInfo(result.outputPath).absolutePath();
    if (!outputDirectory.isEmpty() && QDir(outputDirectory).exists()) {
        openFolderButton =
            box.addButton(QStringLiteral("打开目录"), QMessageBox::ActionRole);
    }
    box.setTextInteractionFlags(Qt::TextSelectableByMouse);
    box.exec();
    if (openFolderButton && box.clickedButton() == openFolderButton) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(outputDirectory));
    }
}

void finishOneShotCapture() noexcept {
    try {
        const HoudiniChineseCapture::OneShotResult result =
            HoudiniChineseCapture::endOneShot();
        g_oneShotCaptureRunning = false;
        showOneShotCaptureResult(result);
    } catch (...) {
        // Never let allocation or host-UI exceptions escape a Qt callback.
        // A second end is safe and ensures capture writers are disabled even
        // if result construction or the completion dialog failed.
        g_oneShotCaptureRunning = false;
        try {
            HoudiniChineseCapture::endOneShot();
        } catch (...) {
        }
    }
}

void captureUntranslatedOnce() {
    auto *app = qobject_cast<QApplication *>(QCoreApplication::instance());
    if (!app || g_oneShotCaptureRunning || QApplication::activeModalWidget())
        return;
    g_oneShotCaptureRunning = true;
    try {
        // Discover roots before enabling paint-hook capture. The two real crash
        // dumps both failed in Qt temporary widget-list destruction at this
        // stage; use Win32 roots plus QObject ownership instead and keep
        // discovery free of concurrent capture writes.
        std::vector<QWidget *> widgets;
        std::vector<QAction *> actions;
        std::unordered_set<QObject *> visitedObjects;
        widgets.reserve(4096);
        actions.reserve(1024);
        visitedObjects.reserve(8192);
        collectKnownWidgetTrees(app, widgets, actions, visitedObjects);
        std::vector<QPointer<QWidget>> guardedWidgets;
        guardedWidgets.reserve(widgets.size());
        for (QWidget *widget : widgets)
            guardedWidgets.emplace_back(widget);
        std::vector<QPointer<QAction>> guardedActions;
        guardedActions.reserve(actions.size());
        for (QAction *action : actions)
            guardedActions.emplace_back(action);

        if (!HoudiniChineseCapture::beginOneShot()) {
            finishOneShotCapture();
            return;
        }

        // QML windows are QWindow/QQuickItem object trees rather than QWidget
        // trees. Capture each instantiated object's text-bearing properties
        // before any nested event processing can delete a transient QML
        // delegate. Hidden and collapsed descendants remain QObject children
        // and are included.
        qsizetype objectPropertyRemaining = 1000000;
        for (QObject *object : visitedObjects) {
            captureCommonObjectText(object, objectPropertyRemaining);
            if (objectPropertyRemaining <= 0)
                break;
        }

        // Houdini's private controls share a UI_Feel hierarchy. Starting from
        // the currently painted controls reaches collapsed groups, inactive
        // pages and static/dynamic submenu models without modifying disclosure
        // state.
        captureNativeUIFeelHierarchies();

        // Native Houdini trees own a private SI_TreeNode hierarchy. Discover
        // one live row per visible tree, climb to its root and walk every
        // child. This includes folded branches without opening them or changing
        // UI state.
        captureNativeSITreeHierarchies();

        QSet<QAbstractItemModel *> visitedModels;
        QSet<QAction *> visitedActions;
        std::vector<QPointer<QMenu>> pendingMenus;
        pendingMenus.reserve(512);
        for (const QPointer<QAction> &action : guardedActions)
            captureActionText(action.data(), visitedActions, pendingMenus);
        for (const QPointer<QWidget> &widget : guardedWidgets)
            captureWidgetInventory(widget.data(), visitedModels, pendingMenus);

        // Menu population can discover further submenus, so consume the growing
        // set until no unvisited menu remains.
        QSet<QMenu *> visitedMenus;
        for (;;) {
            QMenu *next = nullptr;
            for (const QPointer<QMenu> &menuPointer :
                 std::as_const(pendingMenus)) {
                QMenu *menu = menuPointer.data();
                if (menu && !visitedMenus.contains(menu)) {
                    next = menu;
                    break;
                }
            }
            if (!next)
                break;
            expandAndCaptureMenu(next, visitedMenus, visitedActions,
                                 pendingMenus);
        }

        // Accessibility exposes labels of several custom Qt controls that do
        // not store their text in a standard QWidget property.
        QSet<QObject *> accessibleObjects;
        qsizetype accessibleRemaining = 1000000;
        for (const QPointer<QWidget> &widgetPointer : guardedWidgets) {
            QWidget *widget = widgetPointer.data();
            if (widget && widget->isWindow()) {
                captureAccessibleTree(
                    QAccessible::queryAccessibleInterface(widget),
                    accessibleObjects, accessibleRemaining);
            }
        }

        // Repaint every real top-level and render every hidden QWidget
        // offscreen. This reaches custom delegates and Houdini/Qt paint paths
        // in addition to the property/model inventory above.
        for (const QPointer<QWidget> &widgetPointer : guardedWidgets) {
            QWidget *widget = widgetPointer.data();
            if (widget && (!widget->isVisible() || widget->isWindow()))
                renderWidgetForCapture(widget);
        }
        forceMainWindowRepaint();
        redrawAllHoudiniWindows();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents,
                                        250);

        // Qt Quick and Houdini native redraws may complete on the next frame.
        QTimer::singleShot(400, app, [] { finishOneShotCapture(); });
    } catch (...) {
        finishOneShotCapture();
    }
}

// Language switching changes only what the paint hooks render.  Houdini's
// custom controls cache font metrics and layout positions, so repainting alone
// can keep the geometry calculated for the previous language.  Follow the same
// sequence used by SpeedTreeChinese: broadcast an application font change to
// invalidate text/layout caches, then repaint once immediately and once after
// deferred layout requests have settled.  Sending the application-level event
// avoids QApplication::allWidgets(), which is unsafe with Houdini's private UI.
void invalidateWidgetLayoutSizes(QApplication *app) {
    std::vector<QWidget *> widgets;
    std::vector<QAction *> actions;
    std::unordered_set<QObject *> visited;
    collectKnownWidgetTrees(app, widgets, actions, visited);
    if (g_mainQtWindow)
        collectWidgetRoot(g_mainQtWindow(), widgets, actions, visited);
    std::vector<QPointer<QWidget>> targets;
    for (QWidget *widget : widgets) {
        if (qobject_cast<QTabBar *>(widget) ||
            qobject_cast<QAbstractButton *>(widget) ||
            qobject_cast<QComboBox *>(widget) || qobject_cast<QLabel *>(widget))
            targets.emplace_back(widget);
    }
    for (const auto &widget : targets) {
        if (!widget) continue;
        QEvent fontChange(QEvent::FontChange);
        QCoreApplication::sendEvent(widget, &fontChange);
        if (widget) widget->updateGeometry();
    }
}

// Both phases use the same ordering: font invalidation, display layout,
// native layout, then button alignment and repaint. Only the first phase
// traverses size caches; the deferred phase settles the host's queued layout.
void refreshLanguageLayoutPass(QApplication *app, bool invalidateSizes) {
    QEvent fontChange(QEvent::ApplicationFontChange);
    QCoreApplication::sendEvent(app, &fontChange);
    if (invalidateSizes) {
        invalidateWidgetLayoutSizes(app);
        invalidateNativeLayoutSizes();
    }
    invalidateQtQuickTextLayouts();
    reflowTranslatedQuickSizes(!invalidateSizes);
    relayoutAllHoudiniWindows();
    requestNodeInfoViewUpdates();
    redrawAllHoudiniWindows();
    if (g_menuButtons)
        g_menuButtons->followLayoutChange();
    forceMainWindowRepaint();
}

void refreshLanguageDisplay() {
    for (const auto &window : g_knownQtWindows) refreshNativeWindowTitle21(window);
    for (auto it = g_translatedDialogs21.begin(); it != g_translatedDialogs21.end();) {
        if (!*it) it = g_translatedDialogs21.erase(it);
        else { refreshDialogTitle21(*it); ++it; }
    }
    auto *app = qobject_cast<QApplication *>(QCoreApplication::instance());
    if (!app) {
        forceMainWindowRepaint();
        return;
    }
    app->setProperty("hc21_enabled", g_enabled.load());
    refreshLanguageLayoutPass(app, true);
    QTimer::singleShot(0, app, [app] {
        refreshLanguageLayoutPass(app, false);
    });
}

void installRefreshFilter() {
    if (!QApplication::instance())
        return;
    static RefreshEventFilter *filter = nullptr;
    if (!filter) {
        filter = new RefreshEventFilter(QApplication::instance());
        QApplication::instance()->installEventFilter(filter);
        hookLog("installRefreshFilter: installed event filter");
    }
    rememberQtWindow(QGuiApplication::focusWindow());
    // Houdini may already be idle when the plug-in finishes loading, so force
    // one initial paint. Do not enumerate private Qt widget objects here.
    forceMainWindowRepaint();
}

// Dictionary discovery beside the plug-in DLL.
std::wstring modulePathW(HMODULE module) {
    std::vector<wchar_t> buf(1024);
    while (buf.size() <= 32768) {
        DWORD n = GetModuleFileNameW(module, buf.data(),
                                     static_cast<DWORD>(buf.size()));
        if (!n)
            return L"";
        if (n < buf.size())
            return std::wstring(buf.data(), n);
        if (buf.size() == 32768)
            break;
        buf.resize(buf.size() * 2);
    }
    return L"";
}

std::wstring pluginFolder() {
    const std::wstring path = modulePathW(g_moduleHandle);
    if (path.empty()) return {};
    const auto bin = path.substr(0, path.find_last_of(L"\\/"));
    return bin.substr(0, bin.find_last_of(L"\\/"));
}

void loadDictionariesFromDir(const std::wstring &dir) {
    g_dictionaryReady.store(false, std::memory_order_release);
    g_translations.clear();
    g_templateDefinitions.clear();
    g_placeholderTemplates.clear();
    g_matching.clear();
    const std::wstring translationsDir = dir + L"\\translations";
    WIN32_FIND_DATAW data{};
    HANDLE search =
        FindFirstFileW((translationsDir + L"\\*_zh.json").c_str(), &data);
    std::vector<std::wstring> names;
    if (search != INVALID_HANDLE_VALUE) {
        struct CloseSearch {
            HANDLE value;
            ~CloseSearch() { FindClose(value); }
        } held{search};
        do {
            if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                names.emplace_back(data.cFileName);
        } while (FindNextFileW(search, &data));
    }
    std::sort(names.begin(), names.end(),
              [](const std::wstring &left, const std::wstring &right) {
                  return _wcsicmp(left.c_str(), right.c_str()) < 0;
              });
    bool usableDictionaryLoaded = false;
    for (const std::wstring &name : names) {
        bool hasUsableTranslations = false;
        const bool loaded = loadTranslationsFromJson(
            translationsDir + L"\\" + name, &hasUsableTranslations);
        usableDictionaryLoaded = usableDictionaryLoaded ||
                                 (loaded && hasUsableTranslations);
    }
    if (!usableDictionaryLoaded) {
        decltype(g_translations){}.swap(g_translations);
        decltype(g_templateDefinitions){}.swap(g_templateDefinitions);
    }
    g_dictionaryEntries.clear();
    g_dictionaryEntries.reserve(g_translations.size());
    for (auto &entry : g_translations) {
        g_dictionaryEntries.push_back({dictionaryHashBytes(entry.first),
                                       entry.first, std::move(entry.second)});
    }
    std::sort(g_dictionaryEntries.begin(), g_dictionaryEntries.end(),
              [](const DictionaryEntry &left, const DictionaryEntry &right) {
                  if (left.hash != right.hash)
                      return left.hash < right.hash;
                  return left.source < right.source;
              });
    g_dictionaryIndex.clear();
    g_dictionaryIndex.reserve(g_dictionaryEntries.size());
    for (size_t index = 0; index < g_dictionaryEntries.size(); ++index)
        g_dictionaryIndex.emplace(g_dictionaryEntries[index].hash, index);
    rebuildReverseDictionary();
    for (const auto &entry : g_dictionaryEntries)
        g_matching.add(QString::fromUtf8(entry.source.data(),
                           static_cast<qsizetype>(entry.source.size())),
                       QString::fromUtf8(entry.translation.data(),
                           static_cast<qsizetype>(entry.translation.size())));
    g_matching.finalize();
    hookLog("matching: conflicts=%zu", g_matching.conflicts());
    for (const auto &entry : g_templateDefinitions) {
        QString error;
        const bool accepted = g_placeholderTemplates.addOrAssignDeferred(
            QString::fromUtf8(entry.first.data(),
                              static_cast<qsizetype>(entry.first.size())),
            QString::fromUtf8(entry.second.data(),
                              static_cast<qsizetype>(entry.second.size())),
            &error);
        if (!accepted)
            hookLog("invalid placeholder template ignored");
    }
    g_placeholderTemplates.finalize();
    // Parsing maps are startup-only staging storage. Release their buckets
    // after publishing the compact immutable lookup tables.
    decltype(g_templateDefinitions){}.swap(g_templateDefinitions);
    decltype(g_translations){}.swap(g_translations);
    g_dictionaryReady.store(usableDictionaryLoaded, std::memory_order_release);
}

// One ordered, idempotent installation list serves startup and late modules.
// Keep display, measurement and fallback entry points together so a later
// cleanup cannot silently omit a hook from one of the lifecycle paths.
std::array<const char *, 32> g_installerExceptions{};
std::size_t g_installerExceptionCount = 0;

void refreshHookTargets() {
    for (const auto &spec : HoudiniChineseHooks::kHookSpecs) {
        HMODULE module = GetModuleHandleW(spec.module);
        const auto target = module ? GetProcAddress(module, spec.symbol) : nullptr;
        g_hookRegistry.resolve(spec.id, reinterpret_cast<std::uintptr_t>(module),
                                reinterpret_cast<std::uintptr_t>(target));
    }
}

void installRuntimeHooks() {
    refreshHookTargets();
    g_installerExceptionCount = 0;
#define HC_INSTALL(Function) \
    try { Function(); } catch (...) { \
        if (g_installerExceptionCount < g_installerExceptions.size()) \
            g_installerExceptions[g_installerExceptionCount++] = #Function; \
    }
    HC_INSTALL(installMenuButtons);
    HC_INSTALL(installNativeControlTraversalHook);
    HC_INSTALL(installNativeLookDrawHook);
    HC_INSTALL(installUITextRangeHooks);
    HC_INSTALL(installWrappedTextHooks21);
    HC_INSTALL(installRETextHooks);
    HC_INSTALL(installSITreeTraversalHook);
    HC_INSTALL(installHooks);
    HC_INSTALL(installQTextLayoutDrawHook);
    HC_INSTALL(installStyleItemTextHook);
    HC_INSTALL(installUIHelpFeelGetTextHook);
    HC_INSTALL(installUIHelpTipLookRenderHook);
    HC_INSTALL(installUIHelpTipTitleHook);
    HC_INSTALL(installBRHelpTooltipHook);
    HC_INSTALL(installMOTTooltipHooks21);
    HC_INSTALL(installFontMeasurementHooks);
    HC_INSTALL(installQtQuickFontMetricHooks);
    HC_INSTALL(installQtQuickImplicitSizeHooks);
    HC_INSTALL(installNodeTextHooks);
    HC_INSTALL(installStaticTextHooks);
    HC_INSTALL(installNodeRenderTextHooks);
    HC_INSTALL(installCVPaintBufferTextHooks);
    HC_INSTALL(installQtQuickLayoutInputHook);
    HC_INSTALL(installQtQuickTextLayoutHook);
    HC_INSTALL(installUITextPaintHook);
    HC_INSTALL(installUITableTextPaintHooks);
    HC_INSTALL(installNativeDirectTextLookHooks);
#undef HC_INSTALL
}

struct LateLookRecovery {
    bool notified = false;
    bool finished = false;
    unsigned checks = 0;
    unsigned attempts = 0;
    const char *state = "not_requested";
} g_lateLookRecovery;

void checkLateLookRecovery(bool finalCheck) noexcept {
    auto &recovery = g_lateLookRecovery;
    if (recovery.finished) return;
    try {
        ++recovery.checks;
        const auto &record = g_hookRegistry.record(HoudiniChineseHooks::HookId::hookedUILookDraw);
        if (record.inlinePatched) {
            recovery.state = "already_installed";
            recovery.finished = true;
            return;
        }
        if (record.targetChanged || !record.modulePresent || !record.target ||
            record.lastError != ERROR_NOT_ENOUGH_MEMORY ||
            std::strcmp(record.detourStage, "detour_allocate_trampoline") != 0) {
            recovery.state = "not_recoverable";
            recovery.finished = true;
            return;
        }
        bool available = false;
        {
            DetourTransactionGuard guard;
            available = DetourHasAvailableTrampolineRegion(
                reinterpret_cast<void *>(record.target)) != FALSE;
        }
        if (!available) {
            recovery.state = finalCheck ? "no_space_budget_finished" : "waiting_for_space";
            recovery.finished = finalCheck;
            return;
        }
        const unsigned before = record.attempts;
        const bool installed = installNativeLookDrawHook(true);
        if (record.attempts == before && !installed) {
            recovery.state = finalCheck ? "cooldown_budget_finished" : "waiting_for_cooldown";
            recovery.finished = finalCheck;
            return;
        }
        recovery.attempts += record.attempts - before;
        recovery.state = installed ? "installed" : "attach_failed";
        recovery.finished = true;
    } catch (...) {
        recovery.state = "exception";
        recovery.finished = true;
    }
}

void scheduleLateLookRecovery(QCoreApplication *app) {
    if (g_lateLookRecovery.notified) return;
    g_lateLookRecovery.notified = true;
    if constexpr (SYS_VERSION_MAJOR_INT == 21) {
        // Hooks can become ready after the native menus cached English widths.
        // Reuse the tested language-switch reflow once after Houdini uiready.
        QTimer::singleShot(0, app, [] { refreshLanguageDisplay(); });
    }
    g_lateLookRecovery.state = "waiting_for_space";
    // Triggered explicitly by Houdini's uiready.py in the experiment package.
    // Four read-only checks; at most one extra installation transaction.
    QTimer::singleShot(2000, app, [] { checkLateLookRecovery(false); });
    QTimer::singleShot(15000, app, [] { checkLateLookRecovery(false); });
    QTimer::singleShot(45000, app, [] { checkLateLookRecovery(false); });
    QTimer::singleShot(120000, app, [] { checkLateLookRecovery(true); });
}

void runOnGuiThread(QApplication *app) {
    const std::wstring folder = pluginFolder();
    const QString pluginDirectory =
        folder.empty() ? QString() : QString::fromStdWString(folder);
    g_configPath = HoudiniChineseConfig::path(pluginDirectory);
    const HoudiniChineseConfig::Values config =
        HoudiniChineseConfig::load(g_configPath);
    // Config includes QKeySequence: retain GUI-thread initialization, after
    // QApplication construction and before any display hooks can read options.
    g_matching.options = config.matching;
    HoudiniChineseCapture::initialize(pluginDirectory, app);
    g_toggleShortcut = config.toggleShortcut;

    installNativeShortcutEventFilter();
    installRuntimeHooks();
    // 页脚格式串在 libUI 加载后才能定位；英文模式保持原文。
    if (g_enabled.load(std::memory_order_acquire)) {
        setHelpFooterChinese(true);
        setParameterHeaderChinese(true);
    }
    rememberCurrentImportModules();
    g_hookRegistryReady.store(true, std::memory_order_release);
    scheduleExistingQtQuickTextRefresh();
    installRefreshFilter();

    // Keep a bounded-rate incremental fallback for late-loaded modules.
    if (!HoudiniHookLifecycle::kEnableDeferredPolling) {
        hookLog("runOnGuiThread: deferred polling disabled");
        return;
    }

    static bool rescanScheduled = false;
    if (!rescanScheduled) {
        rescanScheduled = true;
        auto *timer = new QTimer(app);
        timer->setInterval(HoudiniHookLifecycle::kDeferredScanIntervalMs);
        QObject::connect(timer, &QTimer::timeout, [] {
            try {
                installRuntimeHooks();
                patchNewImportModules();
                scheduleExistingQtQuickTextRefresh();
            } catch (...) {
                // A failed optional installation must not escape Qt's event
                // callback and terminate the host. Existing hooks stay valid.
            }
        });
        timer->start();
        QTimer::singleShot(HoudiniHookLifecycle::kDeferredScanWindowMs, app,
                           [timer] {
                               timer->setInterval(HoudiniHookLifecycle::kSteadyScanIntervalMs);
                           });
        hookLog("runOnGuiThread: incremental scans started (startup then steady rate)");
    }
}

DWORD workerMain() {
    hookLog("worker: starting, waiting for QApplication");
    // A plug-in linked to one HDK must never patch a process with a different
    // ABI. This checks the HDK API identifier and the full Qt version. The HDK
    // identifier can span several daily builds; private interfaces still need
    // per-build validation even when this public ABI check succeeds.
    if (!isCompatibleRuntime())
        return 1;
    // Hooks remain installed for the process lifetime. Pin this DLL so a host
    // DSO refresh cannot unload their code while patched call sites still
    // reference it.
    HMODULE pinnedModule = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_PIN,
                            reinterpret_cast<LPCWSTR>(g_moduleHandle),
                            &pinnedModule) ||
        !pinnedModule) {
        return 1;
    }
    // Publish the immutable dictionary before any display hook can run.
    const std::wstring moduleFolder = pluginFolder();
    if (!moduleFolder.empty())
        loadDictionariesFromDir(moduleFolder);

    QApplication *app = nullptr;
    for (int i = 0; i < HoudiniHookLifecycle::kApplicationWaitAttempts; ++i) {
        if (QCoreApplication::instance()) {
            if (i == 0)
                hookLog("worker: QCoreApplication::instance() present");
            app = qobject_cast<QApplication *>(QCoreApplication::instance());
            if (app)
                break;
        }
        Sleep(HoudiniHookLifecycle::kApplicationWaitIntervalMs);
    }
    if (!app) {
        hookLog("worker: no QApplication found before timeout (instance=%p)",
                static_cast<void *>(QCoreApplication::instance()));
        return 1;
    }
    hookLog("worker: QApplication found, scheduling on GUI thread");
    QTimer::singleShot(0, app, [app] {
        try {
            runOnGuiThread(app);
        } catch (...) {
            // worker() cannot catch exceptions raised later on the GUI thread.
        }
    });
    return 0;
}

DWORD WINAPI worker(LPVOID) noexcept {
    try {
        return workerMain();
    } catch (...) {
        // No C++ exception may escape a Win32 thread entry point inside the
        // host process. A failed startup leaves Houdini running in English.
        return 1;
    }
}

} // namespace

// Python HUD adapter calls the already-loaded, identity-checked H21 engine.
// UTF-16 caller-owned buffer avoids passing Qt/STL objects across the boundary.
extern "C" __declspec(dllexport) int WINAPI
HoudiniChineseTranslateLabel21(const wchar_t *source, wchar_t *output, int capacity) noexcept {
    try {
        if (!source || !output || capacity < 1 || !g_dictionaryReady.load()) return 0;
        const QString text = QString::fromWCharArray(source);
        if (text.size() > 4096) return 0;
        ScopedAllowHoudiniUiText owner;
        const QString translated = Qt::mightBeRichText(text)
            ? HoudiniChineseRichText::translateTextNodes(text, displayTranslation)
            : displayTranslation(text);
        if (translated.isEmpty() || translated.size() >= capacity) return 0;
        const int count = static_cast<int>(translated.toWCharArray(output));
        output[count] = 0;
        return count;
    } catch (...) { return 0; }
}

// Python help cards can receive a Chinese TipData object cached while the
// overlay was enabled. Recover the original only for exact, unambiguous
// dictionary values so switching back to English never preserves stale text.
extern "C" __declspec(dllexport) int WINAPI
HoudiniChineseRestoreLabel21(const wchar_t *source, wchar_t *output, int capacity) noexcept {
    try {
        if (!source || !output || capacity < 1 || !g_dictionaryReady.load()) return 0;
        const QString text = QString::fromWCharArray(source);
        if (text.size() > 4096) return 0;
        const auto restore = [](const QString &value) {
            return findDictionarySourceExact(value);
        };
        const QString restored = Qt::mightBeRichText(text)
            ? HoudiniChineseRichText::translateTextNodes(text, restore)
            : restore(text);
        if (restored.isEmpty() || restored.size() >= capacity) return 0;
        const int count = static_cast<int>(restored.toWCharArray(output));
        output[count] = 0;
        return count;
    } catch (...) { return 0; }
}

// The caller enables observation, exercises its own UI, then requests a report.
// TRUE means queued, not that the file has already been written. Metadata is
// always serialized on the GUI thread; counters are cumulative and atomic.
extern "C" __declspec(dllexport) void WINAPI
HoudiniChineseEnableHookDiagnostics(BOOL enabled) noexcept {
    g_hookRegistry.enableObservation(enabled != FALSE);
}

extern "C" __declspec(dllexport) BOOL WINAPI
HoudiniChineseNotifyUIReady() noexcept {
    try {
        if (qEnvironmentVariableIntValue("HOUDINI_CHINESE_EXPERIMENT_LATE_LOOK") != 1 ||
            !g_hookRegistryReady.load(std::memory_order_acquire)) return FALSE;
        auto *app = QCoreApplication::instance();
        if (!app) return FALSE;
        return QMetaObject::invokeMethod(app, [app] {
            try { scheduleLateLookRecovery(app); }
            catch (...) {
                g_lateLookRecovery.state = "exception";
                g_lateLookRecovery.finished = true;
            }
        },
                                        Qt::QueuedConnection) ? TRUE : FALSE;
    } catch (...) { return FALSE; }
}

extern "C" __declspec(dllexport) BOOL WINAPI
HoudiniChineseRequestHookReport(const wchar_t *path) noexcept {
    try {
        if (!path || !*path || !g_hookRegistryReady.load(std::memory_order_acquire))
            return FALSE;
        auto *app = QCoreApplication::instance();
        if (!app)
            return FALSE;
        const QString destination = QString::fromWCharArray(path);
        return QMetaObject::invokeMethod(app, [destination] {
            try {
                auto report = HoudiniChineseHooks::hookReport(g_hookRegistry);
                report["build_houdini"] = SYS_VERSION_FULL;
                report["build_qt"] = QT_VERSION_STR;
                report["experimental_qt_inline"] = experimentalQtInlineEnabled();
                report["input_protection_policy"] = "owner-first-v1";
                report["build_hdk_api"] = static_cast<qint64>(HDK_API_VERSION);
                report["runtime_hdk_api"] = static_cast<qint64>(UTgetHDKAPIVersion());
                report["dictionary_ready"] = g_dictionaryReady.load(std::memory_order_acquire);
                report["translation_enabled"] = g_enabled.load(std::memory_order_acquire);
                report["steady_scan_interval_ms"] = HoudiniHookLifecycle::kSteadyScanIntervalMs;
                QJsonObject lateLook;
                lateLook["state"] = g_lateLookRecovery.state;
                lateLook["ui_ready_notified"] = g_lateLookRecovery.notified;
                lateLook["finished"] = g_lateLookRecovery.finished;
                lateLook["checks"] = static_cast<int>(g_lateLookRecovery.checks);
                lateLook["extra_attempts"] = static_cast<int>(g_lateLookRecovery.attempts);
                report["late_look_recovery"] = lateLook;
                QJsonArray exceptions;
                for (std::size_t i = 0; i < g_installerExceptionCount; ++i)
                    exceptions.append(g_installerExceptions[i]);
                report["installer_exceptions"] = exceptions;
                HoudiniChineseHooks::writeHookReport(destination, report);
            } catch (...) {
                // An explicit diagnostic request must not end the host session.
            }
        }, Qt::QueuedConnection) ? TRUE : FALSE;
    } catch (...) { return FALSE; }
}

// Explicit startup after the loader has checked the embedded identity. Merely
// loading this library never installs a hook or starts a Qt operation.
extern "C" __declspec(dllexport) BOOL WINAPI
HoudiniChineseStartEngine() noexcept {
    static std::atomic<bool> started{false};
    if (!isSupportedGuiHost() || !isCompatibleRuntime()) return FALSE;
    bool expected = false;
    if (!started.compare_exchange_strong(expected, true)) return TRUE;
    if (worker(nullptr) == 0) return TRUE;
    started.store(false);
    return FALSE;
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_moduleHandle = hinst;
        DisableThreadLibraryCalls(hinst);
    }
    return TRUE;
}
