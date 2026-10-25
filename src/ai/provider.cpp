#include "ai/provider.hpp"

QJsonObject messageToJson(const ChatMessage &message)
{
    QJsonObject json;
    switch (message.role) {
    case ChatMessage::System:
        json.insert("role", "system");
        json.insert("content", message.content);
        break;
    case ChatMessage::User:
        json.insert("role", "user");
        json.insert("content", message.content);
        break;
    case ChatMessage::Assistant:
        json.insert("role", "assistant");
        json.insert("content", message.content);
        if (!message.toolCalls.isEmpty())
            json.insert("tool_calls", message.toolCalls);
        break;
    case ChatMessage::Tool:
        json.insert("role", "tool");
        json.insert("content", message.content);
        json.insert("tool_call_id", message.toolCallId);
        if (!message.toolName.isEmpty())
            json.insert("name", message.toolName);
        break;
    }
    return json;
}

ChatMessage jsonToMessage(const QJsonObject &json)
{
    ChatMessage message;
    const QString role = json.value("role").toString();
    if (role == "system")
        message.role = ChatMessage::System;
    else if (role == "user")
        message.role = ChatMessage::User;
    else if (role == "assistant")
        message.role = ChatMessage::Assistant;
    else
        message.role = ChatMessage::User;
    message.content = json.value("content").toString();
    message.toolCalls = json.value("tool_calls").toArray();
    message.toolCallId = json.value("tool_call_id").toString();
    message.toolName = json.value("name").toString();
    return message;
}
