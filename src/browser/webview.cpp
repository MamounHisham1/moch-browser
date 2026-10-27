#include "browser/webview.hpp"

#include <QWebEnginePage>

namespace {

// Page type installed by every WebView. When the upload_files tool has
// queued paths, the next file-chooser request is answered from the queue
// (one-shot) instead of opening the stock dialog; with no queue it falls
// through so human browsing is unaffected. FileSelectSave is a download
// target, never an upload, and always uses the default dialog.
class WebPage final : public QWebEnginePage
{
public:
    explicit WebPage(WebView *view) : QWebEnginePage(view), m_view(view) {}

protected:
    QStringList chooseFiles(FileSelectionMode mode, const QStringList &oldFiles,
                            const QStringList &acceptedMimeTypes) override
    {
        if (m_view && mode != QWebEnginePage::FileSelectSave
            && m_view->hasPendingUploadFiles()) {
            const QStringList files = m_view->takePendingUploadFiles();
            m_view->markUploadChooserFired();
            if (mode == QWebEnginePage::FileSelectOpenMultiple)
                return files;
            return files.mid(0, 1);
        }
        return QWebEnginePage::chooseFiles(mode, oldFiles, acceptedMimeTypes);
    }

private:
    WebView *m_view = nullptr;
};

} // namespace

WebView::WebView(QWidget *parent)
    : QWebEngineView(parent)
{
    setPage(new WebPage(this));
}

int WebView::tabIndex() const
{
    return m_tabIndex;
}

void WebView::setTabIndex(int index)
{
    m_tabIndex = index;
}

void WebView::setLoadingState(bool loading)
{
    m_loading = loading;
    if (loading)
        m_loadProgress = 0;
}

void WebView::setLoadProgressValue(int progress)
{
    m_loadProgress = progress;
}

void WebView::setPendingUploadFiles(const QStringList &paths)
{
    m_pendingUploadFiles = paths;
    m_uploadChooserFired = false;
}

void WebView::clearPendingUploadFiles()
{
    m_pendingUploadFiles.clear();
}

QStringList WebView::takePendingUploadFiles()
{
    const QStringList files = m_pendingUploadFiles;
    m_pendingUploadFiles.clear();
    return files;
}

void WebView::markUploadChooserFired()
{
    m_uploadChooserFired = true;
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
