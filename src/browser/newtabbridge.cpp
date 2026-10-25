#include "browser/newtabbridge.hpp"

#include <QJsonArray>

#include "browser/bookmarkstore.hpp"
#include "browser/historystore.hpp"
#include "storage/settings.hpp"

NewTabBridge::NewTabBridge(HistoryStore *history, BookmarkStore *bookmarks, Settings *settings,
                           QObject *parent)
    : QObject(parent), m_history(history), m_bookmarks(bookmarks), m_settings(settings)
{
}

QString NewTabBridge::theme() const
{
    return m_settings->theme();
}

void NewTabBridge::searchRequested(const QString &query)
{
    if (!query.trimmed().isEmpty())
        emit searchRequestedByUser(query);
}

void NewTabBridge::openUrl(const QString &url)
{
    if (!url.trimmed().isEmpty())
        emit openUrlRequested(url);
}

QJsonObject NewTabBridge::entriesToJson(const QList<HistoryEntry> &entries)
{
    QJsonArray array;
    for (const HistoryEntry &entry : entries) {
        QJsonObject object;
        object.insert("url", entry.url);
        object.insert("title", entry.title.isEmpty() ? entry.url : entry.title);
        array.append(object);
    }
    return QJsonObject{{"items", array}};
}

QJsonObject NewTabBridge::getTopSites() const
{
    return entriesToJson(m_history->topSites(9));
}

QJsonObject NewTabBridge::getBookmarks() const
{
    QJsonArray array;
    for (const Bookmark &bookmark : m_bookmarks->all()) {
        QJsonObject object;
        object.insert("url", bookmark.url);
        object.insert("title", bookmark.title.isEmpty() ? bookmark.url : bookmark.title);
        array.append(object);
    }
    return QJsonObject{{"items", array}};
}

QJsonObject NewTabBridge::getRecentHistory() const
{
    return entriesToJson(m_history->visits(12, 0));
}
