#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QTemporaryDir>
#include <QtTest>

#include "ai/openaicompatprovider.hpp"
#include "mockopenaiserver.hpp"
#include "storage/settings.hpp"

class tst_provider : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void wireFormat();
    void plainResponse();
    void toolCallsResponse();
    void unauthorizedResponse();
    void malformedResponse();
    void noApiKey();
    void streamContent();
    void streamToolCalls();

private:
    ChatResponse callSync(LlmProvider *provider, const ChatRequest &request);
    bool waitFor(std::function<bool()> predicate, int timeoutMs = 8000);

    QTemporaryDir m_home;
    MockOpenAiServer m_server;
    Settings *m_settings = nullptr;
    OpenAiCompatProvider *m_provider = nullptr;
};

void tst_provider::initTestCase()
{
    qputenv("XDG_CONFIG_HOME", (m_home.path() + "/config").toUtf8());
    qputenv("XDG_DATA_HOME", (m_home.path() + "/data").toUtf8());
    qputenv("MOCH_API_KEY", "test-key-123");
    QVERIFY(m_server.start());
    m_settings = new Settings(this);
    m_settings->set("ai.base_url", QString("http://127.0.0.1:%1/v1").arg(m_server.port()));
    m_provider = new OpenAiCompatProvider(m_settings, this);
}

bool tst_provider::waitFor(std::function<bool()> predicate, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (predicate())
            return true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    }
    return predicate();
}

ChatResponse tst_provider::callSync(LlmProvider *provider, const ChatRequest &request)
{
    ChatResponse response;
    bool done = false;
    provider->chat(request, [&](ChatResponse r) {
        response = r;
        done = true;
    });
    waitFor([&] { return done; });
    return response;
}

void tst_provider::wireFormat()
{
    m_server.setScenario(MockOpenAiServer::PlainMessage);

    ChatRequest request;
    request.messages.append(ChatMessage{ChatMessage::System, "You are Moch.", {}, {}, {}});
    request.messages.append(ChatMessage{ChatMessage::User, "Hi", {}, {}, {}});
    ToolSpec spec;
    spec.name = "demo_tool";
    spec.description = "A demo tool";
    spec.parameters = QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}};
    request.tools.append(spec);

    const ChatResponse response = callSync(m_provider, request);
    QVERIFY2(response.error.isEmpty(), qPrintable(response.error));
    QCOMPARE(response.content, "Hello!");

    QCOMPARE(m_server.lastRequest.method, QByteArray("POST"));
    QCOMPARE(m_server.lastRequest.path, QByteArray("/v1/chat/completions"));
    QCOMPARE(m_server.lastRequest.headers.value("Authorization"), QByteArray("Bearer test-key-123"));

    const QJsonObject body = QJsonDocument::fromJson(m_server.lastRequest.body).object();
    QCOMPARE(body.value("model").toString(), QString("gpt-4o-mini"));
    QCOMPARE(body.value("tool_choice").toString(), QString("auto"));
    const QJsonArray messages = body.value("messages").toArray();
    QCOMPARE(messages.size(), 2);
    QCOMPARE(messages.at(0).toObject().value("role").toString(), QString("system"));
    QCOMPARE(messages.at(1).toObject().value("role").toString(), QString("user"));
    const QJsonObject tool = body.value("tools").toArray().at(0).toObject();
    QCOMPARE(tool.value("type").toString(), QString("function"));
    QCOMPARE(tool.value("function").toObject().value("name").toString(), QString("demo_tool"));
    QVERIFY(!m_server.lastRequest.body.contains("test-key-123"));
}

void tst_provider::plainResponse()
{
    m_server.setScenario(MockOpenAiServer::PlainMessage);
    ChatRequest request;
    request.messages.append(ChatMessage{ChatMessage::User, "Hi", {}, {}, {}});
    const ChatResponse response = callSync(m_provider, request);
    QVERIFY2(response.error.isEmpty(), qPrintable(response.error));
    QCOMPARE(response.content, "Hello!");
    QCOMPARE(response.finishReason, QString("stop"));
    QCOMPARE(response.promptTokens, 12);
}

