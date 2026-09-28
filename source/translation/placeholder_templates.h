#pragma once

#include <QtCore/QString>
#include <QtCore/QStringView>

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace HoudiniChinesePlaceholders {

constexpr qsizetype kMaximumPatternLength = 2048;
constexpr qsizetype kMaximumCapturedLength = 128;
constexpr size_t kMaximumFields = 16;
constexpr size_t kMaximumMatchSteps = 4096;

enum class ValueType { Integer, Number, Percent };

struct Token final {
    bool placeholder = false;
    QString text;
    ValueType type = ValueType::Integer;
    size_t fieldIndex = 0;
};

struct Template final {
    QString source;
    std::vector<Token> sourceTokens;
    std::vector<Token> translationTokens;
    qsizetype literalLength = 0;
};

inline void setError(QString *error, const QString &message) {
    if (error)
        *error = message;
}

inline bool parseType(const QString &text, ValueType &type) {
    if (text == QStringLiteral("int"))
        type = ValueType::Integer;
    else if (text == QStringLiteral("number"))
        type = ValueType::Number;
    else if (text == QStringLiteral("percent"))
        type = ValueType::Percent;
    else
        return false;
    return true;
}

inline bool validName(const QString &name) {
    if (name.isEmpty())
        return false;
    const auto asciiLetter = [](QChar ch) {
        return (ch >= QLatin1Char('a') && ch <= QLatin1Char('z')) ||
               (ch >= QLatin1Char('A') && ch <= QLatin1Char('Z'));
    };
    if (!(asciiLetter(name[0]) || name[0] == QLatin1Char('_')))
        return false;
    for (qsizetype index = 1; index < name.size(); ++index) {
        const QChar ch = name[index];
        const bool asciiDigit =
            ch >= QLatin1Char('0') && ch <= QLatin1Char('9');
        if (!(asciiLetter(ch) || asciiDigit || ch == QLatin1Char('_')))
            return false;
    }
    return true;
}

inline void appendLiteral(std::vector<Token> &tokens, QChar ch) {
    if (!tokens.empty() && !tokens.back().placeholder)
        tokens.back().text += ch;
    else
        tokens.push_back({false, QString(ch), ValueType::Integer, 0});
}

inline bool parsePattern(const QString &pattern, std::vector<Token> &tokens,
                         std::unordered_map<std::string, ValueType> &fields,
                         QString *error = nullptr) {
    tokens.clear();
    fields.clear();
    if (pattern.isEmpty() || pattern.size() > kMaximumPatternLength) {
        setError(error, QStringLiteral("pattern is empty or too long"));
        return false;
    }
    for (qsizetype position = 0; position < pattern.size();) {
        const QChar ch = pattern[position];
        if (ch == QLatin1Char('{') && position + 1 < pattern.size() &&
            pattern[position + 1] == QLatin1Char('{')) {
            appendLiteral(tokens, QLatin1Char('{'));
            position += 2;
            continue;
        }
        if (ch == QLatin1Char('}') && position + 1 < pattern.size() &&
            pattern[position + 1] == QLatin1Char('}')) {
            appendLiteral(tokens, QLatin1Char('}'));
            position += 2;
            continue;
        }
        if (ch == QLatin1Char('}')) {
            setError(error, QStringLiteral("unescaped closing brace"));
            return false;
        }
        if (ch != QLatin1Char('{')) {
            appendLiteral(tokens, ch);
            ++position;
            continue;
        }
        const qsizetype close = pattern.indexOf(QLatin1Char('}'), position + 1);
        const qsizetype nested =
            pattern.indexOf(QLatin1Char('{'), position + 1);
        if (close < 0 || nested >= 0 && nested < close) {
            setError(error, QStringLiteral("unclosed or nested placeholder"));
            return false;
        }
        if (!tokens.empty() && tokens.back().placeholder) {
            setError(error,
                     QStringLiteral("adjacent placeholders are ambiguous"));
            return false;
        }
        const QString specification =
            pattern.mid(position + 1, close - position - 1);
        const qsizetype colon = specification.indexOf(QLatin1Char(':'));
        if (colon <= 0 || colon == specification.size() - 1 ||
            specification.indexOf(QLatin1Char(':'), colon + 1) >= 0) {
            setError(error, QStringLiteral("placeholder must be {name:type}"));
            return false;
        }
        const QString name = specification.left(colon);
        ValueType type{};
        if (!validName(name) ||
            !parseType(specification.mid(colon + 1), type)) {
            setError(error, QStringLiteral("invalid placeholder name or type"));
            return false;
        }
        const std::string key = name.toLatin1().toStdString();
        if (!fields.emplace(key, type).second) {
            setError(error, QStringLiteral("placeholder names must be unique"));
            return false;
        }
        if (fields.size() > kMaximumFields) {
            setError(error, QStringLiteral("too many placeholders"));
            return false;
        }
        tokens.push_back({true, name, type, 0});
        position = close + 1;
    }
    return !tokens.empty();
}

inline bool asciiDigits(QStringView value, qsizetype begin, qsizetype end) {
    if (begin >= end)
        return false;
    for (qsizetype index = begin; index < end; ++index)
        if (value[index] < QLatin1Char('0') || value[index] > QLatin1Char('9'))
            return false;
    return true;
}

inline bool validIntegerPart(QStringView value) {
    if (value.isEmpty())
        return false;
    const qsizetype firstComma = value.indexOf(QLatin1Char(','));
    if (firstComma < 0)
        return asciiDigits(value, 0, value.size());
    if (firstComma < 1 || firstComma > 3 || !asciiDigits(value, 0, firstComma))
        return false;
    qsizetype position = firstComma;
    while (position < value.size()) {
        if (value[position] != QLatin1Char(',') ||
            position + 4 > value.size() ||
            !asciiDigits(value, position + 1, position + 4))
            return false;
        position += 4;
    }
    return true;
}

inline bool isInteger(QStringView value) {
    if (value.isEmpty() || value != value.trimmed())
        return false;
    if (value[0] == QLatin1Char('-') || value[0] == QLatin1Char('+'))
        value = value.mid(1);
    return validIntegerPart(value);
}

inline bool isNumber(QStringView value) {
    if (value.isEmpty() || value != value.trimmed())
        return false;
    if (value[0] == QLatin1Char('-') || value[0] == QLatin1Char('+'))
        value = value.mid(1);
    qsizetype exponent = value.indexOf(QLatin1Char('e'));
    const qsizetype upperExponent = value.indexOf(QLatin1Char('E'));
    if (exponent < 0 || upperExponent >= 0 && upperExponent < exponent)
        exponent = upperExponent;
    const QStringView mantissa = exponent < 0 ? value : value.left(exponent);
    if (exponent >= 0) {
        if (value.indexOf(QLatin1Char('e'), exponent + 1) >= 0 ||
            value.indexOf(QLatin1Char('E'), exponent + 1) >= 0)
            return false;
        QStringView power = value.mid(exponent + 1);
        if (power.startsWith(QLatin1Char('-')) ||
            power.startsWith(QLatin1Char('+')))
            power = power.mid(1);
        if (!asciiDigits(power, 0, power.size()))
            return false;
    }
    const qsizetype dot = mantissa.indexOf(QLatin1Char('.'));
    if (dot >= 0 && mantissa.indexOf(QLatin1Char('.'), dot + 1) >= 0)
        return false;
    const QStringView integerPart = dot < 0 ? mantissa : mantissa.left(dot);
    const QStringView fraction = dot < 0 ? QStringView() : mantissa.mid(dot + 1);
    return validIntegerPart(integerPart) &&
           (dot < 0 || asciiDigits(fraction, 0, fraction.size()));
}

inline bool validValue(QStringView value, ValueType type) {
    if (value.isEmpty() || value.size() > kMaximumCapturedLength)
        return false;
    if (type == ValueType::Integer)
        return isInteger(value);
    if (type == ValueType::Number)
        return isNumber(value);
    if (!value.endsWith(QLatin1Char('%')))
        return false;
    QStringView number = value.left(value.size() - 1);
    while (number.endsWith(QLatin1Char(' ')))
        number = number.left(number.size() - 1);
    return isNumber(number);
}

class Catalog final {
  public:
    bool addOrAssign(const QString &source, const QString &translation,
                     QString *error = nullptr) {
        return addOrAssignImpl(source, translation, error, true);
    }

    bool addOrAssignDeferred(const QString &source, const QString &translation,
                             QString *error = nullptr) {
        return addOrAssignImpl(source, translation, error, false);
    }

    void finalize() { rebuildIndex(); }

    QString lookup(const QString &text) const {
        if (text.isEmpty() || text.size() > kMaximumPatternLength)
            return {};
        const auto bucket = index_.find(text[0].unicode());
        if (bucket == index_.end())
            return {};
        size_t remainingSteps = kMaximumMatchSteps;
        for (const size_t templateIndex : bucket->second) {
            if (remainingSteps == 0)
                return {};
            const Template &value = templates_[templateIndex];
            std::array<QStringView, kMaximumFields> captures;
            if (!matchSource(value.sourceTokens, QStringView(text), 0, 0,
                             captures, remainingSteps))
                continue;
            QString result;
            for (const Token &token : value.translationTokens) {
                const QStringView part = token.placeholder
                                             ? captures[token.fieldIndex]
                                             : QStringView(token.text);
                if (part.size() > kMaximumPatternLength - result.size())
                    return {};
                result += part;
            }
            return result;
        }
        return {};
    }

    void clear() {
        templates_.clear();
        index_.clear();
    }

  private:
    // Numeric punctuation can also be a literal delimiter (for example the
    // final period in "Value {v:number}."). Retry only bounded captures and
    // accept a split once the entire remaining pattern matches. The shared
    // step budget prevents ambiguous templates from stalling a paint call.
    static bool matchSource(const std::vector<Token> &tokens, QStringView text,
                            size_t tokenIndex, qsizetype position,
                            std::array<QStringView, kMaximumFields> &captures,
                            size_t &remainingSteps) {
        if (remainingSteps == 0)
            return false;
        --remainingSteps;
        while (tokenIndex < tokens.size() && !tokens[tokenIndex].placeholder) {
            const QStringView literal(tokens[tokenIndex].text);
            if (!text.mid(position).startsWith(literal))
                return false;
            position += literal.size();
            ++tokenIndex;
        }
        if (tokenIndex == tokens.size())
            return position == text.size();

        const Token &token = tokens[tokenIndex];
        if (tokenIndex + 1 == tokens.size()) {
            const QStringView captured = text.mid(position);
            if (!validValue(captured, token.type))
                return false;
            captures[token.fieldIndex] = captured;
            return true;
        }
        const QStringView nextLiteral(tokens[tokenIndex + 1].text);
        const qsizetype maximumEnd =
            (std::min)(text.size(), position + kMaximumCapturedLength);
        for (qsizetype end = text.indexOf(nextLiteral, position + 1);
             end >= 0 && end <= maximumEnd;
             end = text.indexOf(nextLiteral, end + 1)) {
            if (remainingSteps == 0)
                return false;
            --remainingSteps;
            const QStringView captured = text.mid(position, end - position);
            if (validValue(captured, token.type) &&
                matchSource(tokens, text, tokenIndex + 1, end, captures,
                            remainingSteps)) {
                captures[token.fieldIndex] = captured;
                return true;
            }
        }
        return false;
    }

    bool addOrAssignImpl(const QString &source, const QString &translation,
                         QString *error, bool rebuild) {
        std::vector<Token> sourceTokens;
        std::vector<Token> translationTokens;
        std::unordered_map<std::string, ValueType> sourceFields;
        std::unordered_map<std::string, ValueType> translationFields;
        if (!parsePattern(source, sourceTokens, sourceFields, error) ||
            !parsePattern(translation, translationTokens, translationFields,
                          error))
            return false;
        if (sourceFields.empty() || sourceFields != translationFields) {
            setError(
                error,
                QStringLiteral(
                    "source and translation placeholders must match exactly"));
            return false;
        }
        if (sourceTokens.front().placeholder ||
            sourceTokens.front().text.isEmpty()) {
            setError(error,
                     QStringLiteral("source template needs a fixed prefix"));
            return false;
        }
        std::unordered_map<std::string, size_t> fieldIndices;
        size_t nextField = 0;
        for (Token &token : sourceTokens) {
            if (!token.placeholder)
                continue;
            token.fieldIndex = nextField;
            fieldIndices.emplace(token.text.toLatin1().toStdString(),
                                 nextField++);
        }
        for (Token &token : translationTokens) {
            if (token.placeholder)
                token.fieldIndex =
                    fieldIndices.at(token.text.toLatin1().toStdString());
        }
        qsizetype literalLength = 0;
        for (const Token &token : sourceTokens)
            if (!token.placeholder)
                literalLength += token.text.size();
        Template value{source, std::move(sourceTokens),
                       std::move(translationTokens), literalLength};
        bool replaced = false;
        for (Template &existing : templates_) {
            if (existing.source == source) {
                existing = std::move(value);
                replaced = true;
                break;
            }
        }
        if (!replaced)
            templates_.push_back(std::move(value));
        if (rebuild)
            rebuildIndex();
        return true;
    }
    void rebuildIndex() {
        std::sort(templates_.begin(), templates_.end(),
                  [](const Template &left, const Template &right) {
                      if (left.literalLength != right.literalLength)
                          return left.literalLength > right.literalLength;
                      return left.source < right.source;
                  });
        index_.clear();
        for (size_t index = 0; index < templates_.size(); ++index)
            index_[templates_[index].sourceTokens.front().text[0].unicode()]
                .push_back(index);
    }

    std::vector<Template> templates_;
    std::unordered_map<unsigned short, std::vector<size_t>> index_;
};

} // namespace HoudiniChinesePlaceholders
