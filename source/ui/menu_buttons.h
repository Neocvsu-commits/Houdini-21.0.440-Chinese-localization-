#pragma once

// ---------------------------------------------------------------------------
// menu row anchor
// ---------------------------------------------------------------------------
#include <QtCore/QRect>
#include <algorithm>
#include <vector>

namespace HoudiniChineseMenuRow {

// Read-only adapter. Resolve again each time; never retain Houdini's pointers.
struct Api {
    void *(*parent)(void *);
    int (*count)(void *);
    void *(*child)(void *, int);
    bool (*isType)(void *, const char *);
    QRect (*rect)(void *);
};

inline void *findMenu(void *root, const Api &api) {
    struct Visit { void *node; int depth; };
    std::vector<Visit> pending{{root, 0}};
    for (int visited = 0; !pending.empty() && visited < 256; ++visited) {
        const auto visit = pending.back();
        pending.pop_back();
        if (!visit.node || visit.depth > 12)
            continue;
        const QRect rect = api.rect(visit.node);
        if (rect.isEmpty() || rect.top() > 100)
            continue;
        if (api.isType(visit.node, ".?AVUI_MenuBar@@"))
            return visit.node;
        const int count = (std::min)(api.count(visit.node), 64);
        for (int i = count - 1; i >= 0; --i)
            pending.push_back({api.child(visit.node, i), visit.depth + 1});
    }
    return nullptr;
}

inline QRect availableSpace(void *root, const Api &api) {
    void *menu = findMenu(root, api);
    if (!menu)
        return {};
    const QRect menuRect = api.rect(menu);
    void *row = menu;
    for (int level = 0; level < 6 && row; ++level) {
        row = api.parent(row);
        if (!row)
            break;
        const int count = api.count(row);
        if (count < 2 || count > 32)
            continue;
        for (int i = 0; i < count; ++i) {
            void *group = api.child(row, i);
            const QRect bounds = api.rect(group);
            if (bounds.isEmpty() || bounds.left() <= menuRect.right() ||
                bounds.top() != menuRect.top() || bounds.height() != menuRect.height())
                continue;
            int dropdowns = 0;
            int occupiedRight = bounds.left();
            const int members = api.count(group);
            if (members < 2 || members > 16)
                continue;
            // The verified Desktop/Take group has two wrapper children, each
            // containing one UI_DropDownMenu. Other visible siblings reserve
            // their own space and must never be covered by the Qt container.
            for (int j = 0; j < members; ++j) {
                void *member = api.child(group, j);
                const QRect memberRect = api.rect(member);
                if (memberRect.isEmpty())
                    continue;
                occupiedRight = (std::max)(occupiedRight, memberRect.right() + 1);
                const int nested = api.count(member);
                if (nested != 1)
                    continue;
                void *dropdown = api.child(member, 0);
                if (api.isType(dropdown, ".?AVUI_DropDownMenu@@") &&
                    !api.rect(dropdown).isEmpty())
                    ++dropdowns;
            }
            if (dropdowns == 2) {
                const int gap = (std::max)(4, bounds.height() / 6);
                const int left = occupiedRight + gap;
                return QRect(left, bounds.top(), bounds.right() + 1 - gap - left,
                             bounds.height());
            }
        }
    }
    return {};
}

} // namespace HoudiniChineseMenuRow

// ---------------------------------------------------------------------------
// menu buttons
// ---------------------------------------------------------------------------
#include <QtCore/QPointer>
#include <QtCore/QSignalBlocker>
#include <QtCore/QTimer>
#include <QtGui/QMouseEvent>
#include <QtGui/QFontMetrics>
#include <QtWidgets/QApplication>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QFrame>
#include <functional>
#include <utility>

namespace HoudiniChineseButtons {

// Retry only while the main window is unavailable. Once the control exists,
// ownership checks are driven by window events or the control's destruction.
class OwnerWatch final : public QObject {
  public:
    using Ensure = std::function<QObject *()>;
    explicit OwnerWatch(QObject *parent, Ensure ensure, int intervalMs = 1000,
                        int maxAttempts = 30)
        : QObject(parent), ensure_(std::move(ensure)), maxAttempts_(maxAttempts) {
        retry_ = new QTimer(this);
        retry_->setInterval(intervalMs);
        connect(retry_, &QTimer::timeout, this, [this] { attempt(); });
        if (auto *app = QCoreApplication::instance()) app->installEventFilter(this);
        request();
    }
    ~OwnerWatch() override { QObject::disconnect(destroyed_); }

