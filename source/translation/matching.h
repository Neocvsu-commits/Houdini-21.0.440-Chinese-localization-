#pragma once

#include <QtCore/QHash>
#include <QtCore/QStringList>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace HoudiniChineseMatching {

struct Options {
    bool enabled = true;
    int threshold = 90; // Strictly greater than this percentage.
    int margin = 5;     // Distance from the best different translation.
};

inline QString normalize(const QString &text) {
    QString result = text;
    // Fold full-width Latin letters/digits only, never mathematical operators.
    for (qsizetype i = 0; i < result.size(); ++i) {
        const ushort c = result[i].unicode();
        if ((c >= 0xFF10 && c <= 0xFF19) || (c >= 0xFF21 && c <= 0xFF3A) ||
            (c >= 0xFF41 && c <= 0xFF5A)) result[i] = QChar(c - 0xFEE0);
    }
    return result.simplified().toCaseFolded();
}

// Only plain Latin display names qualify. Paths, operators, numbers and
// placeholder syntax never become aliases by removing punctuation.
inline QString nameKey(const QString &text) {
    if (text.size() > 64) return {};
    QString key;
    for (QChar ch : text) {
        if (ch >= QLatin1Char('a') && ch <= QLatin1Char('z')) key += ch;
        else if (ch != QLatin1Char(' ') && ch != QLatin1Char('_') &&
                 ch != QLatin1Char('-')) return {};
    }
    return key.size() >= 3 ? key : QString();
}

// A deliberately bounded domain for automatic typo matching. Punctuation,
// expressions, paths and variable placeholders require exact/template lookup.
inline bool eligible(const QString &text) {
    if (text.size() <= 5 || text.size() > 192) return false;
    int letters = 0;
    for (QChar ch : text) {
        if (ch >= QLatin1Char('a') && ch <= QLatin1Char('z')) ++letters;
        else if (!(ch >= QLatin1Char('0') && ch <= QLatin1Char('9')) &&
                 ch != QLatin1Char(' ') && ch != QLatin1Char('_') &&
                 ch != QLatin1Char('-')) return false;
    }
    return letters > 5;
}

inline QString signature(const QString &text) {
    static const QStringList critical = {
        "enable", "enabled", "disable", "disabled", "on", "off", "not",
        "no", "with", "without", "true", "false", "input", "output",
        "minimum", "maximum", "min", "max", "left", "right", "up", "down",
        "before", "after", "include", "exclude", "add", "remove"};
    QString words = text;
    words.replace(QLatin1Char('_'), QLatin1Char(' '));
    words.replace(QLatin1Char('-'), QLatin1Char(' '));
    QString result;
    for (const QString &word : words.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        if (critical.contains(word)) result += word + QLatin1Char('|');
    }
    // Preserve every digit run and its sign, including digits inside names.
    bool inNumber = false;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar ch = text[i];
        if (ch.isDigit()) {
            if (!inNumber) {
                result += QLatin1Char('#');
                if (i > 0 && text[i - 1] == QLatin1Char('-')) result += QLatin1Char('-');
            }
            result += ch;
            inNumber = true;
        } else inNumber = false;
    }
    return result;
}

