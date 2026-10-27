#include "storage/sessionsstore.hpp"

#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include "storage/database.hpp"

namespace {
// Keep only the newest N finished sessions; older rows are pruned on append.
constexpr int kMaxSessions = 200;
} // namespace

SessionsStore::SessionsStore(QObject *parent)
    : QObject(parent)
{
    Database::connection();
    QSqlQuery query(Database::connection());
    query.exec("CREATE TABLE IF NOT EXISTS agent_sessions ("
               "id INTEGER PRIMARY KEY AUTOINCREMENT,"
               "goal TEXT NOT NULL, state TEXT NOT NULL, transcript TEXT NOT NULL,"
               "started_at INTEGER NOT NULL, finished_at INTEGER NOT NULL)");
    query.exec("CREATE INDEX IF NOT EXISTS agent_sessions_finished "
               "ON agent_sessions(finished_at)");
}

int SessionsStore::append(const QString &goal, const QString &state,
                          const QString &transcriptJson, qint64 startedAt,
                          qint64 finishedAt)
{
    QSqlQuery query(Database::connection());
    query.prepare("INSERT INTO agent_sessions (goal, state, transcript,"
                  " started_at, finished_at) VALUES (?, ?, ?, ?, ?)");
    query.addBindValue(goal);
    query.addBindValue(state);
    query.addBindValue(transcriptJson);
    query.addBindValue(startedAt);
    query.addBindValue(finishedAt);
    if (!query.exec()) {
        qWarning("moch: session insert failed: %s",
                 qPrintable(query.lastError().text()));
        return -1;
    }
    const int id = query.lastInsertId().toInt();

    // Prune to the newest kMaxSessions rows (finished_at, then id, desc).
    query.prepare("DELETE FROM agent_sessions WHERE id NOT IN ("
                  "SELECT id FROM agent_sessions"
                  " ORDER BY finished_at DESC, id DESC LIMIT ?)");
    query.addBindValue(kMaxSessions);
    if (!query.exec())
        qWarning("moch: session prune failed: %s",
                 qPrintable(query.lastError().text()));

    emit changed();
    return id;
}

QList<SessionsStore::Session> SessionsStore::recent(int limit) const
{
    QList<Session> sessions;
    QSqlQuery query(Database::connection());
    query.prepare("SELECT id, goal, state, transcript, started_at, finished_at"
                  " FROM agent_sessions"
                  " ORDER BY finished_at DESC, id DESC LIMIT ?");
    query.addBindValue(qMax(limit, 0)); // SQLite reads a negative LIMIT as "all"
    if (!query.exec()) {
        qWarning("moch: session query failed: %s",
                 qPrintable(query.lastError().text()));
        return sessions;
    }
    while (query.next()) {
        Session session;
        session.id = query.value(0).toInt();
        session.goal = query.value(1).toString();
        session.state = query.value(2).toString();
        session.transcriptJson = query.value(3).toString();
        session.startedAt = query.value(4).toLongLong();
        session.finishedAt = query.value(5).toLongLong();
        sessions.append(session);
    }
    return sessions;
}

SessionsStore::Session SessionsStore::get(int id) const
{
    Session session;
    QSqlQuery query(Database::connection());
    query.prepare("SELECT id, goal, state, transcript, started_at, finished_at"
                  " FROM agent_sessions WHERE id = ?");
    query.addBindValue(id);
    if (!query.exec()) {
        qWarning("moch: session lookup failed: %s",
                 qPrintable(query.lastError().text()));
        return session;
    }
    if (!query.next())
        return Session{}; // id stays 0 for a missing row
    session.id = query.value(0).toInt();
    session.goal = query.value(1).toString();
    session.state = query.value(2).toString();
    session.transcriptJson = query.value(3).toString();
    session.startedAt = query.value(4).toLongLong();
    session.finishedAt = query.value(5).toLongLong();
    return session;
}

void SessionsStore::remove(int id)
{
    QSqlQuery query(Database::connection());
    query.prepare("DELETE FROM agent_sessions WHERE id = ?");
    query.addBindValue(id);
    if (!query.exec())
        qWarning("moch: session delete failed: %s",
                 qPrintable(query.lastError().text()));
    emit changed();
}
