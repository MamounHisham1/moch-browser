#include "ai/openaicompatprovider.hpp"

#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

#include "ai/providerdirectory.hpp"
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

// Effective endpoint mode for a request.
// - legacy: ai.provider unset, "openai-compat", or an unknown id (e.g. a
//   hand-edited future provider). Behaves exactly as before the provider
//   directory existed: base URL and key come straight from settings.
// - openrouter: OpenRouter-routed requests, including models whose catalog
//   slug has no native endpoint (anthropic, google, meta-llama, …).
// - native: a ProviderDirectory id; requests go to the provider's own
//   endpoint with the provider's own key.
struct Endpoint
{
    QString providerId;
    bool legacy = false;
    bool openRouter = false;
    bool needsKey = true;
};

Endpoint resolveEndpoint(const Settings *settings)
{
    Endpoint endpoint;
    const QString raw = settings->get("ai.provider").toString().trimmed().toLower();
    if (raw.isEmpty() || raw == QLatin1String("openai-compat")
        || (raw != QLatin1String("openrouter")
            && ProviderDirectory::byId(raw).id.isEmpty())) {
        endpoint.providerId = QStringLiteral("openai-compat");
        endpoint.legacy = true;
        return endpoint;
    }
    endpoint.providerId = raw;
    endpoint.openRouter = raw == QLatin1String("openrouter");
    if (!endpoint.openRouter)
        endpoint.needsKey = ProviderDirectory::byId(raw).needsKey;
    return endpoint;
}

QString endpointBaseUrl(const Settings *settings, const Endpoint &endpoint)
{
    const QString configured = settings->get("ai.base_url").toString().trimmed();
    if (endpoint.legacy || !ProviderDirectory::isGenericBaseUrl(configured))
        return configured;
    if (endpoint.openRouter)
        return QStringLiteral("https://openrouter.ai/api/v1");
    return ProviderDirectory::defaultBaseUrl(
        endpoint.providerId, settings->get("ai.coding_plan", true).toBool());
}
} // namespace

OpenAiCompatProvider::OpenAiCompatProvider(Settings *settings, QObject *parent)
    : LlmProvider(parent), m_settings(settings), m_nam(new QNetworkAccessManager(this))
{
}

QNetworkReply *OpenAiCompatProvider::sendRequest(const ChatRequest &request, bool stream,
                                                 RequestState *state)
{
    const Endpoint endpoint = resolveEndpoint(m_settings);
    QString key;
    if (endpoint.needsKey) {
        // Vault key for this provider wins, then the legacy chain
        // (ai.api_key_env environment variable, else the 0600 key file).
        key = endpoint.legacy
                  ? m_settings->apiKey()
                  : ProviderDirectory::providerKey(m_settings, endpoint.providerId);
    }
    if (endpoint.needsKey && key.isEmpty()) {
        ChatResponse response;
        if (endpoint.legacy) {
            response.error = QString("no_api_key: no model API key configured. Add your "
                                     "key in Moch's Settings dialog (Ctrl+,) or set the "
                                     "%1 environment variable. Key file location: %2")
                                 .arg(m_settings->get("ai.api_key_env", "MOCH_API_KEY")
                                          .toString(),
                                      m_settings->secretPath());
        } else if (endpoint.openRouter) {
            response.error = QString("no_api_key: no OpenRouter key configured — add it "
                                     "in Settings → AI or Secret Box, sign in with "
                                     "OpenRouter in the setup wizard, or set the %1 "
                                     "environment variable. Key file location: %2")
                                 .arg(m_settings->get("ai.api_key_env", "MOCH_API_KEY")
                                          .toString(),
                                      m_settings->secretPath());
        } else {
            const ProviderDirectory::NativeProvider provider =
                ProviderDirectory::byId(endpoint.providerId);
            response.error = QString("no_api_key: no %1 key configured — add it in "
                                     "Settings → AI or Secret Box (%2), get one at %3, "
                                     "or set the %4 environment variable. Key file "
                                     "location: %5")
                                 .arg(provider.name,
                                      ProviderDirectory::settingsKeyFor(provider.id),
                                      provider.docsUrl,
                                      m_settings->get("ai.api_key_env", "MOCH_API_KEY")
                                          .toString(),
                                      m_settings->secretPath());
        }
        finishState(state, response);
        return nullptr;
    }

    const QString baseUrl = endpointBaseUrl(m_settings, endpoint);
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
    if (!key.isEmpty())
        networkRequest.setRawHeader("Authorization", "Bearer " + key.toUtf8());
    // OpenRouter attribution headers are OpenRouter-specific; native
    // providers must never receive them.
    if (endpoint.openRouter)
        networkRequest.setRawHeader("X-Title", "Moch");

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