    void request() {
        if (pending_) return;
        pending_ = true;
        QTimer::singleShot(0, this, [this] {
            pending_ = false;
            remaining_ = maxAttempts_;
            attempt();
        });
    }

  protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() == QEvent::Show || event->type() == QEvent::WinIdChange) {
            if (auto *widget = qobject_cast<QWidget *>(object))
                if (widget->isWindow()) request();
        }
        return false;
    }

  private:
    Ensure ensure_;
    QTimer *retry_ = nullptr;
    QPointer<QObject> tracked_;
    QMetaObject::Connection destroyed_;
    int maxAttempts_;
    int remaining_ = 0;
    bool pending_ = false;

    void attempt() noexcept {
        if (remaining_ <= 0) { retry_->stop(); return; }
        --remaining_;
        try {
            QObject *pair = ensure_();
            if (pair) {
                if (tracked_ != pair) {
                    QObject::disconnect(destroyed_);
                    tracked_ = pair;
                    destroyed_ = connect(pair, &QObject::destroyed, this,
                                         [this] { request(); });
                }
                retry_->stop();
                return;
            }
        } catch (...) {
            // A failed optional UI creation must not escape Qt's event loop.
        }
        if (remaining_ > 0) retry_->start();
        else retry_->stop();
    }
};

enum class Action { ToggleLanguage, EditShortcut };

// Qt owns the left-click gesture, mouse grab, hover and cancellation. The
// secondary action is local to this button, never a host-wide mouse filter.
class Button final : public QToolButton {
  public:
    explicit Button(QWidget *parent) : QToolButton(parent) {
        setAutoRaise(false);
        setAutoRepeat(false);
        setToolButtonStyle(Qt::ToolButtonTextOnly);
        setFocusPolicy(Qt::NoFocus);
        setCursor(Qt::PointingHandCursor);
    }
    std::function<void()> secondaryAction;

  protected:
    void mousePressEvent(QMouseEvent *event) override {
        secondaryPressed_ = event->button() == Qt::RightButton &&
                            event->buttons() == Qt::RightButton;
        if (secondaryPressed_) {
            event->accept();
            return;
        }
        QToolButton::mousePressEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent *event) override {
        const bool activate = secondaryPressed_ &&
                              event->button() == Qt::RightButton &&
                              event->buttons() == Qt::NoButton &&
                              rect().contains(event->position().toPoint());
        secondaryPressed_ = false;
        if (event->button() == Qt::RightButton) {
            event->accept();
            if (activate && secondaryAction)
                secondaryAction();
            return;
        }
        QToolButton::mouseReleaseEvent(event);
    }
    bool event(QEvent *event) override {
        if (event->type() == QEvent::Hide ||
            event->type() == QEvent::UngrabMouse ||
            event->type() == QEvent::WindowDeactivate)
            secondaryPressed_ = false;
        return QToolButton::event(event);
    }

  private:
    bool secondaryPressed_ = false;
};

// One native Qt child hosts the language button over the measured empty
// region. Native controls only supply geometry and are never modified.
class Pair final : public QObject {
  public:
    using Handler = std::function<void(Action)>;
    using Locator = std::function<QRect()>;
    Pair(QWidget *parent, Handler handler, Locator locator)
        : QObject(parent), parent_(parent), handler_(std::move(handler)),
          locator_(std::move(locator)) {
        container_ = new QFrame(parent);
        container_->setObjectName(QStringLiteral("houdini_chinese_button_bar"));
        container_->setAttribute(Qt::WA_DontCreateNativeAncestors);
        container_->setAttribute(Qt::WA_NativeWindow);
        container_->setFocusPolicy(Qt::NoFocus);
        // One scoped style for the control. Explicit margins prevent the
        // host's generic QToolButton style from adding a second inset.
        container_->setStyleSheet(QStringLiteral(
            "QFrame#houdini_chinese_button_bar {"
            "background:#373737; border:none; margin:0px; padding:0px; }"
            "QFrame#houdini_chinese_button_bar QToolButton {"
            "background:#444444; color:#85bdff; border:1px solid #292929;"
            "border-radius:6px; margin:0px; padding:0px 10px; }"
            "QFrame#houdini_chinese_button_bar QToolButton#houdini_chinese_language_button:enabled {"
            "color:#ffffff; }"
            "QFrame#houdini_chinese_button_bar QToolButton:hover {"
            "background:#535353; border-color:#737373; }"
            "QFrame#houdini_chinese_button_bar QToolButton:checked {"
            "background:#35649a; border-color:#91c4ff; }"
            "QFrame#houdini_chinese_button_bar QToolButton:checked:hover {"
            "background:#4075ae; border-color:#b4d8ff; }"
            "QFrame#houdini_chinese_button_bar QToolButton:pressed {"
            "background:#303747; border-color:#606c89; }"
            "QFrame#houdini_chinese_button_bar QToolButton:disabled {"
            "background:#3b3b3b; border-color:#303030; color:#808080; }"));
        auto *layout = new QHBoxLayout(container_);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(6);
        auto *button = new Button(container_);
        languageButton_ = button;
        button->setObjectName(QStringLiteral("houdini_chinese_language_button"));
        button->setText(QStringLiteral("中/英"));
#if defined(HC_HOUDINI_21)
        // H21's host style reports an icon-sized hint for text-only tools.
        button->setMinimumWidth(
            QFontMetrics(button->font()).horizontalAdvance(button->text()) + 24);
#endif
        button->setAccessibleName(QStringLiteral("切换中英文"));
        button->setCheckable(true);
        button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
        connect(button, &QToolButton::clicked, this,
                [this] { queue(Action::ToggleLanguage); });
        button->secondaryAction = [this] { queue(Action::EditShortcut); };
        layout->addWidget(button);
        parent->installEventFilter(this);
        layoutTimer_ = new QTimer(this);
        layoutTimer_->setInterval(16);
        connect(layoutTimer_, &QTimer::timeout, this, [this] {
            refresh();
            if (--layoutFramesRemaining_ <= 0)
                layoutTimer_->stop();
        });
        scheduleRefresh();
        updateState();
    }
    ~Pair() override { delete container_.data(); }

