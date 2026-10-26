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

QUrl UrlResolver::resolveAgentTarget(const QString &input, const QString &searchEngineKey)
{
    const QString trimmed = input.trimmed();
    if (trimmed.isEmpty())
        return QUrl("moch://newtab");
    if (trimmed.startsWith("https://", Qt::CaseInsensitive)
        || trimmed.startsWith("http://", Qt::CaseInsensitive))
        return QUrl(trimmed);

    static const QHash<QString, QString> knownSites = {
        {"youtube", "https://www.youtube.com"},
        {"yt", "https://www.youtube.com"},
        {"google", "https://www.google.com"},
        {"gmail", "https://mail.google.com"},
        {"mail", "https://mail.google.com"},
        {"maps", "https://maps.google.com"},
        {"translate", "https://translate.google.com"},
        {"drive", "https://drive.google.com"},
        {"calendar", "https://calendar.google.com"},
        {"facebook", "https://www.facebook.com"},
        {"fb", "https://www.facebook.com"},
        {"instagram", "https://www.instagram.com"},
        {"whatsapp", "https://web.whatsapp.com"},
        {"twitter", "https://x.com"},
        {"x", "https://x.com"},
        {"reddit", "https://www.reddit.com"},
        {"linkedin", "https://www.linkedin.com"},
        {"github", "https://github.com"},
        {"gitlab", "https://gitlab.com"},
        {"stackoverflow", "https://stackoverflow.com"},
        {"chatgpt", "https://chatgpt.com"},
        {"claude", "https://claude.ai"},
        {"perplexity", "https://www.perplexity.ai"},
        {"wikipedia", "https://en.wikipedia.org"},
        {"amazon", "https://www.amazon.com"},
        {"netflix", "https://www.netflix.com"},
        {"spotify", "https://open.spotify.com"},
        {"twitch", "https://www.twitch.tv"},
        {"tiktok", "https://www.tiktok.com"},
        {"discord", "https://discord.com/app"},
        {"notion", "https://www.notion.so"},
        {"figma", "https://www.figma.com"},
        {"canva", "https://www.canva.com"},
        {"medium", "https://medium.com"},
        {"substack", "https://substack.com"},
        {"hn", "https://news.ycombinator.com"},
        {"paypal", "https://www.paypal.com"},
        {"ebay", "https://www.ebay.com"},
        {"aliexpress", "https://www.aliexpress.com"},
        {"noon", "https://www.noon.com/egypt-en/"},
        {"jumia", "https://www.jumia.com.eg/"},
        {"olx", "https://www.olx.com.eg/"},
        {"bayt", "https://www.bayt.com"},
        {"wuzzuf", "https://wuzzuf.net"},
        {"booking", "https://www.booking.com"},
        {"airbnb", "https://www.airbnb.com"},
    };
    const QString lower = trimmed.toLower();
    const auto known = knownSites.constFind(lower);
    if (known != knownSites.constEnd())
        return QUrl(known.value());

    if (!trimmed.contains(' ')) {
        if (trimmed.contains('.'))
            return QUrl("https://" + trimmed);
        static const QRegularExpression word(R"(^[a-z0-9][a-z0-9-]{2,}[a-z0-9]$)",
                                              QRegularExpression::CaseInsensitiveOption);
        if (word.match(lower).hasMatch())
            return QUrl("https://" + lower + ".com");
    }
    return fromUserInput(trimmed, searchEngineKey);
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
