#pragma once

#include <QTextEdit>
#include <QWidget>

#include "agent/agentservice.hpp"
#include "ai/provider.hpp"

class QLabel;
class QLineEdit;
class QPushButton;
class QToolButton;
class QVBoxLayout;
class Settings;
class TabManager;
class ToolRegistry;

class AiSidebar : public QWidget
{
    Q_OBJECT
public:
    enum class Mode { Chat, Task };

    explicit AiSidebar(Settings *settings, TabManager *tabs, ToolRegistry *tools,
                       QWidget *parent = nullptr);

    bool isExpanded() const { return m_expanded; }
    void setExpanded(bool expanded);
    void applyTheme(const QString &theme);

signals:
    void expandedChanged(bool expanded);

private slots:
    void onSubmit();
    void onStreamDelta(const QString &delta);
    void onChatFinished(const ChatResponse &response);
    void onAgentStateChanged(AgentService::State state);
    void onAgentStep(const AgentService::StepRecord &step);
    void onAgentFinished(AgentService::State state, const QString &summary);
    void onApprovalRequested(const QString &toolName, const QString &reason,
                             const QString &detail);
    void onStopClicked();

private:
    void appendMessage(const QString &role, const QString &text);
    QString currentPageContext() const;

    Settings *m_settings = nullptr;
    TabManager *m_tabs = nullptr;
    ToolRegistry *m_tools = nullptr;
    LlmProvider *m_provider = nullptr;
    AgentService *m_agent = nullptr;

    bool m_expanded = false;
    Mode m_mode = Mode::Chat;
    QTextEdit *m_transcript = nullptr;
    QLabel *m_status = nullptr;
    QLineEdit *m_input = nullptr;
    QPushButton *m_sendButton = nullptr;
    QPushButton *m_stopButton = nullptr;
    QPushButton *m_approveButton = nullptr;
    QPushButton *m_denyButton = nullptr;
    QLabel *m_title = nullptr;
    QToolButton *m_closeButton = nullptr;
    QString m_streamBuffer;
    bool m_streamActive = false;
};