    // Native layout settles across redraws. Follow only during this bounded
    // transition. Idle position polling is disabled; ordinary window events
    // and explicit language-layout notifications drive updates.
    void followLayoutChange() {
        refresh();
        layoutFramesRemaining_ = 12;
        if (!layoutTimer_->isActive())
            layoutTimer_->start();
    }

    void setState(bool enabled, bool available) {
        if (enabled_ == enabled && available_ == available)
            return;
        enabled_ = enabled;
        available_ = available;
        updateState();
    }
    void refresh() {
        if (!parent_ || !container_)
            return;
        if (!parent_->isVisible() || parent_->isMinimized()) {
            container_->hide();
            return;
        }
        // Avoid probing native layouts while Houdini is running a menu/modal
        // interaction. Existing geometry remains valid unless resize hid it.
        if (QApplication::activePopupWidget() || QApplication::activeModalWidget())
            return;
        const QRect space = locator_();
        const int height = space.height();
        const int width = container_->layout()->sizeHint().width();
        if (height < 16 || space.width() < width ||
            !parent_->rect().contains(space)) {
            container_->hide();
            return;
        }
        const QRect target(space.x(), space.y(), width, height);
        if (container_->geometry() != target)
            container_->setGeometry(target);
        if (!container_->isVisible()) {
            container_->show();
            container_->raise();
        }
    }

  protected:
    bool eventFilter(QObject *, QEvent *event) override {
        switch (event->type()) {
        case QEvent::Resize:
        case QEvent::Hide:
        case QEvent::WindowStateChange:
            if (container_)
                container_->hide();
            scheduleRefresh();
            break;
        case QEvent::Show:
        case QEvent::FontChange:
        case QEvent::StyleChange:
        case QEvent::ScreenChangeInternal:
            scheduleRefresh();
            break;
        default:
            break;
        }
        return false;
    }

  private:
    QPointer<QWidget> parent_;
    QPointer<QFrame> container_;
    QPointer<Button> languageButton_;
    QTimer *layoutTimer_ = nullptr;
    int layoutFramesRemaining_ = 0;
    Handler handler_;
    Locator locator_;
    bool pending_ = false;
    bool enabled_ = true;
    bool available_ = false;

    void scheduleRefresh() {
        if (pending_)
            return;
        pending_ = true;
        QTimer::singleShot(0, this, [this] { pending_ = false; refresh(); });
    }
    void queue(Action action) {
        QTimer::singleShot(0, this, [this, action] {
            if (parent_ && parent_->isVisible() && container_ &&
                container_->isVisible() && !QApplication::activeModalWidget() &&
                available_) {
                handler_(action);
            } else {
                // A resize, dictionary failure or modal can invalidate a click
                // before its deferred action runs. Restore the checked state.
                updateState();
            }
        });
    }
    void updateState() {
        if (auto *button = languageButton_.data()) {
            const QSignalBlocker blocker(button);
            button->setChecked(enabled_ && available_);
            button->setEnabled(available_);
        }
    }
};

} // namespace HoudiniChineseButtons
