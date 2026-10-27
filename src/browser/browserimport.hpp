#pragma once

#include <QString>
#include <QStringList>

class BookmarkStore;
class HistoryStore;

// Result of a one-shot import from another browser.
struct ImportSummary
{
    int bookmarks = 0;
    int history = 0;
    QStringList warnings;
};

// Reads bookmarks and history from installed browsers (Chromium family
// and Firefox) and merges them into Moch's stores. Duplicates are skipped;
// existing entries are never modified.
class BrowserImport
{
public:
    // Browser ids the importer can read on this machine (e.g. "chrome",
    // "brave", "edge", "firefox"). Empty when nothing is installed.
    static QStringList detectableBrowsers();

    static bool importFrom(const QString &browserId, BookmarkStore *bookmarks,
                           HistoryStore *history, ImportSummary *summary);
};
