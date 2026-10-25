#include "ui/toolbar.hpp"

#include <QHBoxLayout>
#include <QLineEdit>
#include <QMenu>
#include <QStyle>
#include <QToolButton>

Toolbar::Toolbar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName("navToolbar");

    m_back = makeButton("←");
    m_back->setToolTip("Back (Alt+Left)");
    connect(m_back, &QToolButton::clicked, this, &Toolbar::backRequested);

    m_forward = makeButton("→");
    m_forward->setToolTip("Forward (Alt+Right)");
    connect(m_forward, &QToolButton::clicked, this, &Toolbar::forwardRequested);

    m_reloadStop = makeButton("⟳");
    connect(m_reloadStop, &QToolButton::clicked, this, [this] {
        emit m_loading ? stopRequested() : reloadRequested();
    });

    m_address = new QLineEdit(this);
    m_address->setPlaceholderText("Search or enter address");
    m_address->setClearButtonEnabled(true);
    m_address->setObjectName("addressBar");
    m_security = m_address->addAction(QString(), QLineEdit::LeadingPosition);
    connect(m_address, &QLineEdit::returnPressed, this, [this] {
        emit navigateRequested(m_address->text());
        m_address->clearFocus();
    });

    m_bookmarks = makeButton("★");
    m_bookmarks->setToolTip("Bookmark this page (Ctrl+D)");
    m_bookmarks->setCheckable(true);
    m_bookmarks->setPopupMode(QToolButton::MenuButtonPopup);
    connect(m_bookmarks, &QToolButton::clicked, this, &Toolbar::bookmarkToggleRequested);

    m_downloads = makeButton("↓");
    m_downloads->setToolTip("Downloads");
    connect(m_downloads, &QToolButton::clicked, this, &Toolbar::downloadsPanelRequested);

    m_settings = makeButton("☰");
    m_settings->setToolTip("Menu");
    m_settings->setPopupMode(QToolButton::InstantPopup);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(4);
    layout->addWidget(m_back);
    layout->addWidget(m_forward);
    layout->addWidget(m_reloadStop);
    layout->addWidget(m_address, 1);
    layout->addWidget(m_bookmarks);
    layout->addWidget(m_downloads);
    layout->addWidget(m_settings);

    setLoading(false);
    setNavigationEnabled(false, false);
    setDownloadBadge(0, -1);
}

QToolButton *Toolbar::makeButton(const QString &glyph)
{
    auto *button = new QToolButton(this);
    button->setText(glyph);
    return button;
}

void Toolbar::setAddress(const QString &text)
{
    if (m_address->isModified() && m_address->hasFocus())
        return;
    m_address->setText(text);
}

void Toolbar::setSecurity(const QUrl &url)
{
    if (url.scheme() == "https")
        m_security->setText("🔒");
    else if (url.scheme() == "http")
        m_security->setText("⚠");
    else if (!url.isEmpty())
        m_security->setText("🌐");
    else
        m_security->setText(QString());
    const bool insecure = url.scheme() == "http";
    m_security->setToolTip(insecure ? "Connection is not encrypted" : QString());
}

void Toolbar::setLoading(bool loading)
{
    if (m_loading == loading)
        return;
    m_loading = loading;
    syncReloadStop();
}

void Toolbar::syncReloadStop()
{
    m_reloadStop->setText(m_loading ? "✕" : "⟳");
    m_reloadStop->setToolTip(m_loading ? "Stop (Esc)" : "Reload (Ctrl+R)");
}

void Toolbar::setNavigationEnabled(bool backEnabled, bool forwardEnabled)
{
    m_back->setEnabled(backEnabled);
    m_forward->setEnabled(forwardEnabled);
}

void Toolbar::setBookmarked(bool bookmarked)
{
    m_bookmarks->setChecked(bookmarked);
}

void Toolbar::setDownloadBadge(int activeCount, int percent)
{
    m_activeDownloads = activeCount;
    m_downloadPercent = percent;
    if (activeCount > 0) {
        m_downloads->setText("↓" + QString::number(activeCount));
        m_downloads->setToolTip(percent >= 0
                                     ? QString("Downloading… %1%").arg(percent)
                                     : QString("Downloading…"));
    } else {
        m_downloads->setText("↓");
        m_downloads->setToolTip("Downloads");
    }
}

void Toolbar::setSettingsMenu(QMenu *menu)
{
    m_settings->setMenu(menu);
}

void Toolbar::setBookmarksMenu(QMenu *menu)
{
    m_bookmarks->setMenu(menu);
}
