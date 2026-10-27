#include "agent/taskdetect.hpp"

#include <QRegularExpression>
#include <QStringList>

namespace {

// Action verbs that route to the agent when they lead the message.
const QStringList kVerbs = {
    "go to", "open", "visit", "find", "search", "look up", "browse",
    "click", "press", "tap", "fill", "submit", "buy", "order", "purchase",
    "book", "compare", "check", "post", "tweet", "publish", "send",
    "message", "email", "apply", "sign up", "sign in", "log in", "log out",
    "download", "watch", "play", "follow", "subscribe", "scrape", "extract",
    "track", "monitor", "navigate", "switch to", "close the tab", "new tab",
};

// Verbs that are also recognized mid-sentence, where a politeness phrase
// hides them ("can you click the login button"). Only unambiguous action
// words qualify: "message", "order" or "book" keep their noun readings
// ("the error message says 404") and stay prefix-only so those messages
// land in chat.
const QStringList kAnywhereVerbs = {
    "click", "press", "tap", "fill", "submit", "buy", "purchase",
    "subscribe", "sign up", "sign in", "log in", "log out", "navigate",
    "tweet", "publish", "apply", "scrape", "extract",
};

// Strippable request wrappers: "can you ", "could you please ",
// "please ", "help me ", "i need you to ", "do you mind ", "hey moch, ".
const QRegularExpression &politenessRe()
{
    static const QRegularExpression re(QStringLiteral(
        "^(?:can|could|would|will)\\s+(?:you|u)\\s+(?:please\\s+)?"
        "|^(?:please|kindly)\\s+"
        "|^help\\s+me(?:\\s+to)?\\s+"
        "|^i\\s+(?:need|want)\\s+you\\s+to\\s+"
        "|^do\\s+you\\s+mind\\s+"
        "|^(?:hey\\s+)?moch[,:]\\s*"));
    return re;
}

// Informational questions stay in chat ("how do i find my api key").
// Wh-words need no '?'; auxiliaries only count as questions with one, so
// "can you click x?" is not caught here (its prefix was stripped above).
const QRegularExpression &questionRe()
{
    static const QRegularExpression re(QStringLiteral(
        "^(?:how|why|what|where|when|who|which|whose)\\b"
        "|^(?:is|are|was|were|am|do|does|did|can|could|should|would|will"
        "|has|have)\\b[^\\n]*\\?\\s*$"));
    return re;
}

// \bclick(ed|ing|s)?\b for single words, \bsign up\b for phrases.
const QRegularExpression &verbAnywhereRe()
{
    static const QRegularExpression re([] {
        QStringList parts;
        for (const QString &verb : kAnywhereVerbs) {
            const QString escaped = QRegularExpression::escape(verb);
            parts << (verb.contains(QLatin1Char(' '))
                          ? QStringLiteral("\\b%1\\b").arg(escaped)
                          : QStringLiteral("\\b%1(ed|ing|s)?\\b").arg(escaped));
        }
        return parts.join(QLatin1Char('|'));
    }());
    return re;
}

bool startsWithVerb(const QString &text)
{
    for (const QString &verb : kVerbs) {
        if (text.startsWith(verb))
            return true;
    }
    return false;
}

} // namespace

namespace TaskDetect {

bool looksLikeTask(const QString &text)
{
    const QString t = text.trimmed().toLower();
    if (t.isEmpty())
        return false;

    static const QRegularExpression urlRe(
        "(https?://|www\\.|\\b[a-z0-9-]+\\.(com|net|org|io|dev|ai|app|co)\\b)",
        QRegularExpression::CaseInsensitiveOption);
    if (urlRe.match(t).hasMatch())
        return true;

    // "click the login button" — verb leads, always a task.
    if (startsWithVerb(t))
        return true;

    // Politeness hides the verb mid-sentence ("can you click the login
    // button", "please fill in this form"); peel the prefixes and retry.
    QString stripped = t;
    for (;;) {
        const QRegularExpressionMatch match = politenessRe().match(stripped);
        if (!match.hasMatch())
            break;
        stripped = stripped.mid(match.capturedLength()).trimmed();
    }
    if (startsWithVerb(stripped))
        return true;

    // An action verb anywhere still means the user wants something done
    // ("the cookie banner keeps coming back, click accept for me").
    if (verbAnywhereRe().match(stripped).hasMatch())
        return true;

    // Clear informational questions stay in chat ("how do i find my api
    // key"); checked after the verb rules so "can you click x?" is not
    // mistaken for one. The original text is checked too: stripping a
    // "can you" prefix removes the auxiliary that marks "can you see x?"
    // as a question.
    if (questionRe().match(stripped).hasMatch()
        || questionRe().match(t).hasMatch())
        return false;

    // Default is the agent: the cost of misrouting a question there is a
    // few tool steps, while misrouting an action to chat makes the
    // assistant refuse to act. /ask remains as the explicit override.
    return true;
}

} // namespace TaskDetect
