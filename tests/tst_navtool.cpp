#include <QtWidgets>
#include <QtTest>

#include "browser/tabmanager.hpp"
#include "browser/webview.hpp"
#include "storage/settings.hpp"
#include "tools/toolregistry.hpp"

// Regression for the SIGSEGV in NavigateToUrlTool: the timeout lambda used to
// capture the local QMetaObject::Connection by reference and touch it from a
// 20s single-shot timer, long after execute() returned. This test arms the
// tool against a non-routable address, survives the timer firing, and asserts
// the timeout result arrives.
class tst_navtool : public QObject
{
    Q_OBJECT
private slots:
    void timeoutSurvivesTimerFire();
    void findElementsReturnsMatches();

private:
    QTemporaryDir m_home;
    bool waitForLoad(WebView *view, int timeoutMs = 10000);
};

bool tst_navtool::waitForLoad(WebView *view, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 2000 && !view->isLoading())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    while (view->isLoading() && timer.elapsed() < timeoutMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    return !view->isLoading();
}

void tst_navtool::timeoutSurvivesTimerFire()
{
    qputenv("XDG_CONFIG_HOME", (m_home.path() + "/config").toUtf8());
    qputenv("XDG_DATA_HOME", (m_home.path() + "/data").toUtf8());
    Settings settings;
    TabManager tabs(&settings, nullptr, nullptr, nullptr, this);
    ToolRegistry registry(&tabs, this);

    tabs.addTab(QUrl("moch://newtab"));
    QElapsedTimer clock;
    clock.start();

    ToolResult result;
    bool settled = false;
    registry.execute("navigate_to_url", {{"url", "http://10.255.255.1/"}},
                     [&result, &settled](ToolResult r) {
                         result = r;
                         settled = true;
                     });

    QElapsedTimer wait;
    wait.start();
    while (!settled && wait.elapsed() < 25000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QVERIFY2(settled, "navigate_to_url never settled");
    QVERIFY2(result.errorCode == "timeout" || result.errorCode == "load_failed",
             qPrintable(QString("unexpected outcome: %1").arg(result.errorCode)));

    while (clock.elapsed() < 21000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
}

void tst_navtool::findElementsReturnsMatches()
{
    qputenv("XDG_CONFIG_HOME", (m_home.path() + "/config").toUtf8());
    qputenv("XDG_DATA_HOME", (m_home.path() + "/data").toUtf8());
    Settings settings;
    TabManager tabs(&settings, nullptr, nullptr, nullptr, this);
    ToolRegistry registry(&tabs, this);

    const int index = tabs.addTab(QUrl("data:text/html,<html><body>"
                                       "<input type='text' placeholder='Search'>"
                                       "<input type='text' id='q'>"
                                       "<a href='https://example.com/p/1'>item 1</a>"
                                       "<a href='https://example.com/p/2'>item 2</a>"
                                       "</body></html>"));
    QVERIFY(waitForLoad(tabs.view(index)));

    // selectors that previously crashed JSON.parse with 'is not valid JSON'
    for (const char *selector : {"input[type=text]", "a[href*='/p/']"}) {
        ToolResult result;
        bool settled = false;
        registry.execute("find_elements", {{"selector", QString::fromLatin1(selector)}},
                         [&result, &settled](ToolResult r) {
                             result = r;
                             settled = true;
                         });
        QElapsedTimer wait;
        wait.start();
        while (!settled && wait.elapsed() < 8000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QVERIFY2(settled, qPrintable(QString("no result for %1").arg(selector)));
        QVERIFY2(result.ok, qPrintable(result.errorMessage));
        const int total = result.data.value("total").toInt();
        QVERIFY2(total >= 2, qPrintable(QString("expected matches for %1, got %2")
                                             .arg(selector)
                                             .arg(total)));
        const QJsonObject first = result.data.value("elements").toArray().at(0).toObject();
        QVERIFY(first.contains("tag"));
    }
}

QTEST_MAIN(tst_navtool)
#include "tst_navtool.moc"
