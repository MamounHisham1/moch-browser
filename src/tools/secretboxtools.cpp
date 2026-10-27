#include "tools/toolregistry.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QWebEnginePage>

#include <memory>
#include <utility>

#include "browser/tabmanager.hpp"
#include "browser/webview.hpp"
#include "security/secretbox.hpp"

// Registers the Secret Box agent tools. Contract: a secret's value never
// enters the model context — tools resolve names through
// Security::SecretBox after user approval and inject values into the page
// directly from C++. Every result payload carries lengths only, never
// values. request_secrets extends this with an in-chat credential form:
// the sidebar collects the values and the tool stores the secret ones in
// the vault. See security/secretbox.hpp.

namespace {

// Upper bound for one credential form; a login rarely needs more.
constexpr int kMaxSecretFields = 6;

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

// In-chat credential form (the Rakazo-style flow): the tool raises
// secretBoxRequested and stays pending until the sidebar's card resolves —
// the user's answer or a cancel. Secret fields are stored in the Secret
// Box and reported by name and length only, never by value. Non-secret
// fields (usernames, emails — values the agent must type into the page)
// are returned BY DESIGN and deliberately NOT vaulted: they are not
// secrets, the user typed them into a form addressed to the agent, and
// storing them would scrub them from every future transcript forever
// while serving no injection path (fill_input takes explicit values).
class RequestSecretsTool final : public Tool
{
public:
    explicit RequestSecretsTool(ToolRegistry *registry) : m_registry(registry) {}

    ToolSpec spec() const override
    {
        QJsonObject fieldProps;
        fieldProps.insert("name", QJsonObject{{"type", "string"}});
        fieldProps.insert("label", QJsonObject{{"type", "string"}});
        fieldProps.insert("secret", QJsonObject{{"type", "boolean"}});
        const QJsonObject field{{"type", "object"},
                                {"properties", fieldProps},
                                {"required", QJsonArray{"name"}}};
        QJsonObject properties;
        properties.insert("fields", QJsonObject{{"type", "array"}, {"items", field}});
        properties.insert("purpose", QJsonObject{{"type", "string"}});
        return {"request_secrets",
                "Show the user an in-chat credential form and wait for them to fill "
                "it. fields is a list of {name, label, secret} entries (1..6, unique "
                "non-empty names); secret=true fields are saved into the Secret Box "
                "and reported by name only, never by value; secret=false values "
                "(usernames, emails) are returned so they can be typed into the "
                "page with fill_input.",
                QJsonObject{{"type", "object"},
                            {"properties", properties},
                            {"required", QJsonArray{"fields", "purpose"}}}};
    }

    // Waiting for a human to type must not trip the per-tool timeout.
    int timeoutMs() const override { return 600000; }

    void execute(const QJsonObject &args, std::function<void(ToolResult)> done) override
    {
        const QJsonArray raw = args.value("fields").toArray();
        if (raw.isEmpty() || raw.size() > kMaxSecretFields) {
            done({false, "invalid_arguments",
                  QStringLiteral("fields must hold 1..%1 entries")
                      .arg(kMaxSecretFields),
                  {}});
            return;
        }
        QVariantList fields;
        QStringList seen;
        for (const QJsonValue &item : raw) {
            const QJsonObject field = item.toObject();
            const QString name = field.value("name").toString().trimmed();
            if (name.isEmpty()) {
                done({false, "invalid_arguments", "every field needs a name", {}});
                return;
            }
            if (seen.contains(name)) {
                done({false, "invalid_arguments",
                      QStringLiteral("duplicate field name '%1'").arg(name), {}});
                return;
            }
            seen.append(name);
            fields.append(QVariantMap{{"name", name},
                                      {"label", field.value("label").toString(name)},
                                      {"secret", field.value("secret").toBool()}});
        }
        const QString purpose = args.value("purpose").toString().trimmed();

        // The sidebar resolves exactly once; the flag absorbs stray calls.
        auto settled = std::make_shared<bool>(false);
        auto finish = [done, settled, fields, purpose](bool ok,
                                                       const QVariantMap &values) {
            if (*settled)
                return;
            *settled = true;
            if (!ok) {
                done({false, "user_declined", "user dismissed the credential form", {}});
                return;
            }
            Security::SecretBox box;
            QJsonArray entries;
            for (const QVariant &item : fields) {
                const QVariantMap field = item.toMap();
                const QString name = field.value("name").toString();
                if (!field.value("secret").toBool()) {
                    // Returned by design; not vaulted (class comment).
                    entries.append(QJsonObject{{"name", name},
                                               {"value", values.value(name).toString()}});
                    continue;
                }
                const QString value = values.value(name).toString();
                if (!value.isEmpty())
                    box.put(name, value);
                if (!box.has(name)) {
                    done({false, "empty_secret",
                          QStringLiteral("no value provided for '%1'").arg(name), {}});
                    return;
                }
                QJsonObject record{{"name", name},
                                   {"saved", true},
                                   {"length", box.value(name).length()}};
                if (value.isEmpty())
                    record.insert("reused", true); // kept the stored value
                entries.append(record);
            }
            QJsonObject data{{"fields", entries}};
            if (!purpose.isEmpty())
                data.insert("purpose", purpose);
            done({true, {}, {}, data});
        };
        if (!m_registry) {
            done({false, "no_ui", "no sidebar is available to show the form", {}});
            return;
        }
        m_registry->requestSecretBox(fields, purpose, std::move(finish));
    }

private:
    ToolRegistry *m_registry = nullptr;
};

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

    // Page injection can wait on a slow renderer; no user input is collected
    // here anymore (missing secrets point the model at request_secrets).
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
        if (!box.has(secretName)) {
            // No modal popup here: send the model to the in-chat flow so the
            // user gets a proper credential card in the sidebar and stays in
            // control of the conversation.
            done({false, "secret_missing",
                  QStringLiteral("secret '%1' is not in the Secret Box — call "
                                 "request_secrets so the user can provide it in chat")
                      .arg(secretName),
                  {}});
            return;
        }
        const QString value = box.value(secretName);
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
    registry->registerTool(new RequestSecretsTool(registry));
    registry->registerTool(new ListSecretsTool());
    registry->registerTool(new FillSecretTool(registry));
}
