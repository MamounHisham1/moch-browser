#include "agent/approvalgate.hpp"

ApprovalGate::ApprovalGate(QObject *parent)
    : QObject(parent)
{
}

ApprovalGate::~ApprovalGate() = default;

void ApprovalGate::ask(const Request &request)
{
    m_pending = true;
    emit approvalRequested(request.toolName, request.reason, request.detail);
}

void ApprovalGate::answer(bool approved)
{
    if (!m_pending)
        return;
    m_pending = false;
    emit resolved(approved);
}
