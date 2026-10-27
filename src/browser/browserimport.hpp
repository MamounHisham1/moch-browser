#pragma once

// Browser import from other browsers. Header-only on purpose: the class is
// used by both moch (via browserimport.cpp) and test targets that link
// setupbridge.cpp without browserimport.cpp, and the test build files are
// fixed.

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QSettings>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <functional>

#include "browser/bookmarkstore.hpp"
#include "browser/historystore.hpp"

// Reading other browsers' data needs Qt Sql. Test targets that build this
// file without Qt6::Sql still see the API below; the implementations are
// only compiled where QT_SQL_LIB is defined (moch and the Sql-linked
// tests).
#ifdef QT_SQL_LIB
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#endif

class BookmarkStore;
class HistoryStore;

// Result of a one-shot import from another browser.
struct ImportSummary
{
    int bookmarks = 0;
    int history = 0;
    int duplicates = 0;
    QStringList warnings;
};

// One browser found on this machine, with quick entry counts for the
// setup wizard's import list.
struct BrowserSource
{
    QString id;          // "chrome", "chromium", "brave", "edge", "vivaldi",
                         // "firefox"
    QString name;        // human-readable, e.g. "Google Chrome"
    QString profilePath; // profile directory the import would read
    int bookmarks = 0;
    int history = 0;
    QString error;       // non-empty when counts could not be read
};

// Reads bookmarks and history from installed browsers (Chromium family and
// Firefox) and merges them into Moch's stores. Duplicates are skipped;
// existing entries are never modified. Live databases are copied to a
// private temp location before they are opened, so a running browser or a
// locked file never blocks the import.
class BrowserImport
{
public:
    // The home directory imports read from; overridable so tests can point
    // the scanner at a fixture tree.
    static QString homeDir();

    // Browsers with a usable profile on this machine, with entry counts.
    static QList<BrowserSource> availableBrowsers(
        const QString &home = homeDir());

    // Browser ids the importer can read (e.g. "chrome", "firefox").
    // Empty when nothing is installed.
    static QStringList detectableBrowsers(const QString &home = homeDir());

    // Merges bookmarks and history from `browserId` into the given stores.
    // Either store may be null to import only the other kind. Returns false
    // when nothing could be read; `summary` carries counts and warnings.
    static bool importFrom(const QString &browserId, BookmarkStore *bookmarks,
                           HistoryStore *history, ImportSummary *summary,
                           const QString &home = homeDir());
};

inline QString BrowserImport::homeDir()
{
    const QString home = qEnvironmentVariable("HOME");
    return home.isEmpty() ? QDir::homePath() : home;
}

#ifdef QT_SQL_LIB

namespace {

// One row pending import, before de-duplication.
struct ImportRawEntry
{
    QString url;
    QString title;
    qint64 when = 0;
};

struct ChromiumApp
{
    const char *id;
    const char *name;
    const char *configDir; // relative to ~/.config
};

inline const ChromiumApp kChromiumApps[] = {
    {"chrome", "Google Chrome", "google-chrome"},
    {"chromium", "Chromium", "chromium"},
    {"brave", "Brave", "BraveSoftware/Brave-Browser"},
    {"edge", "Microsoft Edge", "microsoft-edge"},
    {"vivaldi", "Vivaldi", "vivaldi"},
};

// Cap when loading existing rows for duplicate detection; imports are
// one-shot so a generous scan is fine.
constexpr int kImportScanLimit = 100000000;

// Chromium timestamps are microseconds since 1601-01-01; Firefox PRTime is
// microseconds since the Unix epoch.
inline qint64 chromiumToUnixSecs(qint64 micros)
{
    return micros > 0 ? micros / 1000000 - 11644473600 : 0;
}

inline qint64 firefoxToUnixSecs(qint64 micros)
{
    return micros > 0 ? micros / 1000000 : 0;
}

inline void collectChromiumBookmarks(const QJsonObject &node,
                                     QList<ImportRawEntry> *out)
{
    if (node.value("type").toString() == QLatin1String("url")) {
        ImportRawEntry entry;
        entry.url = node.value("url").toString();
        entry.title = node.value("name").toString();
        entry.when = chromiumToUnixSecs(
            node.value("date_added").toString().toLongLong());
        if (!entry.url.isEmpty())
            out->append(entry);
    }
    const QJsonArray children = node.value("children").toArray();
    for (const QJsonValue &child : children)
        collectChromiumBookmarks(child.toObject(), out);
}

// Copies a live SQLite database plus its WAL/SHM sidecars so a running
// browser or a locked file never blocks the read. Returns the copy path,
// or an empty string on failure.
inline QString copySqliteFile(const QString &source, const QString &destPath)
{
    if (!QFileInfo::exists(source) || !QFile::copy(source, destPath))
        return {};
    for (const char *suffix : {"-wal", "-shm"}) {
        const QString side = source + suffix;
        if (QFileInfo::exists(side))
            QFile::copy(side, destPath + suffix);
    }
    return destPath;
}

// Opens a SQLite copy under a dedicated connection and runs `row` for each
// result row of `sql`.
inline bool readSqliteRows(const QString &path, const QString &connection,
                           const QString &sql,
                           const std::function<bool(QSqlQuery &)> &row)
{
    if (QSqlDatabase::contains(connection))
        QSqlDatabase::removeDatabase(connection);
    bool ok = false;
    QString error;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connection);
        db.setDatabaseName(path);
        if (!db.open()) {
            error = db.lastError().text();
        } else {
            QSqlQuery query(db);
            if (!query.exec(sql)) {
                error = query.lastError().text();
            } else {
                ok = true;
                while (query.next()) {
                    if (!row(query)) {
                        ok = false;
                        error = query.lastError().text();
                        break;
                    }
                }
            }
        }
        if (db.isOpen())
            db.close();
    }
    QSqlDatabase::removeDatabase(connection);
    if (!ok && !error.isEmpty())
        qWarning("moch: import read failed on %s: %s", qPrintable(path),
                 qPrintable(error));
    return ok;
}

