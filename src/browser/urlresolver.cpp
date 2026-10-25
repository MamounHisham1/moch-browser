#include "browser/urlresolver.hpp"

#include <QHash>
#include <QRegularExpression>

namespace {
struct Engine {
    const char *name;
    const char *url;
};

const QHash<QString, Engine> &engines()
{
    static const QHash<QString, Engine> engines = {
        {"duckduckgo", {"DuckDuckGo", "https://duckduckgo.com/?q=%1"}},
        {"google", {"Google", "https://www.google.com/search?q=%1"}},
        {"bing", {"Bing", "https://www.bing.com/search?q=%1"}},
        {"wikipedia", {"Wikipedia", "https://en.wikipedia.org/w/index.php?search=%1"}},
    };
    return engines;
}

bool looksLikeUrl(const QString &input)
{
    const QString trimmed = input.trimmed();
    if (trimmed.isEmpty())
        return false;
    if (trimmed.startsWith("localhost", Qt::CaseInsensitive))
        return true;
    static const QRegularExpression scheme("^[a-zA-Z][a-zA-Z0-9+.-]*:");
    if (scheme.match(trimmed).hasMatch())
        return true;
    static const QRegularExpression host(R"(^[^\s/&?#]+\.[a-zA-Z]{2,}([/?#].*)?$)");
    return host.match(trimmed).hasMatch();
}
} // namespace

QStringList UrlResolver::searchEngineKeys()
{
    return engines().keys();
}

QString UrlResolver::searchEngineName(const QString &key)
{
    return engines().value(key, {"Unknown", ""}).name;
}

QString UrlResolver::searchUrl(const QString &query, const QString &searchEngineKey)
{
    return QString(engines().value(searchEngineKey, engines().value("duckduckgo")).url)
        .arg(QString::fromUtf8(QUrl::toPercentEncoding(query)));
}

QUrl UrlResolver::fromUserInput(const QString &input, const QString &searchEngineKey)
{
    const QString trimmed = input.trimmed();
    if (trimmed.isEmpty())
        return QUrl("moch://newtab");
    if (trimmed == "moch://newtab")
        return QUrl("moch://newtab");
    if (looksLikeUrl(trimmed)) {
        if (!trimmed.contains("://"))
            return QUrl("https://" + trimmed);
        return QUrl(trimmed);
    }
    return QUrl(searchUrl(trimmed, searchEngineKey));
}
