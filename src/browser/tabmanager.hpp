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
    void closeTab(int index);
    void setCurrentIndex(int index);
    void navigateCurrent(const QString &userInput);
    void reopenClosedTab();
    bool canGoBack() const;
    bool canGoForward() const;

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

    Settings *m_settings = nullptr;
    QWebEngineProfile *m_profile = nullptr;
    HistoryStore *m_history = nullptr;
    BookmarkStore *m_bookmarks = nullptr;
    QPointer<QStackedWidget> m_stack;
    QList<WebView *> m_views;
    QList<QUrl> m_closedUrls;
    int m_current = -1;
};
