#pragma once

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QVariant>
#include <functional>

#include "storage/database.hpp"

// Persisted recurring agent tasks: one row per routine with its goal, repeat
// interval and next scheduled run, stored in the Moch SQLite database.
// Routines advance only while Moch is running; anything missed while the app
// was closed stays overdue (next_run_at in the past) and fires once on the
// next scheduler tick.
//
// The implementation lives inline in this header and the class carries no
// Q_OBJECT, so nothing here depends on moc output or on this file being
// listed in CMake: the moch target links before agent/routines.cpp is wired
// in, and keeps doing so afterwards. Change notification uses a callback
// (setChangedCallback) instead of a signal for the same reason.
class RoutinesStore : public QObject
{
public:
    struct Routine
    {
        int id = 0;
        QString goal;
        qint64 intervalSeconds = 0;
        bool enabled = true;
        qint64 nextRunAt = 0;
        qint64 lastRunAt = 0; // 0 = never fired
        QString lastState; // "triggered", an agent final state, "skipped_busy"
        qint64 createdAt = 0;
    };

    // A routine that should fire now.
    struct Due
    {
        int id = 0;
        QString goal;
    };

    explicit RoutinesStore(QObject *parent = nullptr)
        : QObject(parent)
    {
        Database::connection();
        QSqlQuery query(Database::connection());
        query.exec("CREATE TABLE IF NOT EXISTS routines ("
                   "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                   "goal TEXT NOT NULL, interval_seconds INTEGER NOT NULL,"
                   "enabled INTEGER NOT NULL DEFAULT 1,"
                   "next_run_at INTEGER NOT NULL, last_run_at INTEGER,"
                   "last_state TEXT, created_at INTEGER NOT NULL)");
    }

    // Adds an enabled routine first scheduled at now + intervalSeconds.
    // Returns the new id, or -1 on failure.
    int add(const QString &goal, qint64 intervalSeconds)
    {
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        QSqlQuery query(Database::connection());
        query.prepare("INSERT INTO routines (goal, interval_seconds, enabled,"
                      " next_run_at, created_at) VALUES (?, ?, 1, ?, ?)");
        query.addBindValue(goal);
        query.addBindValue(intervalSeconds);
        query.addBindValue(now + intervalSeconds);
        query.addBindValue(now);
        if (!query.exec()) {
            qWarning("moch: routine insert failed: %s",
                     qPrintable(query.lastError().text()));
            return -1;
        }
        const int id = query.lastInsertId().toInt();
        pruneDisabled();
        notifyChanged();
        return id;
    }

    void remove(int id)
    {
        QSqlQuery query(Database::connection());
        query.prepare("DELETE FROM routines WHERE id = ?");
        query.addBindValue(id);
        if (!query.exec())
            qWarning("moch: routine delete failed: %s",
                     qPrintable(query.lastError().text()));
        notifyChanged();
    }

    // Disables, or re-enables and reschedules to now + interval so a
    // re-enabled routine starts a fresh full period.
    void setEnabled(int id, bool enabled)
    {
        QSqlQuery query(Database::connection());
        if (enabled) {
            query.prepare("UPDATE routines SET enabled = 1,"
                          " next_run_at = ? + interval_seconds WHERE id = ?");
            query.addBindValue(QDateTime::currentSecsSinceEpoch());
        } else {
            query.prepare("UPDATE routines SET enabled = 0 WHERE id = ?");
        }
        query.addBindValue(id);
        if (!query.exec())
            qWarning("moch: routine update failed: %s",
                     qPrintable(query.lastError().text()));
        notifyChanged();
    }

    // Fire-time bookkeeping: stamps the attempt in last_run_at/last_state and
    // reschedules next_run_at to now + interval, whatever the run's outcome.
    void touch(int id, bool ok, const QString &stateText)
    {
        Q_UNUSED(ok);
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        QSqlQuery query(Database::connection());
        query.prepare("UPDATE routines SET last_run_at = ?, last_state = ?,"
                      " next_run_at = ? + interval_seconds WHERE id = ?");
        query.addBindValue(now);
        query.addBindValue(stateText);
        query.addBindValue(now);
        query.addBindValue(id);
        if (!query.exec())
            qWarning("moch: routine touch failed: %s",
                     qPrintable(query.lastError().text()));
        notifyChanged();
    }

    // Run-completion bookkeeping: records the agent's final state without
    // touching last_run_at/next_run_at (touch() already scheduled the run).
    void recordOutcome(int id, const QString &stateText)
    {
        QSqlQuery query(Database::connection());
        query.prepare("UPDATE routines SET last_state = ? WHERE id = ?");
        query.addBindValue(stateText);
        query.addBindValue(id);
        if (!query.exec())
            qWarning("moch: routine outcome failed: %s",
                     qPrintable(query.lastError().text()));
        notifyChanged();
    }

    // Enabled routines with next_run_at <= now, most overdue first.
    QList<Due> due(qint64 now) const
    {
        QList<Due> routines;
        QSqlQuery query(Database::connection());
        query.prepare("SELECT id, goal FROM routines"
                      " WHERE enabled = 1 AND next_run_at <= ?"
                      " ORDER BY next_run_at, id");
        query.addBindValue(now);
        if (!query.exec()) {
            qWarning("moch: routine query failed: %s",
                     qPrintable(query.lastError().text()));
            return routines;
        }
        while (query.next()) {
            Due routine;
            routine.id = query.value(0).toInt();
            routine.goal = query.value(1).toString();
            routines.append(routine);
        }
        return routines;
    }

    // All routines for the management UI, enabled first, soonest next run
    // within each group.
    QList<Routine> all() const
    {
        QList<Routine> routines;
        QSqlQuery query(Database::connection());
        query.prepare("SELECT id, goal, interval_seconds, enabled, next_run_at,"
                      " last_run_at, last_state, created_at FROM routines"
                      " ORDER BY enabled DESC, next_run_at, id");
        if (!query.exec()) {
            qWarning("moch: routine query failed: %s",
                     qPrintable(query.lastError().text()));
            return routines;
        }
        while (query.next()) {
            Routine routine;
            routine.id = query.value(0).toInt();
            routine.goal = query.value(1).toString();
            routine.intervalSeconds = query.value(2).toLongLong();
            routine.enabled = query.value(3).toInt() != 0;
            routine.nextRunAt = query.value(4).toLongLong();
            routine.lastRunAt = query.value(5).toLongLong();
            routine.lastState = query.value(6).toString();
            routine.createdAt = query.value(7).toLongLong();
            routines.append(routine);
        }
        return routines;
    }

    // Registers the single change listener (the open RoutineDialog); pass an
    // empty function to unregister. Called after every store mutation.
    void setChangedCallback(std::function<void()> callback)
    {
        m_changedCallback = std::move(callback);
    }

private:
    // Keeps at most the 100 newest disabled routines; called from add().
    void pruneDisabled()
    {
        QSqlQuery query(Database::connection());
        query.prepare("DELETE FROM routines WHERE enabled = 0 AND id NOT IN ("
                      "SELECT id FROM routines WHERE enabled = 0"
                      " ORDER BY created_at DESC, id DESC LIMIT 100)");
        if (!query.exec())
            qWarning("moch: routine prune failed: %s",
                     qPrintable(query.lastError().text()));
    }

    void notifyChanged()
    {
        if (m_changedCallback)
            m_changedCallback();
    }

    std::function<void()> m_changedCallback;
};
