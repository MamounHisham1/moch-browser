#include <QtWidgets>
#include <QtTest>

#include "browser/tabmanager.hpp"
#include "browser/webview.hpp"
#include "security/permissionpolicy.hpp"
#include "security/riskclassifier.hpp"
#include "security/secretbox.hpp"
#include "security/secretredactor.hpp"
#include "storage/settings.hpp"
#include "tools/toolregistry.hpp"

// Secret Box: the agent references secrets BY NAME; values live in a 0600
// JSON file, are injected into pages straight from C++ after approval, and
// are scrubbed from anything that could reach the model. These tests cover
// the store, the redaction hook, the tool payloads (names/lengths only),
// and the classification/permission decisions. No value may ever appear in
// a tool payload below.
class tst_secretbox : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void storeRoundtripAndPerms();
    void corruptAndMissingFileRecovery();
    void allValuesReturnsEverything();
    void changedSignalFires();
    void redactionCoversStoredValues();
    void listSecretsNamesOnly();
    void requestSecretsRoundtrip();
    void requestSecretsValidation();
    void fillSecretWithoutTabFailsClean();
    void fillSecretMissingPointsToChatForm();
    void fillSecretInjectionHasNoValueLeak();
    void classificationAndPolicy();

private:
    bool waitForLoad(WebView *view, int timeoutMs = 10000);
    QString runPageJs(WebView *view, const QString &source);
    void resetVault();

    QTemporaryDir m_home;
};

// Each test starts from an empty vault: drop the file and reload.
void tst_secretbox::resetVault()
{
    QFile::remove(Security::SecretBox::filePath());
    Security::SecretBox::setFilePath(Security::SecretBox::filePath());
    const Security::SecretBox box;
    QVERIFY(box.names().isEmpty());
}

void tst_secretbox::initTestCase()
{
    // Hermetic homes for settings/database and the vault file itself.
    qputenv("XDG_CONFIG_HOME", (m_home.path() + "/config").toUtf8());
    qputenv("XDG_DATA_HOME", (m_home.path() + "/data").toUtf8());
    Security::SecretBox::setFilePath(m_home.path() + "/secrets.json");
    const Security::SecretBox box;
    QVERIFY(box.names().isEmpty());
}

bool tst_secretbox::waitForLoad(WebView *view, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 2000 && !view->isLoading())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    while (view->isLoading() && timer.elapsed() < timeoutMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    return !view->isLoading();
}

QString tst_secretbox::runPageJs(WebView *view, const QString &source)
{
    QString out;
    QEventLoop loop;
    QTimer::singleShot(8000, &loop, &QEventLoop::quit);
    view->page()->runJavaScript(source, 0, [&out, &loop](const QVariant &result) {
        out = result.toString();
        loop.quit();
    });
    loop.exec();
    return out;
}

namespace {
// Qt reports a 0600 file as owner-rw AND user-rw (both flags mirror the
// same POSIX owner bits), so assert "owner can read/write, nobody else
// can do anything" instead of comparing exact flag sets.
bool ownerOnly(const QFile &file)
{
    const QFile::Permissions perms = file.permissions();
    const QFile::Permissions ownerRw = QFile::ReadOwner | QFile::WriteOwner
                                       | QFile::ReadUser | QFile::WriteUser;
    const QFile::Permissions leaked = QFile::ReadGroup | QFile::WriteGroup
                                      | QFile::ExeGroup | QFile::ReadOther
                                      | QFile::WriteOther | QFile::ExeOther
                                      | QFile::ExeUser;
    return (perms & ownerRw) == ownerRw && !(perms & leaked);
}
} // namespace

