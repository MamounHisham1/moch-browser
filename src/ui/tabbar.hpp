#pragma once

#include <QIcon>
#include <QWidget>

class QToolButton;
class QTabBar;

class TabBar : public QWidget
{
    Q_OBJECT
public:
    explicit TabBar(QWidget *parent = nullptr);

    int count() const;
    int currentIndex() const;
    void appendTab(const QString &title);
    void setTab(int index, const QString &title, const QIcon &icon);
    void removeLastTabs(int newCount);
    void setCurrentIndex(int index);

signals:
    void currentTabChanged(int index);
    void tabCloseRequested(int index);
    void newTabRequested();
    void tabDuplicateRequested(int index);
    void tabsCloseOthersRequested(int index);
    void tabsCloseRightRequested(int index);

private:
    void showContextMenu(const QPoint &pos);

    QTabBar *m_tabs = nullptr;
    QToolButton *m_newTabButton = nullptr;
    bool m_syncing = false;
};
