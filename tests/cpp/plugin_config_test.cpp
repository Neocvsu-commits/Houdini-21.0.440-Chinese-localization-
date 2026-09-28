#include "../../source/ui/plugin_config.h"
#include "../../source/ui/hotkey_settings.h"

#include <QtCore/QFile>
#include <QtCore/QSettings>
#include <QtCore/QTemporaryDir>
#include <QtTest/QTest>
#include <QtWidgets/QApplication>

#include <cassert>
#include <iostream>

int main(int argc, char **argv) {
    QApplication application(argc, argv);
    assert(HoudiniChineseConfig::isSupportedShortcut(QKeySequence(Qt::Key_F3)));
    assert(
        HoudiniChineseConfig::isSupportedShortcut(QKeySequence(Qt::Key_F12)));
    assert(
        !HoudiniChineseConfig::isSupportedShortcut(QKeySequence(Qt::Key_F13)));
    assert(HoudiniChineseConfig::isSupportedShortcut(
        QKeySequence(QStringLiteral("Ctrl+Shift+L"))));
    assert(HoudiniChineseConfig::isSupportedShortcut(
        QKeySequence(QStringLiteral("Shift+L"))));
    assert(!HoudiniChineseConfig::isSupportedShortcut(
        QKeySequence(QStringLiteral("Shift+~"))));
    assert(HoudiniChineseConfig::isOneShotCaptureShortcut(
        QKeyCombination(Qt::ShiftModifier, Qt::Key_QuoteLeft)));
    assert(HoudiniChineseConfig::isOneShotCaptureShortcut(
        QKeyCombination(Qt::ShiftModifier, Qt::Key_AsciiTilde)));
    assert(!HoudiniChineseConfig::isOneShotCaptureShortcut(
        QKeyCombination(Qt::ControlModifier, Qt::Key_QuoteLeft)));
    assert(!HoudiniChineseConfig::isSupportedShortcut(
        QKeySequence(QStringLiteral("Meta+L"))));
    assert(!HoudiniChineseConfig::isSupportedShortcut(
        QKeySequence(QStringLiteral("Meta+F3"))));
    assert(!HoudiniChineseConfig::isSupportedShortcut(
        QKeySequence(QStringLiteral("Ctrl+Meta+L"))));
    assert(!HoudiniChineseConfig::isSupportedShortcut(
        QKeySequence(QStringLiteral("A"))));
    assert(!HoudiniChineseConfig::isSupportedShortcut(QKeySequence{}));
    assert(!HoudiniChineseConfig::isSupportedShortcut(
        QKeySequence(QStringLiteral("Ctrl+K, Ctrl+C"))));

    QTemporaryDir directory;
    assert(directory.isValid());
    const QString configPath = HoudiniChineseConfig::path(directory.path());
    assert(configPath.endsWith(QStringLiteral("HoudiniChinese.ini")));

    QString error;
    HoudiniChineseConfig::Values values =
        HoudiniChineseConfig::load(configPath, &error);
    assert(error.isEmpty());
    assert(values.toggleShortcut == QKeySequence(Qt::Key_F3));
    assert(QFile::exists(configPath));

    {
        QSettings settings(configPath, QSettings::IniFormat);
        settings.setValue(QStringLiteral("Plugin/version"), 1);
        settings.setValue(QStringLiteral("Hotkey/shortcut"),
                          QStringLiteral("Ctrl+Shift+L"));
        settings.setValue(QStringLiteral("Capture/enabled"), true);
        settings.setValue(QStringLiteral("Legacy/unused"), 1);
        settings.sync();
        assert(settings.status() == QSettings::NoError);
    }
    values = HoudiniChineseConfig::load(configPath, &error);
    assert(error.isEmpty());
    assert(values.toggleShortcut ==
           QKeySequence(QStringLiteral("Ctrl+Shift+L")));

    assert(HoudiniChineseConfig::saveToggleShortcut(
        configPath, QKeySequence(QStringLiteral("Alt+F9")), &error));
    values = HoudiniChineseConfig::load(configPath, &error);
    assert(error.isEmpty());
    assert(values.toggleShortcut == QKeySequence(QStringLiteral("Alt+F9")));
    {
        QSettings settings(configPath, QSettings::IniFormat);
        assert(!settings.contains(QStringLiteral("Legacy/unused")));
        assert(!settings.contains(QStringLiteral("Capture/enabled")));
    }

    {
        QSettings settings(configPath, QSettings::IniFormat);
        settings.setValue(QStringLiteral("Hotkey/shortcut"),
                          QStringLiteral("A"));
        settings.setValue(QStringLiteral("Capture/enabled"),
                          QStringLiteral("maybe"));
        settings.sync();
    }
    values = HoudiniChineseConfig::load(configPath, &error);
    assert(!error.isEmpty());
    assert(values.toggleShortcut == QKeySequence(Qt::Key_F3));

    {
        QFile malformed(configPath);
        assert(malformed.open(QIODevice::WriteOnly | QIODevice::Truncate));
        malformed.write("[Plugin]\nversion=1\n[Hotkey]\nshortcut=Alt+F9\n[broken\n");
    }
    values = HoudiniChineseConfig::load(configPath, &error);
    assert(!error.isEmpty());
    assert(values.toggleShortcut == QKeySequence(Qt::Key_F3));
    assert(QFile::remove(configPath));

    QKeySequence current(Qt::Key_F8);
    assert(HoudiniChineseConfig::saveToggleShortcut(configPath, current));
    {
        auto *dialog = new HoudiniChineseHotkeyDialog(nullptr, current, configPath);
        auto *reset = dialog->findChild<QPushButton *>(QStringLiteral("hotkeyReset"));
        assert(reset);
        QTest::mouseClick(reset, Qt::LeftButton);
        assert(current == QKeySequence(Qt::Key_F8));
        dialog->close();
        assert(HoudiniChineseConfig::load(configPath).toggleShortcut == current);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    {
        auto *dialog = new HoudiniChineseHotkeyDialog(nullptr, current, configPath);
        auto *capture = dialog->findChild<QPushButton *>(QStringLiteral("hotkeyCapture"));
        auto *save = dialog->findChild<QPushButton *>(QStringLiteral("hotkeyAccept"));
        assert(capture && save);
        QTest::mouseClick(capture, Qt::LeftButton);
        assert(!save->isEnabled());
        QTest::keyClick(capture, Qt::Key_QuoteLeft, Qt::ShiftModifier);
        assert(!save->isEnabled()); // Reserved capture command cannot be reassigned.
        QTest::keyClick(capture, Qt::Key_F9, Qt::AltModifier);
        assert(save->isEnabled());
        assert(current == QKeySequence(Qt::Key_F8)); // Pending until Save.
        QTest::mouseClick(save, Qt::LeftButton);
        assert(current == QKeySequence(QStringLiteral("Alt+F9")));
        assert(HoudiniChineseConfig::load(configPath).toggleShortcut == current);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    {
        const QString blockedPath = directory.filePath(QStringLiteral("directory.ini"));
        assert(QDir().mkdir(blockedPath));
        auto *dialog = new HoudiniChineseHotkeyDialog(nullptr, current, blockedPath);
        auto *reset = dialog->findChild<QPushButton *>(QStringLiteral("hotkeyReset"));
        auto *save = dialog->findChild<QPushButton *>(QStringLiteral("hotkeyAccept"));
        QTest::mouseClick(reset, Qt::LeftButton);
        QTest::mouseClick(save, Qt::LeftButton);
        assert(current == QKeySequence(QStringLiteral("Alt+F9")));
        assert(!dialog->findChild<QLabel *>(QStringLiteral("hotkeyError"))->text().isEmpty());
        delete dialog;
    }

    auto matchingValues = HoudiniChineseConfig::load(configPath);
    matchingValues.matching.threshold = 94;
    matchingValues.matching.margin = 7;
    matchingValues.matching.enabled = false;
    assert(HoudiniChineseConfig::write(configPath, matchingValues));
    assert(HoudiniChineseConfig::saveToggleShortcut(configPath, QKeySequence(Qt::Key_F4)));
    const auto preserved = HoudiniChineseConfig::load(configPath);
    assert(preserved.matching.threshold == 94 && preserved.matching.margin == 7);
    assert(!preserved.matching.enabled);
    {
        QSettings settings(configPath, QSettings::IniFormat);
        settings.setValue("Matching/threshold", "broken");
        settings.setValue("Matching/margin", -1);
    }
    const auto invalid = HoudiniChineseConfig::load(configPath);
    assert(invalid.matching.threshold == 90 && invalid.matching.margin == 5);
    std::cout << "plugin_config_test: OK\n";
    return 0;
}