inline std::vector<quint32> grams(const QString &text) {
    std::vector<quint32> result;
    for (qsizetype i = 2; i < text.size(); ++i)
        result.push_back((quint32(text[i - 2].unicode()) << 16) |
                         (quint32(text[i - 1].unicode()) << 8) | text[i].unicode());
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

struct Result {
    bool decided = false; // Includes ambiguity: do not fall through and guess.
    QString translation;
};

class Catalog {
    struct Entry { QString text, translation, guard; };
    QHash<QString, QString> normalized_, names_;
    QHash<quint32, std::vector<int>> postings_;
    std::vector<Entry> entries_;

    static void insert(QHash<QString, QString> &map, const QString &key,
                       const QString &translation) {
        if (key.isEmpty()) return;
        auto it = map.find(key);
        if (it == map.end()) map.insert(key, translation);
        else if (it.value() != translation) it.value().clear();
    }
public:
    Options options;
    void clear() { normalized_.clear(); names_.clear(); postings_.clear(); entries_.clear(); }
    void add(const QString &source, const QString &translation) {
        if (source.isEmpty() || translation.isEmpty()) return;
        const QString key = normalize(source);
        insert(normalized_, key, translation);
        insert(names_, nameKey(key), translation);
    }
    void finalize() {
        postings_.clear(); entries_.clear();
        auto keys = normalized_.keys();
        std::sort(keys.begin(), keys.end());
        for (const QString &key : keys) {
            if (!eligible(key)) continue;
            const int id = static_cast<int>(entries_.size());
            entries_.push_back({key, normalized_.value(key), signature(key)});
            for (quint32 gram : grams(key)) postings_[gram].push_back(id);
        }
    }
    Result equivalent(const QString &source) const {
        const QString key = normalize(source);
        auto it = normalized_.constFind(key);
        if (it != normalized_.cend()) return {true, it.value()};
        it = names_.constFind(nameKey(key));
        return it == names_.cend() ? Result{} : Result{true, it.value()};
    }
    size_t conflicts() const {
        size_t count = 0;
        for (const auto &value : normalized_) if (value.isEmpty()) ++count;
        for (const auto &value : names_) if (value.isEmpty()) ++count;
        return count;
    }
    QStringList conflictKeys() const {
        QStringList result;
        for (auto it = normalized_.cbegin(); it != normalized_.cend(); ++it)
            if (it.value().isEmpty()) result += QStringLiteral("normalized: ") + it.key();
        for (auto it = names_.cbegin(); it != names_.cend(); ++it)
            if (it.value().isEmpty()) result += QStringLiteral("name: ") + it.key();
        result.sort();
        return result;
    }
    QString fuzzy(const QString &source) const {
        if (!options.enabled) return {};
        const QString key = normalize(source);
        if (!eligible(key)) return {};
        // Keep the index's common-trigram guarantee valid: candidate floor >=80.
        const int threshold = std::clamp(options.threshold, 90, 99);
        const int margin = std::clamp(options.margin, 1, 10);
        const int floor = threshold - margin;
        const QString guard = signature(key);
        std::vector<int> ids;
        const auto queryGrams = grams(key);
        size_t visited = 0;
        for (quint32 gram : queryGrams) {
            const auto it = postings_.constFind(gram);
            if (it == postings_.cend()) continue;
            // Exhaustion means no decision, never a decision on a partial set.
            visited += it->size();
            if (visited > 200000) return {};
            for (int id : *it) {
                const int n = static_cast<int>(key.size());
                const int m = static_cast<int>(entries_[static_cast<size_t>(id)].text.size());
                if (std::abs(n - m) <= (100 - floor) * std::max(n, m) / 100)
                    ids.push_back(id);
            }
        }
        std::sort(ids.begin(), ids.end());
        std::vector<int> candidates;
        for (size_t i = 0; i < ids.size();) {
            size_t end = i + 1;
            while (end < ids.size() && ids[end] == ids[i]) ++end;
            const int maximum = std::max(static_cast<int>(key.size()),
                static_cast<int>(entries_[static_cast<size_t>(ids[i])].text.size()));
            const int limit = (100 - floor) * maximum / 100;
            // One edit can remove at most three distinct query trigrams.
            const int required = std::max(1, static_cast<int>(queryGrams.size()) - 3 * limit);
            if (end - i >= static_cast<size_t>(required)) candidates.push_back(ids[i]);
            i = end;
        }
        if (candidates.size() > 8192) return {};
        double best = -1, second = -1;
        QString winner;
        int cells = 0;
        for (int id : candidates) {
            const Entry &entry = entries_[static_cast<size_t>(id)];
            const int n = static_cast<int>(key.size());
            const int m = static_cast<int>(entry.text.size());
            const int maximum = std::max(n, m);
            const int limit = (100 - floor) * maximum / 100;
            if (std::abs(n - m) > limit || entry.guard != guard) continue;
            std::array<int, 193> previous{}, current{};
            for (int j = 0; j <= m; ++j) previous[j] = j;
            bool rejected = false;
            for (int i = 1; i <= n; ++i) {
                current.fill(limit + 1);
                current[0] = i;
                int minimum = limit + 1;
                for (int j = std::max(1, i - limit); j <= std::min(m, i + limit); ++j) {
                    if (++cells > 1000000) return {};
                    current[j] = std::min({previous[j] + 1, current[j - 1] + 1,
                        previous[j - 1] + (key[i - 1] == entry.text[j - 1] ? 0 : 1)});
                    minimum = std::min(minimum, current[j]);
                }
                previous.swap(current);
                if (minimum > limit) { rejected = true; break; }
            }
            if (rejected || previous[m] > limit) continue;
            const double score = 100.0 * (maximum - previous[m]) / maximum;
            if (score > best) {
                if (entry.translation != winner) second = best;
                best = score; winner = entry.translation;
            } else if (entry.translation != winner) second = std::max(second, score);
        }
        return best > threshold && (second < 0 || best - second >= margin)
            ? winner : QString();
    }
};
} // namespace HoudiniChineseMatching
