#pragma once

#include <QString>

namespace TaskDetect {

// Router for the AI sidebar: does this message ask the assistant to do
// something on the web (agent mode) or ask a question about the current
// page (chat mode)? Deliberately biased toward "task": a misrouted task
// costs a few extra tool steps, but a misrouted chat makes the assistant
// claim it cannot act at all.
bool looksLikeTask(const QString &text);

} // namespace TaskDetect
