#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>

namespace HoudiniChineseJson {

struct DictionaryDocument final {
    std::unordered_map<std::string, std::string> translations;
    std::unordered_map<std::string, std::string> templates;
    bool hasTranslations = false;
    bool hasTemplates = false;
};

inline void appendUtf8(std::string &output, uint32_t codePoint) {
    if (codePoint <= 0x7F) {
        output.push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7FF) {
        output.push_back(static_cast<char>(0xC0U | (codePoint >> 6U)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
    } else if (codePoint <= 0xFFFF) {
        output.push_back(static_cast<char>(0xE0U | (codePoint >> 12U)));
        output.push_back(
            static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
    } else {
        output.push_back(static_cast<char>(0xF0U | (codePoint >> 18U)));
        output.push_back(
            static_cast<char>(0x80U | ((codePoint >> 12U) & 0x3FU)));
        output.push_back(
            static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
    }
}

class Parser final {
  public:
    // Parsing can be repeated after construction, including construction from
    // a temporary string. Keep the input alive for the parser's full lifetime.
    explicit Parser(std::string json) : json_(std::move(json)) {}

    bool parseDictionaryDocument(DictionaryDocument &document) {
        position_ = 0;
        DictionaryDocument parsed;
        bool foundTranslations = false;
        bool foundTemplates = false;
        if (json_.size() >= 3 && static_cast<unsigned char>(json_[0]) == 0xEF &&
            static_cast<unsigned char>(json_[1]) == 0xBB &&
            static_cast<unsigned char>(json_[2]) == 0xBF) {
            position_ = 3;
        }
        skipWhitespace();
        if (!consume('{'))
            return false;
        skipWhitespace();
        if (!consume('}')) {
            while (true) {
                std::string key;
                if (!parseString(key))
                    return false;
                skipWhitespace();
                if (!consume(':'))
                    return false;
                skipWhitespace();
                if (key == "translations") {
                    if (foundTranslations ||
                        !parseStringMap(parsed.translations))
                        return false;
                    foundTranslations = true;
                } else if (key == "templates") {
                    if (foundTemplates || !parseStringMap(parsed.templates))
                        return false;
                    foundTemplates = true;
                } else if (!skipValue(0)) {
                    return false;
                }
                skipWhitespace();
                if (consume('}'))
                    break;
                if (!consume(','))
                    return false;
                skipWhitespace();
            }
        }
        skipWhitespace();
        if ((!foundTranslations && !foundTemplates) ||
            position_ != json_.size())
            return false;
        parsed.hasTranslations = foundTranslations;
        parsed.hasTemplates = foundTemplates;
        document = std::move(parsed);
        return true;
    }

  private:
    static bool validUtf8(const std::string &text) noexcept {
        size_t position = 0;
        while (position < text.size()) {
            const auto first = static_cast<unsigned char>(text[position++]);
            if (first <= 0x7F)
                continue;

            uint32_t codePoint = 0;
            int continuationCount = 0;
            uint32_t minimum = 0;
            if (first >= 0xC2 && first <= 0xDF) {
                codePoint = first & 0x1FU;
                continuationCount = 1;
                minimum = 0x80;
            } else if (first >= 0xE0 && first <= 0xEF) {
                codePoint = first & 0x0FU;
                continuationCount = 2;
                minimum = 0x800;
            } else if (first >= 0xF0 && first <= 0xF4) {
                codePoint = first & 0x07U;
                continuationCount = 3;
                minimum = 0x10000;
            } else {
                return false;
            }
            if (position + static_cast<size_t>(continuationCount) > text.size())
                return false;
            for (int index = 0; index < continuationCount; ++index) {
                const auto next = static_cast<unsigned char>(text[position++]);
                if ((next & 0xC0U) != 0x80U)
                    return false;
                codePoint = (codePoint << 6U) | (next & 0x3FU);
            }
            if (codePoint < minimum || codePoint > 0x10FFFFU ||
                (codePoint >= 0xD800U && codePoint <= 0xDFFFU)) {
                return false;
            }
        }
        return true;
    }

    static int hexDigit(char value) noexcept {
        if (value >= '0' && value <= '9')
            return value - '0';
        if (value >= 'a' && value <= 'f')
            return value - 'a' + 10;
        if (value >= 'A' && value <= 'F')
            return value - 'A' + 10;
        return -1;
    }

    void skipWhitespace() noexcept {
        while (position_ < json_.size()) {
            const char value = json_[position_];
            if (value != ' ' && value != '\t' && value != '\r' && value != '\n')
                break;
            ++position_;
        }
    }

    bool consume(char expected) noexcept {
        if (position_ >= json_.size() || json_[position_] != expected)
            return false;
        ++position_;
        return true;
    }

    bool readHexCodeUnit(uint16_t &value) noexcept {
        if (position_ + 4 > json_.size())
            return false;
        uint16_t result = 0;
        for (int index = 0; index < 4; ++index) {
            const int digit = hexDigit(json_[position_++]);
            if (digit < 0)
                return false;
            result = static_cast<uint16_t>((result << 4U) |
                                           static_cast<uint16_t>(digit));
        }
        value = result;
        return true;
    }

    bool parseUnicodeEscape(std::string &output) {
        uint16_t first = 0;
        if (!readHexCodeUnit(first))
            return false;
        if (first >= 0xDC00 && first <= 0xDFFF)
            return false;
        uint32_t codePoint = first;
        if (first >= 0xD800 && first <= 0xDBFF) {
            if (position_ + 2 > json_.size() || json_[position_] != '\\' ||
                json_[position_ + 1] != 'u') {
                return false;
            }
            position_ += 2;
            uint16_t second = 0;
            if (!readHexCodeUnit(second) || second < 0xDC00 || second > 0xDFFF)
                return false;
            codePoint = 0x10000U +
                        ((static_cast<uint32_t>(first) - 0xD800U) << 10U) +
                        (static_cast<uint32_t>(second) - 0xDC00U);
        }
        appendUtf8(output, codePoint);
        return true;
    }

    bool parseString(std::string &output) {
        if (!consume('"'))
            return false;
        output.clear();
        while (position_ < json_.size()) {
            const unsigned char value =
                static_cast<unsigned char>(json_[position_++]);
            if (value == '"')
                return validUtf8(output);
            if (value < 0x20)
                return false;
            if (value != '\\') {
                output.push_back(static_cast<char>(value));
                continue;
            }
            if (position_ >= json_.size())
                return false;
            const char escaped = json_[position_++];
            switch (escaped) {
            case '"':
                output.push_back('"');
                break;
            case '\\':
                output.push_back('\\');
                break;
            case '/':
                output.push_back('/');
                break;
            case 'b':
                output.push_back('\b');
                break;
            case 'f':
                output.push_back('\f');
                break;
            case 'n':
                output.push_back('\n');
                break;
            case 'r':
                output.push_back('\r');
                break;
            case 't':
                output.push_back('\t');
                break;
            case 'u':
                if (!parseUnicodeEscape(output))
                    return false;
                break;
            default:
                return false;
            }
        }
        return false;
    }

    bool parseStringMap(std::unordered_map<std::string, std::string> &output) {
        if (!consume('{'))
            return false;
        skipWhitespace();
        if (consume('}'))
            return true;
        while (true) {
            std::string key;
            std::string value;
            if (!parseString(key))
                return false;
            skipWhitespace();
            if (!consume(':'))
                return false;
            skipWhitespace();
            if (!parseString(value))
                return false;
            if (!output.emplace(std::move(key), std::move(value)).second)
                return false;
            skipWhitespace();
            if (consume('}'))
                return true;
            if (!consume(','))
                return false;
            skipWhitespace();
        }
    }

    bool skipNumber() noexcept {
        const size_t start = position_;
        consume('-');
        if (consume('0')) {
            if (position_ < json_.size() && json_[position_] >= '0' &&
                json_[position_] <= '9') {
                return false;
            }
        } else {
            if (position_ >= json_.size() || json_[position_] < '1' ||
                json_[position_] > '9') {
                return false;
            }
            while (position_ < json_.size() && json_[position_] >= '0' &&
                   json_[position_] <= '9') {
                ++position_;
            }
        }
        if (consume('.')) {
            const size_t digits = position_;
            while (position_ < json_.size() && json_[position_] >= '0' &&
                   json_[position_] <= '9') {
                ++position_;
            }
            if (digits == position_)
                return false;
        }
        if (position_ < json_.size() &&
            (json_[position_] == 'e' || json_[position_] == 'E')) {
            ++position_;
            if (position_ < json_.size() &&
                (json_[position_] == '+' || json_[position_] == '-')) {
                ++position_;
            }
            const size_t digits = position_;
            while (position_ < json_.size() && json_[position_] >= '0' &&
                   json_[position_] <= '9') {
                ++position_;
            }
            if (digits == position_)
                return false;
        }
        return position_ > start;
    }

    bool consumeLiteral(const char *literal) noexcept {
        const size_t start = position_;
        while (*literal) {
            if (position_ >= json_.size() || json_[position_++] != *literal++) {
                position_ = start;
                return false;
            }
        }
        return true;
    }

    bool skipValue(int depth) {
        if (depth > 64 || position_ >= json_.size())
            return false;
        if (json_[position_] == '"') {
            std::string ignored;
            return parseString(ignored);
        }
        if (consume('{')) {
            skipWhitespace();
            if (consume('}'))
                return true;
            while (true) {
                std::string key;
                if (!parseString(key))
                    return false;
                skipWhitespace();
                if (!consume(':'))
                    return false;
                skipWhitespace();
                if (!skipValue(depth + 1))
                    return false;
                skipWhitespace();
                if (consume('}'))
                    return true;
                if (!consume(','))
                    return false;
                skipWhitespace();
            }
        }
        if (consume('[')) {
            skipWhitespace();
            if (consume(']'))
                return true;
            while (true) {
                if (!skipValue(depth + 1))
                    return false;
                skipWhitespace();
                if (consume(']'))
                    return true;
                if (!consume(','))
                    return false;
                skipWhitespace();
            }
        }
        return consumeLiteral("true") || consumeLiteral("false") ||
               consumeLiteral("null") || skipNumber();
    }

    const std::string json_;
    size_t position_ = 0;
};

inline bool
parseTranslations(const std::string &json,
                  std::unordered_map<std::string, std::string> &translations) {
    DictionaryDocument document;
    if (!Parser(json).parseDictionaryDocument(document) ||
        !document.hasTranslations)
        return false;
    translations = std::move(document.translations);
    return true;
}

inline bool parseDictionaryDocument(const std::string &json,
                                    DictionaryDocument &document) {
    return Parser(json).parseDictionaryDocument(document);
}

} // namespace HoudiniChineseJson
