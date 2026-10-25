#pragma once

#include <QObject>
#include <QString>

class ApprovalGate : public QObject
{
    Q_OBJECT
public:
    enum class Answer { Pending, Approved, Denied };

    explicit ApprovalGate(QObject *parent = nullptr);
    ~ApprovalGate() override;

    struct Request
    {
        QString toolName;
        QString reason;
        QString detail;
    };

    // Asks the UI; resolves via answer(). If nobody answers within timeoutMs,
    // the request is denied (fail-closed).
    void ask(const Request &request);
    void answer(bool approved);
    bool hasPending() const { return m_pending; }

signals:
    void approvalRequested(const QString &toolName, const QString &reason, const QString &detail);
    void resolved(bool approved);

private:
    bool m_pending = false;
};
