#pragma once

#include <QObject>

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

private:
    Settings *m_settings = nullptr;
    TabManager *m_tabs = nullptr;
    MainWindow *m_window = nullptr;
};
