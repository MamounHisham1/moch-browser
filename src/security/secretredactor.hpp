#pragma once

#include <QString>
#include <QStringList>

namespace Security {

// Scrubs known secret values from text bound for logs, the transcript, or
// the model. Two sources feed it: an explicit list (setSecrets) and the
// SecretBox vault, which is consulted through a generation-counted cache so
// stored values are covered automatically without re-reading the file.
class SecretRedactor
{
public:
    void setSecrets(const QStringList &secrets);
    QString redact(const QString &text) const;

private:
    QStringList m_secrets;
};

}
