#include "app/applicationcontroller.hpp"

#include <QSqlDatabase>
#include <QStandardPaths>
#include <QWebEngineProfile>

#include "browser/bookmarkstore.hpp"
#include "browser/downloadmanager.hpp"
#include "browser/historystore.hpp"
#include "browser/mochurlscheme.hpp"
#include "browser/tabmanager.hpp"
#include "storage/database.hpp"
#include "storage/settings.hpp"
#include "ui/mainwindow.hpp"

ApplicationController::ApplicationController(QObject *parent)
    : QObject(parent)
{
}

ApplicationController::~ApplicationController() = default;

void ApplicationController::start()
{
    m_settings = new Settings(this);
    m_history = new HistoryStore(this);
    m_bookmarks = new BookmarkStore(this);
    m_downloads = new DownloadManager(this);

    Database::connection();

    // QWebEngineProfile::defaultProfile() is off-the-record with the Qt in
    // use here (verified with 6.11: isOffTheRecord() == true,
    // NoPersistentCookies, MemoryHttpCache) and cannot be switched back,
    // which dropped every cookie on exit. A named profile persists under
    // the app data dir.
    m_profile = new QWebEngineProfile(QStringLiteral("moch"), this);
    m_profile->setPersistentStoragePath(
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/QtWebEngine/moch"));
    m_profile->setPersistentCookiesPolicy(QWebEngineProfile::AllowPersistentCookies);
    m_profile->setHttpCacheType(QWebEngineProfile::DiskHttpCache);
    if (m_profile->isOffTheRecord())
        qWarning("moch: web profile is off-the-record; logins will not persist");
    m_profile->installUrlSchemeHandler("moch", new MochUrlScheme(m_profile));
    connect(m_profile, &QWebEngineProfile::downloadRequested, m_downloads,
            &DownloadManager::handleDownload);

    openWindow();
}

void ApplicationController::openWindow()
{
    TabManager *tabs = new TabManager(m_settings, m_profile, m_history, m_bookmarks, this);
    MainWindow *window = new MainWindow(m_settings, tabs, m_history, m_bookmarks, m_downloads);
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->show();
    const bool firstWindow = m_window == nullptr;
    const bool setupDone = m_settings->get("setup_complete", false).toBool();

    // The first window owns the saved tab session; extra windows never
    // overwrite the file.
    if (firstWindow)
        tabs->setSessionTrackingEnabled(true);
    if (firstWindow && setupDone)
        tabs->restoreSavedSession();
    if (tabs->count() == 0)
        tabs->addTab(setupDone ? QUrl("moch://newtab") : QUrl("moch://setup"));

    connect(window, &MainWindow::newWindowRequested, this, &ApplicationController::openWindow);
    if (firstWindow) {
        m_tabs = tabs;
        m_window = window;
    }
}
