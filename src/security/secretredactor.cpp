#include "secretredactor.hpp"

#include <QRegularExpression>

#include <algorithm>

#include "security/secretbox.hpp"

namespace Security {
namespace {

const QString kMarker = QStringLiteral("«redacted»");

// Cache of SecretBox values, refreshed only when the vault generation
// changes (put/remove/setFilePath), so redact() never touches the file.
// Single-threaded: all callers run on the GUI thread.
const QStringList &secretBoxValues()
{
    static quint64 seenGeneration = 0;
    static QStringList cache;
    const quint64 gen = SecretBox::generation();
    if (gen != seenGeneration) {
        cache = SecretBox::allValues();
        cache.removeAll(QString());
        std::stable_sort(cache.begin(), cache.end(),
                         [](const QString &a, const QString &b) { return a.size() > b.size(); });
        seenGeneration = gen;
    }
    return cache;
}

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

    // Explicit list is already sorted longest-first; merge in the vault
    // values (also longest-first) so the longest match wins overall.
    const QStringList &vaultValues = secretBoxValues();
    if (!vaultValues.isEmpty()) {
        QStringList combined = m_secrets + vaultValues;
        std::stable_sort(combined.begin(), combined.end(),
                         [](const QString &a, const QString &b) { return a.size() > b.size(); });
        for (const QString &secret : combined)
            out.replace(secret, kMarker, Qt::CaseSensitive);
    } else {
        for (const QString &secret : m_secrets)
            out.replace(secret, kMarker, Qt::CaseSensitive);
    }

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
