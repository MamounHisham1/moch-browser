#include "browser/downloadmanager.hpp"

#include <QDesktopServices>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUrl>
#include <QWebEngineDownloadRequest>

DownloadManager::DownloadManager(QObject *parent)
    : QObject(parent)
{
}

void DownloadManager::handleDownload(QWebEngineDownloadRequest *request)
{
    DownloadItem item;
    item.id = m_nextId++;
    item.url = request->url().toString();
    item.directory = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    item.fileName = QFileInfo(request->downloadFileName()).fileName();
    item.total = request->totalBytes();
    request->setDownloadDirectory(item.directory);
    request->setDownloadFileName(item.fileName);
    m_items.append(item);
    m_active++;
    emit itemChanged(item.id);
    emit activeCountChanged(m_active);

    connect(request, &QWebEngineDownloadRequest::receivedBytesChanged, this,
            [this, request, id = item.id] {
                for (DownloadItem &item : m_items) {
                    if (item.id != id)
                        continue;
                    item.received = request->receivedBytes();
                    item.total = request->totalBytes();
                    emit itemChanged(item.id);
                    return;
                }
            });
    connect(request, &QWebEngineDownloadRequest::stateChanged, this,
            [this, request, id = item.id](QWebEngineDownloadRequest::DownloadState state) {
                for (DownloadItem &item : m_items) {
                    if (item.id != id)
                        continue;
                    switch (state) {
                    case QWebEngineDownloadRequest::DownloadCompleted:
                        item.state = DownloadItem::Completed;
                        item.received = item.total;
                        if (m_active > 0) {
                            m_active--;
                            emit activeCountChanged(m_active);
                        }
                        break;
                    case QWebEngineDownloadRequest::DownloadCancelled:
                        item.state = DownloadItem::Cancelled;
                        if (m_active > 0) {
                            m_active--;
                            emit activeCountChanged(m_active);
                        }
                        break;
                    case QWebEngineDownloadRequest::DownloadInterrupted:
                        item.state = DownloadItem::Failed;
                        if (m_active > 0) {
                            m_active--;
                            emit activeCountChanged(m_active);
                        }
                        break;
                    default:
                        break;
                    }
                    emit itemChanged(item.id);
                    return;
                }
            });
    request->accept();
}

QList<DownloadItem> DownloadManager::items() const
{
    return m_items;
}

int DownloadManager::activeCount() const
{
    return m_active;
}

void DownloadManager::cancel(int id)
{
    for (DownloadItem &item : m_items) {
        if (item.id == id)
            item.state = DownloadItem::Cancelled;
    }
    emit itemChanged(id);
}

void DownloadManager::openFile(int id) const
{
    for (const DownloadItem &item : m_items) {
        if (item.id == id)
            QDesktopServices::openUrl(QUrl::fromLocalFile(item.directory + "/" + item.fileName));
    }
}

void DownloadManager::openFolder(int id) const
{
    for (const DownloadItem &item : m_items)
        QDesktopServices::openUrl(QUrl::fromLocalFile(item.directory));
}
