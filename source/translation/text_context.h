#pragma once

#include <QtCore/QVariant>

#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QAbstractSpinBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMenu>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QTabBar>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QTextBrowser>
#include <QtQuick/QQuickItem>

namespace HoudiniChineseTextContext {

// Role describes this draw's owner, never a guess based on the string value.
// Unknown preserves the native compatibility fallback; it is not a label.
enum class Role { Unknown, DisplayLabel, UserValue };
struct Context final {
    Role role = Role::Unknown;
    bool allowNativeText = false;
};
inline thread_local Context current;

class Scope final {
  public:
    explicit Scope(Role role, bool allowNative = false) : previous_(current) {
        // Preserve sticky editor protection. Nested identical contexts need
        // neither a TLS write now nor a restore on return.
        Context next = current;
        if (next.role != Role::UserValue && role != Role::Unknown)
            next.role = role;
        next.allowNativeText = next.allowNativeText || allowNative;
        changed_ = next.role != current.role ||
                   next.allowNativeText != current.allowNativeText;
        if (changed_) current = next;
    }
    ~Scope() { if (changed_) current = previous_; }
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;
  private:
    Context previous_;
    bool changed_ = false;
};

inline Role widgetRole(const QWidget *widget, bool inputMethodFallback = true) {
    if (!widget) return Role::Unknown;
    // Item views contain user-created editors, so test the actual paint owner
    // before classifying a view or its viewport as display text.
    if (auto *combo = qobject_cast<const QComboBox *>(widget))
        return combo->isEditable() ? Role::UserValue : Role::DisplayLabel;
    if (auto *browser = qobject_cast<const QTextBrowser *>(widget))
        return browser->isReadOnly() ? Role::DisplayLabel : Role::UserValue;
    if (qobject_cast<const QLineEdit *>(widget) ||
        qobject_cast<const QAbstractSpinBox *>(widget) ||
        qobject_cast<const QTextEdit *>(widget) ||
        qobject_cast<const QPlainTextEdit *>(widget))
        return Role::UserValue; // Read-only/disabled data is still data.
    if (qobject_cast<const QLabel *>(widget) ||
        qobject_cast<const QAbstractButton *>(widget) ||
        qobject_cast<const QMenu *>(widget) ||
        qobject_cast<const QTabBar *>(widget) ||
        qobject_cast<const QAbstractItemView *>(widget))
        return Role::DisplayLabel;
    const QWidget *parent = widget->parentWidget();
    if (auto *browser = qobject_cast<const QTextBrowser *>(parent))
        if (browser->viewport() == widget)
            return browser->isReadOnly() ? Role::DisplayLabel : Role::UserValue;
    if (auto *edit = qobject_cast<const QTextEdit *>(parent))
        if (edit->viewport() == widget) return Role::UserValue;
    if (auto *edit = qobject_cast<const QPlainTextEdit *>(parent))
        if (edit->viewport() == widget) return Role::UserValue;
    if (auto *view = qobject_cast<const QAbstractItemView *>(parent))
        if (view->viewport() == widget) return Role::DisplayLabel;
    return inputMethodFallback && widget->testAttribute(Qt::WA_InputMethodEnabled)
        ? Role::UserValue : Role::Unknown;
}

inline bool nonEditableComboDisplay(const QQuickItem *input) {
    if (!input || !input->inherits("QQuickTextInput") || input->isEnabled() ||
        !input->property("preeditText").toString().isEmpty() ||
        !input->property("selectedText").toString().isEmpty())
        return false;
    for (auto *owner = input->parentItem(); owner; owner = owner->parentItem()) {
        if (owner->inherits("QQuickComboBox"))
            return !owner->property("editable").toBool() &&
                   owner->property("contentItem").value<QQuickItem *>() == input;
    }
    return false;
}

// GUI thread, or the synchronized updatePaintNode boundary only.
inline Role quickRole(const QObject *object) {
    if (!object) return Role::Unknown;
    if (object->inherits("QQuickTextInput"))
        return nonEditableComboDisplay(qobject_cast<const QQuickItem *>(object))
            ? Role::DisplayLabel : Role::UserValue;
    if (object->inherits("QQuickTextEdit")) return Role::UserValue;
    if (object->inherits("QQuickText")) return Role::DisplayLabel;
    return Role::Unknown;
}

} // namespace HoudiniChineseTextContext
