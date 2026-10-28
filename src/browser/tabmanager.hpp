#pragma once

#include <QIcon>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QUrl>

class QStackedWidget;
class QWebEngineProfile;
class BookmarkStore;
class HistoryStore;
class Settings;
class WebView;

struct TabInfo
{
    int index = -1;
    QString title;
    QUrl url;
    QIcon icon;
    bool loading = false;
    int loadProgress = 0;
};

// One persisted tab of a saved session (url + title, in tab order).
struct SessionTab
{
    QUrl url;
    QString title;
};

class TabManager : public QObject
{
    Q_OBJECT
public:
    explicit TabManager(Settings *settings, QWebEngineProfile *profile,
                        HistoryStore *history = nullptr, BookmarkStore *bookmarks = nullptr,
                        QObject *parent = nullptr);
    ~TabManager() override;

    QWidget *widget() const;
    int count() const;
    int currentIndex() const;
    WebView *view(int index) const;
    WebView *currentView() const;
    TabInfo info(int index) const;

    int addTab(const QUrl &url = QUrl("moch://newtab"), bool background = false);
    // Applies the Moch theme to web-content scrollbars in every tab, and to
    // tabs created from now on.
    void setScrollbarTheme(const QString &theme);
    void closeTab(int index);
    void setCurrentIndex(int index);
    void navigateCurrent(const QString &userInput);
    void reopenClosedTab();
    bool canGoBack() const;
    bool canGoForward() const;

    // Tab session persistence: open tabs are saved (url, title, order,
    // active tab) to a crash-safe JSON file under the app data dir,
    // debounced on open/close/navigate and flushed on destruction.
    static QString sessionFilePath();
    // Reads the last saved session; empty list when missing or unreadable.
    static QList<SessionTab> loadSavedSession(int *activeIndex = nullptr);
    // Writes the current tab list immediately (skipped when tracking is off
    // or the tab widget is gone).
    void saveSessionNow();
    // Restores the session saved on disk; no-op when nothing to restore.
    void restoreSavedSession();
    // Only the session-owning (first) window enables tracking so extra
    // windows never overwrite the saved file.
    void setSessionTrackingEnabled(bool enabled);

signals:
    void tabsChanged();
    void currentChanged(int index);
    void tabTitleChanged(int index, const QString &title);
    void tabIconChanged(int index, const QIcon &icon);
    void tabLoadProgress(int index, int progress);
    void tabUrlChanged(int index, const QUrl &url);
    void tabLoadFinished(int index, const QUrl &url, bool ok);
    void lastTabClosed();
    void searchRequested(const QString &query);
    void openUrlRequested(const QUrl &url);

private:
    void reindex();
    void scheduleSessionSave();

    Settings *m_settings = nullptr;
    QWebEngineProfile *m_profile = nullptr;
    HistoryStore *m_history = nullptr;
    BookmarkStore *m_bookmarks = nullptr;
    QPointer<QStackedWidget> m_stack;
    QList<WebView *> m_views;
    QList<QUrl> m_closedUrls;
    int m_current = -1;
    QTimer *m_saveTimer = nullptr;
    bool m_trackSession = false;
    QString m_scrollbarTheme = QStringLiteral("dark");
};
