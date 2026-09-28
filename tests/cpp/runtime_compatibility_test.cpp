#include "../../source/hooks/hook_lifecycle.h"

#include <cassert>
#include <iostream>

int main() {
    using namespace HoudiniChineseCompatibility;

    assert(isMatchingHdkApi(22000368u, 22000368u));
    assert(!isMatchingHdkApi(22000368u, 21000000u));
    assert(!isMatchingHdkApi(0u, 0u));
    assert(isQt6Major(6));
    assert(!isQt6Major(5));
    assert(isMatchingQtRuntime("6.8.3", "6.8.3"));
    assert(!isMatchingQtRuntime("6.8.3", "6.8.2"));
    assert(!isMatchingQtRuntime("", "6.8.3"));
    assert(hasCoreQtCapabilities(true, true, true));
    assert(!hasCoreQtCapabilities(true, true, false));
    assert(isCompatibleRuntime(22000368u, 22000368u, 6, "6.8.3", "6.8.3", true,
                               true, true));
    assert(!isCompatibleRuntime(22000368u, 21000000u, 6, "6.8.3", "6.8.3", true,
                                true, true));
    assert(!isCompatibleRuntime(22000368u, 22000368u, 5, "6.8.3", "6.8.3", true,
                                true, true));
    assert(!isCompatibleRuntime(22000368u, 22000368u, 6, "6.8.3", "6.8.2", true,
                                true, true));
    assert(!isCompatibleRuntime(22000368u, 22000368u, 6, "6.8.3", "6.8.3", true,
                                false, true));

    std::cout << "runtime_compatibility_test: OK\n";
    return 0;
}
