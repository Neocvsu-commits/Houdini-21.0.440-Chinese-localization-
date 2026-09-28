#include "../../source/translation/matching.h"
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <cassert>
#include <iostream>
#include <random>

using namespace HoudiniChineseMatching;
static double referenceScore(const QString &a, const QString &b) {
    std::vector<int> row(static_cast<size_t>(b.size() + 1));
    for (int j = 0; j <= b.size(); ++j) row[static_cast<size_t>(j)] = j;
    for (int i = 1; i <= a.size(); ++i) {
        int diagonal = row[0]; row[0] = i;
        for (int j = 1; j <= b.size(); ++j) {
            const int old = row[static_cast<size_t>(j)];
            row[static_cast<size_t>(j)] = std::min({old + 1, row[static_cast<size_t>(j - 1)] + 1,
                diagonal + (a[i - 1] == b[j - 1] ? 0 : 1)});
            diagonal = old;
        }
    }
    return 100.0 * (std::max(a.size(), b.size()) - row.back()) / std::max(a.size(), b.size());
}

static void compareExhaustive() {
    std::mt19937 random(1729);
    Catalog index;
    std::vector<QString> entries;
    for (int i = 0; i < 180; ++i) {
        QString text;
        for (unsigned j = 0, length = 6 + random() % 65; j < length; ++j)
            text += QChar(static_cast<ushort>('a' + random() % 6));
        entries.push_back(text);
        index.add(text, QString::number(i));
    }
    index.finalize();
    for (size_t i = 0; i < entries.size(); ++i) {
        QString query = entries[i];
        const int edits = 1 + static_cast<int>(random() % 6);
        for (int e = 0; e < edits; ++e) {
            const qsizetype position = random() % query.size();
            switch (random() % 3) {
            case 0: query[position] = QLatin1Char('x'); break;
            case 1: query.insert(position, QLatin1Char('z')); break;
            default: query.remove(position, 1); break;
            }
        }
        double best = -1, second = -1; QString winner;
        for (size_t j = 0; j < entries.size(); ++j) {
            const double score = referenceScore(query, entries[j]);
            if (score > best) { second = best; best = score; winner = QString::number(j); }
            else second = std::max(second, score);
        }
        const QString expected = eligible(query) && best > 90 && best - second >= 5 ? winner : QString();
        assert(index.fuzzy(query) == expected);
    }
}
int main(int argc, char **argv) {
    compareExhaustive();
    Catalog catalog;
    catalog.add("Concrete", "混凝土");
    catalog.add("Rough Glass", "粗糙玻璃");
    catalog.add("Enable Surface Shadows", "启用表面阴影");
    catalog.add("Surface Shadows 12", "表面阴影12");
    catalog.add("abcdefghij", "十字符");
    catalog.finalize();
    for (const QString &name : {"roughglass", "Rough_Glass", "Rough-Glass",
                                "RoughGlass", " ROUGH\t GLASS "})
        assert(catalog.equivalent(name).translation == QStringLiteral("粗糙玻璃"));
    assert(catalog.equivalent("concrete").translation == QStringLiteral("混凝土"));
    assert(catalog.equivalent(QStringLiteral("Ｃｏｎｃｒｅｔｅ")).translation == QStringLiteral("混凝土"));
    assert(!catalog.equivalent("Rough/Glass").decided);
    assert(catalog.fuzzy("Rough Glsss") == QStringLiteral("粗糙玻璃"));
    assert(catalog.fuzzy("abcdefghiX").isEmpty()); // Exactly 90%, not >90%.
    assert(catalog.fuzzy("Disable Surface Shadows").isEmpty());
    assert(catalog.fuzzy("Surface Shadows 13").isEmpty());
    assert(catalog.fuzzy("Surface Shadows -12").isEmpty());
    assert(catalog.fuzzy("/Rough Glsss").isEmpty());
    assert(catalog.fuzzy("Rough Glsss {x}").isEmpty());
    assert(catalog.fuzzy("glass").isEmpty());
    catalog.options.enabled = false;
    assert(catalog.fuzzy("Rough Glsss").isEmpty());
    assert(!catalog.equivalent("roughglass").translation.isEmpty());
    catalog.options.enabled = true;
    catalog.options.threshold = 95;
    assert(catalog.fuzzy("Rough Glsss").isEmpty());

    Catalog conflict;
    conflict.add("AB CD", "一"); conflict.add("Abcd", "二");
    conflict.add("HELLO WORLD", "甲"); conflict.add("hello world", "乙");
    conflict.finalize();
    assert(conflict.equivalent("ab_cd").decided);
    assert(conflict.equivalent("ab_cd").translation.isEmpty());
    assert(conflict.equivalent("Hello World").translation.isEmpty());
    assert(conflict.conflicts() > 0);

    // Two equally plausible candidates with different translations are rejected.
    Catalog ambiguous;
    ambiguous.add("abcdefghijklmna", "甲");
    ambiguous.add("abcdefghijklmnz", "乙");
    ambiguous.finalize();
    assert(ambiguous.fuzzy("abcdefghijklmnx").isEmpty());
    Catalog same;
    same.add("abcdefghijklmna", "甲");
    same.add("abcdefghijklmnz", "甲");
    same.finalize();
    assert(same.fuzzy("abcdefghijklmnx") == QStringLiteral("甲"));

    if (argc > 1) {
        QFile file(QString::fromLocal8Bit(argv[1])); assert(file.open(QIODevice::ReadOnly));
        auto object = QJsonDocument::fromJson(file.readAll()).object()["translations"].toObject();
        Catalog full;
        QElapsedTimer timer; timer.start();
        for (auto it = object.begin(); it != object.end(); ++it) full.add(it.key(), it.value().toString());
        full.finalize();
        std::cout << "dictionary=" << object.size() << " index_ms=" << timer.elapsed()
                  << " conflicts=" << full.conflicts() << '\n';
        assert(full.equivalent("Rough_Glass").translation == QStringLiteral("粗糙玻璃"));
        assert(full.equivalent("concrete").translation == QStringLiteral("混凝土"));
        assert(full.fuzzy("Rough Glsss") == QStringLiteral("粗糙玻璃"));
        if (argc > 2) {
            QFile report(QString::fromLocal8Bit(argv[2])); assert(report.open(QIODevice::WriteOnly));
            report.write(full.conflictKeys().join(QLatin1Char('\n')).toUtf8());
        }
        timer.restart();
        int hits = 0;
        for (int i = 0; i < 100; ++i) {
            hits += !full.fuzzy("Wireframe Over Ghost Surfacez").isEmpty();
            full.fuzzy("Completely Unknown Rendering Wording");
        }
        std::cout << "200_uncached_queries_ms=" << timer.elapsed() << " hits=" << hits << '\n';
        assert(hits == 100);
    }
    std::cout << "matching tests passed\n";
}
