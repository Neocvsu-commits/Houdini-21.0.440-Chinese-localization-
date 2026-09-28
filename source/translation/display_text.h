#pragma once

// ---------------------------------------------------------------------------
// utf8 text
// ---------------------------------------------------------------------------
#include <QtCore/QString>

#include <cstddef>
#include <cstdint>

namespace HoudiniChineseUtf8 {

constexpr uint64_t kFnvOffset = 1469598103934665603ULL;
constexpr uint64_t kFnvPrime = 1099511628211ULL;

inline uint32_t nextCodePoint(const QString &text, qsizetype &index,
                              qsizetype end) noexcept {
    const uint16_t first = text.at(index++).unicode();
    if (first >= 0xD800 && first <= 0xDBFF && index < end) {
        const uint16_t second = text.at(index).unicode();
        if (second >= 0xDC00 && second <= 0xDFFF) {
            ++index;
            return 0x10000U +
                   ((static_cast<uint32_t>(first) - 0xD800U) << 10U) +
                   (static_cast<uint32_t>(second) - 0xDC00U);
        }
    }
    if (first >= 0xD800 && first <= 0xDFFF)
        return 0xFFFDU;
    return first;
}

template <typename Consumer>
inline void forEachCodePoint(const QString &text, qsizetype begin,
                             qsizetype end, Consumer consume) noexcept {
    qsizetype index = begin;
    while (index < end)
        consume(nextCodePoint(text, index, end));
}

template <typename Consumer>
inline void forEachByte(const QString &text, qsizetype begin, qsizetype end,
                        Consumer consume) noexcept {
    forEachCodePoint(text, begin, end, [&consume](uint32_t value) {
        if (value <= 0x7F) {
            consume(static_cast<unsigned char>(value));
        } else if (value <= 0x7FF) {
            consume(static_cast<unsigned char>(0xC0U | (value >> 6U)));
            consume(static_cast<unsigned char>(0x80U | (value & 0x3FU)));
        } else if (value <= 0xFFFF) {
            consume(static_cast<unsigned char>(0xE0U | (value >> 12U)));
            consume(
                static_cast<unsigned char>(0x80U | ((value >> 6U) & 0x3FU)));
            consume(static_cast<unsigned char>(0x80U | (value & 0x3FU)));
        } else {
            consume(static_cast<unsigned char>(0xF0U | (value >> 18U)));
            consume(
                static_cast<unsigned char>(0x80U | ((value >> 12U) & 0x3FU)));
            consume(
                static_cast<unsigned char>(0x80U | ((value >> 6U) & 0x3FU)));
            consume(static_cast<unsigned char>(0x80U | (value & 0x3FU)));
        }
    });
}

struct Stats final {
    uint64_t hash = kFnvOffset;
    uint32_t length = 0;
};

inline Stats stats(const QString &text, qsizetype begin,
                   qsizetype end) noexcept {
    Stats result;
    forEachByte(text, begin, end, [&result](unsigned char value) {
        result.hash ^= value;
        result.hash *= kFnvPrime;
        ++result.length;
    });
    return result;
}

inline bool equals(const QString &text, qsizetype begin, qsizetype end,
                   const char *utf8, size_t utf8Length) noexcept {
    size_t position = 0;
    bool equal = true;
    forEachByte(text, begin, end,
                [utf8, utf8Length, &position, &equal](unsigned char value) {
                    if (position >= utf8Length ||
                        static_cast<unsigned char>(utf8[position]) != value) {
                        equal = false;
                    }
                    ++position;
                });
    return equal && position == utf8Length;
}

} // namespace HoudiniChineseUtf8

// ---------------------------------------------------------------------------
// display text
// ---------------------------------------------------------------------------
#include <QtCore/QString>

