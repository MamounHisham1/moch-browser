#include "mockopenaiserver.hpp"

namespace {
const char *kPlainBody =
    R"({"choices":[{"message":{"role":"assistant","content":"Hello!"},"finish_reason":"stop"}],"usage":{"prompt_tokens":12,"completion_tokens":3}})";
const char *kToolCallsBody =
    R"({"choices":[{"message":{"role":"assistant","content":"","tool_calls":[{"id":"call_1","type":"function","function":{"name":"get_page_content","arguments":"{}"}}]},"finish_reason":"tool_calls"}]})";
const char *kUnauthorizedBody = R"({"error":{"message":"Incorrect API key provided"}})";

QByteArray sseDelta(const QByteArray &json)
{
    return "data: " + json + "\n\n";
}
} // namespace

MockOpenAiServer::MockOpenAiServer(QObject *parent)
    : QObject(parent)
{
    connect(&m_server, &QTcpServer::newConnection, this, &MockOpenAiServer::onNewConnection);
}

bool MockOpenAiServer::start()
{
    if (!m_server.listen(QHostAddress::LocalHost, 0)) {
        qWarning("mock server: listen failed: %s", qPrintable(m_server.errorString()));
        return false;
    }
    m_port = m_server.serverPort();
    return true;
}

void MockOpenAiServer::onNewConnection()
{
    while (QTcpSocket *socket = m_server.nextPendingConnection()) {
        connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
            static const char *const marker = "\r\n\r\n";
            QByteArray buffer = socket->property("buffer").toByteArray();
            buffer.append(socket->readAll());
            socket->setProperty("buffer", buffer);
            const int headerEnd = buffer.indexOf(marker);
            if (headerEnd < 0)
                return;
            const QByteArray headerBlock = buffer.left(headerEnd);
            int contentLength = 0;
            const QList<QByteArray> lines = headerBlock.split('\n');
            for (const QByteArray &line : lines) {
                if (line.startsWith("Content-Length:") || line.startsWith("content-length:"))
                    contentLength = line.split(':').at(1).trimmed().toInt();
            }
            const int bodyStart = headerEnd + 4;
            if (buffer.size() < bodyStart + contentLength)
                return;

            lastRequest = CapturedRequest();
            const QList<QByteArray> requestLines = headerBlock.split('\n');
            const QList<QByteArray> requestParts = requestLines.at(0).split(' ');
            if (requestParts.size() >= 2) {
                lastRequest.method = requestParts.at(0);
                lastRequest.path = requestParts.at(1);
            }
            for (int i = 1; i < requestLines.size(); ++i) {
                const int colon = requestLines.at(i).indexOf(':');
                if (colon > 0)
                    lastRequest.headers.insert(requestLines.at(i).left(colon).trimmed(),
                                               requestLines.at(i).mid(colon + 1).trimmed());
            }
            lastRequest.body = buffer.mid(bodyStart, contentLength);
            respond(socket);
        });
    }
}

void MockOpenAiServer::respond(QTcpSocket *socket)
{
    switch (m_scenario) {
    case PlainMessage:
        writeHttp(socket, 200, "OK", kPlainBody, "application/json");
        break;
    case ToolCalls:
        writeHttp(socket, 200, "OK", kToolCallsBody, "application/json");
        break;
    case Unauthorized:
        writeHttp(socket, 401, "Unauthorized", kUnauthorizedBody, "application/json");
        break;
    case Malformed:
        writeHttp(socket, 200, "OK", "not json {", "application/json");
        break;
    case StreamContent:
        writeSse(socket, {sseDelta(R"({"choices":[{"delta":{"content":"Moch "}}]})"),
                          sseDelta(R"({"choices":[{"delta":{"content":"streams!"}}]})"),
                          sseDelta(R"({"choices":[{"delta":{},"finish_reason":"stop"}]})"),
                          "data: [DONE]\n\n"});
        break;
    case StreamToolCalls:
        writeSse(socket, {sseDelta(R"({"choices":[{"delta":{"tool_calls":[{"index":0,"id":"call_9","function":{"name":"find_elements","arguments":""}}]}}]})"),
                          sseDelta(R"({"choices":[{"delta":{"tool_calls":[{"index":0,"function":{"arguments":"{\"sel"}}]}}]})"),
                          sseDelta(R"({"choices":[{"delta":{"tool_calls":[{"index":0,"function":{"arguments":"ector\":\"a\"}"}}]}}]})"),
                          sseDelta(R"({"choices":[{"delta":{},"finish_reason":"tool_calls"}]})"),
                          "data: [DONE]\n\n"});
        break;
    }
    socket->flush();
    socket->disconnectFromHost();
}

void MockOpenAiServer::writeHttp(QTcpSocket *socket, int status, const QByteArray &reason,
                                 const QByteArray &body, const char *contentType)
{
    QByteArray response;
    response += "HTTP/1.1 " + QByteArray::number(status) + " " + reason + "\r\n";
    response += QByteArray("Content-Type: ") + contentType + "\r\n";
    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    response += "Connection: close\r\n\r\n";
    response += body;
    socket->write(response);
}

void MockOpenAiServer::writeSse(QTcpSocket *socket, const QList<QByteArray> &events)
{
    QByteArray response;
    response += "HTTP/1.1 200 OK\r\n";
    response += "Content-Type: text/event-stream\r\n";
    response += "Connection: close\r\n\r\n";
    for (const QByteArray &event : events)
        response += event;
    socket->write(response);
}
