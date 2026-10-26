#include "ui/mainwindow.hpp"

#include <QDialog>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QShortcut>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWebEngineFindTextResult>
#include <QWebEngineHistory>
#include <QWebEnginePage>

#include "browser/bookmarkstore.hpp"
#include "browser/downloadmanager.hpp"
#include "browser/historystore.hpp"
#include "browser/tabmanager.hpp"
#include "browser/urlresolver.hpp"
#include "browser/webview.hpp"
#include "storage/settings.hpp"
#include "tools/toolregistry.hpp"
#include "ui/aisidebar.hpp"
#include "ui/downloadspanel.hpp"
#include "ui/icons.hpp"
#include "ui/settingsdialog.hpp"
#include "ui/tabbar.hpp"
#include "ui/toolbar.hpp"

MainWindow::MainWindow(Settings *settings, TabManager *tabs, HistoryStore *history,
                       BookmarkStore *bookmarks, DownloadManager *downloads, QWidget *parent)
    : QMainWindow(parent), m_settings(settings), m_tabs(tabs), m_history(history),
      m_bookmarks(bookmarks), m_downloads(downloads)
{
    setWindowTitle("Moch");
    resize(1280, 800);

    m_tabBar = new TabBar(this);
    m_toolbar = new Toolbar(this);
    m_findBar = buildFindBar();
    m_findBar->hide();
    m_downloadsPanel = new DownloadsPanel(m_downloads, this);
    m_downloadsPanel->hide();

    m_toolRegistry = new ToolRegistry(m_tabs, this);
    m_aiSidebar = new AiSidebar(m_settings, m_tabs, m_toolRegistry, this);
    connect(m_toolbar, &Toolbar::aiToggleRequested, this, [this] {
        m_aiSidebar->setExpanded(!m_aiSidebar->isExpanded());
    });
    connect(m_aiSidebar, &AiSidebar::expandedChanged, m_toolbar,
            &Toolbar::setAiExpanded);

    auto *container = new QWidget(this);
    auto *layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto *mainColumn = new QWidget(container);
    auto *columnLayout = new QVBoxLayout(mainColumn);
    columnLayout->setContentsMargins(0, 0, 0, 0);
    columnLayout->setSpacing(0);
    columnLayout->addWidget(m_tabBar);
    columnLayout->addWidget(m_toolbar);
    columnLayout->addWidget(m_findBar);
    columnLayout->addWidget(m_tabs->widget());
    layout->addWidget(mainColumn, 1);
    layout->addWidget(m_aiSidebar);
    setCentralWidget(container);

    connectTabs();
    buildMenuButton();
    buildStoreMenus();
    buildShortcuts();
    applyTheme();
}

QWidget *MainWindow::buildFindBar()
{
    m_findInput = new QLineEdit(this);
    m_findInput->setObjectName("findInput");
    m_findInput->setPlaceholderText(tr("Find in page"));
    m_findInput->setClearButtonEnabled(true);
    connect(m_findInput, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (auto *page = m_tabs->currentView() ? m_tabs->currentView()->page() : nullptr)
            page->findText(text);
    });
    connect(m_findInput, &QLineEdit::returnPressed, this, [this] { findNext(true); });

    m_findStatus = new QLabel(this);

    auto *previous = new QToolButton(this);
    previous->setObjectName("iconButton");
    previous->setProperty("iconName", "chevron-up");
    previous->setToolTip(tr("Previous match"));
    previous->setAutoRaise(true);
    connect(previous, &QToolButton::clicked, this, [this] { findNext(false); });

    auto *next = new QToolButton(this);
    next->setObjectName("iconButton");
    next->setProperty("iconName", "chevron-down");
    next->setToolTip(tr("Next match"));
    next->setAutoRaise(true);
    connect(next, &QToolButton::clicked, this, [this] { findNext(true); });

    auto *close = new QToolButton(this);
    close->setObjectName("iconButton");
    close->setProperty("iconName", "close");
    close->setToolTip(tr("Close find bar (Esc)"));
    close->setAutoRaise(true);
    connect(close, &QToolButton::clicked, this, &MainWindow::toggleFindBar);

    auto *bar = new QWidget(this);
    bar->setObjectName("findBar");
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(8, 2, 8, 2);
    layout->setSpacing(4);
    layout->addWidget(m_findInput, 1);
    layout->addWidget(m_findStatus);
    layout->addWidget(previous);
    layout->addWidget(next);
    layout->addWidget(close);
    return bar;
}

