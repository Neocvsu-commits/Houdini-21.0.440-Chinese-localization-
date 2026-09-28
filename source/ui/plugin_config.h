#pragma once
#include "../translation/matching.h"

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QSettings>
#include <QtCore/QString>
#include <QtCore/Qt>
#include <QtGui/QKeySequence>

namespace HoudiniChineseConfig {

constexpr int kVersion = 1;

inline QKeySequence defaultToggleShortcut() { return QKeySequence(Qt::Key_F3); }

struct Values final {
    QKeySequence toggleShortcut = defaultToggleShortcut();
    HoudiniChineseMatching::Options matching;
};

inline bool isSupportedBaseKey(int key) noexcept {
    if ((key >= Qt::Key_F1 && key <= Qt::Key_F12) ||
        (key >= Qt::Key_Space && key <= Qt::Key_AsciiTilde)) {
        return true;
    }
    switch (key) {
    case Qt::Key_Tab:
    case Qt::Key_Return:
    case Qt::Key_Backspace:
    case Qt::Key_Insert:
    case Qt::Key_Delete:
    case Qt::Key_Home:
    case Qt::Key_End:
    case Qt::Key_PageUp:
    case Qt::Key_PageDown:
    case Qt::Key_Left:
    case Qt::Key_Up:
    case Qt::Key_Right:
    case Qt::Key_Down:
        return true;
    default:
        return false;
    }
}

inline bool isSupportedShortcut(const QKeySequence &shortcut) noexcept {
    if (shortcut.count() != 1)
        return false;
    const QKeyCombination combination = shortcut[0];
    const int key = combination.key();
    if (!isSupportedBaseKey(key))
        return false;
    constexpr int allowedModifiers =
        Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier;
    const int modifiers = combination.keyboardModifiers().toInt();
    if ((modifiers & ~allowedModifiers) != 0)
        return false;
    // Shift+~ is permanently reserved for the one-shot untranslated UI scan.
    // Do not allow the language toggle to shadow that built-in action.
    if (modifiers == Qt::ShiftModifier &&
        (key == Qt::Key_QuoteLeft || key == Qt::Key_AsciiTilde)) {
        return false;
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F12)
        return true;
    return modifiers != Qt::NoModifier;
}

inline bool isOneShotCaptureShortcut(QKeyCombination pressed) noexcept {
    const int key = pressed.key();
    return pressed.keyboardModifiers() == Qt::ShiftModifier &&
           (key == Qt::Key_QuoteLeft || key == Qt::Key_AsciiTilde);
}

inline QString path(const QString &pluginDirectory) {
    return pluginDirectory.isEmpty()
               ? QString()
               : QDir(pluginDirectory)
                     .filePath(QStringLiteral("HoudiniChinese.ini"));
}

inline void setError(QString *error, const QString &message) {
    if (error && error->isEmpty())
        *error = message;
}

inline bool write(const QString &configPath, const Values &values,
                  QString *error = nullptr) {
    if (error)
        error->clear();
    if (configPath.isEmpty() || !isSupportedShortcut(values.toggleShortcut)) {
        setError(error, QStringLiteral("无效的配置路径或快捷键。"));
        return false;
    }
    if (!QDir().mkpath(QFileInfo(configPath).absolutePath())) {
        setError(error, QStringLiteral("无法创建配置目录。"));
        return false;
    }

    QSettings settings(configPath, QSettings::IniFormat);
    settings.setAtomicSyncRequired(true);
    // The INI has one explicit schema. Rewriting it removes stale or unknown
    // keys instead of silently carrying unsupported settings forward.
    settings.clear();
    settings.setValue(QStringLiteral("Plugin/version"), kVersion);
    settings.setValue(
        QStringLiteral("Hotkey/shortcut"),
        values.toggleShortcut.toString(QKeySequence::PortableText));
    settings.setValue(QStringLiteral("Matching/fuzzyEnabled"), values.matching.enabled);
    settings.setValue(QStringLiteral("Matching/threshold"), std::clamp(values.matching.threshold, 90, 99));
    settings.setValue(QStringLiteral("Matching/margin"), std::clamp(values.matching.margin, 1, 10));
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        setError(error, QStringLiteral("无法写入 HoudiniChinese.ini。"));
        return false;
    }
    return true;
}

inline Values load(const QString &configPath, QString *error = nullptr) {
    if (error)
        error->clear();
    Values result;
    if (configPath.isEmpty()) {
        setError(error, QStringLiteral("无法定位插件配置目录，使用默认设置。"));
        return result;
    }
    if (!QFileInfo::exists(configPath)) {
        write(configPath, result, error);
        return result;
    }

    QSettings settings(configPath, QSettings::IniFormat);
    settings.setFallbacksEnabled(false);
    // QSettings parses lazily. Read a value before checking FormatError so a
    // malformed file cannot silently contribute a partially parsed shortcut.
    bool versionOk = false;
    const int version =
        settings.value(QStringLiteral("Plugin/version"), -1).toInt(&versionOk);
    if (settings.status() != QSettings::NoError) {
        setError(error, QStringLiteral("配置文件无法读取，使用默认设置。"));
        return result;
    }

    if (!versionOk || version != kVersion) {
        setError(error, QStringLiteral("配置版本无效，使用默认设置。"));
        return result;
    }

    const QKeySequence shortcut = QKeySequence::fromString(
        settings.value(QStringLiteral("Hotkey/shortcut")).toString(),
        QKeySequence::PortableText);
    if (isSupportedShortcut(shortcut)) {
        result.toggleShortcut = shortcut;
    } else {
        setError(error, QStringLiteral("快捷键配置无效，使用 F3。"));
    }

    bool thresholdOk = false, marginOk = false;
    const int threshold = settings.value(QStringLiteral("Matching/threshold"), 90).toInt(&thresholdOk);
    const int margin = settings.value(QStringLiteral("Matching/margin"), 5).toInt(&marginOk);
    result.matching.enabled = settings.value(QStringLiteral("Matching/fuzzyEnabled"), true).toBool();
    if (thresholdOk && threshold >= 90 && threshold <= 99) result.matching.threshold = threshold;
    if (marginOk && margin >= 1 && margin <= 10) result.matching.margin = margin;
    return result;
}

inline bool saveToggleShortcut(const QString &configPath,
                               const QKeySequence &shortcut,
                               QString *error = nullptr) {
    if (!isSupportedShortcut(shortcut)) {
        if (error)
            *error = QStringLiteral("无效的快捷键。");
        return false;
    }
    // Editing the shortcut must preserve the independent matching policy.
    Values values = QFileInfo::exists(configPath) ? load(configPath) : Values{};
    values.toggleShortcut = shortcut;
    return write(configPath, values, error);
}

} // namespace HoudiniChineseConfig
