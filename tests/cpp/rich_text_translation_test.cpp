#include "../../source/translation/display_text.h"

#include <QtCore/QString>

#include <cstdlib>
#include <iostream>

namespace {

void check(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

QString translateKnownText(const QString &source) {
    if (source == QStringLiteral("English body"))
        return QStringLiteral("中文正文");
    return {};
}

} // namespace

int main() {
    using HoudiniChineseRichText::translateTextNodes;

    check(translateTextNodes(
              QStringLiteral(
                  "<style>p { color: red; }</style><p>English body</p>"),
              translateKnownText) ==
              QStringLiteral("<style>p { color: red; }</style><p>中文正文</p>"),
          "style content must remain unchanged while visible text translates");

    check(translateTextNodes(
              QStringLiteral(
                  "<p title=\"English body > untouched\">English body</p>"),
              translateKnownText) ==
              QStringLiteral(
                  "<p title=\"English body > untouched\">中文正文</p>"),
          "quoted tag attributes must remain unchanged");

    check(
        translateTextNodes(
            QStringLiteral("<script>English body</script><b>English body</b>"),
            translateKnownText) ==
            QStringLiteral("<script>English body</script><b>中文正文</b>"),
        "script content must remain unchanged");

    check(translateTextNodes(
              QStringLiteral("<script>if (a < b) { x = '>'; }</script>"
                             "<b>English body</b>"),
              translateKnownText) ==
              QStringLiteral("<script>if (a < b) { x = '>'; }</script>"
                             "<b>中文正文</b>"),
          "angle brackets inside raw-text bodies must not confuse tag parsing");

    check(
        translateTextNodes(
            QStringLiteral(
                "<!-- English body > --><stylex>English body</stylex>"),
            translateKnownText) ==
            QStringLiteral("<!-- English body > --><stylex>中文正文</stylex>"),
        "comments and similarly prefixed element names must be classified "
        "exactly");

    check(translateTextNodes(
              QStringLiteral(
                  "<![CDATA[English body > untouched]]><p>English body</p>"),
              translateKnownText) ==
              QStringLiteral(
                  "<![CDATA[English body > untouched]]><p>中文正文</p>"),
          "CDATA markup must remain intact");

    check(
        translateTextNodes(QStringLiteral("<p>Unknown</p>"), translateKnownText)
            .isEmpty(),
        "unchanged rich text must return an empty sentinel");
    check(translateTextNodes(QStringLiteral("<p title=\"unfinished\""),
                             translateKnownText)
              .isEmpty(),
          "incomplete markup must fail closed");
    check(translateTextNodes(
              QStringLiteral("<p>English body</p><script>unfinished"),
              translateKnownText)
              .isEmpty(),
          "an unclosed raw-text element must discard partial translation");

    check(translateTextNodes(QStringLiteral("<p>Comparison</p>"),
                             [](const QString &) {
                                 return QStringLiteral("小于 < 5 & 大于 > 1");
                             }) ==
              QStringLiteral("<p>小于 &lt; 5 &amp; 大于 &gt; 1</p>"),
          "translated plain text must not become markup or an entity");
    check(translateTextNodes(QStringLiteral("<p>Unknown</p>"),
                             [](const QString &text) { return text; })
              .isEmpty(),
          "an identity translation must retain the unchanged sentinel");

    return 0;
}