void MainWindow::connectTabs()
{
    connect(m_tabBar, &TabBar::currentTabChanged, this, [this](int index) {
        m_tabs->setCurrentIndex(index);
        updateNavigationState();
    });
    connect(m_tabBar, &TabBar::tabCloseRequested, this, [this](int index) {
        m_tabs->closeTab(index);
    });
    connect(m_tabBar, &TabBar::newTabRequested, this,
            [this] { m_tabs->addTab(QUrl("moch://newtab")); });
    connect(m_tabBar, &TabBar::tabDuplicateRequested, this, [this](int index) {
        if (const WebView *view = m_tabs->view(index))
            m_tabs->addTab(view->url());
    });
    connect(m_tabBar, &TabBar::tabsCloseOthersRequested, this, [this](int index) {
        for (int i = m_tabs->count() - 1; i >= 0; --i)
            if (i != index)
                m_tabs->closeTab(i);
    });
    connect(m_tabBar, &TabBar::tabsCloseRightRequested, this, [this](int index) {
        for (int i = m_tabs->count() - 1; i > index; --i)
            m_tabs->closeTab(i);
    });

    connect(m_toolbar, &Toolbar::backRequested, this, [this] {
        if (auto *view = m_tabs->currentView())
            view->back();
    });
    connect(m_toolbar, &Toolbar::forwardRequested, this, [this] {
        if (auto *view = m_tabs->currentView())
            view->forward();
    });
    connect(m_toolbar, &Toolbar::reloadRequested, this, [this] {
        if (auto *view = m_tabs->currentView())
            view->reload();
    });
    connect(m_toolbar, &Toolbar::stopRequested, this, [this] {
        if (auto *view = m_tabs->currentView())
            view->stop();
    });
    connect(m_toolbar, &Toolbar::navigateRequested, this,
            [this](const QString &input) { m_tabs->navigateCurrent(input); });
    connect(m_toolbar, &Toolbar::bookmarkToggleRequested, this, [this] {
        if (auto *view = m_tabs->currentView())
            emit bookmarkToggleRequested(view->url());
    });

    connect(m_tabs, &TabManager::tabsChanged, this, [this] {
        m_tabBar->removeLastTabs(m_tabs->count());
        for (int i = 0; i < m_tabs->count(); ++i) {
            const TabInfo info = m_tabs->info(i);
            const QString label = info.title.isEmpty() ? tr("New Tab") : info.title;
            if (i < m_tabBar->count())
                m_tabBar->setTab(i, label, info.icon);
            else
                m_tabBar->appendTab(label);
        }
        m_tabBar->setCurrentIndex(m_tabs->currentIndex());
        updateNavigationState();
    });
    connect(m_tabs, &TabManager::currentChanged, this, [this](int) {
        updateNavigationState();
        updateBookmarkStar();
    });
    connect(m_tabs, &TabManager::tabUrlChanged, this, [this](int, const QUrl &) {
        updateNavigationState();
        updateBookmarkStar();
    });
    connect(m_tabs, &TabManager::tabLoadProgress, this, [this](int index, int progress) {
        if (index == m_tabs->currentIndex())
            m_toolbar->setLoading(progress < 100);
    });
    connect(m_tabs, &TabManager::tabLoadFinished, this,
            &MainWindow::recordHistory);
    connect(m_tabs, &TabManager::searchRequested, this, [this](const QString &query) {
        if (query.startsWith("/ask"))
            m_toolbar->addressBar()->setFocus();
        else
            m_tabs->navigateCurrent(query);
    });
    connect(m_tabs, &TabManager::openUrlRequested, this, [this](const QUrl &url) {
        m_tabs->currentView()->load(url);
    });
    connect(m_tabs, &TabManager::lastTabClosed, this,
            [this] { m_tabs->addTab(QUrl("moch://newtab")); });
    connect(this, &MainWindow::bookmarkToggleRequested, this, [this](const QUrl &url) {
        if (url.isEmpty() || url.scheme() == "moch")
            return;
        if (m_bookmarks->contains(url.toString()))
            m_bookmarks->remove(url.toString());
        else
            m_bookmarks->add(url.toString(), m_tabs->currentView()->title());
        updateBookmarkStar();
    });
    connect(m_toolbar, &Toolbar::downloadsPanelRequested, this, [this] {
        if (m_downloadsPanel->isVisible())
            m_downloadsPanel->hide();
        else {
            m_downloadsPanel->move(mapToGlobal(QPoint(width() - 440, 96)));
            m_downloadsPanel->show();
        }
    });
    connect(m_downloads, &DownloadManager::activeCountChanged, this, [this](int count) {
        m_toolbar->setDownloadBadge(count, -1);
    });
    connect(m_bookmarks, &BookmarkStore::changed, this, &MainWindow::updateBookmarkStar);
}

