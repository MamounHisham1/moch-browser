#pragma once

#include <QString>

class QSqlDatabase;

class Database
{
public:
    static QSqlDatabase connection();
    static QString databasePath();
};
