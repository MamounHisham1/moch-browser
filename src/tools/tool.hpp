#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <functional>

enum class Risk { Safe, Sensitive };

struct ToolResult
{
    bool ok = true;
    QString errorCode;
    QString errorMessage;
    QJsonObject data;
};

struct ToolSpec
{
    QString name;
    QString description;
    QJsonObject parameters;
};

class Tool : public QObject
{
    Q_OBJECT
public:
    explicit Tool(QObject *parent = nullptr) : QObject(parent) {}
    virtual ToolSpec spec() const = 0;
    virtual Risk risk() const { return Risk::Safe; }
    virtual int timeoutMs() const { return 15000; }
    virtual void execute(const QJsonObject &args, std::function<void(ToolResult)> done) = 0;
};
