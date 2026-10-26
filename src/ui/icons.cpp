#include "ui/icons.hpp"

#include <QFile>
#include <QHash>
#include <QPainter>
#include <QSvgRenderer>

ThemeColors ThemeColors::forTheme(const QString &theme)
{
    if (theme == "light")
        return {QColor("#4a4c52"), QColor("#3d6ea5"), QColor("#85878e"), QColor("#c74f4f")};
    return {QColor("#c9cbd1"), QColor("#5b8def"), QColor("#8a8d94"), QColor("#e06c6c")};
}

QHash<QString, QIcon> &Icons::cache()
{
    static QHash<QString, QIcon> cache;
    return cache;
}

QIcon Icons::cached(const QString &name, const QColor &color)
{
    const QString key = name + "|" + color.name();
    const auto it = cache().constFind(key);
    return it != cache().constEnd() ? it.value() : QIcon();
}

QPixmap Icons::pixmap(const QString &name, const QColor &color, int size, int dpr)
{
    QFile file(":/icons/" + name + ".svg");
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("moch: icon %s missing", qPrintable(name));
        return QPixmap();
    }
    QString svg = QString::fromUtf8(file.readAll());
    svg.replace(QStringLiteral("__COLOR__"), color.name());

    const int pixelSize = size * dpr;
    QSvgRenderer renderer(svg.toUtf8());
    QPixmap pix(pixelSize, pixelSize);
    pix.fill(Qt::transparent);
    QPainter painter(&pix);
    renderer.render(&painter, QRectF(0, 0, pixelSize, pixelSize));
    painter.end();
    pix.setDevicePixelRatio(dpr);
    return pix;
}

QIcon Icons::get(const QString &name, const QColor &color, int size)
{
    const QString key = name + "|" + color.name() + "|" + QString::number(size);
    const auto it = cache().constFind(key);
    if (it != cache().constEnd())
        return it.value();

    QIcon icon;
    for (const int dpr : {1, 2})
        icon.addPixmap(pixmap(name, color, size, dpr));
    cache().insert(key, icon);
    return icon;
}
