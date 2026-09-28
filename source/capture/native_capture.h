#pragma once

// ---------------------------------------------------------------------------
// native control capture
// ---------------------------------------------------------------------------
#include <algorithm>
#include <cstddef>
#include <unordered_set>
#include <vector>

namespace HoudiniChineseNativeControl {

// Houdini's private UI_Feel hierarchy backs controls that are not QWidgets.
// The callbacks are resolved from libUI at runtime so the scanner can climb
// from any painted child to its root and then visit hidden/collapsed siblings
// without opening controls or changing their selected state.
struct FeelApi final {
    using GetParentFn = void *(*)(void *feel);
    using GetChildCountFn = int (*)(void *feel);
    using GetChildFn = void *(*)(void *feel, int index);
    using GetClassNameFn = const char *(*)(void *feel);

    GetParentFn getParent = nullptr;
    GetChildCountFn getChildCount = nullptr;
    GetChildFn getChild = nullptr;
    GetClassNameFn getClassName = nullptr;

    explicit operator bool() const noexcept {
        return getParent && getChildCount && getChild && getClassName;
    }
};

struct FeelResult final {
    std::size_t roots = 0;
    std::size_t nodes = 0;
    bool truncated = false;
};

template <typename NodeSink>
FeelResult captureFromVisibleFeels(void *const *visibleFeels,
                                   std::size_t visibleFeelCount,
                                   const FeelApi &api, NodeSink &&nodeSink,
                                   std::size_t maxNodes = 1000000) {
    FeelResult result;
    if (!visibleFeels || visibleFeelCount == 0 || !api || maxNodes == 0)
        return result;

    std::vector<void *> roots;
    roots.reserve(visibleFeelCount);
    std::unordered_set<void *> uniqueRoots;
    uniqueRoots.reserve(visibleFeelCount);
    for (std::size_t index = 0; index < visibleFeelCount; ++index) {
        void *feel = visibleFeels[index];
        if (!feel)
            continue;
        std::unordered_set<void *> parentChain;
        parentChain.reserve(32);
        bool reachedRoot = false;
        for (std::size_t depth = 0; depth < maxNodes; ++depth) {
            if (!parentChain.insert(feel).second) {
                result.truncated = true;
                break;
            }
            void *parent = api.getParent(feel);
            if (!parent || parent == feel) {
                reachedRoot = true;
                break;
            }
            feel = parent;
        }
        if (!reachedRoot)
            result.truncated = true;
        if (uniqueRoots.insert(feel).second)
            roots.push_back(feel);
    }
    result.roots = roots.size();

    std::vector<void *> pending;
    pending.reserve(roots.size());
    for (auto root = roots.rbegin(); root != roots.rend(); ++root)
        pending.push_back(*root);

    std::unordered_set<void *> visited;
    visited.reserve((std::min)(maxNodes, std::size_t{8192}));
    while (!pending.empty() && result.nodes < maxNodes) {
        void *feel = pending.back();
        pending.pop_back();
        if (!feel || !visited.insert(feel).second)
            continue;

        ++result.nodes;
        nodeSink(feel, api.getClassName(feel));

        int childCount = api.getChildCount(feel);
        if (childCount < 0)
            childCount = 0;
        const std::size_t scheduled = pending.size();
        const std::size_t remaining = maxNodes - result.nodes;
        const std::size_t available =
            scheduled < remaining ? remaining - scheduled : 0;
        if (static_cast<std::size_t>(childCount) > available) {
            childCount = static_cast<int>(available);
            result.truncated = true;
        }
        for (int childIndex = childCount - 1; childIndex >= 0; --childIndex) {
            if (void *child = api.getChild(feel, childIndex))
                pending.push_back(child);
        }
    }
    result.truncated = result.truncated || !pending.empty();
    return result;
}

// Dynamic Houdini menus are backed by UI_ItemList rather than UI_Feel
// children. Each entry may return another UI_ItemList, forming the submenu
// hierarchy shown by the arrow controls in a popup.
struct ItemListApi final {
    using GetEntryCountFn = int (*)(void *list);
    using GetEntryFn = const char *(*)(void *list, int index);
    using GetSubmenuFn = void *(*)(void *list, int index);

    GetEntryCountFn getEntryCount = nullptr;
    GetEntryFn getEntry = nullptr;
    GetSubmenuFn getSubmenu = nullptr;

