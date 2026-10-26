#pragma once

#include <QUrl>
#include <QWidget>

class QAction;
class QLineEdit;
class QMenu;
class QToolButton;

class Toolbar : public QWidget
{
    Q_OBJECT
public:
    explicit Toolbar(QWidget *parent = nullptr);

    QLineEdit *addressBar() const { return m_address; }
    void setAddress(const QString &text);
    void setSecurity(const QUrl &url);
    void setLoading(bool loading);
    void setNavigationEnabled(bool backEnabled, bool forwardEnabled);
    void setBookmarked(bool bookmarked);
    void setDownloadBadge(int activeCount, int percent);
    void setSettingsMenu(QMenu *menu);
    void setBookmarksMenu(QMenu *menu);
    void setAiExpanded(bool expanded);
    void applyTheme(const QString &theme);

signals:
    void backRequested();
    void forwardRequested();
    void reloadRequested();
    void stopRequested();
    void navigateRequested(const QString &input);
    void bookmarkToggleRequested();
    void downloadsPanelRequested();
    void aiToggleRequested();

private:
    QToolButton *makeButton(const QString &iconName);
    void applyButtonIcons();

    QLineEdit *m_address = nullptr;
    QAction *m_security = nullptr;
    QToolButton *m_back = nullptr;
    QToolButton *m_forward = nullptr;
    QToolButton *m_reloadStop = nullptr;
    QToolButton *m_bookmarks = nullptr;
    QToolButton *m_downloads = nullptr;
    QToolButton *m_ai = nullptr;
    QToolButton *m_settings = nullptr;
    bool m_loading = false;
    bool m_bookmarked = false;
    bool m_aiExpanded = false;
    int m_activeDownloads = 0;
    int m_downloadPercent = -1;
    QString m_theme = "dark";
};
