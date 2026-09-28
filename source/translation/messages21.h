#pragma once
#include <QtCore/QString>
#include <QtCore/QRegularExpression>

namespace HoudiniChineseMessages21 {
// Anchored UI messages only. Captured filenames/node names remain verbatim.
template<class Lookup>
QString translate(const QString &text, Lookup lookup) {
    if (text.size() > 4096) return {};
    const QString pathPrefix = QStringLiteral("Drag this to another pane to set its path to ");
    if (text.startsWith(pathPrefix) && text.endsWith(QLatin1Char('.'))) {
        const QString path = text.mid(pathPrefix.size(), text.size()-pathPrefix.size()-1);
        if (path.startsWith(QLatin1Char('/')) && !path.contains(QLatin1Char('\n')))
            return QStringLiteral("拖到另一个窗格，将该窗格的路径设为 ") + path + QStringLiteral("。");
    }
    // Houdini's default connector labels include an ordinal and an internal
    // name. Match the whole label, leaving custom labels/types untouched.
    static const QRegularExpression portPattern(QStringLiteral(
        "\\A(Input|Output) ([0-9]+) \\(([A-Za-z_][A-Za-z0-9_]*)\\)\\z"));
    const auto port = portPattern.match(text);
    if (port.hasMatch())
        return (port.captured(1) == QStringLiteral("Input")
                    ? QStringLiteral("输入 ") : QStringLiteral("输出 "))
            + port.captured(2) + QStringLiteral(" (") + port.captured(3) + QLatin1Char(')');
    // Some nodes expose descriptive connector labels such as
    // "Bounding Source (input1)". Translate the visible label only when it
    // is an exact dictionary entry; the internal connector name is data and
    // must stay byte-for-byte recognizable to the user.
    static const QRegularExpression namedPortPattern(QStringLiteral(
        "\\A([^()\\n]{1,128}) \\(([A-Za-z_][A-Za-z0-9_]*)\\)\\z"));
    const auto namedPort = namedPortPattern.match(text);
    if (namedPort.hasMatch()) {
        const QString label = lookup(namedPort.captured(1));
        if (!label.isEmpty())
            return label + QStringLiteral(" (") + namedPort.captured(2) + QLatin1Char(')');
    }
    static const QRegularExpression nodePortPattern(QStringLiteral(
        "\\A([A-Za-z_][A-Za-z0-9_]*) \\(([^()\\n]+)\\)\\n([^()\\n]{1,128} \\([A-Za-z_][A-Za-z0-9_]*\\))\\z"));
    const auto nodePort = nodePortPattern.match(text);
    if (nodePort.hasMatch()) {
        const QString type = lookup(nodePort.captured(2));
        const QString translatedPort = translate(nodePort.captured(3), lookup);
        if (!type.isEmpty() && !translatedPort.isEmpty())
            return nodePort.captured(1) + QStringLiteral("（") + type
                + QStringLiteral("）\n") + translatedPort;
    }
    // The exit key is configured by the user, and may be empty in H21.
    static const QRegularExpression navigationPattern(QStringLiteral(
        "\\AFirst Person Navigation: Press Esc or ([A-Za-z0-9+ ,_-]{0,64}) to exit\\.\\z"));
    const auto navigation = navigationPattern.match(text);
    if (navigation.hasMatch()) {
        const QString key = navigation.captured(1).trimmed();
        return QStringLiteral("第一人称导航：按 Esc")
            + (key.isEmpty() ? QString() : QStringLiteral(" 或 ") + key)
            + QStringLiteral(" 退出。");
    }
    const QString prefix = QStringLiteral("Save changes to \"");
    const QString suffix = QStringLiteral("\" before exiting?");
    if (text.startsWith(prefix) && text.endsWith(suffix)) {
        const QString name = text.mid(prefix.size(), text.size()-prefix.size()-suffix.size());
        if (!name.isEmpty() && !name.contains(QLatin1Char('\n')) && !name.contains(QLatin1Char('"')))
            return QStringLiteral("退出前保存对“") + name + QStringLiteral("”的修改吗？");
    }
    if (text.endsWith(QStringLiteral(") node"))) {
        const auto split = text.lastIndexOf(QStringLiteral(" ("));
        if (split > 0) {
            const QString name = text.left(split);
            const QString type = text.mid(split+2, text.size()-split-8);
            // Houdini node identifiers cannot contain whitespace or parentheses.
            bool identifier = !name.isEmpty();
            for (const QChar c : name)
                if (!(c.isLetterOrNumber() || c == QLatin1Char('_'))) identifier = false;
            const QString translated = lookup(type);
            if (identifier && !translated.isEmpty())
                return name + QStringLiteral("（") + translated + QStringLiteral("）节点");
        }
    }
    return {};
}
}
