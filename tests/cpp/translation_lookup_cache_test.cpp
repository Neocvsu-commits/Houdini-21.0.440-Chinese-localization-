#include "../../source/translation/display_text.h"

#include <cassert>
#include <iostream>
#include <new>

struct FailingOwnedStringCopy final {
    inline static int copiesUntilFailure = -1;

    QString operator()(const QString &text) const {
        if (copiesUntilFailure == 0)
            throw std::bad_alloc();
        if (copiesUntilFailure > 0)
            --copiesUntilFailure;
        return HoudiniChineseLookupCache::detail::OwnedStringCopy{}(text);
    }
};

int main() {
    HoudiniChineseLookupCache::DirectMapped<4> cache;
    int resolutions = 0;

    const auto resolveHello = [&] {
        ++resolutions;
        return QStringLiteral("你好");
    };
    assert(cache.lookup(QStringLiteral("Hello"), resolveHello) ==
           QStringLiteral("你好"));
    assert(cache.lookup(QStringLiteral("Hello"), resolveHello) ==
           QStringLiteral("你好"));
    assert(resolutions == 1);

    const auto resolveMissing = [&] {
        ++resolutions;
        return QString{};
    };
    assert(cache.lookup(QStringLiteral("Missing"), resolveMissing).isEmpty());
    assert(cache.lookup(QStringLiteral("Missing"), resolveMissing).isEmpty());
    assert(resolutions == 2);

    cache.clear();
    assert(cache.lookup(QStringLiteral("Hello"), resolveHello) ==
           QStringLiteral("你好"));
    assert(resolutions == 3);

    // One slot makes replacement deterministic without depending on Qt's hash.
    HoudiniChineseLookupCache::DirectMapped<1> bounded;
    int boundedResolutions = 0;
    const auto countResolve = [&] {
        ++boundedResolutions;
        return QStringLiteral("cached");
    };
    bounded.lookup(QStringLiteral("small"), countResolve);
    const QString large(HoudiniChineseLookupCache::kMaximumCachedTextLength + 1,
                        QLatin1Char('x'));
    assert(bounded.lookup(large, countResolve) == QStringLiteral("cached"));
    assert(bounded.lookup(large, countResolve) == QStringLiteral("cached"));
    assert(boundedResolutions == 3);
    bounded.lookup(QStringLiteral("small"), countResolve);
    assert(boundedResolutions == 3);
    const auto largeResolve = [&] {
        ++boundedResolutions;
        return large;
    };
    assert(bounded.lookup(QStringLiteral("large result"), largeResolve) == large);
    assert(bounded.lookup(QStringLiteral("large result"), largeResolve) == large);
    assert(boundedResolutions == 5);

    HoudiniChineseLookupCache::DirectMapped<1> borrowed;
    QChar inputBuffer[] = {QLatin1Char('k'), QLatin1Char('e'), QLatin1Char('y')};
    QChar outputBuffer[] = {QLatin1Char('v'), QLatin1Char('a'), QLatin1Char('l')};
    const QString input = QString::fromRawData(inputBuffer, 3);
    borrowed.lookup(input, [&] { return QString::fromRawData(outputBuffer, 3); });
    inputBuffer[0] = QLatin1Char('x');
    outputBuffer[0] = QLatin1Char('x');
    int unexpectedResolutions = 0;
    assert(borrowed.lookup(QStringLiteral("key"), [&] {
               ++unexpectedResolutions;
               return QStringLiteral("unexpected");
           }) == QStringLiteral("val"));
    assert(unexpectedResolutions == 0);

    assert(borrowed.lookup(QStringLiteral("collision"), [] {
               return QStringLiteral("other");
           }) == QStringLiteral("other"));
    assert(borrowed.lookup(QStringLiteral("key"), [] {
               return QStringLiteral("resolved again");
           }) == QStringLiteral("resolved again"));

    // Exercise failure during either ownership copy, after a colliding slot
    // already contains a valid entry. The old key/value must remain usable and
    // the new key must resolve afresh instead of returning the stale value.
    for (const int successfulCopies : {0, 1}) {
        HoudiniChineseLookupCache::DirectMapped<1, FailingOwnedStringCopy>
            exceptionSafe;
        FailingOwnedStringCopy::copiesUntilFailure = -1;
        exceptionSafe.lookup(QStringLiteral("old key"), [] {
            return QStringLiteral("old value");
        });
        FailingOwnedStringCopy::copiesUntilFailure = successfulCopies;
        bool failed = false;
        try {
            exceptionSafe.lookup(QStringLiteral("new key"), [] {
                return QStringLiteral("new value");
            });
        } catch (const std::bad_alloc &) {
            failed = true;
        }
        assert(failed);
        FailingOwnedStringCopy::copiesUntilFailure = -1;
        int oldResolutions = 0;
        assert(exceptionSafe.lookup(QStringLiteral("old key"), [&] {
                   ++oldResolutions;
                   return QStringLiteral("unexpected");
               }) == QStringLiteral("old value"));
        assert(oldResolutions == 0);
        int newResolutions = 0;
        assert(exceptionSafe.lookup(QStringLiteral("new key"), [&] {
                   ++newResolutions;
                   return QStringLiteral("new value");
               }) == QStringLiteral("new value"));
        assert(newResolutions == 1);
    }

    std::cout << "translation_lookup_cache_test: OK\n";
    return 0;
}
