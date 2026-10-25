#pragma once

#include <QWebEngineView>

class WebPage;

class WebView : public QWebEngineView
{
    Q_OBJECT
public:
    explicit WebView(QWidget *parent = nullptr);

    int tabIndex() const;
    void setTabIndex(int index);
    bool isLoading() const { return m_loading; }
    int loadProgress() const { return m_loadProgress; }

signals:
    void openInNewTabRequested(const QUrl &url);

protected:
    QWebEngineView *createWindow(QWebEnginePage::WebWindowType type) override;

private:
    friend class TabManager;
    void setLoadingState(bool loading);
    void setLoadProgressValue(int progress);

    int m_tabIndex = -1;
    bool m_loading = false;
    int m_loadProgress = 0;
};