inline int countSqlite(const QString &path, const QString &connection,
                       const QString &sql)
{
    int count = 0;
    readSqliteRows(path, connection, sql, [&count](QSqlQuery &query) {
        count = query.value(0).toInt();
        return true;
    });
    return count;
}

// Returns the profile directory to import for a Chromium-family app, or
// empty when the browser has no usable profile on this machine.
inline QString chromiumProfileDir(const QString &appConfigDir)
{
    QDir dir(appConfigDir);
    if (!dir.exists())
        return {};
    if (dir.exists(QStringLiteral("Default")))
        return appConfigDir + QStringLiteral("/Default");
    const QFileInfoList profiles =
        dir.entryInfoList({QStringLiteral("Profile *")}, QDir::Dirs,
                          QDir::Name);
    return profiles.isEmpty() ? QString() : profiles.first().absolutePath();
}

// Picks the default Firefox profile from ~/.mozilla/firefox/profiles.ini.
// [Install...] sections point at the default via Default=; [ProfileN]
// sections carry Path= with IsRelative=.
inline QString firefoxProfileDir(const QString &home)
{
    const QString base = home + QStringLiteral("/.mozilla/firefox");
    QSettings ini(base + QStringLiteral("/profiles.ini"),
                  QSettings::IniFormat);
    QString preferred;
    for (const QString &group : ini.childGroups()) {
        if (!group.startsWith(QLatin1String("Install")))
            continue;
        ini.beginGroup(group);
        preferred = ini.value("Default").toString();
        ini.endGroup();
        if (!preferred.isEmpty())
            break;
    }
    QString fallback;
    for (const QString &group : ini.childGroups()) {
        if (!group.startsWith(QLatin1String("Profile")))
            continue;
        ini.beginGroup(group);
        const QString path = ini.value("Path").toString();
        const bool relative = ini.value("IsRelative", 1).toInt() != 0;
        const bool markedDefault = ini.value("Default", 0).toInt() != 0;
        ini.endGroup();
        if (path.isEmpty())
            continue;
        const QString full = relative ? base + QLatin1Char('/') + path : path;
        if (!QDir(full).exists())
            continue;
        if (!preferred.isEmpty() && path == preferred)
            return full;
        if (markedDefault)
            return full;
        if (fallback.isEmpty())
            fallback = full;
    }
    return fallback;
}

// Reads a Chromium-family profile into raw rows. Missing files count as
// "no data" (normal for a fresh profile); unreadable-but-present files
// produce warnings. Returns false when nothing at all was readable.
inline bool collectChromium(const QString &profileDir, const QString &tempDir,
                            QList<ImportRawEntry> *bookmarks,
                            QList<ImportRawEntry> *history,
                            QStringList *warnings)
{
    bool any = false;

    QFile bookmarksFile(profileDir + QStringLiteral("/Bookmarks"));
    if (bookmarksFile.exists()) {
        if (bookmarksFile.open(QIODevice::ReadOnly)) {
            QJsonParseError parseError;
            const QJsonDocument doc =
                QJsonDocument::fromJson(bookmarksFile.readAll(), &parseError);
            const QJsonObject roots = doc.object().value("roots").toObject();
            if (parseError.error == QJsonParseError::NoError
                && !roots.isEmpty()) {
                for (const QString &root : roots.keys())
                    collectChromiumBookmarks(roots.value(root).toObject(),
                                             bookmarks);
                any = true;
            } else {
                warnings->append(QStringLiteral("bookmarks file is not valid JSON"));
            }
        } else {
            warnings->append(QStringLiteral("bookmarks file could not be read"));
        }
    }

    const QString historySource = profileDir + QStringLiteral("/History");
    if (QFileInfo::exists(historySource)) {
        const QString copy = copySqliteFile(
            historySource, tempDir + QStringLiteral("/chromium-History"));
        if (copy.isEmpty()) {
            warnings->append(
                QStringLiteral("history database could not be copied"));
            return any;
        }
        if (readSqliteRows(
                copy, "moch-import-chromehist",
                "SELECT url, title, last_visit_time FROM urls"
                " WHERE last_visit_time > 0",
                [history](QSqlQuery &query) {
                    ImportRawEntry entry;
                    entry.url = query.value(0).toString();
                    entry.title = query.value(1).toString();
                    entry.when =
                        chromiumToUnixSecs(query.value(2).toLongLong());
                    if (!entry.url.isEmpty())
                        history->append(entry);
                    return true;
                })) {
            any = true;
        } else {
            warnings->append(QStringLiteral("history could not be read"));
        }
    }
    return any;
}

