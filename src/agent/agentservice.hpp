#pragma once

#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTimer>

#include "agent/approvalgate.hpp"
#include "ai/provider.hpp"
#include "tools/tool.hpp"

class LlmProvider;
class Settings;
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

signals:
    void stateChanged(AgentService::State state);
    void stepRecorded(const AgentService::StepRecord &step);
    void taskFinished(AgentService::State finalState, const QString &summary);
    void progress(const QString &currentAction);

private:
    void setState(State state);
    void runLoop();
    void finish(State finalState, const QString &summary);
    void step(const QString &kind, const QString &label, const QString &detail, bool ok);
    ChatRequest buildRequest() const;
    void handleModelTurn(const ChatResponse &response);
    void executeToolCall(const QJsonObject &call);
    bool checkLimits();

    Settings *m_settings = nullptr;
    LlmProvider *m_provider = nullptr;
    ToolRegistry *m_tools = nullptr;
    ApprovalGate *m_gate = nullptr;

    State m_state = State::Idle;
    QString m_goal;
    QList<ChatMessage> m_messages;
    QList<StepRecord> m_steps;
    QElapsedTimer m_taskTimer;
    QTimer *m_wallClock = nullptr;
    int m_toolCallCount = 0;
    int m_pendingCalls = 0;
    bool m_cancelRequested = false;
};
