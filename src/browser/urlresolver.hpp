#pragma once

#include <QString>
#include <QUrl>

class UrlResolver
{
public:
    static QUrl fromUserInput(const QString &input, const QString &searchEngineKey);
    static QString searchUrl(const QString &query, const QString &searchEngineKey);
    static QStringList searchEngineKeys();
    static QString searchEngineName(const QString &key);
};
