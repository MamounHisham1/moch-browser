#pragma once

#include <QWidget>

class QListWidget;
class DownloadManager;

class DownloadsPanel : public QWidget
{
    Q_OBJECT
public:
    explicit DownloadsPanel(DownloadManager *downloads, QWidget *parent = nullptr);

private:
    void rebuild();
    void refreshItem(int id);
    DownloadManager *m_downloads = nullptr;
    QListWidget *m_list = nullptr;
};
