#include "ui/downloadspanel.hpp"

#include <QFileInfo>
#include <QListWidget>
#include <QMenu>
#include <QVBoxLayout>

#include "browser/downloadmanager.hpp"

DownloadsPanel::DownloadsPanel(DownloadManager *downloads, QWidget *parent)
    : QWidget(parent, Qt::Popup), m_downloads(downloads)
{
    setFixedWidth(420);
    setMinimumHeight(140);

    m_list = new QListWidget(this);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        const QListWidgetItem *item = m_list->itemAt(pos);
        if (!item || item->data(Qt::UserRole).isNull())
            return;
        const int id = item->data(Qt::UserRole).toInt();
        QMenu menu(this);
        menu.addAction(tr("Open file"), this, [this, id] { m_downloads->openFile(id); });
        menu.addAction(tr("Show folder"), this, [this, id] { m_downloads->openFolder(id); });
        menu.exec(m_list->mapToGlobal(pos));
    });

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(m_list);

    connect(m_downloads, &DownloadManager::itemChanged, this, [this](int id) {
        for (int i = 0; i < m_list->count(); ++i) {
            QListWidgetItem *row = m_list->item(i);
            if (row->data(Qt::UserRole).toInt() == id) {
                delete row;
                break;
            }
        }
        for (const DownloadItem &item : m_downloads->items()) {
            if (item.id != id)
                continue;
            QString status;
            switch (item.state) {
            case DownloadItem::Running: {
                const int percent = item.total > 0 ? int(item.received * 100 / item.total) : 0;
                status = tr("%1%").arg(percent);
                break;
            }
            case DownloadItem::Completed:
                status = tr("Done");
                break;
            case DownloadItem::Failed:
                status = tr("Failed");
                break;
            case DownloadItem::Cancelled:
                status = tr("Cancelled");
                break;
            }
            QListWidgetItem *row = new QListWidgetItem(
                tr("%1 — %2").arg(item.fileName, status), m_list);
            row->setData(Qt::UserRole, item.id);
            return;
        }
    });
    connect(m_downloads, &DownloadManager::activeCountChanged, this, [this] { rebuild(); });
    rebuild();
}

void DownloadsPanel::rebuild()
{
    m_list->clear();
    const QList<DownloadItem> items = m_downloads->items();
    if (items.isEmpty()) {
        QListWidgetItem *placeholder = new QListWidgetItem(tr("No downloads yet"), m_list);
        placeholder->setFlags(Qt::NoItemFlags);
        return;
    }
    for (const DownloadItem &item : items) {
        QString status;
        switch (item.state) {
        case DownloadItem::Running: {
            const int percent = item.total > 0 ? int(item.received * 100 / item.total) : 0;
            status = tr("%1%").arg(percent);
            break;
        }
        case DownloadItem::Completed:
            status = tr("Done");
            break;
        case DownloadItem::Failed:
            status = tr("Failed");
            break;
        case DownloadItem::Cancelled:
            status = tr("Cancelled");
            break;
        }
        QListWidgetItem *row = new QListWidgetItem(tr("%1 — %2").arg(item.fileName, status),
                                                   m_list);
        row->setData(Qt::UserRole, item.id);
    }
}
