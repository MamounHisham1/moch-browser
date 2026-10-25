#pragma once

#include <QHash>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>

class MockOpenAiServer : public QObject
{
    Q_OBJECT
public:
    enum Scenario { PlainMessage, ToolCalls, ToolCallClickSubmit, Unauthorized, Malformed, StreamContent, StreamToolCalls };

    struct CapturedRequest
    {
        QByteArray method;
        QByteArray path;
        QHash<QByteArray, QByteArray> headers;
        QByteArray body;
    };

    explicit MockOpenAiServer(QObject *parent = nullptr);
    bool start();
    int port() const { return m_port; }
    void setScenario(Scenario scenario) { m_queue.clear(); m_queue.append(scenario); }
    void setScenarioQueue(const QList<Scenario> &queue) { m_queue = queue; }
    CapturedRequest lastRequest;

private slots:
    void onNewConnection();

private:
    void respond(QTcpSocket *socket);
    void writeHttp(QTcpSocket *socket, int status, const QByteArray &reason,
                   const QByteArray &body, const char *contentType);
    void writeSse(QTcpSocket *socket, const QList<QByteArray> &events);

    QTcpServer m_server;
    int m_port = 0;
    QList<Scenario> m_queue{PlainMessage};
};