// Reads the default Firefox profile's places database into raw rows.
inline bool collectFirefox(const QString &home, const QString &tempDir,
                           QList<ImportRawEntry> *bookmarks,
                           QList<ImportRawEntry> *history, QStringList *warnings)
{
    const QString profileDir = firefoxProfileDir(home);
    if (profileDir.isEmpty()) {
        warnings->append(QStringLiteral("no Firefox profile found"));
        return false;
    }
    const QString places = profileDir + QStringLiteral("/places.sqlite");
    if (!QFileInfo::exists(places)) {
        warnings->append(QStringLiteral("places.sqlite not found"));
        return false;
    }
    const QString copy = copySqliteFile(
        places, tempDir + QStringLiteral("/firefox-places.sqlite"));
    if (copy.isEmpty()) {
        warnings->append(QStringLiteral("places.sqlite could not be copied"));
        return false;
    }
    bool any = false;
    if (readSqliteRows(
            copy, "moch-import-fxbm",
            "SELECT p.url, COALESCE(b.title, ''), b.dateAdded"
            " FROM moz_bookmarks b JOIN moz_places p ON p.id = b.fk"
            " WHERE b.type = 1",
            [bookmarks](QSqlQuery &query) {
                ImportRawEntry entry;
                entry.url = query.value(0).toString();
                entry.title = query.value(1).toString();
                entry.when = firefoxToUnixSecs(query.value(2).toLongLong());
                if (!entry.url.isEmpty() && !entry.url.startsWith("place:"))
                    bookmarks->append(entry);
                return true;
            })) {
        any = true;
    } else {
        warnings->append(QStringLiteral("bookmarks could not be read"));
    }
    if (readSqliteRows(
            copy, "moch-import-fxhist",
            "SELECT url, COALESCE(title, ''), last_visit_date FROM moz_places"
            " WHERE last_visit_date > 0",
            [history](QSqlQuery &query) {
                ImportRawEntry entry;
                entry.url = query.value(0).toString();
                entry.title = query.value(1).toString();
                entry.when = firefoxToUnixSecs(query.value(2).toLongLong());
                if (!entry.url.isEmpty() && !entry.url.startsWith("place:"))
                    history->append(entry);
                return true;
            })) {
        any = true;
    } else {
        warnings->append(QStringLiteral("history could not be read"));
    }
    return any;
}

// Merges raw rows into a store, skipping URLs already present in Moch or
// seen earlier in this run. Returns the number of imported rows and bumps
// `duplicates` for skips.
inline int mergeImportRows(const QList<ImportRawEntry> &rows,
                           QSet<QString> *known,
                           const std::function<void(const ImportRawEntry &)> &insert,
                           int *duplicates)
{
    int imported = 0;
    for (const ImportRawEntry &entry : rows) {
        if (known->contains(entry.url)) {
            ++*duplicates;
            continue;
        }
        insert(entry);
        known->insert(entry.url);
        ++imported;
    }
    return imported;
}

} // namespace

