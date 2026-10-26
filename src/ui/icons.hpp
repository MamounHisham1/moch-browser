#pragma once

#include <QColor>
#include <QHash>
#include <QIcon>
#include <QString>

struct ThemeColors
{
    QColor icon;
    QColor accent;
    QColor muted;
    QColor danger;

    static ThemeColors forTheme(const QString &theme);
};

// Loads :/icons/<name>.svg, recolors the __COLOR__ placeholder, renders via
// QSvgRenderer. Results are cached per (name, color, size).
class Icons
{
public:
    static QIcon get(const QString &name, const QColor &color, int size = 18);
    static QPixmap pixmap(const QString &name, const QColor &color, int size, int dpr);

private:
    static QIcon cached(const QString &name, const QColor &color);
    static QHash<QString, QIcon> &cache();
};
