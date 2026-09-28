#include "../../source/ui/input_events.h"

#include <QtGui/QShortcut>
#include <QtTest/QTest>
#include <QtWidgets/QApplication>
#include <QtWidgets/QLineEdit>

#include <cassert>
#include <iostream>

class Filter final : public QObject {
  public:
    bool enabled = false;
    bool blocked = false;
    int activated = 0;
    QKeySequence configured{QStringLiteral("Ctrl+L")};
    HoudiniChineseShortcut::State state;

    bool eventFilter(QObject *, QEvent *event) override {
        if (!enabled)
            return false;
        const auto result = state.handle(event, configured, blocked);
        if (result.activate)
            ++activated;
        return result.consumed;
    }
};

int main(int argc, char **argv) {
    QApplication application(argc, argv);
    QLineEdit editor;
    editor.show();
    editor.activateWindow();
    editor.setFocus();
    QTest::qWait(20);
    Filter filter;
    application.installEventFilter(&filter);

    int hostActivated = 0;
    QShortcut hostShortcut(filter.configured, &editor);
    QObject::connect(&hostShortcut, &QShortcut::activated,
                     [&hostActivated] { ++hostActivated; });

    // Establish a real Qt shortcut conflict, rather than simulating dispatch.
    QTest::keyClick(&editor, Qt::Key_L, Qt::ControlModifier);
    assert(hostActivated == 1);
    filter.enabled = true;
    QTest::keyClick(&editor, Qt::Key_L, Qt::ControlModifier);
    assert(hostActivated == 1);
    assert(filter.activated == 1);

    // A blocked new press remains available to the host shortcut.
    filter.blocked = true;
    QTest::keyClick(&editor, Qt::Key_L, Qt::ControlModifier);
    assert(hostActivated == 2);
    assert(filter.activated == 1);
    filter.blocked = false;

    HoudiniChineseShortcut::State state;
    const QKeySequence configured(QStringLiteral("Ctrl+L"));
    auto send = [&](QEvent::Type type, int key, Qt::KeyboardModifiers modifiers,
                    bool blocked = false, bool repeat = false) {
        QKeyEvent event(type, key, modifiers, {}, repeat);
        return state.handle(&event, configured, blocked);
    };
    assert(send(QEvent::ShortcutOverride, Qt::Key_L, Qt::ControlModifier).consumed);
    assert(send(QEvent::KeyPress, Qt::Key_L, Qt::ControlModifier).activate);
    auto repeated = send(QEvent::KeyPress, Qt::Key_L, Qt::ControlModifier, false, true);
    assert(repeated.consumed && !repeated.activate);
    assert(send(QEvent::KeyRelease, Qt::Key_L, Qt::ControlModifier, false, true).consumed);
    repeated = send(QEvent::KeyPress, Qt::Key_L, Qt::ControlModifier, true, true);
    assert(repeated.consumed && !repeated.activate);
    // Releasing modifiers or opening a dialog must not leak the owned release.
    assert(send(QEvent::KeyRelease, Qt::Key_L, Qt::NoModifier, true).consumed);
    assert(!send(QEvent::KeyPress, Qt::Key_L, Qt::ControlModifier, true).consumed);
    assert(!send(QEvent::KeyRelease, Qt::Key_L, Qt::NoModifier, true).consumed);
    assert(send(QEvent::KeyPress, Qt::Key_L, Qt::ControlModifier).activate);
    assert(send(QEvent::KeyRelease, Qt::Key_L, Qt::ControlModifier).consumed);
    assert(!send(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier).consumed);
    assert(!send(QEvent::KeyRelease, Qt::Key_A, Qt::NoModifier).consumed);
    assert(send(QEvent::KeyPress, Qt::Key_L, Qt::ControlModifier).activate);
    QEvent deactivate(QEvent::ApplicationDeactivate);
    assert(!state.handle(&deactivate, configured, false).consumed);
    assert(send(QEvent::KeyPress, Qt::Key_L, Qt::ControlModifier).activate);
    assert(send(QEvent::KeyRelease, Qt::Key_L, Qt::NoModifier).consumed);

    QTest::keyClick(&editor, Qt::Key_A);
    assert(editor.text() == QStringLiteral("a"));
    std::cout << "shortcut_events_test: passed\n";
}
