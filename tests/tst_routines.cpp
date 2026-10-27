#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

#include "agent/routines.hpp"
#include "storage/database.hpp"

namespace {
// Database::connection() is a process-wide singleton, so rows would leak
// between test slots without an explicit wipe.
void clearRoutines()
{
    QSqlQuery query(Database::connection());
    query.exec("DELETE FROM routines");
}
} // namespace

class tst_routines : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void addDueAndTouchReschedules();
    void recordOutcomeKeepsCadence();
    void disableEnableReschedules();
    void dueMostOverdueFirst();
    void persistsAcrossInstances();

private:
    QTemporaryDir m_home;
};

void tst_routines::initTestCase()
{
    // Redirect the shared SQLite file into a throwaway home so the test never
    // touches the real profile; must happen before the first Database use.
    qputenv("XDG_CONFIG_HOME", (m_home.path() + "/config").toUtf8());
    qputenv("XDG_DATA_HOME", (m_home.path() + "/data").toUtf8());
    QFile::remove(Database::databasePath());
}

void tst_routines::addDueAndTouchReschedules()
{
    clearRoutines();
    RoutinesStore store;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const int id = store.add(QStringLiteral("check mail"), 60);
    QVERIFY(id > 0);

    // Not due before the first interval elapses.
    QVERIFY(store.due(now).isEmpty());

    // Overdue once the interval has passed (any horizon works).
    const QList<RoutinesStore::Due> due = store.due(now + 3600);
    QCOMPARE(due.size(), 1);
    QCOMPARE(due.first().id, id);
    QCOMPARE(due.first().goal, QStringLiteral("check mail"));

    // touch() stamps the attempt and pushes the next run a full period out.
    store.touch(id, true, QStringLiteral("triggered"));
    const RoutinesStore::Routine row = store.all().first();
    QCOMPARE(row.lastState, QStringLiteral("triggered"));
    QVERIFY(row.lastRunAt >= now);
    QVERIFY(row.nextRunAt >= now + 60);
    QVERIFY(store.due(now + 30).isEmpty());
    QCOMPARE(store.due(row.nextRunAt).size(), 1);
}

void tst_routines::recordOutcomeKeepsCadence()
{
    clearRoutines();
    RoutinesStore store;
    const int id = store.add(QStringLiteral("price watch"), 3600);
    QVERIFY(id > 0);
    store.touch(id, true, QStringLiteral("triggered"));
    const RoutinesStore::Routine before = store.all().first();

    store.recordOutcome(id, QStringLiteral("done"));
    const RoutinesStore::Routine after = store.all().first();
    QCOMPARE(after.lastState, QStringLiteral("done"));
    QCOMPARE(after.nextRunAt, before.nextRunAt);
    QCOMPARE(after.lastRunAt, before.lastRunAt);
}

void tst_routines::disableEnableReschedules()
{
    clearRoutines();
    RoutinesStore store;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const int id = store.add(QStringLiteral("daily digest"), 86400);
    QVERIFY(id > 0);

    // Disabled routines never fire, even when overdue.
    store.setEnabled(id, false);
    store.touch(id, false, QStringLiteral("triggered")); // simulate stale next_run
    QVERIFY(store.due(now + 86400 * 7).isEmpty());

    // Re-enabling starts a fresh period: not due immediately.
    store.setEnabled(id, true);
    QVERIFY(store.due(QDateTime::currentSecsSinceEpoch()).isEmpty());
    const RoutinesStore::Routine row = store.all().first();
    QVERIFY(row.enabled);
    QCOMPARE(store.due(row.nextRunAt).size(), 1);
}

void tst_routines::dueMostOverdueFirst()
{
    clearRoutines();
    RoutinesStore store;
    // Distinct intervals keep next_run_at an exact ordering even when the
    // adds land in the same wall-clock second.
    const int a = store.add(QStringLiteral("hourly"), 3600);
    const int b = store.add(QStringLiteral("minutely"), 60);
    QVERIFY(a > 0 && b > 0);

    const QList<RoutinesStore::Due> due = store.due(QDateTime::currentSecsSinceEpoch()
                                                    + 7200);
    QCOMPARE(due.size(), 2);
    QCOMPARE(due.first().id, b);
    QCOMPARE(due.last().id, a);
}

void tst_routines::persistsAcrossInstances()
{
    clearRoutines();
    RoutinesStore writer;
    const int id = writer.add(QStringLiteral("reopened"), 60);
    QVERIFY(id > 0);

    RoutinesStore reader;
    const QList<RoutinesStore::Routine> rows = reader.all();
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.first().id, id);
    QCOMPARE(rows.first().goal, QStringLiteral("reopened"));
}

QTEST_MAIN(tst_routines)
#include "tst_routines.moc"