inline QList<BrowserSource> BrowserImport::availableBrowsers(const QString &home)
{
    QList<BrowserSource> out;
    QTemporaryDir temp;
    if (!temp.isValid())
        return out;
    const QString tempPath = temp.path();

    for (const ChromiumApp &app : kChromiumApps) {
        const QString profileDir =
            chromiumProfileDir(home + QStringLiteral("/.config/")
                               + QString::fromLatin1(app.configDir));
        if (profileDir.isEmpty())
            continue;
        BrowserSource source;
        source.id = QString::fromLatin1(app.id);
        source.name = QString::fromLatin1(app.name);
        source.profilePath = profileDir;

        // Bookmarks: count url entries in the JSON directly.
        QFile bookmarksFile(profileDir + QStringLiteral("/Bookmarks"));
        if (bookmarksFile.open(QIODevice::ReadOnly)) {
            QList<ImportRawEntry> rows;
            const QJsonObject roots =
                QJsonDocument::fromJson(bookmarksFile.readAll())
                    .object()
                    .value("roots")
                    .toObject();
            for (const QString &root : roots.keys())
                collectChromiumBookmarks(roots.value(root).toObject(), &rows);
            source.bookmarks = rows.size();
        }

        // History: count rows in a private copy of the database.
        const QString historySource = profileDir + QStringLiteral("/History");
        if (QFileInfo::exists(historySource)) {
            const QString copy = copySqliteFile(
                historySource,
                tempPath + QStringLiteral("/%1-History").arg(source.id));
            if (!copy.isEmpty()) {
                source.history = countSqlite(copy, "moch-import-count",
                                             "SELECT COUNT(*) FROM urls");
            } else {
                source.error =
                    QStringLiteral("history database could not be copied");
            }
        }
        out.append(source);
    }

    const QString firefoxDir = firefoxProfileDir(home);
    if (!firefoxDir.isEmpty()) {
        BrowserSource source;
        source.id = QStringLiteral("firefox");
        source.name = QStringLiteral("Firefox");
        source.profilePath = firefoxDir;
        const QString places = firefoxDir + QStringLiteral("/places.sqlite");
        if (QFileInfo::exists(places)) {
            const QString copy = copySqliteFile(
                places, tempPath + QStringLiteral("/firefox-places.sqlite"));
            if (!copy.isEmpty()) {
                source.bookmarks = countSqlite(
                    copy, "moch-import-count",
                    "SELECT COUNT(*) FROM moz_bookmarks WHERE type = 1");
                source.history = countSqlite(
                    copy, "moch-import-count2",
                    "SELECT COUNT(*) FROM moz_places"
                    " WHERE last_visit_date > 0");
            } else {
                source.error = QStringLiteral("places database is locked");
            }
        }
        out.append(source);
    }
    return out;
}

inline QStringList BrowserImport::detectableBrowsers(const QString &home)
{
    QStringList ids;
    for (const BrowserSource &source : availableBrowsers(home))
        ids.append(source.id);
    return ids;
}

inline bool BrowserImport::importFrom(const QString &browserId,
                                      BookmarkStore *bookmarks,
                                      HistoryStore *history,
                                      ImportSummary *summary,
                                      const QString &home)
{
    if (!summary)
        return false;
    *summary = ImportSummary{};
    if (!bookmarks && !history) {
        summary->warnings.append(QStringLiteral("no target stores given"));
        return false;
    }

    QTemporaryDir temp;
    if (!temp.isValid()) {
        summary->warnings.append(QStringLiteral("could not create temp dir"));
        return false;
    }

    QList<ImportRawEntry> bookmarkRows;
    QList<ImportRawEntry> historyRows;
    if (browserId == QLatin1String("firefox")) {
        collectFirefox(home, temp.path(), &bookmarkRows, &historyRows,
                       &summary->warnings);
    } else {
        bool known = false;
        for (const ChromiumApp &app : kChromiumApps) {
            if (browserId != QLatin1String(app.id))
                continue;
            known = true;
            const QString profileDir =
                chromiumProfileDir(home + QStringLiteral("/.config/")
                                   + QString::fromLatin1(app.configDir));
            if (profileDir.isEmpty()) {
                summary->warnings.append(
                    QStringLiteral("no %1 profile found").arg(app.name));
                break;
            }
            collectChromium(profileDir, temp.path(), &bookmarkRows,
                            &historyRows, &summary->warnings);
            break;
        }
        if (!known) {
            summary->warnings.append(
                QStringLiteral("unknown browser '%1'").arg(browserId));
            return false;
        }
    }

    QSet<QString> knownBookmarks;
    QSet<QString> knownHistory;
    if (bookmarks) {
        for (const Bookmark &bookmark : bookmarks->all())
            knownBookmarks.insert(bookmark.url);
        summary->bookmarks = mergeImportRows(
            bookmarkRows, &knownBookmarks,
            [bookmarks](const ImportRawEntry &entry) {
                bookmarks->add(entry.url, entry.title);
            },
            &summary->duplicates);
    }
    if (history) {
        for (const HistoryEntry &entry : history->visits(kImportScanLimit, 0))
            knownHistory.insert(entry.url);
        summary->history = mergeImportRows(
            historyRows, &knownHistory,
            [history](const ImportRawEntry &entry) {
                history->recordVisit(entry.url, entry.title);
            },
            &summary->duplicates);
    }
    return true;
}

#endif // QT_SQL_LIB
