#include "browser/webview.hpp"

WebView::WebView(QWidget *parent)
    : QWebEngineView(parent)
{
}

int WebView::tabIndex() const
{
    return m_tabIndex;
}

void WebView::setTabIndex(int index)
{
    m_tabIndex = index;
}

QWebEngineView *WebView::createWindow(QWebEnginePage::WebWindowType type)
{
    QWebEngineView *view = new WebView(this);
    connect(view, &QWebEngineView::urlChanged, this, [this, view](const QUrl &url) {
        if (!url.isEmpty() && url.toString() != "about:blank") {
            emit openInNewTabRequested(url);
            view->deleteLater();
        }
    });
    return view;
}
