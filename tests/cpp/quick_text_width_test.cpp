#include "../../source/translation/quick_text_width.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

int main() {
    using HoudiniChineseQuickText::adjustedImplicitWidth;

    assert(adjustedImplicitWidth(110.0, 100.0, 140.0, 10.0) == 150.0);
    assert(adjustedImplicitWidth(150.0, 100.0, 140.0, 10.0) == 150.0);
    assert(adjustedImplicitWidth(148.0, 100.0, 140.0, 10.0) == 148.0);
    assert(adjustedImplicitWidth(0.0, 100.0, 40.0, 0.0) == 0.0);

    const double nan = std::numeric_limits<double>::quiet_NaN();
    assert(std::isnan(adjustedImplicitWidth(nan, 10.0, 20.0, 0.0)));
    assert(adjustedImplicitWidth(10.0, -1.0, 20.0, 0.0) == 10.0);

    std::cout << "quick_text_width_test: OK\n";
    return 0;
}
