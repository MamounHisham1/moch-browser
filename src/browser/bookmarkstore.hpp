#pragma once

#include <QList>
#include <QObject>
#include <QString>

struct Bookmark
{
    QString url;
    QString title;
    qint64 addedAt = 0;
};

class BookmarkStore : public QObject
{
    Q_OBJECT
public:
    explicit BookmarkStore(QObject *parent = nullptr);

    void add(const QString &url, const QString &title);
    void remove(const QString &url);
    void clearAll();
    bool contains(const QString &url) const;
    Bookmark forUrl(const QString &url) const;
    QList<Bookmark> all() const;

signals:
    void changed();

private:
    QList<Bookmark> query(const QString &sql, const QList<QVariant> &binds) const;
};
