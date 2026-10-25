#pragma once

#include <QJsonObject>
#include <QString>

namespace Security {

enum class Risk { Safe, Sensitive };

struct ClassifyDetail
{
    Risk risk = Risk::Safe;
    QString reason;
};

Risk classify(const QString &toolName, const QJsonObject &args);
ClassifyDetail classifyDetailed(const QString &toolName, const QJsonObject &args);

}