namespace HoudiniChineseDisplayText {

struct LookupKey final {
    QString text;
    bool modified = false;
};

inline QString removeMnemonicMarkers(const QString &text) {
    if (!text.contains(QLatin1Char('&')))
        return text;
    QString result;
    result.reserve(text.size());
    for (qsizetype index = 0; index < text.size(); ++index) {
        if (text[index] != QLatin1Char('&')) {
            result += text[index];
            continue;
        }
        if (index + 1 < text.size() && text[index + 1] == QLatin1Char('&')) {
            result += QLatin1Char('&');
            ++index;
            continue;
        }
        const bool hasNext = index + 1 < text.size();
        const QChar next = hasNext ? text[index + 1] : QChar();
        const bool likelyMnemonic =
            hasNext && next.isLetterOrNumber() &&
            (index == 0 || text[index - 1].isSpace() || next.isLower());
        if (!likelyMnemonic)
            result += QLatin1Char('&');
    }
    return result;
}

inline LookupKey normalizedFallbackKey(const QString &trimmed) {
    LookupKey result{trimmed, false};
    if (result.text.endsWith(QLatin1Char('*'))) {
        result.text.chop(1);
        result.text = result.text.trimmed();
        result.modified = true;
    }
    result.text = removeMnemonicMarkers(result.text);
    return result;
}

inline QString preserveOuterWhitespace(const QString &source,
                                       const QString &replacement) {
    qsizetype begin = 0;
    qsizetype end = source.size();
    while (begin < end && source[begin].isSpace())
        ++begin;
    while (end > begin && source[end - 1].isSpace())
        --end;
    if (begin == 0 && end == source.size())
        return replacement;
    return source.left(begin) + replacement + source.mid(end);
}

} // namespace HoudiniChineseDisplayText

// ---------------------------------------------------------------------------
// rich text translation
// ---------------------------------------------------------------------------
#include <QtCore/QChar>
#include <QtCore/QString>

namespace HoudiniChineseRichText {

inline qsizetype tagEnd(const QString &html, qsizetype start) {
    if (html.mid(start, 4) == QStringLiteral("<!--")) {
        const qsizetype commentEnd =
            html.indexOf(QStringLiteral("-->"), start + 4);
        return commentEnd < 0 ? -1 : commentEnd + 2;
    }
    if (html.mid(start, 9) == QStringLiteral("<![CDATA[")) {
        const qsizetype cdataEnd =
            html.indexOf(QStringLiteral("]]>"), start + 9);
        return cdataEnd < 0 ? -1 : cdataEnd + 2;
    }
    QChar quote;
    for (qsizetype index = start + 1; index < html.size(); ++index) {
        const QChar current = html.at(index);
        if (!quote.isNull()) {
            if (current == quote)
                quote = QChar();
            continue;
        }
        if (current == QLatin1Char('\'') || current == QLatin1Char('"')) {
            quote = current;
        } else if (current == QLatin1Char('>')) {
            return index;
        }
    }
    return -1;
}

struct TagInfo final {
    QString name;
    bool closing = false;
    bool selfClosing = false;
};

inline TagInfo parseTag(const QString &tag) {
    TagInfo result;
    if (tag.size() < 3 || tag.front() != QLatin1Char('<') ||
        tag.back() != QLatin1Char('>')) {
        return result;
    }

    qsizetype position = 1;
    while (position < tag.size() - 1 && tag.at(position).isSpace())
        ++position;
    if (position < tag.size() - 1 && tag.at(position) == QLatin1Char('/')) {
        result.closing = true;
        ++position;
        while (position < tag.size() - 1 && tag.at(position).isSpace())
            ++position;
    }
    if (position >= tag.size() - 1 || tag.at(position) == QLatin1Char('!') ||
        tag.at(position) == QLatin1Char('?')) {
        return result;
    }

    const qsizetype nameStart = position;
    while (position < tag.size() - 1) {
        const QChar value = tag.at(position);
        if (!(value.isLetterOrNumber() || value == QLatin1Char(':') ||
              value == QLatin1Char('_') || value == QLatin1Char('-'))) {
            break;
        }
        ++position;
    }
    if (position == nameStart)
        return result;
    result.name = tag.mid(nameStart, position - nameStart).toLower();

    qsizetype tail = tag.size() - 2;
    while (tail > position && tag.at(tail).isSpace())
        --tail;
    result.selfClosing = !result.closing && tag.at(tail) == QLatin1Char('/');
    return result;
}

// Replaces only complete text nodes. Markup, attributes and raw style/script
// bodies remain byte-for-byte equivalent in the returned QString. An empty
// result means no text node changed or the markup was incomplete.
template <typename Translate>
QString translateTextNodes(const QString &source, Translate &&translate) {
    QString result;
    result.reserve(source.size());
    bool changed = false;
    QString rawTextElement;
    qsizetype cursor = 0;
    while (cursor < source.size()) {
        if (!rawTextElement.isEmpty()) {
            qsizetype closeStart = cursor;
            for (;;) {
                closeStart = source.indexOf(QStringLiteral("</"), closeStart,
                                            Qt::CaseInsensitive);
                if (closeStart < 0)
                    return {};
                const qsizetype closeEnd = tagEnd(source, closeStart);
                if (closeEnd < 0)
                    return {};
                const TagInfo closeInfo =
                    parseTag(source.mid(closeStart, closeEnd - closeStart + 1));
                if (closeInfo.closing && closeInfo.name == rawTextElement) {
                    result.append(source.constData() + cursor,
                                  closeEnd - cursor + 1);
                    cursor = closeEnd + 1;
                    rawTextElement.clear();
                    break;
                }
                closeStart = closeEnd + 1;
            }
            continue;
        }
        if (source.at(cursor) == QLatin1Char('<')) {
            const qsizetype end = tagEnd(source, cursor);
            if (end < 0)
                return {};
            const QString tag = source.mid(cursor, end - cursor + 1);
            const TagInfo info = parseTag(tag);
            if (!info.closing && !info.selfClosing &&
                (info.name == QStringLiteral("style") ||
                 info.name == QStringLiteral("script"))) {
                rawTextElement = info.name;
            }
            result.append(tag);
            cursor = end + 1;
            continue;
        }

        qsizetype textEnd = source.indexOf(QLatin1Char('<'), cursor);
        if (textEnd < 0)
            textEnd = source.size();
        const QString text = source.mid(cursor, textEnd - cursor);
        const QString translated = translate(text);
        if (!translated.isEmpty() && translated != text) {
            // The callback returns a dictionary's plain text, not trusted
            // markup. Comparison signs and ampersands must remain visible.
            result.append(translated.toHtmlEscaped());
            changed = true;
        } else {
            result.append(text);
        }
        cursor = textEnd;
    }
    if (!rawTextElement.isEmpty())
        return {};
    return changed ? result : QString();
}

} // namespace HoudiniChineseRichText

