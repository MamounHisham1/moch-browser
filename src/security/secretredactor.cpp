#include "secretredactor.hpp"

#include <QRegularExpression>

#include <algorithm>

namespace Security {
namespace {

const QString kMarker = QStringLiteral("«redacted»");

}

void SecretRedactor::setSecrets(const QStringList &secrets)
{
    QStringList cleaned;
    cleaned.reserve(secrets.size());
    for (const QString &s : secrets) {
        if (!s.isEmpty())
            cleaned.append(s);
    }
    std::stable_sort(cleaned.begin(), cleaned.end(),
                     [](const QString &a, const QString &b) { return a.size() > b.size(); });
    m_secrets = cleaned;
}

QString SecretRedactor::redact(const QString &text) const
{
    QString out = text;
    for (const QString &secret : m_secrets)
        out.replace(secret, kMarker, Qt::CaseSensitive);

    static const QRegularExpression bearerToken(
        QStringLiteral("(Bearer\\s+)(\\S{16,})"), QRegularExpression::CaseInsensitiveOption);
    out.replace(bearerToken, QStringLiteral("\\1") + kMarker);

    static const QRegularExpression openAiStyleKey(QStringLiteral("sk-[A-Za-z0-9]{16,}"));
    out.replace(openAiStyleKey, kMarker);

    static const QRegularExpression jwt(
        QStringLiteral("eyJ[A-Za-z0-9_-]+\\.[A-Za-z0-9_-]+\\.[A-Za-z0-9_-]+"));
    out.replace(jwt, kMarker);

    return out;
}

}
