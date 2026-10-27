#pragma once

#include <QStringList>
#include <QWebEngineView>

class WebView : public QWebEngineView
{
    Q_OBJECT
public:
    explicit WebView(QWidget *parent = nullptr);

    int tabIndex() const;
    void setTabIndex(int index);
    bool isLoading() const { return m_loading; }
    int loadProgress() const { return m_loadProgress; }

    // Agent file-upload queue. The upload_files tool queues approved
    // absolute paths here, then clicks the page's file input; the next
    // chooseFiles() for this view answers from the queue (one-shot, oldest
    // first) and raises the chooser flag instead of opening a dialog. An
    // empty queue leaves human browsing untouched. The queue dies with the
    // view, so closing the tab clears it.
    void setPendingUploadFiles(const QStringList &paths);
    void clearPendingUploadFiles();
    bool hasPendingUploadFiles() const { return !m_pendingUploadFiles.isEmpty(); }
    // Set by the page when a chooser request was answered from the queue;
    // reset by setPendingUploadFiles().
    bool uploadChooserFired() const { return m_uploadChooserFired; }
    // Consumed by WebPage only; kept public because WebPage lives in the
    // .cpp and needs no friend declaration.
    QStringList takePendingUploadFiles();
    void markUploadChooserFired();

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
    QStringList m_pendingUploadFiles;
    bool m_uploadChooserFired = false;
};