void MainWindow::recordHistory(int index, const QUrl &url, bool ok)
{
    if (!ok || url.scheme() == "moch" || !m_history)
        return;
    if (const WebView *view = m_tabs->view(index); view && view == m_tabs->currentView())
        m_history->recordVisit(url.toString(), view->title());
}

void MainWindow::updateBookmarkStar()
{
    const WebView *view = m_tabs->currentView();
    const bool bookmarked =
        view && m_bookmarks && m_bookmarks->contains(view->url().toString());
    m_toolbar->setBookmarked(bookmarked);
}

void MainWindow::buildStoreMenus()
{
    if (m_bookmarks) {
        auto *bookmarksMenu = new QMenu(this);
        const auto rebuildBookmarks = [this, bookmarksMenu] {
            bookmarksMenu->clear();
            const QList<Bookmark> all = m_bookmarks->all();
            if (all.isEmpty())
                bookmarksMenu->addAction(tr("(no bookmarks)"))->setEnabled(false);
            for (const Bookmark &bookmark : all) {
                QAction *action = bookmarksMenu->addAction(
                    bookmark.title.isEmpty() ? bookmark.url : bookmark.title);
                connect(action, &QAction::triggered, this, [this, bookmark] {
                    if (auto *view = m_tabs->currentView())
                        view->load(QUrl(bookmark.url));
                });
            }
        };
        connect(m_bookmarks, &BookmarkStore::changed, bookmarksMenu, rebuildBookmarks);
        rebuildBookmarks();
        m_toolbar->setBookmarksMenu(bookmarksMenu);
    }
    if (m_history) {
        auto *showHistory = new QShortcut(QKeySequence("Ctrl+H"), this);
        connect(showHistory, &QShortcut::activated, this, &MainWindow::openHistory);
    }
    auto *showSettings = new QShortcut(QKeySequence("Ctrl+,"), this);
    connect(showSettings, &QShortcut::activated, this, &MainWindow::openSettings);
}

void MainWindow::openHistory()
{
    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("History"));
    dialog->resize(560, 480);
    auto *list = new QListWidget(dialog);
    for (const HistoryEntry &entry : m_history->visits(500, 0)) {
        QListWidgetItem *row = new QListWidgetItem(
            tr("%1  —  %2").arg(entry.title.isEmpty() ? entry.url : entry.title, entry.url),
            list);
        row->setData(Qt::UserRole, entry.url);
    }
    connect(list, &QListWidget::itemActivated, this, [this, dialog](QListWidgetItem *row) {
        if (auto *view = m_tabs->currentView())
            view->load(QUrl(row->data(Qt::UserRole).toString()));
        dialog->accept();
    });
    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(list);
    dialog->show();
}

void MainWindow::openSettings()
{
    auto *dialog = new SettingsDialog(m_settings, this);
    connect(dialog, &SettingsDialog::themeChanged, this, &MainWindow::applyTheme);
    dialog->exec();
}

void MainWindow::buildMenuButton()
{
    m_mainMenu = new QMenu(this);
    QAction *newWindow = m_mainMenu->addAction(tr("New Window") + "\tCtrl+N");
    newWindow->setProperty("iconName", "external");
    connect(newWindow, &QAction::triggered, this, &MainWindow::newWindowRequested);
    QAction *history = m_mainMenu->addAction(tr("History") + "\tCtrl+H");
    history->setProperty("iconName", "clock");
    connect(history, &QAction::triggered, this, &MainWindow::openHistory);
    QAction *runSetup = m_mainMenu->addAction(tr("Personalize Moch…"));
    runSetup->setProperty("iconName", "sparkles");
    connect(runSetup, &QAction::triggered, this, [this] {
        if (auto *view = m_tabs->currentView())
            view->load(QUrl("moch://setup"));
    });
    QAction *askAi = m_mainMenu->addAction(tr("AI Sidebar") + "\tCtrl+I");
    askAi->setProperty("iconName", "panel-right");
    connect(askAi, &QAction::triggered, this, [this] {
        m_aiSidebar->setExpanded(!m_aiSidebar->isExpanded());
    });
    m_mainMenu->addSeparator();
    QAction *settings = m_mainMenu->addAction(tr("Settings") + "\tCtrl+,");
    settings->setProperty("iconName", "settings");
    connect(settings, &QAction::triggered, this, &MainWindow::openSettings);
    m_toolbar->setSettingsMenu(m_mainMenu);
}