    explicit operator bool() const noexcept {
        return getEntryCount && getEntry && getSubmenu;
    }
};

struct ItemListResult final {
    std::size_t lists = 0;
    std::size_t entries = 0;
    bool truncated = false;
};

template <typename TextSink>
ItemListResult captureItemLists(void *const *rootLists,
                                std::size_t rootListCount,
                                const ItemListApi &api, TextSink &&textSink,
                                std::size_t maxEntries = 1000000) {
    ItemListResult result;
    if (!rootLists || rootListCount == 0 || !api || maxEntries == 0)
        return result;

    std::vector<void *> pending;
    pending.reserve(rootListCount);
    for (std::size_t index = rootListCount; index > 0; --index) {
        if (rootLists[index - 1])
            pending.push_back(rootLists[index - 1]);
    }

    std::unordered_set<void *> visited;
    visited.reserve((std::min)(rootListCount, std::size_t{8192}));
    while (!pending.empty() && result.entries < maxEntries) {
        void *list = pending.back();
        pending.pop_back();
        if (!list || !visited.insert(list).second)
            continue;
        ++result.lists;

        int entryCount = api.getEntryCount(list);
        if (entryCount < 0)
            entryCount = 0;
        const std::size_t remaining = maxEntries - result.entries;
        if (static_cast<std::size_t>(entryCount) > remaining) {
            entryCount = static_cast<int>(remaining);
            result.truncated = true;
        }
        for (int index = 0; index < entryCount; ++index) {
            if (const char *entry = api.getEntry(list, index))
                textSink(entry);
            ++result.entries;
            if (void *submenu = api.getSubmenu(list, index))
                pending.push_back(submenu);
        }
    }
    result.truncated = result.truncated || !pending.empty();
    return result;
}

} // namespace HoudiniChineseNativeControl

// ---------------------------------------------------------------------------
// native tree capture
// ---------------------------------------------------------------------------
#include <algorithm>
#include <cstddef>
#include <unordered_set>
#include <vector>

namespace HoudiniChineseNativeTree {

// Houdini's SI_Tree is not a QTreeView. The functions below are resolved from
// libSI at runtime so its model can be walked without changing expansion state.
struct Api final {
    using GetParentFn = void *(*)(void *node);
    using GetChildCountFn = int (*)(void *node);
    using GetChildFn = void *(*)(void *node, int index);
    using GetTextFn = const char *(*)(void *node);

    GetParentFn getParent = nullptr;
    GetChildCountFn getChildCount = nullptr;
    GetChildFn getChild = nullptr;
    GetTextFn getText = nullptr;

    explicit operator bool() const noexcept {
        return getParent && getChildCount && getChild && getText;
    }
};

struct Result final {
    std::size_t roots = 0;
    std::size_t nodes = 0;
    std::size_t texts = 0;
    bool truncated = false;
};

template <typename TextSink>
Result captureFromVisibleNodes(void *const *visibleNodes,
                               std::size_t visibleNodeCount, const Api &api,
                               TextSink &&textSink,
                               std::size_t maxNodes = 1000000) {
    Result result;
    if (!visibleNodes || visibleNodeCount == 0 || !api || maxNodes == 0)
        return result;

    // Rendering gives us one or more currently visible nodes. Climb each one
    // to the model root first; this makes collapsed siblings reachable too.
    std::vector<void *> roots;
    roots.reserve(visibleNodeCount);
    std::unordered_set<void *> uniqueRoots;
    uniqueRoots.reserve(visibleNodeCount);
    for (std::size_t index = 0; index < visibleNodeCount; ++index) {
        void *node = visibleNodes[index];
        if (!node)
            continue;
        std::unordered_set<void *> parentChain;
        parentChain.reserve(32);
        bool reachedRoot = false;
        for (std::size_t depth = 0; depth < maxNodes; ++depth) {
            if (!parentChain.insert(node).second) {
                result.truncated = true;
                break;
            }
            void *parent = api.getParent(node);
            if (!parent || parent == node) {
                reachedRoot = true;
                break;
            }
            node = parent;
        }
        if (!reachedRoot)
            result.truncated = true;
        if (uniqueRoots.insert(node).second)
            roots.push_back(node);
    }
    result.roots = roots.size();

    std::vector<void *> pending;
    pending.reserve(roots.size());
    for (auto root = roots.rbegin(); root != roots.rend(); ++root)
        pending.push_back(*root);

    std::unordered_set<void *> visited;
    visited.reserve((std::min)(maxNodes, std::size_t{8192}));
    while (!pending.empty() && result.nodes < maxNodes) {
        void *node = pending.back();
        pending.pop_back();
        if (!node || !visited.insert(node).second)
            continue;

        ++result.nodes;
        const char *text = api.getText(node);
        if (text && *text) {
            textSink(text);
            ++result.texts;
        }

        // Houdini's getNumChildNodes/getChildNode create lazy children before
        // returning them, so folded branches are followed through the model.
        int childCount = api.getChildCount(node);
        if (childCount < 0)
            childCount = 0;
        const std::size_t scheduled = pending.size();
        const std::size_t remaining = maxNodes - result.nodes;
        const std::size_t available =
            scheduled < remaining ? remaining - scheduled : 0;
        if (static_cast<std::size_t>(childCount) > available) {
            childCount = static_cast<int>(available);
            result.truncated = true;
        }
        for (int childIndex = childCount - 1; childIndex >= 0; --childIndex) {
            if (void *child = api.getChild(node, childIndex))
                pending.push_back(child);
        }
    }
    result.truncated = result.truncated || !pending.empty();
    return result;
}

} // namespace HoudiniChineseNativeTree