void tst_secretbox::storeRoundtripAndPerms()
{
    resetVault();
    const QString valueA = QStringLiteral("vault-roundtrip-value-777");
    const QString valueB = QStringLiteral("second-stored-value");
    Security::SecretBox box;
    box.put(QStringLiteral("github"), valueA);
    box.put(QStringLiteral("mail"), valueB + QStringLiteral("\n"));

    QVERIFY(box.has(QStringLiteral("github")));
    QCOMPARE(box.value(QStringLiteral("github")), valueA);
    // Newlines are trimmed from stored values.
    QCOMPARE(box.value(QStringLiteral("mail")), valueB);
    QCOMPARE(box.names().size(), 2);
    QVERIFY(box.names().contains(QStringLiteral("github")));

    // 0600 after create and re-asserted after every write.
    QFile file(Security::SecretBox::filePath());
    QVERIFY(file.exists());
    QVERIFY(ownerOnly(file));
    box.put(QStringLiteral("github"), valueA);
    QVERIFY(ownerOnly(file));

    // A second instance observes the same vault.
    Security::SecretBox other;
    QVERIFY(other.has(QStringLiteral("mail")));
    QCOMPARE(other.value(QStringLiteral("mail")), valueB);

    box.remove(QStringLiteral("mail"));
    QVERIFY(!box.has(QStringLiteral("mail")));
    QCOMPARE(box.names().size(), 1);
    QVERIFY(ownerOnly(file));

    // Only the JSON object of names -> values, nothing else.
    QFile readBack(Security::SecretBox::filePath());
    QVERIFY(readBack.open(QIODevice::ReadOnly));
    const QJsonDocument doc = QJsonDocument::fromJson(readBack.readAll());
    readBack.close();
    QVERIFY(doc.isObject());
    QCOMPARE(doc.object().size(), 1);
    QCOMPARE(doc.object().value(QStringLiteral("github")).toString(), valueA);
}

void tst_secretbox::corruptAndMissingFileRecovery()
{
    resetVault();
    QFile file(Security::SecretBox::filePath());
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("[1,2,3]");
    file.close();
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);

    // Corrupt file reads as an empty vault...
    Security::SecretBox box;
    QVERIFY(box.names().isEmpty());
    QVERIFY(!box.has(QStringLiteral("anything")));

    // ...and the next write rebuilds a valid vault file.
    box.put(QStringLiteral("recover"), QStringLiteral("recovered-value"));
    QVERIFY(box.has(QStringLiteral("recover")));
    QFile check(Security::SecretBox::filePath());
    QVERIFY(check.open(QIODevice::ReadOnly));
    const QJsonDocument doc = QJsonDocument::fromJson(check.readAll());
    check.close();
    QVERIFY(doc.isObject());
    QCOMPARE(doc.object().value(QStringLiteral("recover")).toString(),
             QStringLiteral("recovered-value"));
    QVERIFY(ownerOnly(check));

    QFile::remove(Security::SecretBox::filePath());
    QVERIFY(!QFile::exists(Security::SecretBox::filePath()));
    box.put(QStringLiteral("fresh"), QStringLiteral("fresh-value"));
    QVERIFY(QFile::exists(Security::SecretBox::filePath()));
    QVERIFY(ownerOnly(QFile(Security::SecretBox::filePath())));
}

void tst_secretbox::allValuesReturnsEverything()
{
    resetVault();
    const QStringList values = {QStringLiteral("alpha-secret-value"),
                                QStringLiteral("beta-secret-value"),
                                QStringLiteral("gamma-secret-value")};
    Security::SecretBox box;
    box.put(QStringLiteral("a"), values.at(0));
    box.put(QStringLiteral("b"), values.at(1));
    box.put(QStringLiteral("c"), values.at(2));

    const QStringList all = Security::SecretBox::allValues();
    for (const QString &value : values)
        QVERIFY2(all.contains(value), qPrintable(QStringLiteral("missing %1").arg(value)));
    // Redaction must be total: every stored value is visible to allValues().
    QCOMPARE(all.size(), 3);
}

void tst_secretbox::changedSignalFires()
{
    resetVault();
    Security::SecretBox box;
    QSignalSpy spy(&box, &Security::SecretBox::changed);
    box.put(QStringLiteral("sig"), QStringLiteral("signal-test-value"));
    box.put(QStringLiteral("sig"), QStringLiteral("signal-test-value-2"));
    box.remove(QStringLiteral("sig"));
    QCOMPARE(spy.count(), 3);
}

