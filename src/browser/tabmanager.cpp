#include "browser/tabmanager.hpp"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTimer>
#include <QWebChannel>
#include <QWebEngineHistory>
#include <QWebEnginePage>
#include <QWebEngineProfile>

#include "browser/bookmarkstore.hpp"
#include "browser/historystore.hpp"
#include "browser/newtabbridge.hpp"
#include "browser/setupbridge.hpp"
#include "browser/urlresolver.hpp"
#include "browser/webview.hpp"
#include "storage/settings.hpp"

namespace {
// Debounce window for session writes; restarts on every tab change.
constexpr int kSessionSaveDelayMs = 1200;

// True for urls that must never come back from a saved session (the
// first-run wizard) or that carry no state (blank views).
bool restorableUrl(const QUrl &url)
{
    if (url.isEmpty())
        return false;
    return !(url.scheme() == QLatin1String("moch")
             && url.host() == QLatin1String("setup"));
}
} // namespace

TabManager::TabManager(Settings *settings, QWebEngineProfile *profile, HistoryStore *history,
                       BookmarkStore *bookmarks, QObject *parent)
    : QObject(parent), m_settings(settings), m_profile(profile), m_history(history),
      m_bookmarks(bookmarks)
{
    m_stack = new QStackedWidget();
    connect(m_stack, &QObject::destroyed, this, [this] {
        m_views.clear();
        m_closedUrls.clear();
        m_current = -1;
    });

    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(kSessionSaveDelayMs);
    connect(m_saveTimer, &QTimer::timeout, this, &TabManager::saveSessionNow);
    connect(this, &TabManager::tabsChanged, this, &TabManager::scheduleSessionSave);
    connect(this, &TabManager::tabUrlChanged, this, &TabManager::scheduleSessionSave);
}

TabManager::~TabManager()
{
    // Flush a pending debounced save so quitting right after a change does
    // not lose the last tab state.
    if (m_saveTimer && m_saveTimer->isActive())
        saveSessionNow();
    if (m_stack && !m_stack->parent())
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
    info.loading = v->isLoading();
    info.loadProgress = v->loadProgress();
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
        view->setLoadProgressValue(progress);
        emit tabLoadProgress(view->tabIndex(), progress);
    });
    connect(view, &QWebEngineView::loadStarted, this, [view] { view->setLoadingState(true); });
    connect(view, &QWebEngineView::loadFinished, this, [this, view](bool ok) {
        view->setLoadingState(false);
        emit tabLoadFinished(view->tabIndex(), view->url(), ok);
        if (!ok)
            qWarning("moch: tab %d failed to load", view->tabIndex());
    });

    if (m_history && m_bookmarks) {
        auto *channel = new QWebChannel(view);
        auto *bridge = new NewTabBridge(m_history, m_bookmarks, m_settings, channel);
        connect(bridge, &NewTabBridge::searchRequestedByUser, this,
                &TabManager::searchRequested);
        connect(bridge, &NewTabBridge::openUrlRequested, this, [this](const QString &url) {
            emit openUrlRequested(QUrl(url));
        });
        channel->registerObject("mochBridge", bridge);
        auto *setup = new SetupBridge(m_settings, view, channel);
        setup->setStores(m_history, m_bookmarks);
        connect(setup, &SetupBridge::finished, view, [view] {
            view->load(QUrl("moch://newtab"));
        });
        channel->registerObject("mochSetup", setup);
        view->page()->setWebChannel(channel);
    }

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

QString TabManager::sessionFilePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/tabsession.json");
}

QList<SessionTab> TabManager::loadSavedSession(int *activeIndex)
{
    if (activeIndex)
        *activeIndex = 0;
    QFile file(sessionFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonArray tabs = root.value("tabs").toArray();
    QList<SessionTab> out;
    for (const QJsonValue &value : tabs) {
        const QJsonObject entry = value.toObject();
        const QUrl url(entry.value("url").toString());
        if (!restorableUrl(url))
            continue;
        SessionTab tab;
        tab.url = url;
        tab.title = entry.value("title").toString();
        out.append(tab);
    }
    if (activeIndex)
        *activeIndex = qMax(0, root.value("active").toInt());
    return out;
}

void TabManager::saveSessionNow()
{
    if (m_saveTimer)
        m_saveTimer->stop();
    if (!m_trackSession || !m_stack)
        return;

    QJsonArray tabs;
    const QUrl activeUrl = currentView() ? currentView()->url() : QUrl();
    int active = 0;
    for (int i = 0; i < m_views.size(); ++i) {
        QUrl url = m_views.at(i)->url();
        if (!restorableUrl(url))
            continue;
        if (url.isEmpty())
            url = QUrl("moch://newtab");
        QJsonObject entry;
        entry.insert("url", url.toString());
        entry.insert("title", m_views.at(i)->title());
        tabs.append(entry);
    }
    // The active tab's saved position: first entry matching its url, else 0.
    if (!activeUrl.isEmpty()) {
        for (int i = 0; i < tabs.size(); ++i) {
            if (QUrl(tabs.at(i).toObject().value("url").toString()) == activeUrl) {
                active = i;
                break;
            }
        }
    }

    QJsonObject root;
    root.insert("active", active);
    root.insert("tabs", tabs);

    const QString path = sessionFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning("moch: cannot write tab session %s", qPrintable(path));
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    // QSaveFile writes to a temp file and renames on commit, so a crash
    // mid-write never corrupts the previous session.
    if (!file.commit())
        qWarning("moch: cannot commit tab session %s", qPrintable(path));
}

void TabManager::restoreSavedSession()
{
    if (!m_stack || !m_trackSession)
        return;
    int active = 0;
    const QList<SessionTab> saved = loadSavedSession(&active);
    if (saved.isEmpty())
        return;
    for (const SessionTab &tab : saved)
        addTab(tab.url, true);
    if (m_views.isEmpty())
        return;
    setCurrentIndex(qBound(0, active, m_views.size() - 1));
}

void TabManager::setSessionTrackingEnabled(bool enabled)
{
    m_trackSession = enabled;
}

void TabManager::scheduleSessionSave()
{
    if (m_trackSession && m_stack)
        m_saveTimer->start();
}
