#include "security/secretbox.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

namespace Security {
namespace {

// Trust model of the API-key file (~/.local/share/moch/secret): plain
// owner-only file, chmod 0600, no encryption beyond file permissions.
struct Vault
{
    QString path;
    QJsonObject entries;
    bool loaded = false;
    quint64 generation = 0;
};

Vault &vault()
{
    static Vault v;
    return v;
}

QString defaultPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
         + QStringLiteral("/secrets.json");
}

QString cleanName(const QString &name)
{
    return name.trimmed();
}

QString cleanValue(const QString &value)
{
    // Trim newlines only (never log the value). Carriage returns are
    // removed anywhere; bare newlines are trimmed from both ends.
    QString out = value;
    out.remove(QLatin1Char('\r'));
    while (out.startsWith(QLatin1Char('\n')))
        out.remove(0, 1);
    while (out.endsWith(QLatin1Char('\n')))
        out.chop(1);
    return out;
}

// Missing file is an empty vault; a corrupt file is treated as empty and
// left untouched until the next put/remove rewrites it.
void ensureLoaded(Vault &v)
{
    if (v.loaded)
        return;
    v.loaded = true;
    v.entries = QJsonObject();
    QFile file(v.path);
    if (!file.exists())
        return;
    // Re-assert owner-only permissions on every touch.
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (doc.isObject())
        v.entries = doc.object();
}

// Atomic rewrite: temp file + rename (QSaveFile), then re-assert 0600 on
// the final path — enforced on create and on every write.
void persist(Vault &v)
{
    QDir().mkpath(QFileInfo(v.path).absolutePath());
    QSaveFile file(v.path);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning("moch: cannot write secrets vault");
        return;
    }
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    file.write(QJsonDocument(v.entries).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        qWarning("moch: cannot commit secrets vault");
        return;
    }
    QFile::setPermissions(v.path, QFile::ReadOwner | QFile::WriteOwner);
    ++v.generation;
}

} // namespace

SecretBox::SecretBox(QObject *parent)
    : QObject(parent)
{
    Vault &v = vault();
    if (v.path.isEmpty())
        v.path = defaultPath();
}

QStringList SecretBox::keyNames()
{
    Vault &v = vault();
    ensureLoaded(v);
    QStringList keys = v.entries.keys();
    std::sort(keys.begin(), keys.end());
    return keys;
}

QStringList SecretBox::names() const
{
    return keyNames();
}

bool SecretBox::has(const QString &name) const
{
    Vault &v = vault();
    ensureLoaded(v);
    return v.entries.contains(cleanName(name));
}

QString SecretBox::value(const QString &name) const
{
    Vault &v = vault();
    ensureLoaded(v);
    return v.entries.value(cleanName(name)).toString();
}

void SecretBox::put(const QString &name, const QString &value)
{
    const QString key = cleanName(name);
    if (key.isEmpty())
        return;
    Vault &v = vault();
    ensureLoaded(v);
    v.entries.insert(key, cleanValue(value));
    persist(v);
    emit changed();
}

void SecretBox::remove(const QString &name)
{
    const QString key = cleanName(name);
    if (key.isEmpty())
        return;
    Vault &v = vault();
    ensureLoaded(v);
    if (!v.entries.contains(key))
        return;
    v.entries.remove(key);
    persist(v);
    emit changed();
}

QStringList SecretBox::allValues()
{
    Vault &v = vault();
    if (v.path.isEmpty())
        v.path = defaultPath();
    ensureLoaded(v);
    QStringList out;
    out.reserve(v.entries.size());
    for (auto it = v.entries.begin(); it != v.entries.end(); ++it)
        out.append(it.value().toString());
    return out;
}

quint64 SecretBox::generation()
{
    return vault().generation;
}

QString SecretBox::filePath()
{
    Vault &v = vault();
    if (v.path.isEmpty())
        v.path = defaultPath();
    return v.path;
}

void SecretBox::setFilePath(const QString &path)
{
    Vault &v = vault();
    v.path = path;
    v.loaded = false;
    v.entries = QJsonObject();
    ++v.generation;
}

} // namespace Security
