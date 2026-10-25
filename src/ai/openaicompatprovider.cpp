#include "ai/openaicompatprovider.hpp"

#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

#include "storage/settings.hpp"

namespace {
constexpr int kRequestTimeoutMs = 90 * 1000;
constexpr int kErrorDetailLimit = 500;

QString truncateDetail(const QString &detail)
{
    if (detail.size() <= kErrorDetailLimit)
        return detail;
    return detail.left(kErrorDetailLimit) + "…";
}
} // namespace

OpenAiCompatProvider::OpenAiCompatProvider(Settings *settings, QObject *parent)
    : LlmProvider(parent), m_settings(settings), m_nam(new QNetworkAccessManager(this))
{
}

QNetworkReply *OpenAiCompatProvider::sendRequest(const ChatRequest &request, bool stream,
                                                 RequestState *state)
{
    const QString key = m_settings->apiKey();
    if (key.isEmpty()) {
        ChatResponse response;
        response.error = "no_api_key: no model API key configured. Set the MOCH_API_KEY "
                         "environment variable or store one in ~/.local/share/moch/secret.";
        finishState(state, response);
        return nullptr;
    }

    const QString baseUrl = m_settings->get("ai.base_url").toString();
    const QString model = m_settings->get("ai.model").toString();
    const QUrl url(baseUrl + (baseUrl.endsWith("/") ? "" : "/") + "chat/completions");

    QJsonObject body;
    body.insert("model", model);
    body.insert("temperature", request.temperature);
    body.insert("stream", stream);
    QJsonArray messages;
    for (const ChatMessage &message : request.messages)
        messages.append(messageToJson(message));
    body.insert("messages", messages);
    if (!request.tools.isEmpty()) {
        QJsonArray tools;
        for (const ToolSpec &tool : request.tools) {
            QJsonObject function;
            function.insert("name", tool.name);
            function.insert("description", tool.description);
            function.insert("parameters", tool.parameters);
            QJsonObject toolJson;
            toolJson.insert("type", "function");
            toolJson.insert("function", function);
            tools.append(toolJson);
        }
        body.insert("tools", tools);
        body.insert("tool_choice", "auto");
    }

    QNetworkRequest networkRequest(url);
    networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    networkRequest.setRawHeader("Authorization", "Bearer " + key.toUtf8());

    state->timer = new QTimer(this);
    state->timer->setSingleShot(true);
    connect(state->timer, &QTimer::timeout, this, [this, state] {
        if (state->reply)
            state->reply->abort();
    });
    state->timer->start(kRequestTimeoutMs);

    QNetworkReply *reply = m_nam->post(networkRequest, QJsonDocument(body).toJson());
    state->reply = reply;
    state->stream = stream;
    m_active.append(state);
    return reply;
}

void OpenAiCompatProvider::chat(const ChatRequest &request,
                                std::function<void(ChatResponse)> callback)
{
    auto *state = new RequestState();
    state->callback = std::move(callback);

    QNetworkReply *reply = sendRequest(request, false, state);
    if (!reply)
        return;

    connect(reply, &QNetworkReply::finished, this, [this, state] {
        if (state->finished)
            return;
        QNetworkReply *reply = state->reply;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray bodyData = reply->readAll();
        if (status == 0) {
            ChatResponse response;
            response.error = QString("network_error: %1").arg(reply->errorString());
            finishState(state, response);
            return;
        }
        if (status >= 400) {
            ChatResponse response;
            const QJsonDocument doc = QJsonDocument::fromJson(bodyData);
            QString detail;
            if (doc.isObject()) {
                const QJsonValue msg = doc.object().value("error").toObject().value("message");
                if (msg.isString())
                    detail = msg.toString();
            }
            if (detail.isEmpty())
                detail = QString::fromUtf8(bodyData);
            if (status == 401 || status == 403)
                response.error = QString("auth_error (HTTP %1): %2 — check your API key.")
                                     .arg(status)
                                     .arg(truncateDetail(detail));
            else if (status == 429)
                response.error = QString("rate_limited (HTTP 429): %1 — slow down or check "
                                         "your provider quota.")
                                     .arg(truncateDetail(detail));
            else
                response.error = QString("http_error (HTTP %1): %2")
                                     .arg(status)
                                     .arg(truncateDetail(detail));
            finishState(state, response);
            return;
        }
        finishState(state, parseCompletion(bodyData));
    });
}

