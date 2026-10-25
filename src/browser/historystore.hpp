#pragma once

#include <QList>
#include <QObject>
#include <QString>

struct HistoryEntry
{
    QString url;
    QString title;
    qint64 visitedAt = 0;
};

class HistoryStore : public QObject
{
    Q_OBJECT
public:
    explicit HistoryStore(QObject *parent = nullptr);

    void recordVisit(const QString &url, const QString &title);
    QList<HistoryEntry> visits(int limit, int offset) const;
    QList<HistoryEntry> search(const QString &query, int limit = 50) const;
    QList<HistoryEntry> topSites(int limit = 9) const;
    void removeForUrl(const QString &url);
    void clearAll();

signals:
    void changed();

private:
    QList<HistoryEntry> query(const QString &sql, const QList<QVariant> &binds) const;
};
