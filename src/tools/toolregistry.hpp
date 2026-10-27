#pragma once

#include <QHash>
#include <QList>

#include "tools/tool.hpp"

class TabManager;

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

private:
    bool typeMatches(const QJsonValue &value, const QString &type) const;

    TabManager *m_tabs = nullptr;
    QHash<QString, Tool *> m_tools;
};

// Registers the Secret Box tools (list_secrets, fill_secret).
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