void OpenAiCompatProvider::chatStream(const ChatRequest &request,
                                      std::function<void(ChatResponse)> callback)
{
    auto *state = new RequestState();
    state->callback = std::move(callback);

    QNetworkReply *reply = sendRequest(request, true, state);
    if (!reply)
        return;

    connect(reply, &QNetworkReply::readyRead, this, [this, state] {
        if (state->finished)
            return;
        handleReplyData(state, state->reply->readAll());
    });
    connect(reply, &QNetworkReply::finished, this, [this, state] {
        if (state->finished)
            return;
        handleReplyData(state, state->reply->readAll());
        completeStream(state);
    });
}

void OpenAiCompatProvider::handleReplyData(RequestState *state, const QByteArray &data)
{
    state->contentBuffer.append(data);
    int newlineIndex;
    while ((newlineIndex = state->contentBuffer.indexOf('\n')) >= 0) {
        const QString line = QString::fromUtf8(state->contentBuffer.left(newlineIndex));
        state->contentBuffer.remove(0, newlineIndex + 1);
        handleStreamLine(state, line);
    }
}

void OpenAiCompatProvider::handleStreamLine(RequestState *state, const QString &line)
{
    const QString trimmed = line.trimmed();
    if (!trimmed.startsWith("data:"))
        return;
    const QString payload = trimmed.mid(5).trimmed();
    if (payload == "[DONE]")
        return;

    const QJsonDocument doc = QJsonDocument::fromJson(payload.toUtf8());
    if (!doc.isObject())
        return;
    const QJsonObject delta = doc.object().value("choices").toArray().at(0).toObject()
                                  .value("delta").toObject();
    const QString content = delta.value("content").toString();
    if (!content.isEmpty()) {
        state->content += content;
        emit streamDelta(content);
    }
    const QJsonArray toolCalls = delta.value("tool_calls").toArray();
    for (const QJsonValue &value : toolCalls) {
        const QJsonObject call = value.toObject();
        const int index = call.value("index").toInt(0);
        while (state->toolCalls.size() <= index)
            state->toolCalls.append(RequestState::PartialToolCall());
        if (call.contains("id"))
            state->toolCalls[index].id = call.value("id").toString();
        const QJsonObject function = call.value("function").toObject();
        if (function.contains("name"))
            state->toolCalls[index].name = function.value("name").toString();
        state->toolCalls[index].arguments += function.value("arguments").toString();
    }
    const QString finishReason = doc.object().value("choices").toArray().at(0).toObject()
                                     .value("finish_reason").toString();
    if (!finishReason.isEmpty())
        state->finishReason = finishReason;
}

void OpenAiCompatProvider::completeStream(RequestState *state)
{
    ChatResponse response;
    response.content = state->content;
    response.finishReason = state->finishReason;
    QJsonArray calls;
    for (const RequestState::PartialToolCall &call : std::as_const(state->toolCalls)) {
        QJsonObject function;
        function.insert("name", call.name);
        function.insert("arguments", call.arguments);
        QJsonObject callJson;
        callJson.insert("id", call.id);
        callJson.insert("type", "function");
        callJson.insert("function", function);
        calls.append(callJson);
    }
    response.toolCalls = calls;
    const QJsonObject usage = QJsonDocument::fromJson(state->contentBuffer).object()
                                  .value("usage").toObject();
    response.promptTokens = usage.value("prompt_tokens").toInt();
    response.completionTokens = usage.value("completion_tokens").toInt();
    finishState(state, response);
}

ChatResponse OpenAiCompatProvider::parseCompletion(const QByteArray &body) const
{
    ChatResponse response;
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) {
        response.error = "bad_response: provider returned malformed JSON";
        return response;
    }
    const QJsonObject root = doc.object();
    const QJsonObject choice = root.value("choices").toArray().at(0).toObject();
    const QJsonObject message = choice.value("message").toObject();
    response.content = message.value("content").toString();
    response.toolCalls = message.value("tool_calls").toArray();
    response.finishReason = choice.value("finish_reason").toString();
    const QJsonObject usage = root.value("usage").toObject();
    response.promptTokens = usage.value("prompt_tokens").toInt();
    response.completionTokens = usage.value("completion_tokens").toInt();
    return response;
}

void OpenAiCompatProvider::abort()
{
    for (RequestState *state : std::as_const(m_active)) {
        if (state->reply && !state->finished)
            state->reply->abort();
    }
}

void OpenAiCompatProvider::finishState(RequestState *state, const ChatResponse &response)
{
    state->finished = true;
    if (state->timer) {
        state->timer->stop();
        state->timer->deleteLater();
    }
    if (state->reply) {
        state->reply->deleteLater();
        state->reply = nullptr;
    }
    m_active.removeAll(state);
    if (state->callback)
        state->callback(response);
    delete state;
}