// ---------------------------------------------------------------------------
// translation lookup cache
// ---------------------------------------------------------------------------
#include <QtCore/QHashFunctions>
#include <QtCore/QString>

#include <array>
#include <cstddef>
#include <utility>

// Small per-thread cache for the immutable runtime dictionary. It caches both
// hits and misses so repeated paint/measurement calls do not redo exact,
// mnemonic and placeholder matching. Direct mapping keeps the hot path free of
// locks and allocations after a slot has warmed up.
namespace HoudiniChineseLookupCache {

// Long help documents and transient editor buffers may also reach the paint
// hooks. Translate them normally without retaining arbitrarily large strings
// in every render thread's cache.
constexpr qsizetype kMaximumCachedTextLength = 4096;

namespace detail {
struct OwnedStringCopy final {
    QString operator()(const QString &text) const {
        return QString(text.constData(), text.size());
    }
};
} // namespace detail

template <std::size_t SlotCount = 256,
          typename CopyText = detail::OwnedStringCopy>
class DirectMapped final {
    static_assert(SlotCount > 0 && (SlotCount & (SlotCount - 1)) == 0,
                  "SlotCount must be a power of two");

  public:
    template <typename Resolver>
    QString lookup(const QString &source, Resolver &&resolver) {
        if (source.size() > kMaximumCachedTextLength)
            return std::forward<Resolver>(resolver)();
        const std::size_t hash = qHash(source);
        Entry &entry = entries_[hash & (SlotCount - 1)];
        if (entry.valid && entry.hash == hash && entry.source == source)
            return entry.translation;

        QString translation = std::forward<Resolver>(resolver)();
        if (translation.size() > kMaximumCachedTextLength)
            return translation;
        // QString::fromRawData() can wrap borrowed storage. Copies alone do
        // not take ownership, so explicitly copy before retaining either text.
        // Complete both allocations before touching an occupied slot. A failed
        // copy must never pair a new key with the previous key's translation.
        QString ownedSource = CopyText{}(source);
        QString ownedTranslation = CopyText{}(translation);
        entry.source.swap(ownedSource);
        entry.translation.swap(ownedTranslation);
        entry.hash = hash;
        entry.valid = true;
        return translation;
    }

    void clear() noexcept {
        for (Entry &entry : entries_)
            entry = Entry{};
    }

  private:
    struct Entry final {
        std::size_t hash = 0;
        QString source;
        QString translation;
        bool valid = false;
    };

    std::array<Entry, SlotCount> entries_{};
};

} // namespace HoudiniChineseLookupCache
