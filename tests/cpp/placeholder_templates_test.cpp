#include "../../source/translation/placeholder_templates.h"
#include "../../source/translation/translation_json.h"

#include <fstream>
#include <iostream>
#include <iterator>

namespace {
bool check(bool condition, const char *message) {
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}
} // namespace

int main(int argc, char **argv) {
    HoudiniChinesePlaceholders::Catalog catalog;
    if (!check(catalog.addOrAssign(
                   QStringLiteral("Frame {current:int} of {total:int}"),
                   QStringLiteral("第 {current:int} 帧，共 {total:int} 帧")),
               "valid integer template is accepted") ||
        !check(
            catalog.addOrAssign(
                QStringLiteral(
                    "Rendered {count:int} frames in {seconds:number} seconds"),
                QStringLiteral(
                    "在 {seconds:number} 秒内渲染了 {count:int} 帧")),
            "translation may reorder fields") ||
        !check(catalog.addOrAssign(QStringLiteral("Progress: {value:percent}"),
                                   QStringLiteral("进度：{value:percent}")),
               "percent template is accepted") ||
        !check(catalog.lookup(QStringLiteral("Frame 12 of 240")) ==
                   QStringLiteral("第 12 帧，共 240 帧"),
               "integer values are restored") ||
        !check(catalog.lookup(
                   QStringLiteral("Rendered 120 frames in 3.5 seconds")) ==
                   QStringLiteral("在 3.5 秒内渲染了 120 帧"),
               "number values are restored after reordering") ||
        !check(catalog.lookup(QStringLiteral("Progress: 75%")) ==
                   QStringLiteral("进度：75%"),
               "percent is preserved") ||
        !check(catalog.lookup(QStringLiteral("Frame 1,200 of 2,400")) ==
                   QStringLiteral("第 1,200 帧，共 2,400 帧"),
               "grouped integers are preserved") ||
        !check(catalog.lookup(
                   QStringLiteral("Rendered 12 frames in 1.25e+2 seconds")) ==
                   QStringLiteral("在 1.25e+2 秒内渲染了 12 帧"),
               "scientific notation is preserved") ||
        !check(catalog.lookup(QStringLiteral("Progress: 75 %")) ==
                   QStringLiteral("进度：75 %"),
               "space before percent is preserved") ||
        !check(catalog.addOrAssign(QStringLiteral("Value {{raw}}: {value:int}"),
                                   QStringLiteral("值 {{raw}}：{value:int}")),
               "escaped braces are accepted") ||
        !check(catalog.lookup(QStringLiteral("Value {raw}: 9")) ==
                   QStringLiteral("值 {raw}：9"),
               "escaped braces render as literals") ||
        !check(catalog.lookup(QStringLiteral("Frame node1 of 240")).isEmpty(),
               "arbitrary text is rejected") ||
        !check(catalog.lookup(QStringLiteral("Frame 12,34 of 240")).isEmpty(),
               "invalid digit grouping is rejected") ||
        !check(!catalog.addOrAssign(QStringLiteral("Value {value:text}"),
                                    QStringLiteral("值 {value:text}")),
               "unbounded text fields are rejected") ||
        !check(!catalog.addOrAssign(QStringLiteral("Value {value:int}"),
                                    QStringLiteral("值 {other:int}")),
               "mismatched fields are rejected") ||
        !check(!catalog.addOrAssign(QStringLiteral("{value:int} items"),
                                    QStringLiteral("{value:int} 项")),
               "templates require a fixed prefix") ||
        !check(
            !catalog.addOrAssign(QStringLiteral("Range {start:int}{end:int}"),
                                 QStringLiteral("范围 {start:int}–{end:int}")),
            "adjacent placeholders are rejected") ||
        !check(!catalog.addOrAssign(
                   QStringLiteral("Value {value:int}"),
                   QStringLiteral("值 {value:int} / {value:int}")),
               "duplicate translation placeholders are rejected")) {
        return 1;
    }

    HoudiniChinesePlaceholders::Catalog punctuation;
    if (!check(punctuation.addOrAssign(QStringLiteral("Value {v:number}."),
                                      QStringLiteral("数值 {v:number}。")) &&
                   punctuation.lookup(QStringLiteral("Value 1.25.")) ==
                       QStringLiteral("数值 1.25。"),
               "decimal punctuation inside a capture is not the suffix") ||
        !check(punctuation.addOrAssign(QStringLiteral("Pair {a:int},{b:int}."),
                                      QStringLiteral("数对 {a:int}、{b:int}。")) &&
                   punctuation.lookup(QStringLiteral("Pair 1,000,2.")) ==
                       QStringLiteral("数对 1,000、2。"),
               "separator matching retries when an early split cannot finish") ||
        !check(punctuation.lookup(QStringLiteral("Value 1.25.extra")).isEmpty(),
               "a suffix must consume the entire source"))
        return 1;

    HoudiniChinesePlaceholders::Catalog bounded;
    QString ambiguous = QStringLiteral("Stress ");
    for (size_t index = 0; index < HoudiniChinesePlaceholders::kMaximumFields;
         ++index) {
        if (index != 0)
            ambiguous += QLatin1Char('1');
        ambiguous += QStringLiteral("{n%1:int}").arg(index);
    }
    ambiguous += QLatin1Char('.');
    if (!check(bounded.addOrAssign(ambiguous, ambiguous) &&
                   bounded.lookup(QStringLiteral("Stress ") +
                                  QString(128, QLatin1Char('1')) +
                                  QStringLiteral("invalid")).isEmpty(),
               "ambiguous numeric templates stop at the shared work budget"))
        return 1;

    const std::string document =
        R"({"templates":{"Frame {current:int} of {total:int}":"第 {current:int} 帧，共 {total:int} 帧"}})";
    HoudiniChineseJson::DictionaryDocument parsed;
    HoudiniChinesePlaceholders::Catalog loaded;
    if (!check(HoudiniChineseJson::parseDictionaryDocument(document, parsed),
               "template-only JSON document parses"))
        return 1;
    for (const auto &entry : parsed.templates) {
        if (!check(loaded.addOrAssign(QString::fromUtf8(entry.first.data(),
                                                        entry.first.size()),
                                      QString::fromUtf8(entry.second.data(),
                                                        entry.second.size())),
                   "JSON template compiles"))
            return 1;
    }
    if (!check(loaded.lookup(QStringLiteral("Frame 8 of 24")) ==
                   QStringLiteral("第 8 帧，共 24 帧"),
               "JSON load to runtime lookup works end to end"))
        return 1;

    for (int index = 1; index < argc; ++index) {
        std::ifstream file(argv[index], std::ios::binary);
        if (!check(file.is_open(), "template dictionary can be opened"))
            return 1;
        const std::string json{std::istreambuf_iterator<char>(file),
                               std::istreambuf_iterator<char>()};
        HoudiniChineseJson::DictionaryDocument repository;
        if (!check(
                HoudiniChineseJson::parseDictionaryDocument(json, repository) &&
                    repository.hasTemplates,
                "template dictionary JSON is valid"))
            return 1;
        HoudiniChinesePlaceholders::Catalog repositoryCatalog;
        for (const auto &entry : repository.templates) {
            QString error;
            if (!repositoryCatalog.addOrAssign(
                    QString::fromUtf8(entry.first.data(), entry.first.size()),
                    QString::fromUtf8(entry.second.data(), entry.second.size()),
                    &error)) {
                std::cerr << "INVALID TEMPLATE: " << entry.first << ": "
                          << error.toStdString() << '\n';
                return 1;
            }
        }
    }

    std::cout << "placeholder_templates_test: OK\n";
    return 0;
}
