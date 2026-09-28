#include "../../source/translation/display_text_layout.h"

#include <QtGui/QFontMetricsF>
#include <QtGui/QGuiApplication>
#include <QtGui/QImage>

#include <cassert>
#include <iostream>

namespace {

void layoutOneLine(QTextLayout &layout, qreal width, const QPointF &position) {
    layout.beginLayout();
    QTextLine line = layout.createLine();
    assert(line.isValid());
    line.setLineWidth(width);
    line.setPosition(position);
    layout.endLayout();
}

bool coversWholeText(const QTextLayout &layout) {
    if (layout.lineCount() == 0)
        return false;
    const QTextLine last = layout.lineAt(layout.lineCount() - 1);
    return last.textStart() + last.textLength() == layout.text().size();
}

} // namespace

int main(int argc, char **argv) {
    QGuiApplication application(argc, argv);
    const QFont font(QStringLiteral("Arial"), 12);
    const QFontMetricsF metrics(font);
    const QString sourceText = QStringLiteral("File");
    const QString translatedText = QStringLiteral("文件与项目设置");
    const QPointF linePosition(7.0, 11.0);
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    option.setAlignment(Qt::AlignLeft);
    option.setTextDirection(Qt::LeftToRight);

    QTextLayout source(sourceText, font);
    source.setTextOption(option);
    source.setCacheEnabled(true);
    QTextLayout::FormatRange uniformFormat;
    uniformFormat.start = 0;
    uniformFormat.length = sourceText.size();
    uniformFormat.format.setForeground(Qt::red);
    uniformFormat.format.setFontWeight(QFont::Bold);
    source.setFormats({uniformFormat});
    // A naturally sized label may grow to show the whole translation.
    layoutOneLine(source, 1000000.0, linePosition);
    source.lineAt(0).setLineWidth(source.lineAt(0).naturalTextWidth());
    const qreal originalWidth = source.lineAt(0).width();
    const auto originalFormats = source.formats();

    assert(HoudiniChineseTextLayout::canTranslate(&source));
    QImage paintDevice(200, 100, QImage::Format_ARGB32_Premultiplied);
    auto display = HoudiniChineseTextLayout::makeDisplayLayout(
        &source, translatedText, &paintDevice);
    assert(display);
    assert(coversWholeText(*display));
    assert(display->text() == translatedText);
    assert(display->font() == source.font());
    assert(display->cacheEnabled() == source.cacheEnabled());
    assert(display->textOption().wrapMode() == option.wrapMode());
    assert(display->textOption().alignment() == option.alignment());
    assert(display->textOption().textDirection() == option.textDirection());
    assert(display->formats().size() == 1);
    assert(display->formats().constFirst().format == uniformFormat.format);
    assert(display->formats().constFirst().start == 0);
    assert(display->formats().constFirst().length == translatedText.size());
    assert(display->lineAt(0).position() == linePosition);
    assert(source.text() == sourceText);
    assert(source.formats() == originalFormats);
    assert(source.lineCount() == 1);
    assert(source.lineAt(0).width() == originalWidth);
    assert(source.lineAt(0).position() == linePosition);

    // A fixed-width container must either contain the complete replacement or
    // reject it. Returning a partial layout would silently hide tail text.
    QTextLayout fixed(sourceText, font);
    fixed.setTextOption(option);
    layoutOneLine(fixed, metrics.horizontalAdvance(sourceText) + 50.0,
                  QPointF());
    const QString longText(100, QChar(0x6587));
    assert(!HoudiniChineseTextLayout::makeDisplayLayout(&fixed, longText));
    assert(fixed.text() == sourceText);
    assert(coversWholeText(fixed));

    // Caller-visible text/format offsets remain meaningful when translation
    // is unsafe; both eligibility and the builder enforce the same boundary.
    QTextLayout mixed(sourceText, font);
    QTextLayout::FormatRange partial = uniformFormat;
    partial.length = 2;
    mixed.setFormats({partial});
    assert(!HoudiniChineseTextLayout::canTranslate(&mixed));
    assert(!HoudiniChineseTextLayout::makeDisplayLayout(&mixed, translatedText));
    auto second = partial;
    second.start = 2;
    second.format.setForeground(Qt::blue);
    mixed.setFormats({partial, second});
    assert(!HoudiniChineseTextLayout::canTranslate(&mixed));
    assert(!HoudiniChineseTextLayout::makeDisplayLayout(&mixed, translatedText));

    QTextLayout preedit(sourceText, font);
    preedit.setPreeditArea(1, QStringLiteral("typing"));
    assert(!HoudiniChineseTextLayout::canTranslate(&preedit));
    assert(!HoudiniChineseTextLayout::makeDisplayLayout(&preedit, translatedText));
    assert(preedit.preeditAreaPosition() == 1);
    assert(preedit.preeditAreaText() == QStringLiteral("typing"));

    assert(!HoudiniChineseTextLayout::canTranslate(nullptr));
    assert(!HoudiniChineseTextLayout::makeDisplayLayout(nullptr, translatedText));
    assert(!HoudiniChineseTextLayout::makeDisplayLayout(&source, QString()));
    QTextLayout unlaid(sourceText, font);
    auto firstLayout =
        HoudiniChineseTextLayout::makeDisplayLayout(&unlaid, translatedText);
    assert(firstLayout && coversWholeText(*firstLayout));
    assert(unlaid.lineCount() == 0);
    std::cout << "display_text_layout_test: OK\n";
}
