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

    m_tabs = new TabManager(m_settings, profile, this);
    m_window = new MainWindow(m_settings, m_tabs);
    m_window->show();

    if (m_settings->get("startup_behavior", "new_tab").toString() == "new_tab")
        m_tabs->addTab(QUrl("moch://newtab"));
    else
        m_tabs->addTab(QUrl("moch://newtab"));
}
