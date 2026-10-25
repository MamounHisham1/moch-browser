#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QTcpServer;
class QTcpSocket;

// Implements OpenRouter's desktop OAuth flow: a temporary loopback HTTP
// server receives the ?code= redirect after the user approves in the
// browser, then the code is exchanged for an API key.
class OpenRouterAuth : public QObject
{
    Q_OBJECT
public:
    explicit OpenRouterAuth(QObject *parent = nullptr);
    ~OpenRouterAuth() override;

    bool start();
    QString authUrl() const;
    void exchangeCode(const QString &code);
    void stop();

signals:
    void codeReceived(const QString &code);
    void keyReady(const QString &apiKey);
    void failed(const QString &reason);

private:
    void handleConnection(QTcpSocket *socket);

    QTcpServer *m_server = nullptr;
    QNetworkAccessManager *m_nam = nullptr;
    int m_port = 0;
};
