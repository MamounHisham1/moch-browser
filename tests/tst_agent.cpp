#include <QElapsedTimer>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtWidgets>
#include <QtTest>

#include "agent/agentservice.hpp"
#include "ai/openaicompatprovider.hpp"
#include "browser/tabmanager.hpp"
#include "mockopenaiserver.hpp"
#include "storage/settings.hpp"
#include "tools/toolregistry.hpp"

class tst_agent : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void toolValidation();
    void toolDispatchErrors();
    void toolsReachRealTabs();
    void agentCompletesWithToolCall();
    void agentApprovalFlow();
    void agentSecretConsentOncePerTask();
    void agentReadOnlyDenies();
    void agentInvalidModelJsonStillRecovers();
    void cleanupTestCase();

private:
    ChatResponse callSync(LlmProvider *provider, const ChatRequest &request);
    bool waitFor(const std::function<bool()> &predicate, int timeoutMs = 15000);

    QTemporaryDir m_home;
    MockOpenAiServer m_server;
    Settings *m_settings = nullptr;
    OpenAiCompatProvider *m_provider = nullptr;
    TabManager *m_tabs = nullptr;
    ToolRegistry *m_registry = nullptr;
};

void tst_agent::initTestCase()
{
    qputenv("XDG_CONFIG_HOME", (m_home.path() + "/config").toUtf8());
    qputenv("XDG_DATA_HOME", (m_home.path() + "/data").toUtf8());
    qputenv("MOCH_API_KEY", "agent-test-key");
    QVERIFY(m_server.start());
    m_settings = new Settings(this);
    m_settings->set("ai.base_url", QString("http://127.0.0.1:%1/v1").arg(m_server.port()));
    m_provider = new OpenAiCompatProvider(m_settings, this);
    m_tabs = new TabManager(m_settings, nullptr, nullptr, nullptr, this);
    m_registry = new ToolRegistry(m_tabs, this);
}

void tst_agent::cleanupTestCase() { }

bool tst_agent::waitFor(const std::function<bool()> &predicate, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    return predicate();
}

void tst_agent::toolValidation()
{
    QVERIFY(!m_registry->specs().isEmpty());
    QVERIFY(m_registry->specs().size() >= 15);

    QStringList names;
    for (const ToolSpec &spec : m_registry->specs())
        names << spec.name;
    QVERIFY(names.contains("get_current_page"));
    QVERIFY(names.contains("click_element"));
    QVERIFY(names.contains("fill_input"));
    QVERIFY(names.contains("navigate_to_url"));

    QString error;
    ToolSpec clickSpec;
    for (const ToolSpec &spec : m_registry->specs())
        if (spec.name == "click_element")
            clickSpec = spec;
    QVERIFY(!m_registry->validateArgs(clickSpec, {}, &error));
    QVERIFY(error.contains("selector"));
    QVERIFY(m_registry->validateArgs(clickSpec, {{"selector", "button"}}, &error));
    QVERIFY(!m_registry->validateArgs(clickSpec, {{"selector", 42}}, &error));

    ToolResult unknown;
    m_registry->execute("does_not_exist", {}, [&](ToolResult result) { unknown = result; });
    QVERIFY(!unknown.ok);
    QCOMPARE(unknown.errorCode, QString("unknown_tool"));

    ToolResult invalid;
    m_registry->execute("click_element", {}, [&](ToolResult result) { invalid = result; });
    QVERIFY(!invalid.ok);
    QCOMPARE(invalid.errorCode, QString("invalid_arguments"));
}

void tst_agent::toolDispatchErrors()
{
    ToolResult noTab;
    m_registry->execute("get_current_page", {}, [&](ToolResult result) { noTab = result; });
    QVERIFY(!noTab.ok);
    QCOMPARE(noTab.errorCode, QString("no_tab"));

    ToolResult badScheme;
    m_registry->execute("navigate_to_url", {{"url", "file:///etc/passwd"}},
                        [&](ToolResult result) { badScheme = result; });
    QVERIFY(!badScheme.ok);
    QCOMPARE(badScheme.errorCode, QString("bad_scheme"));
}

void tst_agent::toolsReachRealTabs()
{
    const int added = m_tabs->addTab(QUrl("data:text/html,<title>ToolTab</title>"));
    QVERIFY(added >= 0);
    ToolResult result;
    m_registry->execute("get_tab_list", {}, [&](ToolResult r) { result = r; });
    QVERIFY(result.ok);
    QCOMPARE(result.data.value("tabs").toArray().size(), 1);
    QCOMPARE(result.data.value("tabs").toArray().at(0).toObject().value("index").toInt(), added);
    m_tabs->closeTab(added);
}

