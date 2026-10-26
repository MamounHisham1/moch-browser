#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTcpSocket>
#include <QUrlQuery>
#include <QtTest>

#include "ai/modelcatalog.hpp"
#include "browser/openrouterauth.hpp"
#include "browser/setupbridge.hpp"
#include "storage/settings.hpp"

class tst_setup : public QObject
{
    Q_OBJECT
private slots:
    void catalogParse();
    void catalogFallback();
    void useCaseCatalogComplete();
    void bridgeSelectionRoundtrip();
    void bridgeFinishMarksComplete();
    void bridgeApiKeyStored();
    void oauthServerDeliversCode();

private:
    QTemporaryDir m_home;
};

void tst_setup::catalogParse()
{
    const QByteArray body = R"JSON({
        "data": [
            {"id": "openai/gpt-4o-mini", "name": "GPT-4o mini",
             "context_length": 128000,
             "pricing": {"prompt": "0.00000015", "completion": "0.0000006"}},
            {"id": "meta-llama/llama-3.1-8b-instruct:free", "name": "Llama 3.1 8B (free)",
             "context_length": 131072,
             "pricing": {"prompt": "0", "completion": "0"}},
            {"id": "bad-entry-no-slash"}
        ]})JSON";
    const QJsonArray models = ModelCatalog::parse(body);
    QCOMPARE(models.size(), 3);

    const QJsonObject first = models.at(0).toObject();
    QCOMPARE(first.value("id").toString(), QString("openai/gpt-4o-mini"));
    QCOMPARE(first.value("provider").toString(), QString("openai"));
    QCOMPARE(first.value("free").toBool(), false);
    QCOMPARE(first.value("context").toInt(), 128000);

    const QJsonObject free = models.at(1).toObject();
    QCOMPARE(free.value("free").toBool(), true);
    QCOMPARE(free.value("provider").toString(), QString("meta-llama"));
}

void tst_setup::catalogFallback()
{
    const QJsonArray models = ModelCatalog::fallbackModels();
    QVERIFY(models.size() >= 8);
    QStringList ids;
    for (const QJsonValue &value : models)
        ids << value.toObject().value("id").toString();
    QVERIFY2(ids.contains("openai/gpt-4o-mini"), "curated list includes a cheap default");
    QVERIFY(std::any_of(ids.cbegin(), ids.cend(),
                        [](const QString &id) { return id.contains(":free"); }));
}

void tst_setup::useCaseCatalogComplete()
{
    const QJsonArray useCases = SetupBridge::useCaseCatalog();
    QCOMPARE(useCases.size(), 16);
    QSet<QString> ids;
    int platformCount = 0;
    for (const QJsonValue &value : useCases) {
        const QJsonObject uc = value.toObject();
        ids.insert(uc.value("id").toString());
        platformCount += uc.value("platforms").toArray().size();
        for (const QJsonValue &p : uc.value("platforms").toArray()) {
            QVERIFY(!p.toObject().value("loginUrl").toString().isEmpty());
            QVERIFY(p.toObject().value("loginUrl").toString().startsWith("https://")
                    || p.toObject().value("loginUrl").toString().startsWith("http://"));
        }
    }
    QVERIFY(ids.contains("jobs"));
    QVERIFY(ids.contains("social"));
    QVERIFY(platformCount >= 60);
}

void tst_setup::bridgeSelectionRoundtrip()
{
    qputenv("XDG_CONFIG_HOME", (m_home.path() + "/config").toUtf8());
    qputenv("XDG_DATA_HOME", (m_home.path() + "/data").toUtf8());
    Settings settings;
    SetupBridge bridge(&settings, nullptr, this);

    QCOMPARE(bridge.getSelectedUseCases().size(), 0);
    bridge.setSelectedUseCases(QVariantList{"jobs", "social"});
    QCOMPARE(bridge.getSelectedUseCases().size(), 2);
    QCOMPARE(bridge.getSelectedUseCases().at(0).toString(), QString("jobs"));

    bridge.setPlatformSignedIn("linkedin", true);
    bridge.setPlatformSignedIn("x", true);
    QVERIFY(bridge.getPlatformStates().value("linkedin").toBool());
    bridge.setPlatformSignedIn("linkedin", false);
    QCOMPARE(bridge.getPlatformStates().value("linkedin").toBool(), false);
    QVERIFY(bridge.getPlatformStates().value("x").toBool());

    bridge.saveAiConfig("openai-compat", "https://openrouter.ai/api/v1", "openai/gpt-4o-mini");
    QCOMPARE(settings.get("ai.model").toString(), QString("openai/gpt-4o-mini"));
    QCOMPARE(settings.get("ai.base_url").toString(), QString("https://openrouter.ai/api/v1"));
}

void tst_setup::bridgeFinishMarksComplete()
{
    Settings settings;
    SetupBridge bridge(&settings, nullptr, this);
    QCOMPARE(settings.get("setup_complete", false).toBool(), false);
    QSignalSpy finishedSpy(&bridge, &SetupBridge::finished);
    bridge.finish();
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(settings.get("setup_complete").toBool(), true);
}

void tst_setup::bridgeApiKeyStored()
{
    Settings settings;
    SetupBridge bridge(&settings, nullptr, this);
    bridge.saveApiKey("sk-or-test-key-123");
    QCOMPARE(settings.apiKey(), QString("sk-or-test-key-123"));
    const QString summary = bridge.getAiSummary();
    const QJsonObject parsed = QJsonDocument::fromJson(summary.toUtf8()).object();
    QCOMPARE(parsed.value("hasKey").toBool(), true);
    QVERIFY(!summary.contains("sk-or-test-key-123"));
}

void tst_setup::oauthServerDeliversCode()
{
    OpenRouterAuth auth;
    QVERIFY(auth.start());
    QVERIFY(auth.authUrl().contains("openrouter.ai/auth"));
    QVERIFY(auth.authUrl().contains("127.0.0.1"));

    QSignalSpy codeSpy(&auth, &OpenRouterAuth::codeReceived);
    const QUrl callback(
        QUrlQuery(QUrl(auth.authUrl()).query()).queryItemValue("callback_url"));
    const int port = callback.port();

    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, port);
    QVERIFY(socket.waitForConnected(3000));
    socket.write("GET /callback?code=abc123 HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");
    QVERIFY(codeSpy.wait(4000));
    QCOMPARE(codeSpy.at(0).at(0).toString(), QString("abc123"));
    auth.stop();
}

QTEST_MAIN(tst_setup)
#include "tst_setup.moc"
