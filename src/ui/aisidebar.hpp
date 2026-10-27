#pragma once

#include <QTextCursor>
#include <QTextEdit>
#include <QWidget>

#include "agent/agentservice.hpp"
#include "ai/provider.hpp"
#include "storage/sessionsstore.hpp"

class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QTextBrowser;
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
    void onHistoryAnchorClicked(const QUrl &url);
    void onStopClicked();

private:
    void appendMessage(const QString &role, const QString &text);
    void appendHtml(const QString &html);
    void renderTranscript();
    QString bubbleHtml(const QString &inner) const;
    void beginAssistantBubble();
    void finishAssistantBubble();
    void updateAssistantBubble(const QString &delta);
    bool transcriptAtBottom() const;
    void scrollTranscriptToBottom();
    QString currentPageContext() const;
    // Session history panel and read-only replay of a stored session.
    void showHistoryPanel();
    void showTranscriptPage();
    void openSession(int id);
    void renderHistoryList();
    void renderSessionTranscript(const SessionsStore::Session &session);
    // Streaming caret handling across transcript takeovers (history list or
    // session replay): detach drops the live bubble, resume rebuilds it.
    void detachStreamingBubble();
    void resumeStreamingBubble();

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
    QToolButton *m_historyButton = nullptr;
    QToolButton *m_backButton = nullptr;
    QStackedWidget *m_viewStack = nullptr;
    QTextBrowser *m_historyView = nullptr;
    SessionsStore *m_sessions = nullptr;
    QString m_streamBuffer;
    QTextCursor m_streamCursor;
    bool m_streamPlaceholder = false;
    QString m_transcriptBody;
    bool m_streamingBubble = false;
    bool m_streamActive = false;
    // Stick-to-bottom state: whether the user follows the tail, and a guard
    // distinguishing our programmatic scrollbar moves from user scrolls.
    bool m_pinnedToBottom = true;
    bool m_programmaticScroll = false;
    // Transcript takeover state: id of the replayed session (0 = live view)
    // and whether the streaming caret was detached while browsing history.
    int m_viewingSession = 0;
    bool m_streamDetached = false;
};
