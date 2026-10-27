#pragma once

#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>

#include "agent/approvalgate.hpp"
#include "ai/provider.hpp"
#include "tools/tool.hpp"

class LlmProvider;
class Settings;
class SessionsStore;
class ToolRegistry;

class AgentService : public QObject
{
    Q_OBJECT
public:
    enum class State { Idle, Running, AwaitingApproval, Done, Failed, Cancelled, TimedOut };
    Q_ENUM(State)

    struct StepRecord
    {
        QString kind;
        QString label;
        QString detail;
        bool ok = true;
    };

    explicit AgentService(Settings *settings, LlmProvider *provider, ToolRegistry *tools,
                          QObject *parent = nullptr);

    State state() const { return m_state; }
    QString goal() const { return m_goal; }
    ApprovalGate *approvalGate() const { return m_gate; }

    void startTask(const QString &goal, const QString &pageContext);
    void cancel();
    QString transcriptJson() const;

    // Optional persistence sink for finished tasks; when set, every finish()
    // appends one session (goal, final state, transcript, timestamps).
    void setSessionsStore(SessionsStore *store) { m_sessions = store; }

signals:
    void stateChanged(AgentService::State state);
    void stepRecorded(const AgentService::StepRecord &step);
    void taskFinished(AgentService::State finalState, const QString &summary);
    void progress(const QString &currentAction);

private:
    void setState(State state);
    void runLoop();
    void finish(State finalState, const QString &summary);
    void saveSession(State finalState);
    void step(const QString &kind, const QString &label, const QString &detail, bool ok);
    ChatRequest buildRequest() const;
    void handleModelTurn(const ChatResponse &response);
    void executeToolCall(const QJsonObject &call);
    bool checkLimits();

    Settings *m_settings = nullptr;
    LlmProvider *m_provider = nullptr;
    ToolRegistry *m_tools = nullptr;
    ApprovalGate *m_gate = nullptr;
    SessionsStore *m_sessions = nullptr;

    State m_state = State::Idle;
    QString m_goal;
    QList<ChatMessage> m_messages;
    QList<StepRecord> m_steps;
    QElapsedTimer m_taskTimer;
    QTimer *m_wallClock = nullptr;
    int m_toolCallCount = 0;
    int m_pendingCalls = 0;
    bool m_cancelRequested = false;
    // Session persistence bookkeeping: wall-clock start of the task and a
    // guard so a task that finishes twice (e.g. cancel + timeout) saves once.
    qint64 m_startedAt = 0;
    bool m_sessionSaved = false;
    // Once-per-task secret consent: names whose injection was already
    // approved (via the in-chat form or an earlier fill_secret approval).
    QSet<QString> m_approvedSecrets;
    // Wall-clock budget parking while a credential card waits on a human.
    bool m_wallClockPaused = false;
    qint64 m_wallClockRemainingMs = 0;
};
