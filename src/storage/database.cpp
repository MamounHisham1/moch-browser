#include "storage/database.hpp"

#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QVariant>

QString Database::databasePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/moch.db";
}

QSqlDatabase Database::connection()
{
    if (QSqlDatabase::contains("moch")) {
        QSqlDatabase db = QSqlDatabase::database("moch");
        if (!db.isOpen())
            db.open();
        return db;
    }
    const QString path = databasePath();
    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "moch");
    db.setDatabaseName(path);
    if (!db.open())
        qWarning("moch: cannot open database %s: %s", qPrintable(path),
                 qPrintable(db.lastError().text()));

    QSqlQuery query(db);
    query.exec("CREATE TABLE IF NOT EXISTS history ("
               "id INTEGER PRIMARY KEY AUTOINCREMENT,"
               "url TEXT NOT NULL, title TEXT NOT NULL, visited_at INTEGER NOT NULL)");
    query.exec("CREATE INDEX IF NOT EXISTS history_url ON history(url)");
    query.exec("CREATE INDEX IF NOT EXISTS history_visited ON history(visited_at)");
    query.exec("CREATE TABLE IF NOT EXISTS bookmarks ("
               "url TEXT PRIMARY KEY, title TEXT NOT NULL, added_at INTEGER NOT NULL)");
    query.exec("CREATE TABLE IF NOT EXISTS agent_tasks ("
               "id TEXT PRIMARY KEY, created_at INTEGER NOT NULL,"
               "status TEXT NOT NULL, goal TEXT NOT NULL, transcript TEXT NOT NULL)");
    return db;
}
