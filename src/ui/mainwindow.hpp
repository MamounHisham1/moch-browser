#pragma once

#include <QMainWindow>
#include <QUrl>

class AiSidebar;
class QLabel;
class QMenu;
class QLineEdit;
class BookmarkStore;
class DownloadManager;
class DownloadsPanel;
class HistoryStore;
class Settings;
class TabBar;
class TabManager;
class ToolRegistry;
class Toolbar;
class QWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(Settings *settings, TabManager *tabs, HistoryStore *history,
                        BookmarkStore *bookmarks, DownloadManager *downloads,
                        QWidget *parent = nullptr);

    Toolbar *toolbar() const { return m_toolbar; }
    TabBar *tabBar() const { return m_tabBar; }
    AiSidebar *aiSidebar() const { return m_aiSidebar; }
    ToolRegistry *toolRegistry() const { return m_toolRegistry; }

public slots:
    void applyTheme();

signals:
    void newWindowRequested();
    void bookmarkToggleRequested(const QUrl &url);
    void openUrlRequested(const QUrl &url);

private:
    QWidget *buildFindBar();
    void applyMenuIcons();
    void connectTabs();
    void buildShortcuts();
    void buildMenuButton();
    void buildStoreMenus();
    void openHistory();
    void openSettings();
    void recordHistory(int index, const QUrl &url, bool ok);
    void updateBookmarkStar();
    void updateNavigationState();
    void findNext(bool forward);
    void toggleFindBar();

    Settings *m_settings = nullptr;
    TabManager *m_tabs = nullptr;
    Toolbar *m_toolbar = nullptr;
    TabBar *m_tabBar = nullptr;
    HistoryStore *m_history = nullptr;
    BookmarkStore *m_bookmarks = nullptr;
    DownloadManager *m_downloads = nullptr;
    DownloadsPanel *m_downloadsPanel = nullptr;
    ToolRegistry *m_toolRegistry = nullptr;
    QMenu *m_mainMenu = nullptr;
    AiSidebar *m_aiSidebar = nullptr;
    QWidget *m_findBar = nullptr;
    QLineEdit *m_findInput = nullptr;
    QLabel *m_findStatus = nullptr;
};
