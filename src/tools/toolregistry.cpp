#include "tools/toolregistry.hpp"

#include <QJsonArray>
#include <QTimer>

#include <algorithm>
#include <memory>

#include "browser/tabmanager.hpp"
#include "tools/pagetools.hpp"

ToolRegistry::ToolRegistry(TabManager *tabs, QObject *parent)
    : QObject(parent), m_tabs(tabs)
{
    registerBuiltinTools(this);
    registerSecretBoxTools(this);
#if defined(__GNUC__)
    // Weak import: null when tools/uploadfiles.cpp is not linked into this
    // target, so consumers keep linking before CMake wiring lands.
    if (registerUploadTools)
        registerUploadTools(this);
#else
    registerUploadTools(this);
#endif
}

void ToolRegistry::registerTool(Tool *tool)
{
    if (!tool || tool->spec().name.isEmpty())
        return;
    tool->setParent(this);
    m_tools.insert(tool->spec().name, tool);
}

QList<ToolSpec> ToolRegistry::specs() const
{
    QList<ToolSpec> out;
    for (Tool *tool : m_tools)
        out.append(tool->spec());
    std::sort(out.begin(), out.end(),
              [](const ToolSpec &a, const ToolSpec &b) { return a.name < b.name; });
    return out;
}

bool ToolRegistry::typeMatches(const QJsonValue &value, const QString &type) const
{
    if (type == "string")
        return value.isString();
    if (type == "number")
        return value.isDouble();
    if (type == "boolean")
        return value.isBool();
    if (type == "object")
        return value.isObject();
    if (type == "array")
        return value.isArray();
    return true;
}

bool ToolRegistry::validateArgs(const ToolSpec &spec, const QJsonObject &args,
                                QString *error) const
{
    const QJsonObject properties = spec.parameters.value("properties").toObject();
    const QJsonArray required = spec.parameters.value("required").toArray();
    for (const QJsonValue &r : required) {
        const QString key = r.toString();
        if (!args.contains(key)) {
            if (error)
                *error = QString("missing required argument '%1'").arg(key);
            return false;
        }
    }
    for (auto it = args.begin(); it != args.end(); ++it) {
        const QJsonObject prop = properties.value(it.key()).toObject();
        if (prop.isEmpty() && !properties.isEmpty()) {
            if (error)
                *error = QString("unknown argument '%1'").arg(it.key());
            return false;
        }
        const QString type = prop.value("type").toString();
        if (!type.isEmpty() && !typeMatches(it.value(), type)) {
            if (error)
                *error = QString("argument '%1' must be a %2").arg(it.key(), type);
            return false;
        }
    }
    return true;
}

void ToolRegistry::execute(const QString &name, const QJsonObject &args,
                           std::function<void(ToolResult)> done)
{
    Tool *tool = m_tools.value(name);
    if (!tool) {
        done({false, "unknown_tool", QString("tool '%1' does not exist").arg(name), {}});
        return;
    }
    QString validationError;
    if (!validateArgs(tool->spec(), args, &validationError)) {
        done({false, "invalid_arguments", validationError, {}});
        return;
    }
    auto settled = std::make_shared<bool>(false);
    QTimer *timeout = new QTimer(this);
    timeout->setSingleShot(true);
    timeout->start(tool->timeoutMs());
    connect(timeout, &QTimer::timeout, this, [done, settled, timeout] {
        if (*settled)
            return;
        *settled = true;
        timeout->deleteLater();
        done({false, "timeout", "tool execution exceeded its time limit", {}});
    });
    tool->execute(args, [done, settled, timeout](ToolResult result) {
        if (*settled)
            return;
        *settled = true;
        timeout->stop();
        timeout->deleteLater();
        done(result);
    });
}
