#pragma once

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>
#include <QtCore/QString>

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <new>
#include <string>

#include "../translation/display_text.h"

// Shift+~ one-shot untranslated-string capture. Hook threads only copy misses
// into a preallocated hash table; JSON parsing, deduplication and disk I/O
// happen on the GUI thread. Capture remains disabled between explicit scans.
namespace HoudiniChineseCapture {

constexpr size_t kCaptureSlotCount = 65536;
constexpr size_t kCaptureArenaBytes = 32 * 1024 * 1024;
static_assert((kCaptureSlotCount & (kCaptureSlotCount - 1)) == 0,
              "capture slot count must be a power of two");
static_assert(kCaptureArenaBytes <= UINT32_MAX,
              "capture arena offsets must fit in uint32_t");

struct CaptureSlot final {
    // 0 = empty, 1 = writer owns it, 2 = ready, 3 = drained.
    std::atomic<uint32_t> state{0};
    uint64_t hash = 0;
    uint32_t offset = 0;
    uint32_t length = 0;
};

// Admission and the writer count must share one atomic modification order.
// Separate enabled/count atomics let shutdown observe zero writers while a
// racing writer still observes enabled, then free the storage under that writer.
constexpr uint32_t kSubmittersEnabled = uint32_t{1} << 31;
constexpr uint32_t kSubmitterCountMask = kSubmittersEnabled - 1;
inline std::atomic<uint32_t> g_submitterGate{0};
inline std::atomic_bool g_oneShotActive{false};
inline std::unique_ptr<CaptureSlot[]> g_slots;
inline std::unique_ptr<char[]> g_arena;
inline std::atomic<uint32_t> g_arenaUsed{0};
inline std::atomic_bool g_arenaExhausted{false};
inline std::atomic_bool g_tableFull{false};
inline std::atomic<uint32_t> g_dropped{0};
inline QString g_outputPath;
inline std::map<std::string, std::string> g_captured;
inline bool g_writePending = false;
inline size_t g_oneShotStartingCount = 0;
inline bool g_initialized = false;
inline bool g_existingLoaded = false;

struct OneShotResult final {
    bool ready = false;
    bool saved = false;
    size_t added = 0;
    size_t total = 0;
    uint32_t dropped = 0;
    QString outputPath;
};

inline bool isEnabled() noexcept {
    return (g_submitterGate.load(std::memory_order_acquire) &
            kSubmittersEnabled) != 0;
}

inline bool hasAllocatedStorage() noexcept { return g_slots && g_arena; }

inline void disableSubmissions() noexcept {
    g_submitterGate.fetch_and(kSubmitterCountMask, std::memory_order_acq_rel);
}

class ActiveSubmitter final {
  public:
    ActiveSubmitter() noexcept {
        uint32_t state = g_submitterGate.load(std::memory_order_acquire);
        while ((state & kSubmittersEnabled) != 0 &&
               (state & kSubmitterCountMask) != kSubmitterCountMask) {
            if (g_submitterGate.compare_exchange_weak(
                    state, state + 1, std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                active_ = true;
                break;
            }
        }
    }
    ~ActiveSubmitter() noexcept {
        if (active_)
            g_submitterGate.fetch_sub(1, std::memory_order_release);
    }

    explicit operator bool() const noexcept { return active_; }

    ActiveSubmitter(const ActiveSubmitter &) = delete;
    ActiveSubmitter &operator=(const ActiveSubmitter &) = delete;

  private:
    bool active_ = false;
};

inline bool waitForSubmitters() noexcept {
    constexpr ULONGLONG kTimeoutMs = 1000;
    const ULONGLONG deadline = GetTickCount64() + kTimeoutMs;
    while ((g_submitterGate.load(std::memory_order_acquire) &
            kSubmitterCountMask) != 0) {
        if (GetTickCount64() >= deadline)
            return false;
        SwitchToThread();
    }
    return true;
}

inline bool equalsStored(const QString &text, qsizetype begin, qsizetype end,
                         const CaptureSlot &slot) noexcept {
    return HoudiniChineseUtf8::equals(text, begin, end,
                                      g_arena.get() + slot.offset, slot.length);
}

inline bool reserveArena(uint32_t length, uint32_t &offset) noexcept {
    uint32_t used = g_arenaUsed.load(std::memory_order_relaxed);
    for (;;) {
        if (used > kCaptureArenaBytes || length > kCaptureArenaBytes - used) {
            g_arenaExhausted.store(true, std::memory_order_release);
            return false;
        }
        if (g_arenaUsed.compare_exchange_weak(used, used + length,
                                              std::memory_order_acq_rel,
                                              std::memory_order_relaxed)) {
            offset = used;
            return true;
        }
    }
}

inline void submit(const QString &source) noexcept {
    if (!isEnabled())
        return;
    ActiveSubmitter submitter;
    // A successful admission holds storage alive until this scope exits,
    // even if endOneShot() closes admission immediately afterwards.
    if (!submitter || !hasAllocatedStorage())
        return;
    if (g_arenaExhausted.load(std::memory_order_acquire) ||
        g_tableFull.load(std::memory_order_acquire)) {
        g_dropped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    qsizetype begin = 0;
    qsizetype end = source.size();
    while (begin < end && source.at(begin).isSpace())
        ++begin;
    while (end > begin && source.at(end - 1).isSpace())
        --end;
    if (end - begin < 2 || end - begin > 16384)
        return;

    bool hasLetter = false;
    HoudiniChineseUtf8::forEachCodePoint(
        source, begin, end, [&hasLetter](uint32_t value) {
            hasLetter =
                hasLetter || QChar::isLetter(static_cast<char32_t>(value));
        });
    const HoudiniChineseUtf8::Stats stats =
        HoudiniChineseUtf8::stats(source, begin, end);
    if (!hasLetter || !stats.length)
        return;

    size_t slotIndex =
        static_cast<size_t>(stats.hash) & (kCaptureSlotCount - 1);
    for (size_t probe = 0; probe < kCaptureSlotCount; ++probe) {
        CaptureSlot &slot = g_slots[slotIndex];
        uint32_t state = slot.state.load(std::memory_order_acquire);
        if ((state == 2 || state == 3) && slot.hash == stats.hash &&
            slot.length == stats.length &&
            equalsStored(source, begin, end, slot)) {
            return;
        }
        if (state == 0) {
            uint32_t empty = 0;
            if (slot.state.compare_exchange_strong(empty, 1,
                                                   std::memory_order_acq_rel,
                                                   std::memory_order_acquire)) {
                uint32_t offset = 0;
                if (!reserveArena(stats.length + 1, offset)) {
                    slot.state.store(0, std::memory_order_release);
                    g_dropped.fetch_add(1, std::memory_order_relaxed);
                    return;
                }
                uint32_t position = 0;
                HoudiniChineseUtf8::forEachByte(
                    source, begin, end,
                    [offset, &position](unsigned char value) {
                        g_arena[offset + position++] = static_cast<char>(value);
                    });
                g_arena[offset + position] = '\0';
                slot.hash = stats.hash;
                slot.offset = offset;
                slot.length = position;
                slot.state.store(2, std::memory_order_release);
                return;
            }
        }
        slotIndex = (slotIndex + 1) & (kCaptureSlotCount - 1);
    }
    g_tableFull.store(true, std::memory_order_release);
    g_dropped.fetch_add(1, std::memory_order_relaxed);
}

inline bool loadExisting() {
    std::map<std::string, std::string> loaded;
    QFile file(g_outputPath);
    if (!file.exists()) {
        g_captured.clear();
        return true;
    }
    if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024 * 1024)
        return false;
    QJsonParseError error{};
    const QJsonDocument document =
        QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return false;
    const QJsonValue translationsValue =
        document.object().value(QStringLiteral("translations"));
    if (!translationsValue.isObject())
        return false;
    const QJsonObject translations = translationsValue.toObject();
    for (auto iterator = translations.constBegin();
         iterator != translations.constEnd(); ++iterator) {
        if (!iterator.value().isString())
            return false;
        const QByteArray key = iterator.key().toUtf8();
        const QByteArray translatedValue = iterator.value().toString().toUtf8();
        loaded.emplace(
            std::string(key.constData(), static_cast<size_t>(key.size())),
            std::string(translatedValue.constData(),
                        static_cast<size_t>(translatedValue.size())));
    }
    g_captured.swap(loaded);
    return true;
}

inline bool prepareStorage() noexcept {
    if (hasAllocatedStorage())
        return true;
    try {
        const QString outputDirectory = QFileInfo(g_outputPath).absolutePath();
        if (outputDirectory.isEmpty() || !QDir().mkpath(outputDirectory)) {
            return false;
        }
        if (!g_existingLoaded) {
            if (!loadExisting())
                return false;
            g_existingLoaded = true;
        }
        std::unique_ptr<CaptureSlot[]> slots(
            new (std::nothrow) CaptureSlot[kCaptureSlotCount]{});
        std::unique_ptr<char[]> arena(
            new (std::nothrow) char[kCaptureArenaBytes]);
        if (!slots || !arena)
            return false;
        g_arenaUsed.store(0, std::memory_order_relaxed);
        g_arenaExhausted.store(false, std::memory_order_relaxed);
        g_tableFull.store(false, std::memory_order_relaxed);
        g_dropped.store(0, std::memory_order_relaxed);
        g_slots = std::move(slots);
        g_arena = std::move(arena);
        return true;
    } catch (...) {
        return false;
    }
}

inline void releaseSavedStorage() noexcept {
    g_slots.reset();
    g_arena.reset();
    g_arenaUsed.store(0, std::memory_order_relaxed);
    g_arenaExhausted.store(false, std::memory_order_relaxed);
    g_tableFull.store(false, std::memory_order_relaxed);
    g_dropped.store(0, std::memory_order_relaxed);
    std::map<std::string, std::string>().swap(g_captured);
    g_existingLoaded = false;
}

inline bool flush() {
    if (!g_slots || !g_arena)
        return false;
    bool changed = false;
    bool fullyDrained = true;
    for (size_t index = 0; index < kCaptureSlotCount; ++index) {
        CaptureSlot &slot = g_slots[index];
        if (slot.state.load(std::memory_order_acquire) != 2)
            continue;
        try {
            changed = g_captured
                          .emplace(std::string(g_arena.get() + slot.offset,
                                               slot.length),
                                   "")
                          .second ||
                      changed;
            slot.state.store(3, std::memory_order_release);
        } catch (...) {
            // Keep the slot ready and storage alive for the next explicit
            // scan. A partial save must never discard an undrained record.
            fullyDrained = false;
        }
    }
    g_writePending = g_writePending || changed;
    if (!g_writePending || g_outputPath.isEmpty())
        return fullyDrained;

    QJsonObject translations;
    for (const auto &entry : g_captured) {
        translations.insert(
            QString::fromUtf8(entry.first.data(),
                              static_cast<qsizetype>(entry.first.size())),
            QString::fromUtf8(entry.second.data(),
                              static_cast<qsizetype>(entry.second.size())));
    }
    QJsonObject root{
        {QStringLiteral("$schema"), QStringLiteral("sp-translation-v1")},
        {QStringLiteral("id"), QStringLiteral("houdini-runtime-untranslated")},
        {QStringLiteral("language"), QStringLiteral("zh-CN")},
        {QStringLiteral("description"),
         QStringLiteral("Shift+~ 一次性嗅探的未命中 UI 原文")},
        {QStringLiteral("captureDropped"),
         static_cast<double>(g_dropped.load(std::memory_order_relaxed))},
        {QStringLiteral("translations"), translations},
    };
    const QByteArray bytes =
        QJsonDocument(root).toJson(QJsonDocument::Indented);
    QSaveFile file(g_outputPath);
    if (file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() &&
        file.commit()) {
        g_writePending = false;
        return fullyDrained;
    }
    return false;
}

inline void initialize(const QString &pluginDirectory,
                       QCoreApplication *application) {
    if (!application || pluginDirectory.isEmpty())
        return;
    if (g_initialized)
        return;
    g_outputPath =
        pluginDirectory +
        QStringLiteral("/translations/untranslated_captured.pending.json");
    g_initialized = true;
    // Storage and the existing JSON file are loaded lazily on Shift+~. Normal
    // Houdini startup pays no capture I/O and retains no 32 MiB capture arena.
    disableSubmissions();
    QObject::connect(application, &QCoreApplication::aboutToQuit, [] {
        disableSubmissions();
        if (waitForSubmitters()) {
            try {
                flush();
            } catch (...) {
            }
        }
    });
}

inline bool beginOneShot() {
    if (!g_initialized || g_outputPath.isEmpty())
        return false;
    if (g_oneShotActive.exchange(true, std::memory_order_acq_rel))
        return false;
    if (!prepareStorage()) {
        g_oneShotActive.store(false, std::memory_order_release);
        return false;
    }
    // Drain any entries left ready by an interrupted previous pass before
    // measuring how many strings this pass contributes. Keep capture disabled
    // during the drain so hook writers never overlap JSON/map maintenance.
    disableSubmissions();
    if (!waitForSubmitters()) {
        g_oneShotActive.store(false, std::memory_order_release);
        return false;
    }
    g_writePending = true;
    try {
        flush();
    } catch (...) {
        g_oneShotActive.store(false, std::memory_order_release);
        return false;
    }
    g_oneShotStartingCount = g_captured.size();
    g_submitterGate.store(kSubmittersEnabled, std::memory_order_release);
    return true;
}

inline OneShotResult endOneShot() {
    OneShotResult result;
    result.ready = g_initialized && hasAllocatedStorage();
    result.outputPath = g_outputPath;
    disableSubmissions();
    const bool quiescent = waitForSubmitters();
    try {
        result.saved = result.ready && quiescent && flush();
    } catch (...) {
        result.saved = false;
    }
    result.total = g_captured.size();
    result.added = result.total >= g_oneShotStartingCount
                       ? result.total - g_oneShotStartingCount
                       : 0;
    result.dropped = g_dropped.load(std::memory_order_relaxed);
    g_oneShotActive.store(false, std::memory_order_release);
    if (result.saved)
        releaseSavedStorage();
    return result;
}

} // namespace HoudiniChineseCapture
