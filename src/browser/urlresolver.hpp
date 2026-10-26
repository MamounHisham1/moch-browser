#pragma once

#include <QString>
#include <QUrl>

class UrlResolver
{
public:
    static QUrl fromUserInput(const QString &input, const QString &searchEngineKey);
    // Agent-flavored resolution: well-known single-word site names resolve to
    // their real domains, bare hostnames get https, phrases fall back to search.
    static QUrl resolveAgentTarget(const QString &input, const QString &searchEngineKey);
    static QString searchUrl(const QString &query, const QString &searchEngineKey);
    static QStringList searchEngineKeys();
    static QString searchEngineName(const QString &key);
};
