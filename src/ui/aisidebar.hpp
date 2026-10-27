#pragma once

#include <QTextCursor>
#include <QTextEdit>
#include <QVariant>
#include <QWidget>

#include <functional>

#include "agent/agentservice.hpp"
#include "ai/provider.hpp"
#include "storage/sessionsstore.hpp"
#include "tools/toolregistry.hpp"

class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QTextBrowser;
class QToolButton;
class QVBoxLayout;
class RoutinesStore;
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
    // Scheduler tick for persisted routines: starts at most one due routine
    // on the shared agent; driven by a 30 s timer and a startup catch-up.
    void runDueRoutines();

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
    // request_secrets arrived: show the credential card now, or queue it
    // while another card is open.
    void onSecretBoxRequested(const QVariantList &fields, const QString &purpose,
                              SecretBoxAnswer finish);

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
    // Lazily creates the one AgentService shared by manual and routine runs.
    void ensureAgent();
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
    // In-chat credential card (request_secrets): build/show the form, resolve
    // it on Save/Cancel, and drop it when the owning task dies.
    void showSecretCard(const QVariantList &fields, const QString &purpose,
                        SecretBoxAnswer finish);
    void onSecretCardSave();
    void onSecretCardCancel();
    void collapseSecretCard();
    void cancelSecretCard();

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
    // Scheduled routines: the store, its tick timer, the header button and
    // the routine owning the task currently on the agent (0 = manual task).
    QToolButton *m_routinesButton = nullptr;
    RoutinesStore *m_routines = nullptr;
    QTimer *m_routineTimer = nullptr;
    int m_activeRoutineId = 0;

    // In-chat credential card (request_secrets): the live form, the callback
    // that resolves the pending tool, and a queue of requests that arrived
    // while a card was already on screen.
    struct PendingSecretRequest
    {
        QVariantList fields;
        QString purpose;
        SecretBoxAnswer finish;
    };
    QWidget *m_secretCard = nullptr;
    QLabel *m_secretPurpose = nullptr;
    QWidget *m_secretFormHost = nullptr;
    QVBoxLayout *m_secretRows = nullptr;
    QList<QWidget *> m_secretRowWidgets;
    QList<QLineEdit *> m_secretEdits;
    QVariantList m_secretFields;
    QPushButton *m_secretSaveButton = nullptr;
    QPushButton *m_secretCancelButton = nullptr;
    SecretBoxAnswer m_secretFinish;
    QList<PendingSecretRequest> m_secretQueue;
};
