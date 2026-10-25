#pragma once

#include <QList>
#include <QObject>
#include <QString>

class QWebEngineDownloadRequest;

struct DownloadItem
{
    int id = 0;
    QString url;
    QString fileName;
    QString directory;
    qint64 received = 0;
    qint64 total = 0;
    enum State { Running, Completed, Failed, Cancelled } state = Running;
};

class DownloadManager : public QObject
{
    Q_OBJECT
public:
    explicit DownloadManager(QObject *parent = nullptr);

    void handleDownload(QWebEngineDownloadRequest *request);
    QList<DownloadItem> items() const;
    int activeCount() const;
    void cancel(int id);
    void openFile(int id) const;
    void openFolder(int id) const;

signals:
    void itemChanged(int id);
    void activeCountChanged(int count);

private:
    QList<DownloadItem> m_items;
    int m_nextId = 1;
    int m_active = 0;
};
