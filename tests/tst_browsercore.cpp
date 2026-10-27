#include <QElapsedTimer>
#include <QJsonDocument>
#include <QNetworkCookie>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>
#include <QWebEngineCookieStore>
#include <QWebEnginePage>
#include <QWebEngineProfile>

#include "browser/bookmarkstore.hpp"
#include "browser/browserimport.hpp"
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
    void initTestCase();
    void urlDetection();
    void urlSearchFallback();
    void agentTargetResolution();
    void searchEngines();
    void settingsDefaultsAndRoundtrip();
    void historyRecordAndSearch();
    void historyTopSitesDedup();
    void bookmarksCrud();
    void tabLifecycle();
    void namedProfilePersistsCookies();
    void sessionSaveRestoreRoundtrip();
    void importFromFixtureProfiles();

private:
    QTemporaryDir m_home;
    bool waitForLoad(WebView *view, int timeoutMs = 10000);
};

void tst_browsercore::initTestCase()
{
    // Redirect all user data away from the real profile before anything in
    // this process touches QStandardPaths or WebEngine.
    qputenv("XDG_CONFIG_HOME", (m_home.path() + "/config").toUtf8());
    qputenv("XDG_DATA_HOME", (m_home.path() + "/data").toUtf8());
}

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

void tst_browsercore::agentTargetResolution()
{
    struct Case { const char *in; const char *want; } cases[] = {
        {"youtube", "https://www.youtube.com"},
        {"GMAIL", "https://mail.google.com"},
        {"whatsapp", "https://web.whatsapp.com"},
        {"x", "https://x.com"},
        {"noon", "https://www.noon.com/egypt-en/"},
        {"example.com", "https://example.com"},
        {"https://already.full", "https://already.full"},
        {"somebrandname", "https://somebrandname.com"},
    };
    for (const Case &c : cases)
        QCOMPARE(UrlResolver::resolveAgentTarget(QString::fromLatin1(c.in), "duckduckgo")
                     .toString(),
                 QString::fromLatin1(c.want));
    const QUrl phrase = UrlResolver::resolveAgentTarget("best kitchen knives", "duckduckgo");
    QCOMPARE(phrase.host(), QString("duckduckgo.com"));
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

void tst_browsercore::namedProfilePersistsCookies()
{
    // A constructed named profile must persist cookies to disk across
    // "restarts" — the same setup ApplicationController now uses at
    // startup (QWebEngineProfile::defaultProfile() is off-the-record with
    // the Qt in use here and cannot persist anything). Storage stays in a
    // temp dir; the real profile is never touched.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString storage = dir.path() + "/webengine";
    QNetworkCookie cookie(QByteArray("moch_probe"), QByteArray("persist1"));
    cookie.setDomain(QStringLiteral("example.com"));
    cookie.setPath(QStringLiteral("/"));
    cookie.setExpirationDate(QDateTime::currentDateTimeUtc().addDays(1));
    {
        QWebEngineProfile profile(QStringLiteral("tst_moch"));
        QVERIFY(!profile.isOffTheRecord());
        QCOMPARE(profile.persistentCookiesPolicy(),
                 QWebEngineProfile::AllowPersistentCookies);
        profile.setPersistentStoragePath(storage);
        // Chromium's network service (and with it the cookie backend)
        // starts lazily on the first navigation, so load a page first.
        QWebEnginePage page(&profile);
        page.load(QUrl("data:text/html,<h1>hi</h1>"));
        QTRY_VERIFY(!page.isLoading());
        QWebEngineCookieStore *store = profile.cookieStore();
        // cookieAdded fires on Chromium's IO thread; queue into the main
        // thread via a context object (QSignalSpy drops unregistered
        // cross-thread payload types).
        QList<QNetworkCookie> added;
        QObject::connect(store, &QWebEngineCookieStore::cookieAdded, store,
                         [&added](const QNetworkCookie &c) { added.append(c); });
        store->loadAllCookies();
        store->setCookie(cookie, QUrl("https://example.com/"));
        QTRY_VERIFY(added.size() == 1);
    } // profile destructor flushes the cookie database

    // "Restart" evidence: the cookie must be in the profile's Cookies
    // database on disk, so any later profile instance restores it.
    const QString cookiesDb = storage + "/Cookies";
    QTemporaryDir copyDir;
    QVERIFY(copyDir.isValid());
    const QString copyPath = copyDir.filePath("Cookies");
    bool flushed = false;
    QElapsedTimer drain;
    drain.start();
    while (!flushed && drain.elapsed() < 10000) {
        QTest::qWait(500);
        if (QFileInfo::exists(cookiesDb) && QFile::copy(cookiesDb, copyPath))
            flushed = true;
    }
    QVERIFY2(flushed, "cookie database must be flushed to disk");
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "moch-cookies");
        db.setDatabaseName(copyPath);
        QVERIFY(db.open());
        QSqlQuery query(db);
        query.prepare("SELECT COUNT(*) FROM cookies WHERE name = ?");
        query.addBindValue(QStringLiteral("moch_probe"));
        QVERIFY(query.exec());
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 1);
        query.finish();
        db.close();
    }
    QSqlDatabase::removeDatabase("moch-cookies");
}

