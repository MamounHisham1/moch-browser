#include "tools/toolregistry.hpp"

#include <QApplication>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QWebEnginePage>

#include "browser/tabmanager.hpp"
#include "browser/webview.hpp"
#include "security/secretbox.hpp"

// Registers the Secret Box agent tools. Contract: a secret's value never
// enters the model context — tools resolve names through
// Security::SecretBox after user approval and inject values into the page
// directly from C++. Every result payload carries lengths only, never
// values. See security/secretbox.hpp.

namespace {

QJsonObject toolParams(
    std::initializer_list<std::pair<const char *, const char *>> properties = {},
    std::initializer_list<const char *> required = {})
{
    QJsonObject props;
    for (const auto &[name, type] : properties)
        props.insert(name, QJsonObject{{"type", type}});
    QJsonArray req;
    for (const char *name : required)
        req.append(name);
    return QJsonObject{{"type", "object"}, {"properties", props}, {"required", req}};
}

// JSON string literal for embedding inside generated JavaScript, same
// escaping trick pagetools uses.
QString jsLiteral(const QString &text)
{
    return QString::fromUtf8(QJsonDocument(QJsonArray{text}).toJson()) + QLatin1String("[0]");
}

class ListSecretsTool final : public Tool
{
public:
    ToolSpec spec() const override
    {
        return {"list_secrets",
                "List the names of the user's stored secrets (Secret Box). Returns names "
                "only; secret values are never returned by this tool.",
                toolParams()};
    }
    void execute(const QJsonObject &, std::function<void(ToolResult)> done) override
    {
        const Security::SecretBox box;
        QJsonArray names;
        for (const QString &name : box.names())
            names.append(name);
        done({true, {}, {}, QJsonObject{{"secrets", names}}});
    }
};

class FillSecretTool final : public Tool
{
public:
    explicit FillSecretTool(ToolRegistry *registry) : m_registry(registry) {}

    ToolSpec spec() const override
    {
        return {"fill_secret",
                "Fill a stored secret (Secret Box) into an input or textarea matching the "
                "CSS selector. The value is injected by the browser itself and is never "
                "shown to the model. Requires explicit user approval.",
                toolParams({{"selector", "string"},
                            {"secret_name", "string"},
                            {"purpose", "string"},
                            {"tab_index", "number"}},
                           {"selector", "secret_name", "purpose"})};
    }

    Risk risk() const override { return Risk::Sensitive; }

    // The modal prompt can wait arbitrarily long for the user to type.
    int timeoutMs() const override { return 600000; }

    void execute(const QJsonObject &args, std::function<void(ToolResult)> done) override
    {
        TabManager *tabs = m_registry ? m_registry->tabs() : nullptr;
        WebView *view = nullptr;
        if (tabs) {
            if (args.contains("tab_index"))
                view = tabs->view(args.value("tab_index").toInt(-1));
            else
                view = tabs->currentView();
        }
        if (!view) {
            done({false, "no_tab", "no such tab", {}});
            return;
        }
        const QString selector = args.value("selector").toString();
        const QString secretName = args.value("secret_name").toString().trimmed();
        const QString purpose = args.value("purpose").toString().trimmed();
        if (secretName.isEmpty()) {
            done({false, "invalid_arguments", "secret name must not be empty", {}});
            return;
        }

        Security::SecretBox box;
        QString value;
        if (box.has(secretName)) {
            value = box.value(secretName);
        } else {
            // Secret not stored yet: ask the user directly, then keep it
            // in the vault so the redactor covers it from now on.
            QString message = QStringLiteral("The secret '%1' is not stored in the "
                                             "Secret Box yet.")
                                  .arg(secretName);
            if (!purpose.isEmpty())
                message += QStringLiteral("\n\nPurpose: %1").arg(purpose);
            message += tr("\n\nEnter the value to fill in. It will be saved in the "
                          "Secret Box, never displayed again, and never sent to the "
                          "AI model.");
            bool ok = false;
            QString provided =
                QInputDialog::getText(QApplication::activeWindow(), tr("Secret Box"),
                                      message, QLineEdit::Password, QString(), &ok);
            if (!ok) {
                done({false, "user_declined", "user did not provide the secret", {}});
                return;
            }
            provided.remove(QLatin1Char('\r'));
            provided.remove(QLatin1Char('\n'));
            if (provided.isEmpty()) {
                done({false, "user_declined", "empty value provided", {}});
                return;
            }
            box.put(secretName, provided);
            value = box.value(secretName);
        }
        if (value.isEmpty()) {
            done({false, "empty_secret", "the stored secret is empty", {}});
            return;
        }

        // Same injection mechanics as fill_input: focus, set value, fire
        // input+change events. Main frame only for v1. The script embeds
        // the real value and is therefore never logged; the JSON result
        // reports tag and length only.
        const QString script = QStringLiteral(
            "(function(){"
            "let el=null;"
            "try { el = document.querySelector(%1); } catch(e) {"
            "  return JSON.stringify({found:false, error:'bad_selector', message:String(e)}); }"
            "if (!el) return JSON.stringify({found:false});"
            "if (el.disabled) return JSON.stringify({found:false, error:'disabled', message:'element is disabled'});"
            "if (el.readOnly) return JSON.stringify({found:false, error:'readonly', message:'element is read-only'});"
            "el.focus();"
            "el.value = %2;"
            "el.dispatchEvent(new Event('input',{bubbles:true}));"
            "el.dispatchEvent(new Event('change',{bubbles:true}));"
            "return JSON.stringify({found:true, filled:{tag:el.tagName.toLowerCase(),"
            "  length: String(el.value).length}});"
            "})()")
                                   .arg(jsLiteral(selector), jsLiteral(value));
        view->page()->runJavaScript(script, 0, [done](const QVariant &result) {
            const QJsonObject data =
                QJsonDocument::fromJson(result.toString().toUtf8()).object();
            if (!data.value("found").toBool()) {
                const QString error = data.contains("error")
                                          ? data.value("error").toString()
                                          : QStringLiteral("not_found");
                const QString message = data.contains("message")
                                            ? data.value("message").toString()
                                            : QStringLiteral("no element matches");
                done({false, error, message, {}});
                return;
            }
            done({true, {}, {}, data});
        });
    }

private:
    ToolRegistry *m_registry = nullptr;
};

} // namespace

void registerSecretBoxTools(ToolRegistry *registry)
{
    if (!registry)
        return;
    registry->registerTool(new ListSecretsTool());
    registry->registerTool(new FillSecretTool(registry));
}
