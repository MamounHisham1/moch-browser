#include "app/applicationcontroller.hpp"

#include <QWebEngineProfile>

#include "browser/mochurlscheme.hpp"
#include "browser/tabmanager.hpp"
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

    QWebEngineProfile *profile = QWebEngineProfile::defaultProfile();
    profile->installUrlSchemeHandler("moch", new MochUrlScheme(profile));

    openWindow();
}

void ApplicationController::openWindow()
{
    QWebEngineProfile *profile = QWebEngineProfile::defaultProfile();
    TabManager *tabs = new TabManager(m_settings, profile, this);
    MainWindow *window = new MainWindow(m_settings, tabs);
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->show();
    tabs->addTab(QUrl("moch://newtab"));
    connect(window, &MainWindow::newWindowRequested, this, &ApplicationController::openWindow);

    if (!m_window) {
        m_tabs = tabs;
        m_window = window;
    }
}