void tst_agent::agentCompletesWithToolCall()
{
    m_server.setScenarioQueue({MockOpenAiServer::ToolCalls, MockOpenAiServer::PlainMessage});

    AgentService agent(m_settings, m_provider, m_registry, this);
    QList<AgentService::State> states;
    connect(&agent, &AgentService::stateChanged,
            [&](AgentService::State state) { states.append(state); });

    agent.startTask("list the open tabs", QString());
    QVERIFY(waitFor([&] {
        return agent.state() == AgentService::State::Done
               || agent.state() == AgentService::State::Failed;
    }));
    QCOMPARE(agent.state(), AgentService::State::Done);
    QVERIFY(states.contains(AgentService::State::Running));
    QVERIFY(m_server.lastRequest.body.contains("get_tab_list") ||
            agent.transcriptJson().contains("get_tab_list"));
}

void tst_agent::agentApprovalFlow()
{
    m_settings->set("agent.permission_level", "cautious");
    m_server.setScenarioQueue({MockOpenAiServer::ToolCallClickSubmit,
                               MockOpenAiServer::PlainMessage});

    AgentService agent(m_settings, m_provider, m_registry, this);
    agent.startTask("click the submit button on the page", QString());

    QVERIFY(waitFor([&] { return agent.state() == AgentService::State::AwaitingApproval; }));
    QVERIFY(agent.approvalGate()->hasPending());
    agent.approvalGate()->answer(true);

    QVERIFY(waitFor([&] {
        return agent.state() == AgentService::State::Done
               || agent.state() == AgentService::State::Failed;
    }));
    QCOMPARE(agent.state(), AgentService::State::Done);
    QVERIFY(agent.transcriptJson().contains("approval"));
}

void tst_agent::agentSecretConsentOncePerTask()
{
    m_settings->set("agent.permission_level", "cautious");
    m_server.setScenarioQueue({MockOpenAiServer::ToolCallFillSecret,
                               MockOpenAiServer::ToolCallFillSecret,
                               MockOpenAiServer::PlainMessage});

    AgentService agent(m_settings, m_provider, m_registry, this);
    QList<AgentService::State> states;
    connect(&agent, &AgentService::stateChanged,
            [&](AgentService::State state) { states.append(state); });

    agent.startTask("fill the password field", QString());
    QVERIFY(waitFor([&] { return agent.state() == AgentService::State::AwaitingApproval; }));
    agent.approvalGate()->answer(true);

    QVERIFY(waitFor([&] {
        return agent.state() == AgentService::State::Done
               || agent.state() == AgentService::State::Failed;
    }));
    QCOMPARE(agent.state(), AgentService::State::Done);
    // The second fill_secret for the same secret within this task must not
    // re-prompt: consent is once per task.
    QCOMPARE(states.count(AgentService::State::AwaitingApproval), 1);
    QVERIFY(agent.transcriptJson().contains("fill_secret"));
}

void tst_agent::agentReadOnlyDenies()
{
    m_settings->set("agent.permission_level", "readonly");
    m_server.setScenarioQueue({MockOpenAiServer::ToolCallClickSubmit,
                               MockOpenAiServer::PlainMessage});

    AgentService agent(m_settings, m_provider, m_registry, this);
    agent.startTask("click something", QString());

    QVERIFY(waitFor([&] {
        return agent.state() == AgentService::State::Done
               || agent.state() == AgentService::State::Failed;
    }));
    QCOMPARE(agent.state(), AgentService::State::Done);
    QVERIFY2(agent.transcriptJson().contains("\"kind\":\"denied\""),
             "click should be denied in readonly mode");
    m_settings->set("agent.permission_level", "cautious");
}

void tst_agent::agentInvalidModelJsonStillRecovers()
{
    m_server.setScenarioQueue({MockOpenAiServer::Malformed});

    AgentService agent(m_settings, m_provider, m_registry, this);
    agent.startTask("do a thing", QString());
    QVERIFY(waitFor([&] { return agent.state() == AgentService::State::Failed; }));
    QVERIFY(!agent.transcriptJson().isEmpty());
}

QTEST_MAIN(tst_agent)
#include "tst_agent.moc"
