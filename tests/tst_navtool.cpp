#include <QtWidgets>
#include <QtTest>

#include "browser/tabmanager.hpp"
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

private:
    QTemporaryDir m_home;
};

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

QTEST_MAIN(tst_navtool)
#include "tst_navtool.moc"
