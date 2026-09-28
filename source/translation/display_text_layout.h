#pragma once

#include <QtGui/QTextLayout>

#include <algorithm>
#include <memory>

namespace HoudiniChineseTextLayout {

// Source character offsets cannot be mapped safely onto a translated string.
// A uniform format can cover the complete replacement; mixed/partial formats
// and input-method preedit text must keep the original display layout.
inline bool canTranslate(const QTextLayout *source) {
    if (!source || !source->preeditAreaText().isEmpty())
        return false;
    const QList<QTextLayout::FormatRange> formats = source->formats();
    return formats.isEmpty() ||
           (formats.size() == 1 && formats.constFirst().start == 0 &&
            formats.constFirst().length >= source->text().size());
}

// Build only a temporary display layout. The source text, formatting, line
// geometry and owner are never modified. A null result means the caller must
// draw the original layout, including when a fixed container cannot fit all
// translated text within the existing number of lines.
inline std::unique_ptr<QTextLayout>
makeDisplayLayout(const QTextLayout *source, const QString &text,
                  QPaintDevice *device = nullptr) {
    if (text.isEmpty() || !canTranslate(source))
        return {};

    auto result = std::make_unique<QTextLayout>(text, source->font(), device);
    result->setTextOption(source->textOption());
    result->setCacheEnabled(source->cacheEnabled());

    const QList<QTextLayout::FormatRange> formats = source->formats();
    if (!formats.isEmpty()) {
        QTextLayout::FormatRange format = formats.constFirst();
        format.start = 0;
        format.length = text.size();
        result->setFormats({format});
    }

    result->beginLayout();
    const int sourceLineCount = (std::max)(1, source->lineCount());
    for (int index = 0; index < sourceLineCount; ++index) {
        QTextLine line = result->createLine();
        if (!line.isValid())
            break;
        if (source->lineCount() <= 0)
            continue;
        const QTextLine original = source->lineAt(index);
        const qreal sourceWidth =
            original.width() > 0.0
                ? original.width()
                : (std::max)(qreal{1.0}, original.naturalTextWidth());
        const bool contentSized =
            sourceWidth <= original.naturalTextWidth() + 1.0;
        if (contentSized)
            line.setLineWidth(1000000.0);
        const qreal width =
            contentSized ? (std::max)(qreal{1.0}, line.naturalTextWidth())
                         : sourceWidth;
        line.setLineWidth(width);
        line.setPosition(original.position());
    }
    result->endLayout();

    if (result->lineCount() == 0)
        return {};
    const QTextLine last = result->lineAt(result->lineCount() - 1);
    if (last.textStart() + last.textLength() < text.size())
        return {};
    return result;
}

} // namespace HoudiniChineseTextLayout