void tst_browsercore::sessionSaveRestoreRoundtrip()
{
    Settings settings;

    // Tracking off → nothing is written, even on explicit save.
    QFile::remove(TabManager::sessionFilePath());
    {
        TabManager tabs(&settings, nullptr);
        tabs.addTab(QUrl("data:text/html,<title>Untracked</title>"));
        tabs.saveSessionNow();
    }
    QVERIFY(!QFile::exists(TabManager::sessionFilePath()));

    // Tracked session: two tabs, first one active.
    int active = -1;
    {
        TabManager tabs(&settings, nullptr);
        tabs.setSessionTrackingEnabled(true);
        const int first = tabs.addTab(QUrl("data:text/html,<title>One</title><h1>1</h1>"));
        tabs.addTab(QUrl("data:text/html,<title>Two</title><h1>2</h1>"));
        tabs.setCurrentIndex(first);
        QVERIFY(waitForLoad(tabs.view(0)));
        QVERIFY(waitForLoad(tabs.view(1)));
        QCOMPARE(tabs.view(0)->title(), QString("One"));
        tabs.saveSessionNow();

        const QList<SessionTab> saved = TabManager::loadSavedSession(&active);
        QCOMPARE(saved.size(), 2);
        QCOMPARE(active, first);
        QVERIFY(saved.at(0).url.toString().startsWith("data:"));
        QVERIFY(saved.at(1).url.toString().startsWith("data:"));
        QCOMPARE(saved.at(0).title, QString("One"));
    }

    // A fresh manager restores order and the active tab.
    {
        TabManager reopened(&settings, nullptr);
        QCOMPARE(reopened.count(), 0);
        reopened.setSessionTrackingEnabled(true);
        reopened.restoreSavedSession();
        QCOMPARE(reopened.count(), 2);
        QCOMPARE(reopened.currentIndex(), active);
        QCOMPARE(TabManager::loadSavedSession().size(), 2);
    }

    // Corrupt or truncated file degrades to "nothing to restore".
    QFile file(TabManager::sessionFilePath());
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("{not json");
    file.close();
    QVERIFY(TabManager::loadSavedSession().isEmpty());
    QVERIFY(TabManager::loadSavedSession(&active).isEmpty());
    QCOMPARE(active, 0);

    QFile::remove(TabManager::sessionFilePath());
}

namespace {
BrowserSource detectId(const QList<BrowserSource> &sources, const QString &id)
{
    for (const BrowserSource &source : sources)
        if (source.id == id)
            return source;
    return {};
}
} // namespace

