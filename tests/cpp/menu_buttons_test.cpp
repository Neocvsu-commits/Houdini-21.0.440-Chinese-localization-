#include "../../source/ui/menu_buttons.h"
#include "../../source/ui/input_events.h"
#include <QtCore/QEventLoop>

#include "../../source/hooks/native_rtti.h"
#include <memory>
#include <vector>
#include <cassert>
#include <cstdlib>
#include <iostream>

#undef assert
#define assert(expression)                                                     \
    do {                                                                       \
        if (!(expression)) {                                                   \
            std::cerr << "Assertion failed at line " << __LINE__ << ": "      \
                      << #expression << '\n';                                  \
            std::abort();                                                      \
        }                                                                      \
    } while (false)

using namespace HoudiniChineseButtons;

static void flush() {
    for (int i = 0; i < 3; ++i)
        QCoreApplication::processEvents();
}

static void waitMs(int milliseconds) {
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

static void sendMouse(QWidget *widget, QEvent::Type type,
                      Qt::MouseButton button, Qt::MouseButtons buttons,
                      QPoint position) {
    const QPoint global = widget->mapToGlobal(position);
    QMouseEvent event(type, QPointF(position), QPointF(global), button, buttons,
                      Qt::NoModifier);
    QCoreApplication::sendEvent(widget, &event);
}

static void mouseClick(QWidget *widget, Qt::MouseButton button) {
    const QPoint position = widget->rect().center();
    sendMouse(widget, QEvent::MouseButtonPress, button, button, position);
    sendMouse(widget, QEvent::MouseButtonRelease, button, Qt::NoButton, position);
}

struct FakeFeel {
    const char *type = ".?AVUI_Feel@@";
    QRect rect;
    FakeFeel *parent = nullptr;
    std::vector<std::unique_ptr<FakeFeel>> children;
    FakeFeel *add(QRect geometry, const char *name = ".?AVUI_Feel@@") {
        auto child = std::make_unique<FakeFeel>();
        child->type = name;
        child->rect = geometry;
        child->parent = this;
        auto *result = child.get();
        children.push_back(std::move(child));
        return result;
    }
};

static void checkNativeAnchor() {
    const HoudiniChineseMenuRow::Api api{
        [](void *p) -> void * { return static_cast<FakeFeel *>(p)->parent; },
        [](void *p) { return int(static_cast<FakeFeel *>(p)->children.size()); },
        [](void *p, int i) -> void * { return static_cast<FakeFeel *>(p)->children[size_t(i)].get(); },
        [](void *p, const char *name) { return p && std::strcmp(static_cast<FakeFeel *>(p)->type, name) == 0; },
        [](void *p) { return p ? static_cast<FakeFeel *>(p)->rect : QRect(); }
    };
    FakeFeel root;
    root.rect = QRect(0, 0, 1800, 1000);
    auto *row = root.add(QRect(15, 2, 1785, 35));
    auto *scroller = row->add(QRect(15, 2, 470, 35));
    scroller->add(QRect(15, 2, 470, 35), ".?AVUI_MenuBar@@");
    auto *group = row->add(QRect(485, 2, 900, 35));
    auto *desktop = group->add(QRect(500, 2, 234, 35));
    desktop->add(desktop->rect, ".?AVUI_DropDownMenu@@");
    auto *take = group->add(QRect(740, 2, 192, 35));
    take->add(take->rect, ".?AVUI_DropDownMenu@@");
    auto *spacer = group->add(QRect(938, 8, 2, 24));
    QRect space = HoudiniChineseMenuRow::availableSpace(&root, api);
    assert(space == QRect(945, 2, 435, 35));
    // Widening Take and changing other native controls changes the boundary.
    take->rect.setWidth(292);
    take->children[0]->rect = take->rect;
    spacer->rect.translate(100, 0);
    space = HoudiniChineseMenuRow::availableSpace(&root, api);
    assert(space.left() == 1045 && space.right() == 1379);
    auto *extra = group->add(QRect(1040, 2, 80, 35));
    assert(HoudiniChineseMenuRow::availableSpace(&root, api).left() == 1125);
    extra->add(extra->rect, ".?AVUI_DropDownMenu@@");
    assert(HoudiniChineseMenuRow::availableSpace(&root, api).isEmpty());
    extra->rect = QRect();
    assert(!HoudiniChineseMenuRow::availableSpace(&root, api).isEmpty());
    take->rect = QRect();
    assert(HoudiniChineseMenuRow::availableSpace(&root, api).isEmpty());
}

class RttiLeft { public: virtual ~RttiLeft() = default; int left = 1; };
class RttiRight { public: virtual ~RttiRight() = default; int right = 2; };
class RttiBoth final : public RttiLeft, public RttiRight {};
class RttiVirtual final : public virtual RttiLeft {};

static void checkBaseAdjustment() {
    RttiBoth object;
    RttiRight *right = &object;
    using HoudiniChineseNativeRtti::nonVirtualBase;
    assert(nonVirtualBase(right, ".?AVRttiLeft@@") == static_cast<RttiLeft *>(&object));
    assert(nonVirtualBase(&object, ".?AVRttiRight@@") == right);
    assert(nonVirtualBase(right, ".?AVUnknown@@") == nullptr);
    RttiVirtual virtualBase;
    assert(nonVirtualBase(&virtualBase, ".?AVRttiLeft@@") == nullptr);
}

static void checkOwnerWatch() {
    QObject lifetime;
    QPointer<QObject> owner;
    int queries = 0;
    OwnerWatch watch(&lifetime, [&]() -> QObject * { ++queries; return owner.data(); }, 5, 3);
    flush();
    waitMs(50);
    assert(queries == 3); // Startup failure stops after a bounded budget.
    waitMs(30);
    assert(queries == 3);
    owner = new QObject;
    QWidget newWindow;
    newWindow.show(); // A later main-window event restarts discovery.
    flush();
    assert(queries > 3);
    const int settled = queries;
    waitMs(50);
    assert(queries == settled); // No idle ownership polling after success.
    delete owner.data();
    owner = new QObject;
    flush();
    assert(queries == settled + 1); // Pair/owner destruction schedules repair.
    waitMs(30);
    assert(queries == settled + 1);
    for (auto *timer : watch.findChildren<QTimer *>()) assert(!timer->isActive());
    delete owner.data();
    // A pending callback is cancelled with its watcher.
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    checkOwnerWatch();
    checkNativeAnchor();
    checkBaseAdjustment();
    QWidget root;
    root.resize(640, 200);
    root.show();
    int toggles = 0, settings = 0;
    bool enabled = true;
    QRect space(10, 5, 570, 30);
    Pair *pair = nullptr;
    pair = new Pair(&root, [&](Action action) {
        if (action == Action::ToggleLanguage) {
            ++toggles;
            enabled = !enabled;
            pair->setState(enabled, true);
        } else if (action == Action::EditShortcut) {
            ++settings;
        }
    }, [&] { return space; });
    pair->setState(true, true);
    pair->refresh();
    flush();
    auto *language = root.findChild<QToolButton *>(QStringLiteral("houdini_chinese_language_button"));
    auto *link = root.findChild<QToolButton *>(QStringLiteral("houdini_chinese_author_button"));
    assert(language && !link && language->isVisible());
    auto *bar = root.findChild<QFrame *>(QStringLiteral("houdini_chinese_button_bar"));
    assert(bar && bar->testAttribute(Qt::WA_NativeWindow));
    assert(bar->geometry().left() == space.left());
    assert(language->isChecked());
    mouseClick(language, Qt::LeftButton);
    flush();
    assert(toggles == 1 && !language->isChecked());
    mouseClick(language, Qt::LeftButton);
    flush();
    assert(toggles == 2 && language->isChecked());
    mouseClick(language, Qt::RightButton);
    flush();
    assert(settings == 1 && toggles == 2);
    // Dragging off either gesture must never dispatch an action.
    sendMouse(language, QEvent::MouseButtonPress, Qt::LeftButton,
              Qt::LeftButton, language->rect().center());
    sendMouse(language, QEvent::MouseButtonRelease, Qt::LeftButton,
              Qt::NoButton, QPoint(-20, -20));
    sendMouse(language, QEvent::MouseButtonPress, Qt::RightButton,
              Qt::RightButton, language->rect().center());
    sendMouse(language, QEvent::MouseButtonRelease, Qt::RightButton,
              Qt::NoButton, QPoint(-20, -20));
    flush();
    assert(toggles == 2 && settings == 1);
    // Painting the same anchors must not duplicate buttons or callbacks.
    for (int i = 0; i < 10; ++i) {
        pair->refresh();
    }
    flush();
    assert(root.findChildren<QToolButton *>().size() == 1);
    pair->setState(false, true);
    assert(!language->isChecked());
    assert(language->toolTip().isEmpty());
    pair->setState(false, false);
    assert(!language->isEnabled());
    // The host can reflow without a Qt resize (e.g. Desktop/Take changes).
    space.translate(30, 0);
    pair->followLayoutChange();
    assert(bar->geometry().left() == space.left());
    // A later native layout pass is followed without waiting 500 ms.
    space.translate(20, 0);
    waitMs(60);
    assert(bar->geometry().left() == space.left());
    waitMs(250);
    for (auto *timer : pair->findChildren<QTimer *>()) {
        assert(timer->interval() != 500);
        if (timer->interval() == 16)
            assert(!timer->isActive());
    }
    // If the native layout is unavailable or too narrow, hide the control.
    space = QRect(20, 5, 30, 30);
    pair->refresh();
    assert(!language->isVisible());
    space = QRect();
    pair->refresh();
    assert(!bar->isVisible());
    space = QRect(20, 5, 570, 30);
    root.resize(700, 200);
    flush();
    assert(bar->isVisible() && bar->geometry().left() == 20);
    root.hide();
    flush();
    assert(!bar->isVisible());
    root.show();
    flush();
    assert(bar->isVisible());
    // Layout invalidation between click and queued dispatch cancels the action.
    pair->setState(true, true);
    language->click();
    space = QRect();
    pair->refresh();
    flush();
    assert(toggles == 2 && language->isChecked());
    // Destroying the owner cancels pending work safely.
    auto *temporary = new QWidget;
    new Pair(temporary, [&](Action) { assert(false); }, [] { return QRect(0, 0, 180, 30); });
    delete temporary;
    flush();
    std::cout << "menu_buttons_test: passed\n";
}
