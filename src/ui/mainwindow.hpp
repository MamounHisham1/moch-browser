#pragma once

#include <QMainWindow>

class QAction;
class QLineEdit;
class QTabBar;
class Settings;
class TabManager;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(Settings *settings, TabManager *tabs, QWidget *parent = nullptr);

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void buildToolbar();
    void buildTabBar();
    void connectTabs();
    void updateNavigationState();
    void applyTheme();

    Settings *m_settings = nullptr;
    TabManager *m_tabs = nullptr;
    QLineEdit *m_addressBar = nullptr;
    QTabBar *m_tabBar = nullptr;
    QAction *m_backAction = nullptr;
    QAction *m_forwardAction = nullptr;
    QAction *m_reloadAction = nullptr;
};
