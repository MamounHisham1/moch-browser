#include "ui/tabbar.hpp"

#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QMenu>
#include <QMouseEvent>
#include <QTabBar>
#include <QToolButton>

#include "ui/icons.hpp"

namespace {
class PressAwareTabBar : public QTabBar
{
public:
    using QTabBar::QTabBar;

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::MiddleButton) {
            const int index = tabAt(event->position().toPoint());
            if (index >= 0) {
                emit tabCloseRequested(index);
                return;
            }
        }
        QTabBar::mousePressEvent(event);
    }
};
} // namespace

TabBar::TabBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName("tabStrip");
    m_tabs = new PressAwareTabBar(this);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setExpanding(false);
    m_tabs->setUsesScrollButtons(true);
    m_tabs->setElideMode(Qt::ElideRight);
    m_tabs->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tabs, &QTabBar::customContextMenuRequested, this, [this](const QPoint &pos) {
        showContextMenu(m_tabs->mapTo(this, pos));
    });

    m_newTabButton = new QToolButton(this);
    m_newTabButton->setObjectName("newTabButton");
    m_newTabButton->setIcon(Icons::get("plus", ThemeColors::forTheme("dark").icon, 16));
    m_newTabButton->setToolTip(tr("New tab (Ctrl+T)"));
    m_newTabButton->setAutoRaise(true);
    m_newTabButton->setFixedSize(30, 28);
    m_newTabButton->setIconSize(QSize(16, 16));
    connect(m_newTabButton, &QToolButton::clicked, this, &TabBar::newTabRequested);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(2, 0, 0, 0);
    layout->setSpacing(2);
    layout->addWidget(m_tabs);
    layout->addWidget(m_newTabButton, 0, Qt::AlignVCenter);
    layout->addStretch(1);

    connect(m_tabs, &QTabBar::currentChanged, this, [this](int index) {
        if (!m_syncing)
            emit currentTabChanged(index);
    });
    connect(m_tabs, &QTabBar::tabCloseRequested, this, &TabBar::tabCloseRequested);
}

int TabBar::count() const
{
    return m_tabs->count();
}

int TabBar::currentIndex() const
{
    return m_tabs->currentIndex();
}

void TabBar::appendTab(const QString &title)
{
    m_syncing = true;
    m_tabs->addTab(title);
    m_syncing = false;
}

void TabBar::setTab(int index, const QString &title, const QIcon &icon)
{
    if (index < 0 || index >= m_tabs->count())
        return;
    m_tabs->setTabText(index, title);
    if (!icon.isNull())
        m_tabs->setTabIcon(index, icon);
}

void TabBar::removeLastTabs(int newCount)
{
    m_syncing = true;
    while (m_tabs->count() > newCount)
        m_tabs->removeTab(m_tabs->count() - 1);
    m_syncing = false;
}

void TabBar::setCurrentIndex(int index)
{
    if (index < 0 || index >= m_tabs->count())
        return;
    m_syncing = true;
    m_tabs->setCurrentIndex(index);
    m_syncing = false;
}

void TabBar::applyTheme(const QString &theme)
{
    m_newTabButton->setIcon(Icons::get("plus", ThemeColors::forTheme(theme).icon, 16));
}

void TabBar::showContextMenu(const QPoint &pos)
{
    const int index = m_tabs->tabAt(m_tabs->mapFrom(this, pos));
    if (index < 0)
        return;

    QMenu menu(this);
    QAction *duplicate = menu.addAction(tr("Duplicate Tab"));
    QAction *closeOthers = menu.addAction(tr("Close Other Tabs"));
    QAction *closeRight = menu.addAction(tr("Close Tabs to the Right"));
    closeOthers->setEnabled(m_tabs->count() > 1);
    closeRight->setEnabled(index < m_tabs->count() - 1);

    QAction *chosen = menu.exec(mapToGlobal(pos));
    if (chosen == duplicate)
        emit tabDuplicateRequested(index);
    else if (chosen == closeOthers)
        emit tabsCloseOthersRequested(index);
    else if (chosen == closeRight)
        emit tabsCloseRightRequested(index);
}
