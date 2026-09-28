#include "../../source/capture/native_capture.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

namespace {

struct Feel final {
    const char *className = nullptr;
    Feel *parent = nullptr;
    std::vector<Feel *> children;
    bool collapsed = false;
};

void *getParent(void *raw) { return static_cast<Feel *>(raw)->parent; }

int getChildCount(void *raw) {
    return static_cast<int>(static_cast<Feel *>(raw)->children.size());
}

void *getChild(void *raw, int index) {
    return static_cast<Feel *>(raw)->children.at(
        static_cast<std::size_t>(index));
}

const char *getClassName(void *raw) {
    return static_cast<Feel *>(raw)->className;
}

struct ItemList;
struct Item final {
    const char *text = nullptr;
    ItemList *submenu = nullptr;
};
struct ItemList final {
    std::vector<Item> entries;
};

int getEntryCount(void *raw) {
    return static_cast<int>(static_cast<ItemList *>(raw)->entries.size());
}

const char *getEntry(void *raw, int index) {
    return static_cast<ItemList *>(raw)
        ->entries.at(static_cast<std::size_t>(index))
        .text;
}

void *getSubmenu(void *raw, int index) {
    return static_cast<ItemList *>(raw)
        ->entries.at(static_cast<std::size_t>(index))
        .submenu;
}

int excessiveFeelChildCount(void *) { return 1000000; }

void *unexpectedFeelChild(void *, int) {
    assert(false && "bounded traversal must not request this child");
    return nullptr;
}

} // namespace

int main() {
    Feel window{"UI_Window"};
    Feel visible{"UI_MenuItem", &window};
    Feel foldedMenu{"UI_ParentMenuItem", &window, {}, true};
    Feel hiddenMenu{"UI_Menu", &foldedMenu};
    Feel hiddenEntry{"UI_MenuItem", &hiddenMenu};
    Feel switcher{"UI_Switcher", &window};
    Feel hiddenPage{"UI_GroupFeel", &switcher};
    window.children = {&visible, &foldedMenu, &switcher};
    foldedMenu.children = {&hiddenMenu};
    hiddenMenu.children = {&hiddenEntry};
    switcher.children = {&hiddenPage};

    HoudiniChineseNativeControl::FeelApi feelApi{&getParent, &getChildCount,
                                                 &getChild, &getClassName};
    void *painted[] = {&visible};
    std::vector<std::string> classes;
    const auto feelResult =
        HoudiniChineseNativeControl::captureFromVisibleFeels(
            painted, 1, feelApi,
            [&](void *, const char *name) { classes.emplace_back(name); });
    assert(feelResult.roots == 1);
    assert(feelResult.nodes == 7);
    assert(!feelResult.truncated);
    assert((classes == std::vector<std::string>{"UI_Window", "UI_MenuItem",
                                                "UI_ParentMenuItem", "UI_Menu",
                                                "UI_MenuItem", "UI_Switcher",
                                                "UI_GroupFeel"}));
    assert(foldedMenu.collapsed);

    Feel malformed{"UI_Malformed"};
    void *malformedRoot[] = {&malformed};
    HoudiniChineseNativeControl::FeelApi excessiveFeelApi{
        &getParent, &excessiveFeelChildCount, &unexpectedFeelChild,
        &getClassName};
    const auto excessiveFeel =
        HoudiniChineseNativeControl::captureFromVisibleFeels(
            malformedRoot, 1, excessiveFeelApi, [](void *, const char *) {}, 1);
    assert(excessiveFeel.nodes == 1);
    assert(excessiveFeel.truncated);

    Feel cycleA{"Cycle A"};
    Feel cycleB{"Cycle B"};
    cycleA.parent = &cycleB;
    cycleB.parent = &cycleA;
    void *cyclicVisible[] = {&cycleA};
    const auto cyclicFeel =
        HoudiniChineseNativeControl::captureFromVisibleFeels(
            cyclicVisible, 1, feelApi, [](void *, const char *) {}, 10);
    assert(cyclicFeel.nodes == 1);
    assert(cyclicFeel.truncated);

    Feel deepRoot{"Deep Root"};
    Feel deepMiddle{"Deep Middle", &deepRoot};
    Feel deepLeaf{"Deep Leaf", &deepMiddle};
    void *deepVisible[] = {&deepLeaf};
    const auto depthLimited =
        HoudiniChineseNativeControl::captureFromVisibleFeels(
            deepVisible, 1, feelApi, [](void *, const char *) {}, 2);
    assert(depthLimited.truncated);

    ItemList noise{{{"Anti-Aliased Noise"}, {"Patterns"}}};
    ItemList root{{{"Noise", &noise}, {"Shaders"}}};
    void *rootLists[] = {&root};
    HoudiniChineseNativeControl::ItemListApi itemApi{&getEntryCount, &getEntry,
                                                     &getSubmenu};
    std::vector<std::string> entries;
    const auto listResult = HoudiniChineseNativeControl::captureItemLists(
        rootLists, 1, itemApi,
        [&](const char *text) { entries.emplace_back(text); });
    assert(listResult.lists == 2);
    assert(listResult.entries == 4);
    assert(!listResult.truncated);
    assert((entries == std::vector<std::string>{"Noise", "Shaders",
                                                "Anti-Aliased Noise",
                                                "Patterns"}));

    std::cout << "native_control_capture_test: OK\n";
    return 0;
}
