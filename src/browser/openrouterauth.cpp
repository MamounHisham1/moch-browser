#include "browser/openrouterauth.hpp"

#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace {
const char *kAuthBase = "https://openrouter.ai/auth";
const char *kExchangeUrl = "https://openrouter.ai/api/v1/auth/keys";

QByteArray okPage(const QString &message)
{
    return "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n"
           "<html><body style='background:#141517;color:#e8e8ea;"
           "font-family:system-ui;display:flex;align-items:center;"
           "justify-content:center;height:100vh'><h2>" +
           message.toUtf8() + "</h2></body></html>";
}
} // namespace

OpenRouterAuth::OpenRouterAuth(QObject *parent)
    : QObject(parent), m_nam(new QNetworkAccessManager(this))
{
}

OpenRouterAuth::~OpenRouterAuth()
{
    stop();
}

bool OpenRouterAuth::start()
{
    stop();
    m_server = new QTcpServer(this);
    if (!m_server->listen(QHostAddress::LocalHost, 0)) {
        qWarning("moch: oauth listener failed: %s", qPrintable(m_server->errorString()));
        delete m_server;
        m_server = nullptr;
        return false;
    }
    m_port = m_server->serverPort();
    connect(m_server, &QTcpServer::newConnection, this, [this] {
        while (QTcpSocket *socket = m_server->nextPendingConnection())
            handleConnection(socket);
    });
    return true;
}

void OpenRouterAuth::handleConnection(QTcpSocket *socket)
{
    connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
    connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
        const QByteArray request = socket->readAll();
        const int space = request.indexOf(' ');
        const int secondSpace = request.indexOf(' ', space + 1);
        if (space < 0 || secondSpace < 0)
            return;
        const QByteArray path = request.mid(space + 1, secondSpace - space - 1);
        const QUrl url(QString::fromUtf8(path));
        const QUrlQuery query(url.query());
        const QString code = query.queryItemValue("code");

        if (code.isEmpty()) {
            socket->write(okPage(QStringLiteral("Moch: sign-in was cancelled.")));
            socket->disconnectFromHost();
            emit failed(QStringLiteral("no code in callback"));
            return;
        }
        socket->write(okPage(
            QStringLiteral("Signed in to OpenRouter. You can return to Moch.")));
        socket->disconnectFromHost();
        emit codeReceived(code);
    });
}

QString OpenRouterAuth::authUrl() const
{
    QUrl url(kAuthBase);
    QUrlQuery query;
    query.addQueryItem("callback_url",
                       QString("http://127.0.0.1:%1/callback").arg(m_port));
    url.setQuery(query);
    return url.toString();
}

void OpenRouterAuth::exchangeCode(const QString &code)
{
    QNetworkRequest request{QUrl(kExchangeUrl)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply *reply =
        m_nam->post(request, QJsonDocument(QJsonObject{{"code", code}}).toJson());
    QTimer::singleShot(15000, reply, &QNetworkReply::abort);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit failed(QStringLiteral("key exchange failed: %1").arg(reply->errorString()));
            return;
        }
        const QJsonObject response =
            QJsonDocument::fromJson(reply->readAll()).object();
        const QString key = response.value("key").toString();
        if (key.isEmpty()) {
            emit failed(QStringLiteral("exchange response had no key"));
            return;
        }
        emit keyReady(key);
    });
}

void OpenRouterAuth::stop()
{
    if (m_server) {
        m_server->close();
        delete m_server;
        m_server = nullptr;
    }
}