void MainWindow::buildShortcuts()
{
    const auto shortcut = [this](const QKeySequence &key, auto &&handler) {
        auto *action = new QShortcut(key, this);
        connect(action, &QShortcut::activated, this, std::forward<decltype(handler)>(handler));
    };

    shortcut(QKeySequence("Ctrl+T"), [this] { m_tabs->addTab(QUrl("moch://newtab")); });
    shortcut(QKeySequence("Ctrl+W"), [this] { m_tabs->closeTab(m_tabs->currentIndex()); });
    shortcut(QKeySequence("Ctrl+L"), [this] {
        m_toolbar->addressBar()->setFocus();
        m_toolbar->addressBar()->selectAll();
    });
    shortcut(QKeySequence("Ctrl+R"), [this] {
        if (auto *view = m_tabs->currentView())
            view->reload();
    });
    shortcut(QKeySequence("F5"), [this] {
        if (auto *view = m_tabs->currentView())
            view->reload();
    });
    shortcut(QKeySequence("Ctrl+Shift+T"), [this] { m_tabs->reopenClosedTab(); });
    shortcut(QKeySequence("Ctrl+Tab"), [this] {
        if (m_tabs->count() > 0)
            m_tabs->setCurrentIndex((m_tabs->currentIndex() + 1) % m_tabs->count());
    });
    shortcut(QKeySequence("Ctrl+N"), [this] { emit newWindowRequested(); });
    shortcut(QKeySequence("Ctrl+D"), [this] {
        if (auto *view = m_tabs->currentView())
            emit bookmarkToggleRequested(view->url());
    });
    shortcut(QKeySequence("Ctrl+F"), [this] { toggleFindBar(); });
    shortcut(QKeySequence("Alt+Left"), [this] {
        if (auto *view = m_tabs->currentView())
            view->back();
    });
    shortcut(QKeySequence("Alt+Right"), [this] {
        if (auto *view = m_tabs->currentView())
            view->forward();
    });
    shortcut(QKeySequence("F11"), [this] {
        isFullScreen() ? showNormal() : showFullScreen();
    });
    shortcut(QKeySequence("Ctrl+I"), [this] {
        m_aiSidebar->setExpanded(!m_aiSidebar->isExpanded());
    });
    shortcut(QKeySequence(Qt::Key_Escape), [this] {
        if (m_findBar->isVisible())
            toggleFindBar();
        else if (auto *view = m_tabs->currentView())
            view->stop();
    });
}

void MainWindow::updateNavigationState()
{
    const WebView *view = m_tabs->currentView();
    m_toolbar->setNavigationEnabled(m_tabs->canGoBack(), m_tabs->canGoForward());
    if (view) {
        m_toolbar->setAddress(view->url().toString());
        m_toolbar->setSecurity(view->url());
        m_toolbar->setLoading(view->isLoading());
    }
    updateBookmarkStar();
}

void MainWindow::toggleFindBar()
{
    if (m_findBar->isVisible()) {
        m_findBar->hide();
        m_findStatus->clear();
        if (auto *page = m_tabs->currentView() ? m_tabs->currentView()->page() : nullptr)
            page->findText(QString());
        if (auto *view = m_tabs->currentView())
            view->setFocus();
    } else {
        m_findBar->show();
        m_findInput->setFocus();
        m_findInput->selectAll();
    }
}

void MainWindow::findNext(bool forward)
{
    if (auto *page = m_tabs->currentView() ? m_tabs->currentView()->page() : nullptr) {
        const QWebEnginePage::FindFlags flags =
                forward ? QWebEnginePage::FindFlags() : QWebEnginePage::FindBackward;
        page->findText(m_findInput->text(), flags, [this](const QWebEngineFindTextResult &result) {
            m_findStatus->setText(result.numberOfMatches() > 0 ? QString() : tr("No matches"));
        });
    }
}

void MainWindow::applyTheme()
{
    const QString theme = m_settings->theme();
    QFile qss(":/themes/" + theme + ".qss");
    if (qss.open(QIODevice::ReadOnly))
        setStyleSheet(QString::fromUtf8(qss.readAll()));
    else
        qWarning("moch: theme %s not found", qPrintable(theme));
    m_toolbar->applyTheme(theme);
    m_tabBar->applyTheme(theme);
    m_aiSidebar->applyTheme(theme);
    applyMenuIcons();
}

void MainWindow::applyMenuIcons()
{
    const ThemeColors colors = ThemeColors::forTheme(m_settings->theme());
    const auto menuIcons = [colors](QMenu *menu) {
        if (!menu)
            return;
        for (QAction *action : menu->actions()) {
            const QString name = action->property("iconName").toString();
            if (!name.isEmpty())
                action->setIcon(Icons::get(name, colors.icon, 16));
        }
    };
    menuIcons(m_mainMenu);
}