void tst_secretbox::redactionCoversStoredValues()
{
    resetVault();
    const QString value1 = QStringLiteral("super-secret-value-42");
    const QString value2 = QStringLiteral("another-hidden-value");
    Security::SecretBox box;
    box.put(QStringLiteral("k1"), value1);
    box.put(QStringLiteral("k2"), value2);

    // No setSecrets() call: the redactor must pick up vault values itself.
    Security::SecretRedactor redactor;
    const QString out = redactor.redact(
        QStringLiteral("token=super-secret-value-42 and another-hidden-value end"));
    QVERIFY(!out.contains(value1));
    QVERIFY(!out.contains(value2));
    QCOMPARE(out.count(QStringLiteral("«redacted»")), 2);

    // Generation counter: newly stored values are covered immediately.
    const QString value3 = QStringLiteral("third-secret-value-appears-later");
    box.put(QStringLiteral("k3"), value3);
    QVERIFY(!redactor.redact(QStringLiteral("x %1 y").arg(value3)).contains(value3));

    // After removal the value is no longer a stored secret, so it passes.
    box.remove(QStringLiteral("k3"));
    QCOMPARE(redactor.redact(QStringLiteral("x %1 y").arg(value3)),
             QStringLiteral("x %1 y").arg(value3));

    // The explicit setSecrets() path still works.
    Security::SecretRedactor explicitRedactor;
    explicitRedactor.setSecrets({value1});
    QVERIFY(!explicitRedactor.redact(QStringLiteral("v=%1").arg(value1)).contains(value1));
}

