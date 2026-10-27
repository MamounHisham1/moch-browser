#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

// Local secret vault for the agent, in the spirit of Rakazo's credential
// handling: values are stored locally in a 0600 file and referenced by
// name. Values never travel to the model — tools inject them into pages
// directly from C++ after explicit user approval, and the transcript
// redactor scrubs them if they ever leak into logged text.
namespace Security {

class SecretBox : public QObject
{
    Q_OBJECT
public:
    explicit SecretBox(QObject *parent = nullptr);

    // Names are trimmed; values are stripped of carriage returns and
    // trimmed of newlines. Values are never logged anywhere.
    QStringList names() const;
    bool has(const QString &name) const;
    QString value(const QString &name) const;
    void put(const QString &name, const QString &value);
    void remove(const QString &name);

    // Every stored value, for the transcript redactor. Backed by an
    // in-process cache; the file is read at most once per generation.
    static QStringList allValues();

    // Bumped by put()/remove() (and setFilePath) so the redactor can cache
    // allValues() without re-reading the file on every call.
    static quint64 generation();

    // Vault file location (default: AppDataLocation/secrets.json, 0600).
    static QString filePath();
    static void setFilePath(const QString &path);

signals:
    void changed();

private:
    static QStringList keyNames();
};

} // namespace Security
