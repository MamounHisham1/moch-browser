#pragma once

#include <QList>
#include <QObject>
#include <QString>

// Persisted AI sessions: one record per agent task with its goal, final
// state and step transcript, so past sessions can be listed and re-read.
class SessionsStore : public QObject
{
    Q_OBJECT
public:
    struct Session
    {
        int id = 0;
        QString goal;
        QString state;
        QString transcriptJson;
        qint64 startedAt = 0;
        qint64 finishedAt = 0;
    };

    explicit SessionsStore(QObject *parent = nullptr);

    // Inserts or updates nothing: every finished task appends one row.
    // Returns the new session id, or -1 on failure.
    int append(const QString &goal, const QString &state,
               const QString &transcriptJson, qint64 startedAt,
               qint64 finishedAt);

    QList<Session> recent(int limit = 50) const;
    Session get(int id) const;
    void remove(int id);

signals:
    void changed();
};
