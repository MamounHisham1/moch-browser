#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>

class Settings : public QObject
{
    Q_OBJECT
public:
    explicit Settings(QObject *parent = nullptr);

    QJsonObject values() const { return m_values; }
    QVariant get(const QString &path, const QVariant &fallback = {}) const;
    void set(const QString &path, const QVariant &value);
    void reset();

    QString apiKey() const;
    QString theme() const { return get("theme", "dark").toString(); }
    QString searchEngine() const { return get("search_engine", "duckduckgo").toString(); }

    static QString settingsPath();

signals:
    void changed();

private:
    void load();
    void save();

    QJsonObject m_values;
    mutable QString m_apiKeyCache;
    mutable bool m_apiKeyCached = false;
};
