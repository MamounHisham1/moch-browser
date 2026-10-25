#pragma once

#include <QJsonObject>
#include <QString>

#include "riskclassifier.hpp"

namespace Security {

enum class Permission { ReadOnly, Cautious, Autonomous };
enum class Decision { Allow, NeedsApproval, Deny };

struct DecisionDetail
{
    Decision decision = Decision::Allow;
    QString reason;
};

Decision decide(Permission permission, const QString &toolName, Risk risk,
                const QJsonObject &args);
DecisionDetail decideDetailed(Permission permission, const QString &toolName, Risk risk,
                              const QJsonObject &args);

}
