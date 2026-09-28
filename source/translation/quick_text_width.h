#pragma once

#include <algorithm>
#include <cmath>

namespace HoudiniChineseQuickText {

// QQuickText can publish its implicit width through more than one setter in a
// single layout pass. Apply the translation delta only while the current width
// still resembles the source-language measurement.
inline double adjustedImplicitWidth(double currentWidth, double sourceTextWidth,
                                    double translatedTextWidth,
                                    double horizontalPadding) noexcept {
    if (!std::isfinite(currentWidth) || !std::isfinite(sourceTextWidth) ||
        !std::isfinite(translatedTextWidth) ||
        !std::isfinite(horizontalPadding) || sourceTextWidth < 0.0 ||
        translatedTextWidth < 0.0 || horizontalPadding < 0.0) {
        return currentWidth;
    }

    const double sourceExpected = sourceTextWidth + horizontalPadding;
    const double translatedExpected = translatedTextWidth + horizontalPadding;
    if (std::abs(currentWidth - translatedExpected) <=
        std::abs(currentWidth - sourceExpected)) {
        return currentWidth;
    }
    return (std::max)(0.0,
                      currentWidth + translatedTextWidth - sourceTextWidth);
}

} // namespace HoudiniChineseQuickText
