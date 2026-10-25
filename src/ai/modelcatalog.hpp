#pragma once

#include <QJsonArray>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

// Fetches the OpenRouter model catalog (public endpoint, no key needed) and
// normalizes it for the setup wizard. Falls back to a small curated list
// when the network is unavailable.
class ModelCatalog : public QObject
{
    Q_OBJECT
public:
    explicit ModelCatalog(QObject *parent = nullptr);

    void fetchModels();
    void cancel();

    static QJsonArray parse(const QByteArray &body);
    static QJsonArray fallbackModels();

signals:
    void modelsReady(const QJsonArray &models);
    void modelsFailed(const QString &reason);

private:
    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_reply = nullptr;
};
