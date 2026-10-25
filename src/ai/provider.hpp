#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

#include <functional>

struct ChatMessage
{
    enum Role { System, User, Assistant, Tool };

    Role role = User;
    QString content;
    QString toolCallId;
    QString toolName;
    QJsonArray toolCalls;
};

struct ToolSpec
{
    QString name;
    QString description;
    QJsonObject parameters;
};

struct ChatRequest
{
    QList<ChatMessage> messages;
    QList<ToolSpec> tools;
    double temperature = 0.2;
};

struct ChatResponse
{
    QString content;
    QJsonArray toolCalls;
    QString finishReason;
    int promptTokens = 0;
    int completionTokens = 0;
    QString error;
};

QJsonObject messageToJson(const ChatMessage &message);
ChatMessage jsonToMessage(const QJsonObject &json);

class LlmProvider : public QObject
{
    Q_OBJECT
public:
    explicit LlmProvider(QObject *parent = nullptr)
        : QObject(parent) {}
    ~LlmProvider() override = default;

    virtual QString id() const = 0;
    virtual void chat(const ChatRequest &request, std::function<void(ChatResponse)> callback) = 0;

public slots:
    virtual void abort() = 0;

signals:
    void streamDelta(const QString &text);
};
