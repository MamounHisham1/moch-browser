#include "browser/tabmanager.hpp"

#include <QStackedWidget>
#include <QTimer>
#include <QWebEngineHistory>
#include <QWebEnginePage>
#include <QWebEngineProfile>

#include "browser/urlresolver.hpp"
#include "browser/webview.hpp"
#include "storage/settings.hpp"

TabManager::TabManager(Settings *settings, QWebEngineProfile *profile, QObject *parent)
    : QObject(parent), m_settings(settings), m_profile(profile)
{
    m_stack = new QStackedWidget();
}

TabManager::~TabManager()
{
    delete m_stack;
}

QWidget *TabManager::widget() const
{
    return m_stack;
}

int TabManager::count() const
{
    return m_views.size();
}

int TabManager::currentIndex() const
{
    return m_current;
}

WebView *TabManager::view(int index) const
{
    return (index >= 0 && index < m_views.size()) ? m_views.at(index) : nullptr;
}

WebView *TabManager::currentView() const
{
    return view(m_current);
}

TabInfo TabManager::info(int index) const
{
    TabInfo info;
    const WebView *v = view(index);
    if (!v)
        return info;
    info.index = index;
    info.title = v->title().isEmpty() ? tr("New Tab") : v->title();
    info.url = v->url();
    info.icon = v->icon();
    info.loadProgress = 100;
    return info;
}

int TabManager::addTab(const QUrl &url, bool background)
{
    WebView *view = new WebView(m_stack);
    if (m_profile)
        view->setPage(new QWebEnginePage(m_profile, view));
    view->setTabIndex(m_views.size());
    m_stack->addWidget(view);
    m_views.append(view);

    connect(view, &WebView::openInNewTabRequested, this, [this](const QUrl &url) {
        addTab(url);
    });
    connect(view, &QWebEngineView::titleChanged, this, [this, view](const QString &title) {
        emit tabTitleChanged(view->tabIndex(), title);
        emit tabsChanged();
    });
    connect(view, &QWebEngineView::urlChanged, this, [this, view](const QUrl &url) {
        emit tabUrlChanged(view->tabIndex(), url);
    });
    connect(view, &QWebEngineView::iconChanged, this, [this, view](const QIcon &icon) {
        emit tabIconChanged(view->tabIndex(), icon);
        emit tabsChanged();
    });
    connect(view, &QWebEngineView::loadProgress, this, [this, view](int progress) {
        emit tabLoadProgress(view->tabIndex(), progress);
    });

    view->load(url);
    if (!background)
        setCurrentIndex(m_views.size() - 1);
    else
        emit tabsChanged();
    return m_views.size() - 1;
}

void TabManager::closeTab(int index)
{
    WebView *v = view(index);
    if (!v)
        return;
    if (!v->url().isEmpty() && v->url().scheme() != "moch")
        m_closedUrls.prepend(v->url());
    m_stack->removeWidget(v);
    m_views.removeAll(v);
    v->deleteLater();
    reindex();

    if (m_views.isEmpty()) {
        m_current = -1;
        emit tabsChanged();
        emit lastTabClosed();
        return;
    }
    if (m_current >= m_views.size())
        m_current = m_views.size() - 1;
    setCurrentIndex(m_current);
    emit tabsChanged();
}

void TabManager::setCurrentIndex(int index)
{
    if (index < 0 || index >= m_views.size() || index == m_current)
        return;
    m_current = index;
    m_stack->setCurrentIndex(index);
    emit currentChanged(index);
    emit tabsChanged();
}

void TabManager::navigateCurrent(const QString &userInput)
{
    WebView *v = currentView();
    if (!v)
        return;
    v->load(UrlResolver::fromUserInput(userInput, m_settings->searchEngine()));
}

void TabManager::reopenClosedTab()
{
    if (m_closedUrls.isEmpty())
        return;
    const QUrl url = m_closedUrls.takeFirst();
    addTab(url);
}

bool TabManager::canGoBack() const
{
    const WebView *v = currentView();
    return v && v->history()->canGoBack();
}

bool TabManager::canGoForward() const
{
    const WebView *v = currentView();
    return v && v->history()->canGoForward();
}

void TabManager::reindex()
{
    for (int i = 0; i < m_views.size(); ++i)
        m_views.at(i)->setTabIndex(i);
    if (m_current >= m_views.size())
        m_current = m_views.size() - 1;
}