void tst_secretbox::listSecretsNamesOnly()
{
    resetVault();
    const QString value = QStringLiteral("LIST-VALUE-must-not-leak");
    Security::SecretBox box;
    box.put(QStringLiteral("list-acct"), value);

    ToolRegistry registry(nullptr);
    ToolResult result;
    bool settled = false;
    registry.execute("list_secrets", {}, [&result, &settled](ToolResult r) {
        result = r;
        settled = true;
    });
    QElapsedTimer wait;
    wait.start();
    while (!settled && wait.elapsed() < 5000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QVERIFY(settled);
    QVERIFY(result.ok);
    const QJsonArray secrets = result.data.value("secrets").toArray();
    QCOMPARE(secrets.size(), 1);
    QCOMPARE(secrets.at(0).toString(), QStringLiteral("list-acct"));
    // Names only — and the value appears nowhere in the payload.
    const QString payload =
        QString::fromUtf8(QJsonDocument(result.data).toJson(QJsonDocument::Compact));
    QVERIFY2(!payload.contains(value), "list_secrets leaked a secret value");
}

// request_secrets end-to-end through a fake answerer: the sidebar side of
// the secretBoxRequested signal. The tool must stay pending while the form
// is open, save secret fields into the vault, report them saved-only (name
// + length, no value), and return non-secret values by design.
void tst_secretbox::requestSecretsRoundtrip()
{
    resetVault();
    const QString secret = QStringLiteral("ROUNDTRIP-SECRET-9021");
    const QString email = QStringLiteral("user@example.com");
    ToolRegistry registry(nullptr);

    std::function<void(bool, const QVariantMap &)> finish;
    QVariantList announced;
    QString announcedPurpose;
    connect(&registry, &ToolRegistry::secretBoxRequested,
            [&](const QVariantList &fields, const QString &purpose,
                std::function<void(bool, const QVariantMap &)> f) {
                announced = fields;
                announcedPurpose = purpose;
                finish = std::move(f);
            });

    ToolResult result;
    bool settled = false;
    const QJsonArray fields{
        QJsonObject{{"name", QStringLiteral("gh-login")},
                    {"label", QStringLiteral("Password")},
                    {"secret", true}},
        QJsonObject{{"name", QStringLiteral("gh-mail")},
                    {"label", QStringLiteral("Email")},
                    {"secret", false}}};
    registry.execute("request_secrets",
                     {{"fields", fields},
                      {"purpose", QStringLiteral("sign in to github")}},
                     [&result, &settled](ToolResult r) {
                         result = r;
                         settled = true;
                     });
    // The tool waits for the human: nothing settles until finish is called.
    QVERIFY2(!settled, "request_secrets settled before the user answered");
    QCOMPARE(announced.size(), 2);
    QCOMPARE(announcedPurpose, QStringLiteral("sign in to github"));
    QCOMPARE(announced.at(0).toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("gh-login"));
    QCOMPARE(announced.at(0).toMap().value(QStringLiteral("secret")).toBool(), true);

    finish(true, {{QStringLiteral("gh-login"), secret},
                  {QStringLiteral("gh-mail"), email}});
    QVERIFY(settled);
    QVERIFY2(result.ok, qPrintable(result.errorMessage));

    const QJsonArray out = result.data.value("fields").toArray();
    QCOMPARE(out.size(), 2);
    // Secret field: saved-only record — name, saved flag, length, no value.
    const QJsonObject secretRecord = out.at(0).toObject();
    QCOMPARE(secretRecord.value("name").toString(), QStringLiteral("gh-login"));
    QCOMPARE(secretRecord.value("saved").toBool(), true);
    QCOMPARE(secretRecord.value("length").toInt(), secret.length());
    // Non-secret field: the value is returned by design.
    const QJsonObject plainRecord = out.at(1).toObject();
    QCOMPARE(plainRecord.value("name").toString(), QStringLiteral("gh-mail"));
    QCOMPARE(plainRecord.value("value").toString(), email);

    const QString payload =
        QString::fromUtf8(QJsonDocument(result.data).toJson(QJsonDocument::Compact));
    QVERIFY2(!payload.contains(secret), "request_secrets leaked a secret value");

    // The secret landed in the vault (so the redactor covers it and
    // fill_secret can inject it); the plain email was deliberately not
    // stored — it is returned to the agent instead.
    const Security::SecretBox box;
    QVERIFY(box.has(QStringLiteral("gh-login")));
    QCOMPARE(box.value(QStringLiteral("gh-login")), secret);
    QVERIFY(!box.has(QStringLiteral("gh-mail")));
}

// Argument validation and the cancel path. Validation failures settle
// synchronously; a well-formed call with nobody answering stays pending;
// a cancel resolves user_declined.
void tst_secretbox::requestSecretsValidation()
{
    resetVault();
    ToolRegistry registry(nullptr);

    const auto expectInvalid = [&registry](const QJsonObject &args, const char *what) {
        ToolResult result;
        bool settled = false;
        registry.execute("request_secrets", args,
                         [&result, &settled](ToolResult r) {
                             result = r;
                             settled = true;
                         });
        QVERIFY2(settled, what); // rejections never wait for a user
        QVERIFY2(!result.ok, what);
        QCOMPARE(result.errorCode, QStringLiteral("invalid_arguments"));
    };

    expectInvalid({}, "missing required arguments");
    expectInvalid({{"fields", QJsonArray{}}, {"purpose", "x"}}, "zero fields");
    expectInvalid({{"fields", QJsonArray{QJsonObject{{"name", QStringLiteral("")}}}},
                   {"purpose", "x"}},
                  "empty field name");
    expectInvalid({{"fields", QJsonArray{QJsonObject{{"name", QStringLiteral("a")}},
                                         QJsonObject{{"name", QStringLiteral("a")}}}},
                   {"purpose", "x"}},
                  "duplicate field names");
    QJsonArray tooMany;
    for (int i = 0; i < 7; ++i)
        tooMany.append(QJsonObject{{"name", QStringLiteral("f%1").arg(i)}});
    expectInvalid({{"fields", tooMany}, {"purpose", "x"}}, "more than six fields");
    expectInvalid({{"fields", QJsonArray{QJsonObject{{"name", QStringLiteral("a")}}}}},
                  "missing purpose");

    // A well-formed call with no answerer must still be waiting.
    bool pendingSettled = false;
    registry.execute("request_secrets",
                     {{"fields", QJsonArray{QJsonObject{{"name", QStringLiteral("a")},
                                                        {"secret", true}}}},
                      {"purpose", QStringLiteral("x")}},
                     [&pendingSettled](ToolResult) { pendingSettled = true; });
    QElapsedTimer wait;
    wait.start();
    while (wait.elapsed() < 300)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QVERIFY2(!pendingSettled, "request_secrets settled with nobody answering");

    // Cancel path: ok=false collapses into user_declined, nothing stored.
    connect(&registry, &ToolRegistry::secretBoxRequested,
            [](const QVariantList &, const QString &,
               std::function<void(bool, const QVariantMap &)> f) { f(false, {}); });
    ToolResult declined;
    bool declinedSettled = false;
    registry.execute("request_secrets",
                     {{"fields", QJsonArray{QJsonObject{{"name", QStringLiteral("b")},
                                                        {"secret", true}}}},
                      {"purpose", QStringLiteral("x")}},
                     [&declined, &declinedSettled](ToolResult r) {
                         declined = r;
                         declinedSettled = true;
                     });
    QVERIFY(declinedSettled);
    QVERIFY(!declined.ok);
    QCOMPARE(declined.errorCode, QStringLiteral("user_declined"));
    QVERIFY(!Security::SecretBox().has(QStringLiteral("b")));
}

void tst_secretbox::fillSecretWithoutTabFailsClean()
{
    ToolRegistry registry(nullptr);
    ToolResult result;
    bool settled = false;
    registry.execute("fill_secret",
                     {{"selector", QStringLiteral("#pw")},
                      {"secret_name", QStringLiteral("whatever")},
                      {"purpose", QStringLiteral("test")}},
                     [&result, &settled](ToolResult r) {
                         result = r;
                         settled = true;
                     });
    QElapsedTimer wait;
    wait.start();
    while (!settled && wait.elapsed() < 5000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QVERIFY(settled);
    QVERIFY(!result.ok);
    QCOMPARE(result.errorCode, QStringLiteral("no_tab"));
}

// With a live tab but an unknown secret name, fill_secret must fail fast
// with a pointer to the in-chat request_secrets flow — no modal may block
// the run (a QInputDialog would hang this test forever under offscreen).
void tst_secretbox::fillSecretMissingPointsToChatForm()
{
    resetVault();
    Settings settings;
    TabManager tabs(&settings, nullptr, nullptr, nullptr, this);
    ToolRegistry registry(&tabs, this);
    const int index = tabs.addTab(QUrl("data:text/html,<html><body>"
                                       "<input id='pw' type='password'>"
                                       "</body></html>"));
    QVERIFY(waitForLoad(tabs.view(index)));

    ToolResult result;
    bool settled = false;
    registry.execute("fill_secret",
                     {{"selector", QStringLiteral("#pw")},
                      {"secret_name", QStringLiteral("never-stored-name")},
                      {"purpose", QStringLiteral("sign in")}},
                     [&result, &settled](ToolResult r) {
                         result = r;
                         settled = true;
                     });
    QElapsedTimer wait;
    wait.start();
    while (!settled && wait.elapsed() < 5000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QVERIFY2(settled, "missing-secret fill_secret did not settle (modal?)");
    QVERIFY(!result.ok);
    QCOMPARE(result.errorCode, QStringLiteral("secret_missing"));
    QVERIFY(result.errorMessage.contains(QStringLiteral("request_secrets")));
}

void tst_secretbox::fillSecretInjectionHasNoValueLeak()
{
    resetVault();
    const QString value = QStringLiteral("LIVE-SECRET-abc123");
    Security::SecretBox box;
    box.put(QStringLiteral("live-token"), value);

    Settings settings;
    TabManager tabs(&settings, nullptr, nullptr, nullptr, this);
    ToolRegistry registry(&tabs, this);

    const int index = tabs.addTab(QUrl("data:text/html,<html><body>"
                                       "<input id='pw' type='password'>"
                                       "</body></html>"));
    QVERIFY(waitForLoad(tabs.view(index)));

    ToolResult result;
    bool settled = false;
    registry.execute("fill_secret",
                     {{"selector", QStringLiteral("#pw")},
                      {"secret_name", QStringLiteral("live-token")},
                      {"purpose", QStringLiteral("sign in to the test page")}},
                     [&result, &settled](ToolResult r) {
                         result = r;
                         settled = true;
                     });
    QElapsedTimer wait;
    wait.start();
    while (!settled && wait.elapsed() < 20000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QVERIFY2(settled, "fill_secret never settled");
    QVERIFY2(result.ok, qPrintable(result.errorMessage));
    QCOMPARE(result.data.value("found").toBool(), true);
    QCOMPARE(result.data.value("filled").toObject().value("length").toInt(),
             value.length());

    // The payload carries lengths only, never the value.
    const QString payload =
        QString::fromUtf8(QJsonDocument(result.data).toJson(QJsonDocument::Compact));
    QVERIFY2(!payload.contains(value), "fill_secret leaked a secret value");

    // The injection itself landed: the page input holds the value.
    QCOMPARE(runPageJs(tabs.view(index),
                       QStringLiteral("document.getElementById('pw').value")),
             value);

    // Missing element keeps fill_input's error contract.
    ToolResult miss;
    bool missSettled = false;
    registry.execute("fill_secret",
                     {{"selector", QStringLiteral("#nonexistent")},
                      {"secret_name", QStringLiteral("live-token")},
                      {"purpose", QStringLiteral("no element")}},
                     [&miss, &missSettled](ToolResult r) {
                         miss = r;
                         missSettled = true;
                     });
    wait.restart();
    while (!missSettled && wait.elapsed() < 20000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QVERIFY(missSettled);
    QVERIFY(!miss.ok);
    QCOMPARE(miss.errorCode, QStringLiteral("not_found"));
    QVERIFY(!QString::fromUtf8(QJsonDocument(miss.data).toJson()).contains(value));
}

void tst_secretbox::classificationAndPolicy()
{
    using Security::Decision;
    using Security::Permission;
    using Security::Risk;

    const QJsonObject args{{"selector", QStringLiteral("#password")},
                           {"secret_name", QStringLiteral("github-login")},
                           {"purpose", QStringLiteral("sign in to github")}};
    const Security::ClassifyDetail detail = Security::classifyDetailed("fill_secret", args);
    QCOMPARE(detail.risk, Risk::Sensitive);
    // The approval reason shows the secret NAME and purpose — never a value.
    QVERIFY(detail.reason.contains(QStringLiteral("github-login")));
    QVERIFY(detail.reason.contains(QStringLiteral("sign in to github")));

    // Read-only denies outright; every other level requires approval, even
    // when someone passes a Safe risk verdict.
    QCOMPARE(Security::decide(Permission::ReadOnly, "fill_secret", detail.risk, args),
             Decision::Deny);
    QCOMPARE(Security::decide(Permission::Cautious, "fill_secret", detail.risk, args),
             Decision::NeedsApproval);
    QCOMPARE(Security::decide(Permission::Autonomous, "fill_secret", detail.risk, args),
             Decision::NeedsApproval);
    QCOMPARE(Security::decide(Permission::Autonomous, "fill_secret", Risk::Safe, args),
             Decision::NeedsApproval);

    // list_secrets is safe everywhere, including read-only.
    const Security::ClassifyDetail list = Security::classifyDetailed("list_secrets", {});
    QCOMPARE(list.risk, Risk::Safe);
    QCOMPARE(Security::decide(Permission::ReadOnly, "list_secrets", list.risk, {}),
             Decision::Allow);
    QCOMPARE(Security::decide(Permission::Cautious, "list_secrets", list.risk, {}),
             Decision::Allow);

    // request_secrets is just a form: safe everywhere (even read-only), so
    // showing the card never trips the approval gate. The sensitive step
    // remains the later fill_secret of a stored secret.
    const QJsonObject askArgs{
        {"fields", QJsonArray{QJsonObject{{"name", QStringLiteral("pw")},
                                          {"secret", true}}}},
        {"purpose", QStringLiteral("sign in")}};
    const Security::ClassifyDetail ask =
        Security::classifyDetailed("request_secrets", askArgs);
    QCOMPARE(ask.risk, Risk::Safe);
    QCOMPARE(Security::decide(Permission::ReadOnly, "request_secrets", ask.risk, askArgs),
             Decision::Allow);
    QCOMPARE(Security::decide(Permission::Cautious, "request_secrets", ask.risk, askArgs),
             Decision::Allow);
    QCOMPARE(Security::decide(Permission::Autonomous, "request_secrets", ask.risk, askArgs),
             Decision::Allow);

    // All three tools are registered; fill_secret requires its three arguments.
    ToolRegistry registry(nullptr);
    QStringList names;
    for (const ToolSpec &spec : registry.specs())
        names << spec.name;
    QVERIFY(names.contains(QStringLiteral("list_secrets")));
    QVERIFY(names.contains(QStringLiteral("request_secrets")));
    QVERIFY(names.contains(QStringLiteral("fill_secret")));
    ToolSpec fillSpec;
    for (const ToolSpec &spec : registry.specs()) {
        if (spec.name == QLatin1String("fill_secret"))
            fillSpec = spec;
    }
    QString error;
    QVERIFY(!registry.validateArgs(fillSpec, {}, &error));
    QVERIFY(error.contains(QStringLiteral("selector")));
    QVERIFY(registry.validateArgs(fillSpec, args, &error));
}

QTEST_MAIN(tst_secretbox)
#include "tst_secretbox.moc"