void tst_provider::toolCallsResponse()
{
    m_server.setScenario(MockOpenAiServer::ToolCalls);
    ChatRequest request;
    request.messages.append(ChatMessage{ChatMessage::User, "Read the page", {}, {}, {}});
    const ChatResponse response = callSync(m_provider, request);
    QVERIFY2(response.error.isEmpty(), qPrintable(response.error));
    QCOMPARE(response.finishReason, QString("tool_calls"));
    const QJsonObject call = response.toolCalls.at(0).toObject();
    QCOMPARE(call.value("id").toString(), QString("call_1"));
    QCOMPARE(call.value("function").toObject().value("name").toString(),
             QString("get_page_content"));
    QCOMPARE(call.value("function").toObject().value("arguments").toString(), QString("{}"));
}

void tst_provider::unauthorizedResponse()
{
    m_server.setScenario(MockOpenAiServer::Unauthorized);
    ChatRequest request;
    request.messages.append(ChatMessage{ChatMessage::User, "Hi", {}, {}, {}});
    const ChatResponse response = callSync(m_provider, request);
    QVERIFY(response.error.contains("401"));
    QVERIFY(response.error.contains("check your API key"));
}

void tst_provider::malformedResponse()
{
    m_server.setScenario(MockOpenAiServer::Malformed);
    ChatRequest request;
    request.messages.append(ChatMessage{ChatMessage::User, "Hi", {}, {}, {}});
    const ChatResponse response = callSync(m_provider, request);
    QVERIFY(response.error.contains("bad_response"));
}

void tst_provider::noApiKey()
{
    qputenv("MOCH_API_KEY", "");
    Settings fresh(this);
    fresh.set("ai.base_url", QString("http://127.0.0.1:%1/v1").arg(m_server.port()));
    OpenAiCompatProvider provider(&fresh, this);
    ChatRequest request;
    request.messages.append(ChatMessage{ChatMessage::User, "Hi", {}, {}, {}});
    const ChatResponse response = callSync(&provider, request);
    QVERIFY(response.error.startsWith("no_api_key"));
    qputenv("MOCH_API_KEY", "test-key-123");
}

void tst_provider::streamContent()
{
    m_server.setScenario(MockOpenAiServer::StreamContent);
    QString accumulated;
    connect(m_provider, &LlmProvider::streamDelta, this,
            [&](const QString &delta) { accumulated += delta; });

    ChatRequest request;
    request.messages.append(ChatMessage{ChatMessage::User, "Hi", {}, {}, {}});
    ChatResponse response;
    bool done = false;
    m_provider->chatStream(request, [&](ChatResponse r) {
        response = r;
        done = true;
    });
    QVERIFY(waitFor([&] { return done; }));
    QVERIFY2(response.error.isEmpty(), qPrintable(response.error));
    QCOMPARE(accumulated, QString("Moch streams!"));
    QCOMPARE(response.content, QString("Moch streams!"));
    QCOMPARE(response.finishReason, QString("stop"));
    disconnect(m_provider, &LlmProvider::streamDelta, this, nullptr);
}

void tst_provider::streamToolCalls()
{
    m_server.setScenario(MockOpenAiServer::StreamToolCalls);
    ChatRequest request;
    request.messages.append(ChatMessage{ChatMessage::User, "Hi", {}, {}, {}});
    ChatResponse response;
    bool done = false;
    m_provider->chatStream(request, [&](ChatResponse r) {
        response = r;
        done = true;
    });
    QVERIFY(waitFor([&] { return done; }));
    QVERIFY2(response.error.isEmpty(), qPrintable(response.error));
    QCOMPARE(response.finishReason, QString("tool_calls"));
    const QJsonObject call = response.toolCalls.at(0).toObject();
    QCOMPARE(call.value("id").toString(), QString("call_9"));
    QCOMPARE(call.value("function").toObject().value("name").toString(),
             QString("find_elements"));
    QCOMPARE(call.value("function").toObject().value("arguments").toString(),
             QString("{\"selector\":\"a\"}"));
}

QTEST_MAIN(tst_provider)
#include "tst_provider.moc"
