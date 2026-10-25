#pragma once

#include <QObject>

class BookmarkStore;
class DownloadManager;
class HistoryStore;
class MainWindow;
class Settings;
class TabManager;

class ApplicationController : public QObject
{
    Q_OBJECT
public:
    explicit ApplicationController(QObject *parent = nullptr);
    ~ApplicationController() override;
    void start();

private slots:
    void openWindow();

private:
    Settings *m_settings = nullptr;
    HistoryStore *m_history = nullptr;
    BookmarkStore *m_bookmarks = nullptr;
    DownloadManager *m_downloads = nullptr;
    TabManager *m_tabs = nullptr;
    MainWindow *m_window = nullptr;
};
