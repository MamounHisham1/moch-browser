#include "ui/toolbar.hpp"

#include <QHBoxLayout>
#include <QLineEdit>
#include <QMenu>
#include <QStyle>
#include <QToolButton>

#include "ui/icons.hpp"

Toolbar::Toolbar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName("navToolbar");
    // Plain QWidget ignores QSS backgrounds unless this is set.
    setAttribute(Qt::WA_StyledBackground, true);

    m_back = makeButton("arrow-left");
    m_back->setToolTip(tr("Back (Alt+←)"));
    connect(m_back, &QToolButton::clicked, this, &Toolbar::backRequested);

    m_forward = makeButton("arrow-right");
    m_forward->setToolTip(tr("Forward (Alt+→)"));
    connect(m_forward, &QToolButton::clicked, this, &Toolbar::forwardRequested);

    m_reloadStop = makeButton("reload");
    connect(m_reloadStop, &QToolButton::clicked, this, [this] {
        emit m_loading ? stopRequested() : reloadRequested();
    });

    m_address = new QLineEdit(this);
    m_address->setPlaceholderText(tr("Search or enter address"));
    m_address->setClearButtonEnabled(true);
    m_address->setObjectName("addressBar");
    m_security = m_address->addAction(QString(), QLineEdit::LeadingPosition);
    connect(m_address, &QLineEdit::returnPressed, this, [this] {
        emit navigateRequested(m_address->text());
        m_address->clearFocus();
    });

    m_bookmarks = makeButton("star");
    m_bookmarks->setToolTip(tr("Bookmark this page (Ctrl+D)"));
    m_bookmarks->setCheckable(true);
    m_bookmarks->setPopupMode(QToolButton::MenuButtonPopup);
    connect(m_bookmarks, &QToolButton::clicked, this, &Toolbar::bookmarkToggleRequested);

    m_downloads = makeButton("download");
    m_downloads->setToolTip(tr("Downloads"));
    connect(m_downloads, &QToolButton::clicked, this, &Toolbar::downloadsPanelRequested);

    m_ai = makeButton("panel-right");
    m_ai->setToolTip(tr("AI Sidebar (Ctrl+I)"));
    m_ai->setCheckable(true);
    connect(m_ai, &QToolButton::clicked, this, &Toolbar::aiToggleRequested);

    m_settings = makeButton("settings");
    m_settings->setToolTip(tr("Menu"));
    m_settings->setPopupMode(QToolButton::InstantPopup);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(2);
    layout->addWidget(m_back);
    layout->addWidget(m_forward);
    layout->addWidget(m_reloadStop);
    layout->addWidget(m_address, 1);
    layout->addWidget(m_bookmarks);
    layout->addWidget(m_downloads);
    layout->addWidget(m_ai);
    layout->addWidget(m_settings);

    setLoading(false);
    setNavigationEnabled(false, false);
    setDownloadBadge(0, -1);
}

QToolButton *Toolbar::makeButton(const QString &iconName)
{
    auto *button = new QToolButton(this);
    button->setObjectName("iconButton");
    button->setIconSize(QSize(16, 16));
    button->setIcon(Icons::get(iconName, ThemeColors::forTheme(m_theme).icon, 16));
    button->setAutoRaise(true);
    button->setProperty("iconName", iconName);
    return button;
}

void Toolbar::applyButtonIcons()
{
    const ThemeColors colors = ThemeColors::forTheme(m_theme);
    const auto set = [this, &colors](QToolButton *button, const QString &name = QString()) {
        const QString iconName = name.isEmpty() ? button->property("iconName").toString() : name;
        button->setIcon(Icons::get(iconName, colors.icon, 16));
    };
    set(m_back);
    set(m_forward);
    set(m_reloadStop, m_loading ? "close" : "reload");
    set(m_bookmarks, m_bookmarked ? "star-filled" : "star");
    set(m_downloads, "download");
    m_downloads->setIcon(Icons::get("download",
                                    m_activeDownloads > 0 ? colors.accent : colors.icon, 16));
    set(m_ai, "panel-right");
    m_ai->setIcon(Icons::get("panel-right",
                             m_aiExpanded ? colors.accent : colors.icon, 16));
    set(m_settings);
    m_security->setIcon(Icons::get(m_security->property("iconName").toString(),
                                   m_security->property("iconDanger").toBool()
                                       ? colors.danger
                                       : colors.icon, 16));
}

void Toolbar::setAddress(const QString &text)
{
    if (m_address->isModified() && m_address->hasFocus())
        return;
    m_address->setText(text);
}

void Toolbar::setSecurity(const QUrl &url)
{
    QString name;
    bool danger = false;
    if (url.scheme() == "https")
        name = "lock";
    else if (url.scheme() == "http") {
        name = "alert";
        danger = true;
    } else if (!url.isEmpty())
        name = "globe";
    else
        name = "search"; // empty page: Chrome-style search glyph
    m_security->setProperty("iconName", name);
    m_security->setProperty("iconDanger", danger);
    const bool insecure = url.scheme() == "http";
    m_security->setToolTip(insecure ? tr("Connection is not encrypted") : QString());
    applyButtonIcons();
}

void Toolbar::setLoading(bool loading)
{
    if (m_loading == loading)
        return;
    m_loading = loading;
    m_reloadStop->setProperty("iconName", m_loading ? "close" : "reload");
    m_reloadStop->setToolTip(m_loading ? tr("Stop (Esc)") : tr("Reload (Ctrl+R)"));
    applyButtonIcons();
}

void Toolbar::setNavigationEnabled(bool backEnabled, bool forwardEnabled)
{
    m_back->setEnabled(backEnabled);
    m_forward->setEnabled(forwardEnabled);
}

void Toolbar::setBookmarked(bool bookmarked)
{
    if (m_bookmarked == bookmarked)
        return;
    m_bookmarked = bookmarked;
    m_bookmarks->setChecked(bookmarked);
    applyButtonIcons();
}

void Toolbar::setDownloadBadge(int activeCount, int percent)
{
    m_activeDownloads = activeCount;
    m_downloadPercent = percent;
    if (activeCount > 0)
        m_downloads->setToolTip(percent >= 0 ? tr("Downloading… %1%").arg(percent)
                                             : tr("Downloading…"));
    else
        m_downloads->setToolTip(tr("Downloads"));
    applyButtonIcons();
}

void Toolbar::setSettingsMenu(QMenu *menu)
{
    m_settings->setMenu(menu);
}

void Toolbar::setBookmarksMenu(QMenu *menu)
{
    m_bookmarks->setMenu(menu);
}

void Toolbar::setAiExpanded(bool expanded)
{
    if (m_aiExpanded == expanded)
        return;
    m_aiExpanded = expanded;
    m_ai->setChecked(expanded);
    applyButtonIcons();
}

void Toolbar::applyTheme(const QString &theme)
{
    m_theme = theme;
    applyButtonIcons();
}
