#include "browser/historystore.hpp"

#include <QDateTime>
#include <QHash>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUrl>

#include <algorithm>

#include "storage/database.hpp"

HistoryStore::HistoryStore(QObject *parent)
    : QObject(parent)
{
    Database::connection();
}

void HistoryStore::recordVisit(const QString &url, const QString &title)
{
    QSqlQuery query(Database::connection());
    query.prepare("INSERT INTO history (url, title, visited_at) VALUES (?, ?, ?)");
    query.addBindValue(url);
    query.addBindValue(title);
    query.addBindValue(QDateTime::currentSecsSinceEpoch());
    if (!query.exec())
        qWarning("moch: history insert failed: %s", qPrintable(query.lastError().text()));
    emit changed();
}

QList<HistoryEntry> HistoryStore::query(const QString &sql, const QList<QVariant> &binds) const
{
    QList<HistoryEntry> entries;
    QSqlQuery query(Database::connection());
    query.prepare(sql);
    for (const QVariant &bind : binds)
        query.addBindValue(bind);
    if (!query.exec()) {
        qWarning("moch: history query failed: %s", qPrintable(query.lastError().text()));
        return entries;
    }
    while (query.next()) {
        HistoryEntry entry;
        entry.url = query.value(0).toString();
        entry.title = query.value(1).toString();
        entry.visitedAt = query.value(2).toLongLong();
        entries.append(entry);
    }
    return entries;
}

QList<HistoryEntry> HistoryStore::visits(int limit, int offset) const
{
    return query("SELECT url, title, visited_at FROM history "
                 "ORDER BY visited_at DESC LIMIT ? OFFSET ?",
                 {limit, offset});
}

QList<HistoryEntry> HistoryStore::search(const QString &needle, int limit) const
{
    const QString pattern = "%" + needle + "%";
    return query("SELECT url, title, visited_at FROM history "
                 "WHERE url LIKE ? OR title LIKE ? "
                 "GROUP BY url ORDER BY MAX(visited_at) DESC LIMIT ?",
                 {pattern, pattern, limit});
}

QList<HistoryEntry> HistoryStore::topSites(int limit) const
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const qint64 weekAgo = now - 7 * 24 * 3600;
    const QList<HistoryEntry> recent = query(
        "SELECT url, title, visited_at FROM history WHERE visited_at > ? AND url LIKE 'http%'",
        {weekAgo});

    struct Site
    {
        QString url;
        QString title;
        double score = 0;
    };
    QHash<QString, Site> byHost;
    for (const HistoryEntry &entry : recent) {
        QString host = QUrl(entry.url).host();
        if (host.startsWith("www."))
            host.remove(0, 4);
        if (host.isEmpty())
            continue;
        const qint64 ageDays = (now - entry.visitedAt) / 86400;
        const double decay = ageDays < 1 ? 1.0 : (ageDays < 3 ? 0.5 : 0.25);
        Site &site = byHost[host];
        site.score += decay;
        if (entry.visitedAt > 0 && site.url.isEmpty())
            site.url = entry.url;
        if (!entry.title.isEmpty())
            site.title = entry.title;
        if (entry.visitedAt > 0)
            site.url = site.url.isEmpty() ? entry.url : site.url;
    }

    QList<Site> sites;
    for (const Site &site : std::as_const(byHost))
        sites.append(site);
    std::sort(sites.begin(), sites.end(),
              [](const Site &a, const Site &b) { return a.score > b.score; });

    QList<HistoryEntry> out;
    for (const Site &site : sites) {
        if (out.size() >= limit)
            break;
        HistoryEntry entry;
        entry.url = site.url;
        entry.title = site.title;
        entry.visitedAt = qint64(site.score);
        out.append(entry);
    }
    return out;
}

void HistoryStore::removeForUrl(const QString &url)
{
    QSqlQuery query(Database::connection());
    query.prepare("DELETE FROM history WHERE url = ?");
    query.addBindValue(url);
    if (!query.exec())
        qWarning("moch: history delete failed: %s", qPrintable(query.lastError().text()));
    emit changed();
}

void HistoryStore::clearAll()
{
    QSqlQuery query(Database::connection());
    if (!query.exec("DELETE FROM history"))
        qWarning("moch: history clear failed: %s", qPrintable(query.lastError().text()));
    emit changed();
}
