#pragma once

// ---------------------------------------------------------------------------
// shortcut events
// ---------------------------------------------------------------------------
#include <QtCore/QEvent>
#include <QtCore/QSet>
#include <QtGui/QKeyEvent>
#include <QtGui/QKeySequence>

namespace HoudiniChineseShortcut {

struct Result final {
    bool consumed = false;
    bool activate = false;
};

// GUI-thread state shared by the application's shortcut event filter. Reserve
// the combination before Qt dispatches QShortcut, then own its press/release
// pair even if a modal dialog opens or the configured combination changes.
class State final {
  public:
    Result handle(QEvent *event, const QKeySequence &configured, bool blocked) {
        const QEvent::Type type = event->type();
        if (type == QEvent::ApplicationDeactivate) {
            // A physical release can reach a different application after the
            // user switches away. Do not retain that key into the next visit.
            heldKeys_.clear();
            return {};
        }
        if (type != QEvent::ShortcutOverride && type != QEvent::KeyPress &&
            type != QEvent::KeyRelease)
            return {};

        auto *key = static_cast<QKeyEvent *>(event);
        const int code = key->key();
        const bool held = heldKeys_.contains(code);
        if (type == QEvent::KeyRelease) {
            if (!held)
                return {};
            // Qt may pair an autorepeat release with the next repeat press.
            // Only the physical release relinquishes ownership.
            if (!key->isAutoRepeat())
                heldKeys_.remove(code);
            event->accept();
            return {true, false};
        }

        const bool matches = configured.count() == 1 &&
                             key->keyCombination() == configured[0];
        if (!held && (blocked || !matches))
            return {};

        event->accept();
        if (type == QEvent::ShortcutOverride)
            return {true, false};

        heldKeys_.insert(code);
        return {true, !held && !key->isAutoRepeat()};
    }

  private:
    QSet<int> heldKeys_;
};

} // namespace HoudiniChineseShortcut
