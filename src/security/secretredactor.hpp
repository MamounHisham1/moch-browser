#pragma once

#include <QString>
#include <QStringList>

namespace Security {

class SecretRedactor
{
public:
    void setSecrets(const QStringList &secrets);
    QString redact(const QString &text) const;

private:
    QStringList m_secrets;
};

}
