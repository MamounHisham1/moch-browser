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
    QList<ToolSpec> specs() const;
    bool validateArgs(const ToolSpec &spec, const QJsonObject &args, QString *error) const;

    void execute(const QString &name, const QJsonObject &args,
                 std::function<void(ToolResult)> done);

private:
    bool typeMatches(const QJsonValue &value, const QString &type) const;

    TabManager *m_tabs = nullptr;
    QHash<QString, Tool *> m_tools;
};
