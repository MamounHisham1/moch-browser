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

signals:
    void openInNewTabRequested(const QUrl &url);

protected:
    QWebEngineView *createWindow(QWebEnginePage::WebWindowType type) override;

private:
    int m_tabIndex = -1;
};
