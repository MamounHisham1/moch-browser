#include "browser/bookmarkstore.hpp"

#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include "storage/database.hpp"

BookmarkStore::BookmarkStore(QObject *parent)
    : QObject(parent)
{
    Database::connection();
}

void BookmarkStore::add(const QString &url, const QString &title)
{
    QSqlQuery query(Database::connection());
    query.prepare("INSERT OR REPLACE INTO bookmarks (url, title, added_at) VALUES (?, ?, ?)");
    query.addBindValue(url);
    query.addBindValue(title);
    query.addBindValue(QDateTime::currentSecsSinceEpoch());
    if (!query.exec())
        qWarning("moch: bookmark insert failed: %s", qPrintable(query.lastError().text()));
    emit changed();
}

void BookmarkStore::remove(const QString &url)
{
    QSqlQuery query(Database::connection());
    query.prepare("DELETE FROM bookmarks WHERE url = ?");
    query.addBindValue(url);
    if (!query.exec())
        qWarning("moch: bookmark delete failed: %s", qPrintable(query.lastError().text()));
    emit changed();
}

QList<Bookmark> BookmarkStore::query(const QString &sql, const QList<QVariant> &binds) const
{
    QList<Bookmark> out;
    QSqlQuery query(Database::connection());
    query.prepare(sql);
    for (const QVariant &bind : binds)
        query.addBindValue(bind);
    if (!query.exec()) {
        qWarning("moch: bookmark query failed: %s", qPrintable(query.lastError().text()));
        return out;
    }
    while (query.next()) {
        Bookmark bookmark;
        bookmark.url = query.value(0).toString();
        bookmark.title = query.value(1).toString();
        bookmark.addedAt = query.value(2).toLongLong();
        out.append(bookmark);
    }
    return out;
}

bool BookmarkStore::contains(const QString &url) const
{
    return !query("SELECT url, title, added_at FROM bookmarks WHERE url = ?", {url}).isEmpty();
}

Bookmark BookmarkStore::forUrl(const QString &url) const
{
    const QList<Bookmark> found =
        query("SELECT url, title, added_at FROM bookmarks WHERE url = ?", {url});
    return found.isEmpty() ? Bookmark{} : found.first();
}

QList<Bookmark> BookmarkStore::all() const
{
    return query("SELECT url, title, added_at FROM bookmarks ORDER BY title COLLATE NOCASE", {});
}
