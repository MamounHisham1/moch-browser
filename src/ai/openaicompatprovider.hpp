#pragma once

#include <QList>
#include <QTimer>

#include "ai/provider.hpp"

class QNetworkAccessManager;
class QNetworkReply;
class Settings;

class OpenAiCompatProvider : public LlmProvider
{
    Q_OBJECT
public:
    explicit OpenAiCompatProvider(Settings *settings, QObject *parent = nullptr);

    QString id() const override { return "openai-compat"; }
    void chat(const ChatRequest &request, std::function<void(ChatResponse)> callback) override;
    void chatStream(const ChatRequest &request, std::function<void(ChatResponse)> callback);
    void abort() override;

private:
    struct RequestState
    {
        QNetworkReply *reply = nullptr;
        QTimer *timer = nullptr;
        std::function<void(ChatResponse)> callback;
        bool stream = false;
        bool finished = false;
        QByteArray contentBuffer;
        QString content;
        QString finishReason;
        struct PartialToolCall
        {
            QString id;
            QString name;
            QString arguments;
        };
        QList<PartialToolCall> toolCalls;
    };

    QNetworkReply *sendRequest(const ChatRequest &request, bool stream, RequestState *state);
    void finishState(RequestState *state, const ChatResponse &response);
    void handleReplyData(RequestState *state, const QByteArray &data);
    ChatResponse parseCompletion(const QByteArray &body) const;
    void handleStreamLine(RequestState *state, const QString &line);
    void completeStream(RequestState *state);

    Settings *m_settings = nullptr;
    QNetworkAccessManager *m_nam = nullptr;
    QList<RequestState *> m_active;
};
