#include "../../source/translation/display_text.h"

#include <iostream>
#include <string>

namespace {

bool check(bool condition, const char *message) {
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}

} // namespace

int main() {
    const QString text = QString::fromUtf8(
        "Curly \xE2\x80\x9Cquote\xE2\x80\x9D \xF0\x9F\x98\x80");
    const std::string utf8 = text.toUtf8().toStdString();
    const HoudiniChineseUtf8::Stats stats =
        HoudiniChineseUtf8::stats(text, 0, text.size());

    if (!check(stats.length == utf8.size(), "UTF-8 byte length matches Qt") ||
        !check(HoudiniChineseUtf8::equals(text, 0, text.size(), utf8.data(),
                                          utf8.size()),
               "non-ASCII and supplementary text compares exactly") ||
        !check(
            !HoudiniChineseUtf8::equals(text, 0, text.size(), "different", 9),
            "different UTF-8 text does not compare equal")) {
        return 1;
    }

    const char32_t boundaries[] = {0,       0x7F,    0x80,     0x7FF,
                                    0x800,   0xD7FF,  0xE000,   0xFFFF,
                                    0x10000, 0x2FFFF, 0x100000, 0x10FFFF};
    for (char32_t boundary : boundaries) {
        const QString sample = QString::fromUcs4(&boundary, 1);
        const QByteArray bytes = sample.toUtf8();
        const auto sampleStats =
            HoudiniChineseUtf8::stats(sample, 0, sample.size());
        uint64_t expectedHash = HoudiniChineseUtf8::kFnvOffset;
        for (char byte : bytes) {
            expectedHash ^= static_cast<unsigned char>(byte);
            expectedHash *= HoudiniChineseUtf8::kFnvPrime;
        }
        if (!check(sampleStats.hash == expectedHash &&
                       sampleStats.length == bytes.size() &&
                       HoudiniChineseUtf8::equals(sample, 0, sample.size(),
                                                   bytes.constData(),
                                                   static_cast<size_t>(bytes.size())),
                   "UTF-8 width boundaries, embedded NUL and supplementary planes match Qt"))
            return 1;
    }
    const QString padded = QStringLiteral("[") + text + QStringLiteral("]");
    if (!check(HoudiniChineseUtf8::equals(padded, 1, padded.size() - 1,
                                          utf8.data(), utf8.size()),
               "substring bounds do not include neighboring characters") ||
        !check(HoudiniChineseUtf8::equals(QString(), 0, 0, nullptr, 0),
               "empty UTF-8 input does not dereference its buffer") ||
        !check(!HoudiniChineseUtf8::equals(text, 0, text.size(), utf8.data(),
                                           utf8.size() - 1),
               "truncated UTF-8 input does not compare equal"))
        return 1;

    std::cout << "utf8_text_test: OK\n";
    return 0;
}
