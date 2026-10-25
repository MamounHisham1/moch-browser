#include "storage/settings.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QStandardPaths>

static const QJsonObject defaultValues()
{
    QJsonObject agent;
    agent.insert("permission_level", "cautious");
    agent.insert("max_task_seconds", 300);
    agent.insert("max_tool_calls", 40);
    agent.insert("confirm_downloads", true);

    QJsonObject ai;
    ai.insert("provider", "openai-compat");
    ai.insert("base_url", "https://api.openai.com/v1");
    ai.insert("model", "gpt-4o-mini");
    ai.insert("api_key_env", "MOCH_API_KEY");
    ai.insert("temperature", 0.2);

    QJsonObject privacy;
    privacy.insert("do_not_track", true);
    privacy.insert("send_page_content", true);

    QJsonObject headless;
    headless.insert("enabled", false);
    headless.insert("cdp_url", "");

    QJsonObject root;
    root.insert("theme", "dark");
    root.insert("search_engine", "duckduckgo");
    root.insert("startup_behavior", "new_tab");
    root.insert("agent", agent);
    root.insert("ai", ai);
    root.insert("privacy", privacy);
    root.insert("headless_engine", headless);
    return root;
}

static QJsonObject mergeDefaults(const QJsonObject &loaded, const QJsonObject &defaults)
{
    QJsonObject out = defaults;
    for (auto it = loaded.begin(); it != loaded.end(); ++it) {
        const QString key = it.key();
        if (it.value().isObject() && out.value(key).isObject())
            out.insert(key, mergeDefaults(it.value().toObject(), out.value(key).toObject()));
        else
            out.insert(key, it.value());
    }
    return out;
}

Settings::Settings(QObject *parent)
    : QObject(parent)
{
    load();
}

QString Settings::settingsPath()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    return base + "/moch/settings.json";
}

void Settings::load()
{
    m_values = defaultValues();
    QFile file(settingsPath());
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (doc.isObject())
            m_values = mergeDefaults(doc.object(), defaultValues());
    }
}

void Settings::save()
{
    const QString path = settingsPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning("moch: cannot write settings %s", qPrintable(path));
        return;
    }
    file.write(QJsonDocument(m_values).toJson(QJsonDocument::Indented));
    file.close();
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
}

QVariant Settings::get(const QString &path, const QVariant &fallback) const
{
    QJsonObject node = m_values;
    const QStringList parts = path.split('.');
    for (int i = 0; i < parts.size() - 1; ++i) {
        const QJsonValue child = node.value(parts[i]);
        if (!child.isObject())
            return fallback;
        node = child.toObject();
    }
    const QJsonValue leaf = node.value(parts.last());
    return leaf.isUndefined() ? fallback : leaf.toVariant();
}

static QJsonObject setAtPath(QJsonObject node, const QStringList &parts, const QJsonValue &value)
{
    if (parts.isEmpty())
        return node;
    if (parts.size() == 1) {
        node.insert(parts.first(), value);
        return node;
    }
    QJsonObject child = node.value(parts.first()).toObject();
    node.insert(parts.first(), setAtPath(child, parts.mid(1), value));
    return node;
}

void Settings::set(const QString &path, const QVariant &value)
{
    m_values = setAtPath(m_values, path.split('.'), QJsonValue::fromVariant(value));
    save();
    m_apiKeyCached = false;
    emit changed();
}

void Settings::reset()
{
    m_values = defaultValues();
    save();
    m_apiKeyCached = false;
    emit changed();
}

QString Settings::apiKey() const
{
    if (m_apiKeyCached)
        return m_apiKeyCache;
    const QString envName = get("ai.api_key_env", "MOCH_API_KEY").toString();
    const QString fromEnv = qEnvironmentVariable(envName.toUtf8().constData());
    if (!fromEnv.isEmpty()) {
        m_apiKeyCache = fromEnv;
        m_apiKeyCached = true;
        return fromEnv;
    }
    const QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                         + "/secret";
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        m_apiKeyCache = QString::fromUtf8(file.readAll().trimmed());
        m_apiKeyCached = true;
        return m_apiKeyCache;
    }
    m_apiKeyCache = QString();
    m_apiKeyCached = true;
    return QString();
}
