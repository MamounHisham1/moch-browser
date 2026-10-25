#include "app/applicationcontroller.hpp"

#include <QSqlDatabase>
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

    QWebEngineProfile *profile = QWebEngineProfile::defaultProfile();
    profile->installUrlSchemeHandler("moch", new MochUrlScheme(profile));
    connect(profile, &QWebEngineProfile::downloadRequested, m_downloads,
            &DownloadManager::handleDownload);

    openWindow();
}

void ApplicationController::openWindow()
{
    QWebEngineProfile *profile = QWebEngineProfile::defaultProfile();
    TabManager *tabs = new TabManager(m_settings, profile, m_history, m_bookmarks, this);
    MainWindow *window = new MainWindow(m_settings, tabs, m_history, m_bookmarks, m_downloads);
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->show();
    tabs->addTab(QUrl("moch://newtab"));
    connect(window, &MainWindow::newWindowRequested, this, &ApplicationController::openWindow);

    if (!m_window) {
        m_tabs = tabs;
        m_window = window;
    }
}
