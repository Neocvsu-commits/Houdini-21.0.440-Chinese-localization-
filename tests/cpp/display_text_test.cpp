#include "../../source/translation/display_text.h"

#include <iostream>

namespace {
bool check(bool condition, const char *message) {
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}
} // namespace

int main() {
    using HoudiniChineseDisplayText::normalizedFallbackKey;
    using HoudiniChineseDisplayText::preserveOuterWhitespace;
    if (!check(normalizedFallbackKey(QStringLiteral("&Save")).text ==
                   QStringLiteral("Save"),
               "leading mnemonic is removed") ||
        !check(normalizedFallbackKey(QStringLiteral("E&xit")).text ==
                   QStringLiteral("Exit"),
               "embedded mnemonic is removed") ||
        !check(normalizedFallbackKey(QStringLiteral("Save &As")).text ==
                   QStringLiteral("Save As"),
               "word mnemonic is removed") ||
        !check(normalizedFallbackKey(QStringLiteral("Render & Export")).text ==
                   QStringLiteral("Render & Export"),
               "standalone ampersand is preserved") ||
        !check(normalizedFallbackKey(QStringLiteral("R&D")).text ==
                   QStringLiteral("R&D"),
               "uppercase abbreviation ampersand is preserved") ||
        !check(normalizedFallbackKey(QStringLiteral("Save && Close")).text ==
                   QStringLiteral("Save & Close"),
               "escaped literal ampersand is decoded") ||
        !check(normalizedFallbackKey(QStringLiteral("Save *")).text ==
                       QStringLiteral("Save") &&
                   normalizedFallbackKey(QStringLiteral("Save *")).modified,
               "modified suffix is separated") ||
        !check(preserveOuterWhitespace(QStringLiteral("  Save\t"),
                                       QStringLiteral("保存")) ==
                   QStringLiteral("  保存\t"),
               "lookup trimming does not remove display whitespace") ||
        !check(preserveOuterWhitespace(QStringLiteral("Save"),
                                       QStringLiteral("保存")) ==
                   QStringLiteral("保存"),
               "untrimmed labels return the replacement directly"))
        return 1;
    std::cout << "display_text_test: OK\n";
    return 0;
}
