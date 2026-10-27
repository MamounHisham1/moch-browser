#pragma once

#include <QHash>
#include <QList>
#include <QVariant>
#include <functional>

#include "tools/tool.hpp"

class TabManager;

// Callback resolving an in-chat credential request (request_secrets):
// (true, values) with values mapping every field name to the typed text,
// or (false, {}) when the user cancels. An alias because moc
// mis-normalizes std::function<..., const T &, ...> signatures.
using SecretBoxAnswer = std::function<void(bool, const QVariantMap &)>;

class ToolRegistry : public QObject
{
    Q_OBJECT
public:
    explicit ToolRegistry(TabManager *tabs, QObject *parent = nullptr);

    void registerTool(Tool *tool);
    TabManager *tabs() const { return m_tabs; }
    QList<ToolSpec> specs() const;
    bool validateArgs(const ToolSpec &spec, const QJsonObject &args, QString *error) const;

    void execute(const QString &name, const QJsonObject &args,
                 std::function<void(ToolResult)> done);

    // Invocation helper for tools that need credentials from the user
    // (request_secrets): forwards to the secretBoxRequested signal so tools
    // never emit a foreign object's signal directly.
    void requestSecretBox(const QVariantList &fields, const QString &purpose,
                          SecretBoxAnswer finish);

signals:
    // Raised by request_secrets: the sidebar shows an in-chat credential
    // card (fields = list of {name, label, secret:bool}) and must resolve
    // finish() exactly once — (true, values) on submit where values maps
    // every field name to the typed text, or (false, {}) on cancel. Values
    // are handed to the tool only, never logged. Direct connections only:
    // std::function is not a metatype, so this must not be queued.
    void secretBoxRequested(const QVariantList &fields, const QString &purpose,
                            SecretBoxAnswer finish);

private:
    bool typeMatches(const QJsonValue &value, const QString &type) const;

    TabManager *m_tabs = nullptr;
    QHash<QString, Tool *> m_tools;
};

// Registers the Secret Box tools (list_secrets, request_secrets,
// fill_secret).
void registerSecretBoxTools(ToolRegistry *registry);

// Registers the agent file-upload tool (upload_files). Declared weak so
// targets that do not link tools/uploadfiles.cpp still link: the symbol
// resolves to null there and registration is skipped (see the guarded call
// in ToolRegistry::ToolRegistry). It activates automatically once the file
// joins a CMake target.
#if defined(__GNUC__)
__attribute__((weak)) void registerUploadTools(ToolRegistry *registry);
#else
void registerUploadTools(ToolRegistry *registry);
#endif
