#include "ui/mainwindow.hpp"

#include <QAction>
#include <QFile>
#include <QKeyEvent>
#include <QLineEdit>
#include <QTabBar>
#include <QToolBar>
#include <QToolButton>
#include <QWebEngineHistory>

#include "browser/tabmanager.hpp"
#include "browser/webview.hpp"
#include "storage/settings.hpp"

MainWindow::MainWindow(Settings *settings, TabManager *tabs, QWidget *parent)
    : QMainWindow(parent), m_settings(settings), m_tabs(tabs)
{
    setWindowTitle("Moch");
    resize(1280, 800);

    buildTabBar();
    buildToolbar();

    setCentralWidget(m_tabs->widget());
    connectTabs();
    applyTheme();
}

void MainWindow::buildTabBar()
{
    m_tabBar = new QTabBar();
    m_tabBar->setTabsClosable(true);
    m_tabBar->setMovable(true);
    m_tabBar->setExpanding(false);
    m_tabBar->setUsesScrollButtons(true);
    m_tabBar->setElideMode(Qt::ElideRight);

    connect(m_tabBar, &QTabBar::currentChanged, this, [this](int index) {
        m_tabs->setCurrentIndex(index);
        updateNavigationState();
    });
    connect(m_tabBar, &QTabBar::tabCloseRequested, this, [this](int index) {
        m_tabs->closeTab(index);
    });
}

void MainWindow::buildToolbar()
{
    QToolBar *toolbar = new QToolBar();
    toolbar->setMovable(false);
    toolbar->setObjectName("navToolbar");

    m_backAction = toolbar->addAction("←");
    m_forwardAction = toolbar->addAction("→");
    m_reloadAction = toolbar->addAction("⟳");
    connect(m_backAction, &QAction::triggered, this, [this] {
        if (auto *v = m_tabs->currentView())
            v->back();
    });
    connect(m_forwardAction, &QAction::triggered, this, [this] {
        if (auto *v = m_tabs->currentView())
            v->forward();
    });
    connect(m_reloadAction, &QAction::triggered, this, [this] {
        if (auto *v = m_tabs->currentView())
            v->reload();
    });

    m_addressBar = new QLineEdit();
    m_addressBar->setPlaceholderText("Search or enter address");
    m_addressBar->setClearButtonEnabled(true);
    m_addressBar->setObjectName("addressBar");
    connect(m_addressBar, &QLineEdit::returnPressed, this, [this] {
        m_tabs->navigateCurrent(m_addressBar->text());
        m_addressBar->clearFocus();
    });
    toolbar->addWidget(m_addressBar);

    QToolButton *newTabButton = new QToolButton();
    newTabButton->setText("+");
    connect(newTabButton, &QToolButton::clicked, this, [this] {
        m_tabs->addTab(QUrl("moch://newtab"));
    });
    toolbar->addWidget(newTabButton);

    addToolBar(toolbar);
}

void MainWindow::connectTabs()
{
    connect(m_tabs, &TabManager::tabsChanged, this, [this] {
        m_tabBar->blockSignals(true);
        while (m_tabBar->count() > m_tabs->count())
            m_tabBar->removeTab(m_tabBar->count() - 1);
        for (int i = 0; i < m_tabs->count(); ++i) {
            const TabInfo info = m_tabs->info(i);
            const QString label = info.title.isEmpty() ? tr("New Tab") : info.title;
            if (i < m_tabBar->count())
                m_tabBar->setTabText(i, label);
            else
                m_tabBar->addTab(label);
            if (!info.icon.isNull())
                m_tabBar->setTabIcon(i, info.icon);
        }
        m_tabBar->setCurrentIndex(m_tabs->currentIndex());
        m_tabBar->blockSignals(false);
    });
    connect(m_tabs, &TabManager::currentChanged, this, [this](int index) {
        m_tabBar->setCurrentIndex(index);
        updateNavigationState();
    });
    connect(m_tabs, &TabManager::tabUrlChanged, this, [this](int, const QUrl &url) {
        if (m_tabs->currentView() && m_tabs->currentView()->url() == url)
            m_addressBar->setText(url.toString());
        updateNavigationState();
    });
    connect(m_tabs, &TabManager::lastTabClosed, this, [this] {
        m_tabs->addTab(QUrl("moch://newtab"));
    });
}

void MainWindow::updateNavigationState()
{
    m_backAction->setEnabled(m_tabs->canGoBack());
    m_forwardAction->setEnabled(m_tabs->canGoForward());
    if (m_tabs->currentView())
        m_addressBar->setText(m_tabs->currentView()->url().toString());
}

void MainWindow::applyTheme()
{
    QFile qss(":/themes/" + m_settings->theme() + ".qss");
    if (qss.open(QIODevice::ReadOnly))
        setStyleSheet(QString::fromUtf8(qss.readAll()));
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    const QKeySequence seq(int(event->modifiers()) | event->key());
    if (seq == QKeySequence("Ctrl+T")) {
        m_tabs->addTab(QUrl("moch://newtab"));
        return;
    }
    if (seq == QKeySequence("Ctrl+W")) {
        m_tabs->closeTab(m_tabs->currentIndex());
        return;
    }
    if (seq == QKeySequence("Ctrl+L")) {
        m_addressBar->setFocus();
        m_addressBar->selectAll();
        return;
    }
    if (seq == QKeySequence("Ctrl+R") || seq == QKeySequence("F5")) {
        if (auto *v = m_tabs->currentView())
            v->reload();
        return;
    }
    if (seq == QKeySequence("Ctrl+Shift+T")) {
        m_tabs->reopenClosedTab();
        return;
    }
    if (seq == QKeySequence("Ctrl+Tab")) {
        const int next = (m_tabs->currentIndex() + 1) % m_tabs->count();
        m_tabs->setCurrentIndex(next);
        return;
    }
    QMainWindow::keyPressEvent(event);
}
