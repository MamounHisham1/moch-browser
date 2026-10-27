#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "storage/database.hpp"
#include "storage/sessionsstore.hpp"

namespace {
QString stepTranscript(const QString &label)
{
    return QString::fromUtf8(QJsonDocument(QJsonArray{
        QJsonObject{{"kind", "start"}, {"label", label}, {"ok", true}},
        QJsonObject{{"kind", "tool"}, {"label", "navigate"}, {"ok", true}}})
        .toJson(QJsonDocument::Compact));
}
} // namespace

class tst_sessions : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void appendGetRoundtrip();
    void recentOrdersNewestFirst();
    void recentLimit();
    void removeDeletes();
    void prunesToTwoHundred();
    void persistsAcrossInstances();
    void changedSignal();

private:
    QTemporaryDir m_home;
};

void tst_sessions::initTestCase()
{
    // Redirect the shared SQLite file into a throwaway home so the test never
    // touches the real profile; must happen before the first Database use.
    qputenv("XDG_CONFIG_HOME", (m_home.path() + "/config").toUtf8());
    qputenv("XDG_DATA_HOME", (m_home.path() + "/data").toUtf8());
    QFile::remove(Database::databasePath());
}

void tst_sessions::appendGetRoundtrip()
{
    SessionsStore store;
    const QString transcript = stepTranscript("Find flights");
    const int id = store.append("Find flights", "done", transcript, 1000, 2000);
    QVERIFY(id > 0);

    const SessionsStore::Session session = store.get(id);
    QCOMPARE(session.id, id);
    QCOMPARE(session.goal, QStringLiteral("Find flights"));
    QCOMPARE(session.state, QStringLiteral("done"));
    QCOMPARE(session.transcriptJson, transcript);
    QCOMPARE(session.startedAt, qint64(1000));
    QCOMPARE(session.finishedAt, qint64(2000));

    const QJsonArray steps =
        QJsonDocument::fromJson(session.transcriptJson.toUtf8()).array();
    QCOMPARE(steps.size(), 2);
    QCOMPARE(steps.at(0).toObject().value("label").toString(),
             QStringLiteral("Find flights"));

    // Unknown ids come back as a default record with id 0.
    QCOMPARE(store.get(id + 12345).id, 0);
}

void tst_sessions::recentOrdersNewestFirst()
{
    SessionsStore store;
    const int a = store.append("task A", "done", stepTranscript("A"), 1, 500000);
    const int b = store.append("task B", "failed", stepTranscript("B"), 2, 600000);
    const int c = store.append("task C", "cancelled", stepTranscript("C"), 3, 700000);
    QVERIFY(a > 0 && b > 0 && c > 0);

    const QList<SessionsStore::Session> sessions = store.recent(3);
    QCOMPARE(sessions.size(), 3);
    QCOMPARE(sessions.at(0).goal, QStringLiteral("task C"));
    QCOMPARE(sessions.at(0).state, QStringLiteral("cancelled"));
    QCOMPARE(sessions.at(1).goal, QStringLiteral("task B"));
    QCOMPARE(sessions.at(1).state, QStringLiteral("failed"));
    QCOMPARE(sessions.at(2).goal, QStringLiteral("task A"));
}

void tst_sessions::recentLimit()
{
    SessionsStore store;
    QCOMPARE(store.recent(1).size(), 1);
    QCOMPARE(store.recent(0).size(), 0);
    QCOMPARE(store.recent(-1).size(), 0);
}

void tst_sessions::removeDeletes()
{
    SessionsStore store;
    const int id = store.append("doomed task", "done", stepTranscript("doomed"),
                                10, 800000);
    QVERIFY(id > 0);
    QCOMPARE(store.get(id).goal, QStringLiteral("doomed task"));

    store.remove(id);
    QCOMPARE(store.get(id).id, 0);
    for (const SessionsStore::Session &session : store.recent(1000))
        QVERIFY2(session.id != id, "removed session still listed in recent()");
}

void tst_sessions::prunesToTwoHundred()
{
    SessionsStore store;
    QList<int> ids;
    for (int i = 0; i < 205; ++i) {
        const int id = store.append(QStringLiteral("task %1").arg(i), "done",
                                    stepTranscript(QStringLiteral("t%1").arg(i)),
                                    1000000 + i, 1000000 + i);
        QVERIFY(id > 0);
        ids.append(id);
    }

    // Exactly the newest 200 rows survive, oldest five are gone.
    QCOMPARE(store.recent(1000).size(), 200);
    for (int i = 0; i < 5; ++i)
        QCOMPARE(store.get(ids.at(i)).id, 0);
    for (int i = 5; i < ids.size(); ++i)
        QVERIFY(store.get(ids.at(i)).id != 0);

    const QList<SessionsStore::Session> sessions = store.recent(200);
    QCOMPARE(sessions.first().goal, QStringLiteral("task 204"));
    QCOMPARE(sessions.last().goal, QStringLiteral("task 5"));
}

void tst_sessions::persistsAcrossInstances()
{
    const QString transcript = stepTranscript("reopened");
    SessionsStore writer;

    // Clear earlier tests' rows: their finished_at values are newer, and
    // append() prunes to the newest 200, which would delete this row too.
    const QList<SessionsStore::Session> stale = writer.recent(1000);
    for (const SessionsStore::Session &session : stale)
        writer.remove(session.id);

    const int id = writer.append("reopened task", "done", transcript, 42, 900000);
    QVERIFY(id > 0);

    SessionsStore reader;
    const SessionsStore::Session session = reader.get(id);
    QCOMPARE(session.id, id);
    QCOMPARE(session.goal, QStringLiteral("reopened task"));
    QCOMPARE(session.transcriptJson, transcript);
    QCOMPARE(session.startedAt, qint64(42));
    QCOMPARE(session.finishedAt, qint64(900000));
}

void tst_sessions::changedSignal()
{
    SessionsStore store;
    QSignalSpy spy(&store, &SessionsStore::changed);
    const int id = store.append("signalled task", "failed", stepTranscript("x"),
                                7, 950000);
    QVERIFY(id > 0);
    store.remove(id);
    QCOMPARE(spy.count(), 2);
}

QTEST_MAIN(tst_sessions)
#include "tst_sessions.moc"
