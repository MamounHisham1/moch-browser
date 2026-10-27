#include <QtTest>

#include "agent/taskdetect.hpp"

class tst_taskdetect : public QObject
{
    Q_OBJECT

private slots:
    void taskPhrasings();
    void chatPhrasings();
};

void tst_taskdetect::taskPhrasings()
{
    // Plain imperatives route to the agent.
    QVERIFY(TaskDetect::looksLikeTask("click the login button"));
    QVERIFY(TaskDetect::looksLikeTask("go to wikipedia.org"));
    QVERIFY(TaskDetect::looksLikeTask("search for flights to tokyo"));

    // Politeness hiding the verb mid-sentence — the chat-mode trap this
    // heuristic exists to avoid.
    QVERIFY(TaskDetect::looksLikeTask("can you click the login button"));
    QVERIFY(TaskDetect::looksLikeTask("could you please fill in this form?"));
    QVERIFY(TaskDetect::looksLikeTask("please click accept on the cookie banner"));
    QVERIFY(TaskDetect::looksLikeTask("I need you to click checkout"));
    QVERIFY(TaskDetect::looksLikeTask("would you mind pressing the export button"));
    QVERIFY(TaskDetect::looksLikeTask("hey moch, click the first result"));

    // Action verb anywhere in the sentence.
    QVERIFY(TaskDetect::looksLikeTask(
        "the cookie banner keeps coming back, click accept for me"));
    QVERIFY(TaskDetect::looksLikeTask(
        "the submit button looks broken, press it anyway"));

    // URLs always route to the agent.
    QVERIFY(TaskDetect::looksLikeTask("open https://example.com/docs"));
    QVERIFY(TaskDetect::looksLikeTask("what is on example.com these days?"));
}

void tst_taskdetect::chatPhrasings()
{
    // Clear informational questions stay in chat; everything else defaults
    // to the agent, so only these and the empty message return false.
    QVERIFY(!TaskDetect::looksLikeTask("how do I find my API key"));
    QVERIFY(!TaskDetect::looksLikeTask("what does this page say about pricing?"));
    QVERIFY(!TaskDetect::looksLikeTask("is there a discount code on this page?"));
    QVERIFY(!TaskDetect::looksLikeTask("can you see the search box?"));
    QVERIFY(!TaskDetect::looksLikeTask(""));

    // The inverted default: statements with no action verb still reach the
    // agent rather than dying in chat.
    QVERIFY(TaskDetect::looksLikeTask("the error message says 404"));
    QVERIFY(TaskDetect::looksLikeTask("summarize this page"));
    QVERIFY(TaskDetect::looksLikeTask("get me flights to tokyo"));
}

QTEST_MAIN(tst_taskdetect)
#include "tst_taskdetect.moc"
