#include "browser/browserimport.hpp"

QStringList BrowserImport::detectableBrowsers()
{
    return {};
}

bool BrowserImport::importFrom(const QString &browserId, BookmarkStore *bookmarks,
                               HistoryStore *history, ImportSummary *summary)
{
    Q_UNUSED(browserId);
    Q_UNUSED(bookmarks);
    Q_UNUSED(history);
    Q_UNUSED(summary);
    return false;
}
