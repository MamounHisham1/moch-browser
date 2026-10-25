#pragma once

#include <QMainWindow>
#include <QUrl>

class QLabel;
class QLineEdit;
class Settings;
class TabBar;
class TabManager;
class Toolbar;
class QWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(Settings *settings, TabManager *tabs, QWidget *parent = nullptr);

    Toolbar *toolbar() const { return m_toolbar; }
    TabBar *tabBar() const { return m_tabBar; }

public slots:
    void applyTheme();

signals:
    void newWindowRequested();
    void bookmarkToggleRequested(const QUrl &url);
    void openUrlRequested(const QUrl &url);

private:
    QWidget *buildFindBar();
    void connectTabs();
    void buildShortcuts();
    void buildMenuButton();
    void updateNavigationState();
    void findNext(bool forward);
    void toggleFindBar();

    Settings *m_settings = nullptr;
    TabManager *m_tabs = nullptr;
    Toolbar *m_toolbar = nullptr;
    TabBar *m_tabBar = nullptr;
    QWidget *m_findBar = nullptr;
    QLineEdit *m_findInput = nullptr;
    QLabel *m_findStatus = nullptr;
};
