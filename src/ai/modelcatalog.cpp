#include "ai/modelcatalog.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

namespace {
const char *kCatalogUrl = "https://openrouter.ai/api/v1/models";
constexpr int kTimeoutMs = 12000;

QString providerOf(const QString &modelId)
{
    const int slash = modelId.indexOf('/');
    return slash > 0 ? modelId.left(slash) : QStringLiteral("other");
}
} // namespace

ModelCatalog::ModelCatalog(QObject *parent)
    : QObject(parent), m_nam(new QNetworkAccessManager(this))
{
}

void ModelCatalog::fetchModels()
{
    if (m_reply)
        return;
    QNetworkRequest request{QUrl(kCatalogUrl)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    m_reply = m_nam->get(request);
    QTimer::singleShot(kTimeoutMs, this, [this] {
        if (m_reply)
            m_reply->abort();
    });
    connect(m_reply, &QNetworkReply::finished, this, [this] {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit modelsFailed(reply->errorString());
            return;
        }
        const QJsonArray models = parse(reply->readAll());
        if (models.isEmpty()) {
            emit modelsFailed(QStringLiteral("catalog response was empty or malformed"));
            return;
        }
        emit modelsReady(models);
    });
}

void ModelCatalog::cancel()
{
    if (m_reply)
        m_reply->abort();
}

QJsonArray ModelCatalog::parse(const QByteArray &body)
{
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    const QJsonArray data = doc.object().value("data").toArray();
    QJsonArray out;
    for (const QJsonValue &value : data) {
        const QJsonObject model = value.toObject();
        const QString id = model.value("id").toString();
        if (id.isEmpty())
            continue;
        const QJsonObject pricing = model.value("pricing").toObject();
        const bool free = pricing.value("prompt").toString() == "0"
                          && pricing.value("completion").toString() == "0";
        QJsonObject entry;
        entry.insert("id", id);
        entry.insert("name", model.value("name").toString(id));
        entry.insert("provider", providerOf(id));
        entry.insert("free", free);
        entry.insert("context", model.value("context_length").toInt());
        out.append(entry);
    }
    return out;
}

QJsonArray ModelCatalog::fallbackModels()
{
    static const char *const known[] = {
        "openai/gpt-4o-mini", "openai/gpt-4o", "openai/gpt-4.1-mini", "openai/gpt-4.1",
        "anthropic/claude-sonnet-4", "anthropic/claude-3.5-haiku",
        "google/gemini-2.0-flash-001", "google/gemini-2.5-pro",
        "meta-llama/llama-3.3-70b-instruct", "deepseek/deepseek-chat-v3-0324",
        "mistralai/mistral-small-3.1-24b-instruct", "qwen/qwen-2.5-72b-instruct",
        "meta-llama/llama-3.1-8b-instruct:free", "deepseek/deepseek-r1:free",
        "google/gemma-3-27b-it:free", "qwen/qwen-2.5-7b-instruct:free",
    };
    QJsonArray out;
    for (const char *id : known) {
        const QString modelId = QString::fromLatin1(id);
        QJsonObject entry;
        entry.insert("id", modelId);
        entry.insert("name", modelId);
        entry.insert("provider", providerOf(modelId));
        entry.insert("free", modelId.contains(":free"));
        entry.insert("context", 0);
        out.append(entry);
    }
    return out;
}