void tst_browsercore::importFromFixtureProfiles()
{
    QTemporaryDir home;
    QVERIFY(home.isValid());
    const QString homePath = home.path();

    // --- Chromium fixture: Bookmarks JSON + History sqlite -------------
    const QString chromeDefault = homePath + "/.config/google-chrome/Default";
    QVERIFY(QDir().mkpath(chromeDefault));
    {
        QFile file(chromeDefault + "/Bookmarks");
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"JSON({
            "roots": {
                "bookmark_bar": {"type": "folder", "name": "Bar", "children": [
                    {"type": "url", "name": "Chrome BM",
                     "url": "https://chrome.example.com/",
                     "date_added": "13390000000000000"},
                    {"type": "folder", "name": "Folder", "children": [
                        {"type": "url", "name": "Nested",
                         "url": "https://nested.example.com/",
                         "date_added": "13390000000000001"}]}
                ]},
                "other": {"type": "folder", "name": "Other", "children": [
                    {"type": "url", "name": "Dup",
                     "url": "https://dup.example.com/",
                     "date_added": "13390000000000002"}
                ]}
            }
        })JSON");
        file.close();
    }
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "fixture-chrome");
        db.setDatabaseName(chromeDefault + "/History");
        QVERIFY(db.open());
        QSqlQuery query(db);
        QVERIFY(query.exec("CREATE TABLE urls (id INTEGER PRIMARY KEY,"
                           " url TEXT, title TEXT, last_visit_time INTEGER)"));
        QVERIFY(query.exec(
            "INSERT INTO urls (url, title, last_visit_time) VALUES"
            " ('https://chrome.example.com/', 'Chrome', 13400000000000000)"));
        QVERIFY(query.exec(
            "INSERT INTO urls (url, title, last_visit_time) VALUES"
            " ('https://historyonly.example.com/', 'HO', 13400000000000001)"));
        query.finish();
        db.close();
    }
    QSqlDatabase::removeDatabase("fixture-chrome");

    // --- Firefox fixture: profiles.ini + places.sqlite -----------------
    const QString ffBase = homePath + "/.mozilla/firefox";
    const QString ffProfile = ffBase + "/abc123.default";
    QVERIFY(QDir().mkpath(ffProfile));
    {
        QFile ini(ffBase + "/profiles.ini");
        QVERIFY(ini.open(QIODevice::WriteOnly));
        ini.write("[Profile0]\nName=default\nIsRelative=1\n"
                  "Path=abc123.default\nDefault=1\n");
        ini.close();
    }
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "fixture-ff");
        db.setDatabaseName(ffProfile + "/places.sqlite");
        QVERIFY(db.open());
        QSqlQuery query(db);
        QVERIFY(query.exec("CREATE TABLE moz_places (id INTEGER PRIMARY KEY,"
                           " url TEXT, title TEXT, last_visit_date INTEGER)"));
        QVERIFY(query.exec("CREATE TABLE moz_bookmarks (id INTEGER PRIMARY KEY,"
                           " type INTEGER, fk INTEGER, title TEXT,"
                           " dateAdded INTEGER)"));
        QVERIFY(query.exec(
            "INSERT INTO moz_places (url, title, last_visit_date) VALUES"
            " ('https://ff.example.com/', 'FF', 1700000000000000)"));
        QVERIFY(query.exec(
            "INSERT INTO moz_places (url, title, last_visit_date) VALUES"
            " ('https://ff2.example.com/', 'FF2', 1700000000000001)"));
        QVERIFY(query.exec(
            "INSERT INTO moz_bookmarks (type, fk, title, dateAdded) VALUES"
            " (1, 1, 'FF bookmark', 1700000000000000)"));
        QVERIFY(query.exec(
            "INSERT INTO moz_bookmarks (type, fk, title, dateAdded) VALUES"
            " (1, 2, 'FF bookmark 2', 1700000000000001)"));
        QVERIFY(query.exec(
            "INSERT INTO moz_bookmarks (type, fk, title, dateAdded) VALUES"
            " (2, NULL, 'Folder', 1700000000000002)"));
        query.finish();
        db.close();
    }
    QSqlDatabase::removeDatabase("fixture-ff");

    // --- Detection with entry counts -----------------------------------
    const QList<BrowserSource> found = BrowserImport::availableBrowsers(homePath);
    QCOMPARE(found.size(), 2);
    QCOMPARE(detectId(found, "chrome").bookmarks, 3);
    QCOMPARE(detectId(found, "chrome").history, 2);
    QCOMPARE(detectId(found, "firefox").bookmarks, 2);
    QCOMPARE(detectId(found, "firefox").history, 2);
    const QStringList ids = BrowserImport::detectableBrowsers(homePath);
    QVERIFY(ids.contains("chrome"));
    QVERIFY(ids.contains("firefox"));

    // --- Merge with duplicate skipping ----------------------------------
    HistoryStore history;
    BookmarkStore bookmarks;
    history.clearAll();
    bookmarks.clearAll();
    history.recordVisit("https://chrome.example.com/", "Existing history");
    bookmarks.add("https://dup.example.com/", "Existing bookmark");

    ImportSummary summary;
    QVERIFY(BrowserImport::importFrom("chrome", &bookmarks, &history, &summary,
                                      homePath));
    QCOMPARE(summary.bookmarks, 2);  // dup.example.com already present
    QCOMPARE(summary.history, 1);    // chrome.example.com already present
    QCOMPARE(summary.duplicates, 2);
    QVERIFY(summary.warnings.isEmpty());
    QCOMPARE(bookmarks.forUrl("https://nested.example.com/").title,
             QString("Nested"));
    QCOMPARE(history.search("historyonly").size(), 1);

    // Importing again skips everything as duplicates.
    QVERIFY(BrowserImport::importFrom("chrome", &bookmarks, &history, &summary,
                                      homePath));
    QCOMPARE(summary.bookmarks, 0);
    QCOMPARE(summary.history, 0);
    QCOMPARE(summary.duplicates, 5);

    QVERIFY(BrowserImport::importFrom("firefox", &bookmarks, &history, &summary,
                                      homePath));
    QCOMPARE(summary.bookmarks, 2);
    QCOMPARE(summary.history, 2);
    QVERIFY(summary.warnings.isEmpty());
    QCOMPARE(bookmarks.forUrl("https://ff2.example.com/").title,
             QString("FF bookmark 2"));

    // Unknown browsers fail with a warning; stores are untouched.
    const int bookmarksBefore = bookmarks.all().size();
    QVERIFY(!BrowserImport::importFrom("netscape", &bookmarks, &history,
                                       &summary, homePath));
    QVERIFY(!summary.warnings.isEmpty());
    QCOMPARE(bookmarks.all().size(), bookmarksBefore);

    history.clearAll();
    bookmarks.clearAll();
}

QTEST_MAIN(tst_browsercore)
#include "tst_browsercore.moc"
