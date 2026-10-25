#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>

class BookmarkStore;
class HistoryStore;
class Settings;

class NewTabBridge : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString theme READ theme CONSTANT)
public:
    explicit NewTabBridge(HistoryStore *history, BookmarkStore *bookmarks, Settings *settings,
                          QObject *parent = nullptr);

    QString theme() const;

    Q_INVOKABLE void searchRequested(const QString &query);
    Q_INVOKABLE void openUrl(const QString &url);
    Q_INVOKABLE QJsonObject getTopSites() const;
    Q_INVOKABLE QJsonObject getBookmarks() const;
    Q_INVOKABLE QJsonObject getRecentHistory() const;

signals:
    void searchRequestedByUser(const QString &query);
    void openUrlRequested(const QString &url);

private:
    static QJsonObject entriesToJson(const QList<HistoryEntry> &entries);
    HistoryStore *m_history = nullptr;
    BookmarkStore *m_bookmarks = nullptr;
    Settings *m_settings = nullptr;
};
