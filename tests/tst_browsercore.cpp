#include <QJsonDocument>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

#include "browser/bookmarkstore.hpp"
#include "browser/historystore.hpp"
#include "browser/tabmanager.hpp"
#include "browser/urlresolver.hpp"
#include "browser/webview.hpp"
#include "storage/database.hpp"
#include "storage/settings.hpp"

class tst_browsercore : public QObject
{
    Q_OBJECT
private slots:
    void urlDetection();
    void urlSearchFallback();
    void searchEngines();
    void settingsDefaultsAndRoundtrip();
    void historyRecordAndSearch();
    void historyTopSitesDedup();
    void bookmarksCrud();
    void tabLifecycle();

private:
    QTemporaryDir m_home;
    bool waitForLoad(WebView *view, int timeoutMs = 10000);
};

bool tst_browsercore::waitForLoad(WebView *view, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!view->isLoading() && timer.elapsed() < 2000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    while (view->isLoading() && timer.elapsed() < timeoutMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    return !view->isLoading();
}

void tst_browsercore::urlDetection()
{
    QCOMPARE(UrlResolver::fromUserInput("https://example.com", "duckduckgo").toString(),
             QString("https://example.com"));
    QCOMPARE(UrlResolver::fromUserInput("example.com", "duckduckgo").toString(),
             QString("https://example.com"));
    QCOMPARE(UrlResolver::fromUserInput("example.com/path?q=1", "duckduckgo").toString(),
             QString("https://example.com/path?q=1"));
    QCOMPARE(UrlResolver::fromUserInput("localhost:8080", "duckduckgo").toString(),
             QString("https://localhost:8080"));
    QCOMPARE(UrlResolver::fromUserInput("http://localhost", "duckduckgo").toString(),
             QString("http://localhost"));
    QCOMPARE(UrlResolver::fromUserInput("moch://newtab", "duckduckgo").toString(),
             QString("moch://newtab"));
}

void tst_browsercore::urlSearchFallback()
{
    const QUrl url = UrlResolver::fromUserInput("laptop comparison 2026", "duckduckgo");
    QCOMPARE(url.host(), QString("duckduckgo.com"));
    QVERIFY(url.query().contains("laptop comparison 2026"));
    QVERIFY(UrlResolver::fromUserInput("what is a browser?", "google").host()
            == QString("www.google.com"));
}

void tst_browsercore::searchEngines()
{
    QVERIFY(UrlResolver::searchEngineKeys().contains("duckduckgo"));
    QVERIFY(UrlResolver::searchEngineKeys().contains("google"));
    QCOMPARE(UrlResolver::searchEngineName("duckduckgo"), QString("DuckDuckGo"));
}

void tst_browsercore::settingsDefaultsAndRoundtrip()
{
    qputenv("XDG_CONFIG_HOME", (m_home.path() + "/config").toUtf8());
    qputenv("XDG_DATA_HOME", (m_home.path() + "/data").toUtf8());

    Settings fresh;
    QCOMPARE(fresh.theme(), QString("dark"));
    QCOMPARE(fresh.get("agent.permission_level").toString(), QString("cautious"));
    QCOMPARE(fresh.get("agent.max_tool_calls").toInt(), 80);
    QCOMPARE(fresh.get("ai.temperature").toDouble(), 0.2);

    fresh.set("agent.max_tool_calls", 77);
    fresh.set("ai.model", "test-model");

    Settings reloaded;
    QCOMPARE(reloaded.get("agent.max_tool_calls").toInt(), 77);
    QCOMPARE(reloaded.get("ai.model").toString(), QString("test-model"));
    QCOMPARE(reloaded.theme(), QString("dark"));
}

void tst_browsercore::historyRecordAndSearch()
{
    qputenv("XDG_CONFIG_HOME", (m_home.path() + "/config").toUtf8());
    qputenv("XDG_DATA_HOME", (m_home.path() + "/data").toUtf8());
    QFile::remove(Database::databasePath());

    HistoryStore store;
    store.recordVisit("https://example.com/", "Example");
    store.recordVisit("https://example.org/", "Other");
    store.recordVisit("https://example.com/page", "Example Page");

    QCOMPARE(store.visits(10, 0).size(), 3);
    const QList<HistoryEntry> found = store.search("example.org");
    QCOMPARE(found.size(), 1);
    QCOMPARE(found.first().url, QString("https://example.org/"));
    QVERIFY(store.search("nonexistent-xyz").isEmpty());

    store.removeForUrl("https://example.org/");
    QCOMPARE(store.visits(10, 0).size(), 2);
    store.clearAll();
    QVERIFY(store.visits(10, 0).isEmpty());
}

void tst_browsercore::historyTopSitesDedup()
{
    HistoryStore store;
    store.clearAll();
    for (int i = 0; i < 5; ++i)
        store.recordVisit(QString("https://top.example.com/a%1").arg(i), "Top");
    store.recordVisit("https://rare.example.com/", "Rare");

    const QList<HistoryEntry> sites = store.topSites(2);
    QVERIFY2(sites.size() == 2, "two hosts expected");
    QVERIFY(sites.first().url.contains("top.example.com"));
    for (const HistoryEntry &entry : sites)
        QVERIFY(!QUrl(entry.url).host().isEmpty());
    store.clearAll();
}

void tst_browsercore::bookmarksCrud()
{
    BookmarkStore store;
    store.clearAll();
    QVERIFY(!store.contains("https://keep.example.com/"));

    QSignalSpy changedSpy(&store, &BookmarkStore::changed);
    store.add("https://keep.example.com/", "Keep");
    QCOMPARE(changedSpy.count(), 1);
    QVERIFY(store.contains("https://keep.example.com/"));
    QCOMPARE(store.forUrl("https://keep.example.com/").title, QString("Keep"));
    QCOMPARE(store.all().size(), 1);

    store.remove("https://keep.example.com/");
    QVERIFY(!store.contains("https://keep.example.com/"));
    QVERIFY(store.forUrl("https://keep.example.com/").url.isEmpty());
}

void tst_browsercore::tabLifecycle()
{
    qputenv("XDG_CONFIG_HOME", (m_home.path() + "/config").toUtf8());
    qputenv("XDG_DATA_HOME", (m_home.path() + "/data").toUtf8());

    Settings settings;
    TabManager tabs(&settings, nullptr);

    QCOMPARE(tabs.count(), 0);
    const int first = tabs.addTab(QUrl("data:text/html,<title>One</title><h1>1</h1>"));
    QCOMPARE(first, 0);
    const int second = tabs.addTab(QUrl("data:text/html,<title>Two</title><h1>2</h1>"));
    QCOMPARE(tabs.count(), 2);
    QCOMPARE(tabs.currentIndex(), second);

    tabs.setCurrentIndex(first);
    QCOMPARE(tabs.currentIndex(), first);

    QVERIFY(waitForLoad(tabs.view(first)));
    QCOMPARE(tabs.view(first)->title(), QString("One"));

    tabs.closeTab(first);
    QCOMPARE(tabs.count(), 1);
    QCOMPARE(tabs.view(0), tabs.currentView());

    tabs.closeTab(0);
    QCOMPARE(tabs.count(), 0);
    QCOMPARE(tabs.currentView(), nullptr);

    tabs.reopenClosedTab();
    QCOMPARE(tabs.count(), 1);
}

QTEST_MAIN(tst_browsercore)
#include "tst_browsercore.moc"
