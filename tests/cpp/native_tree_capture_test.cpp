#include "../../source/capture/native_capture.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

namespace {

struct Node final {
    const char *text = nullptr;
    Node *parent = nullptr;
    std::vector<Node *> children;
    bool folded = false;
    int childQueries = 0;
};

void *getParent(void *raw) { return static_cast<Node *>(raw)->parent; }

int getChildCount(void *raw) {
    Node *node = static_cast<Node *>(raw);
    ++node->childQueries;
    return static_cast<int>(node->children.size());
}

void *getChild(void *raw, int index) {
    return static_cast<Node *>(raw)->children.at(
        static_cast<std::size_t>(index));
}

const char *getText(void *raw) { return static_cast<Node *>(raw)->text; }

int negativeChildCount(void *) { return -1; }

int excessiveChildCount(void *) { return 1000000; }

void *unexpectedChild(void *, int) {
    assert(false && "bounded traversal must not request this child");
    return nullptr;
}

} // namespace

int main() {
    Node root{"Root"};
    Node visible{"Visible", &root};
    Node folded{"Folded", &root, {}, true};
    Node hiddenChild{"Hidden Child", &folded};
    Node hiddenGrandchild{"Hidden Grandchild", &hiddenChild};
    root.children = {&visible, &folded};
    folded.children = {&hiddenChild};
    hiddenChild.children = {&hiddenGrandchild};

    HoudiniChineseNativeTree::Api api{&getParent, &getChildCount, &getChild,
                                      &getText};
    void *painted[] = {&visible};
    std::vector<std::string> captured;
    const auto result = HoudiniChineseNativeTree::captureFromVisibleNodes(
        painted, 1, api,
        [&](const char *text) { captured.emplace_back(text); });

    assert(result.roots == 1);
    assert(result.nodes == 5);
    assert(result.texts == 5);
    assert(!result.truncated);
    assert((captured == std::vector<std::string>{"Root", "Visible", "Folded",
                                                 "Hidden Child",
                                                 "Hidden Grandchild"}));
    // The folded flag is deliberately never changed; child discovery comes
    // from the backing hierarchy rather than disclosure state.
    assert(folded.folded);
    assert(folded.childQueries == 1);

    Node malformed{"Malformed"};
    void *malformedRoot[] = {&malformed};
    HoudiniChineseNativeTree::Api negativeApi{&getParent, &negativeChildCount,
                                              &unexpectedChild, &getText};
    const auto negative = HoudiniChineseNativeTree::captureFromVisibleNodes(
        malformedRoot, 1, negativeApi, [](const char *) {}, 1);
    assert(negative.nodes == 1);
    assert(!negative.truncated);

    HoudiniChineseNativeTree::Api excessiveApi{&getParent, &excessiveChildCount,
                                               &unexpectedChild, &getText};
    const auto excessive = HoudiniChineseNativeTree::captureFromVisibleNodes(
        malformedRoot, 1, excessiveApi, [](const char *) {}, 1);
    assert(excessive.nodes == 1);
    assert(excessive.truncated);

    Node cycleA{"Cycle A"};
    Node cycleB{"Cycle B"};
    cycleA.parent = &cycleB;
    cycleB.parent = &cycleA;
    void *cyclicVisible[] = {&cycleA};
    const auto cyclic = HoudiniChineseNativeTree::captureFromVisibleNodes(
        cyclicVisible, 1, api, [](const char *) {}, 10);
    assert(cyclic.nodes == 1);
    assert(cyclic.truncated);

    Node deepRoot{"Deep Root"};
    Node deepMiddle{"Deep Middle", &deepRoot};
    Node deepLeaf{"Deep Leaf", &deepMiddle};
    void *deepVisible[] = {&deepLeaf};
    const auto depthLimited = HoudiniChineseNativeTree::captureFromVisibleNodes(
        deepVisible, 1, api, [](const char *) {}, 2);
    assert(depthLimited.truncated);

    std::cout << "native_tree_capture_test: OK\n";
    return 0;
}
