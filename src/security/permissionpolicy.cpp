#include "permissionpolicy.hpp"

namespace Security {
namespace {

const QStringList kActionTools = {
    QStringLiteral("click_element"), QStringLiteral("fill_input"),
    QStringLiteral("select_option"), QStringLiteral("navigate_to_url"),
};

const QStringList kPaymentWords = {
    QStringLiteral("purchase"), QStringLiteral("pay"),
    QStringLiteral("donate"),   QStringLiteral("card"),
};

bool argsMentionPayment(const QJsonObject &args)
{
    for (const QString &word : kPaymentWords) {
        for (const QJsonValue &v : args) {
            if (v.isString() && v.toString().contains(word, Qt::CaseInsensitive))
                return true;
        }
    }
    return false;
}

QString paymentWord(const QJsonObject &args)
{
    for (const QString &word : kPaymentWords) {
        for (const QJsonValue &v : args) {
            if (v.isString() && v.toString().contains(word, Qt::CaseInsensitive))
                return word;
        }
    }
    return QString();
}

}

DecisionDetail decideDetailed(Permission permission, const QString &toolName, Risk risk,
                              const QJsonObject &args)
{
    const QString tool = toolName.toLower();

    // Filling a stored secret into a page always requires explicit user
    // approval, at every permission level; read-only denies it outright
    // (like other action tools).
    if (tool == QLatin1String("fill_secret")) {
        if (permission == Permission::ReadOnly)
            return {Decision::Deny,
                    QStringLiteral("read-only permission denies secret injection")};
        return {Decision::NeedsApproval,
                QStringLiteral("fill_secret injects a stored secret into a page and always "
                               "requires explicit approval")};
    }

    // Uploading local files sends user data to the site; like secret
    // injection it always requires explicit approval, and read-only denies
    // it outright. The classifier's reason lists the exact paths.
    if (tool == QLatin1String("upload_files")) {
        if (permission == Permission::ReadOnly)
            return {Decision::Deny,
                    QStringLiteral("read-only permission denies file uploads")};
        return {Decision::NeedsApproval,
                QStringLiteral("upload_files sends local files to a page and always "
                               "requires explicit approval")};
    }

    if (permission == Permission::ReadOnly) {
        if (kActionTools.contains(tool))
            return {Decision::Deny,
                    QStringLiteral("read-only permission denies action tool '%1'").arg(tool)};
        return {Decision::Allow,
                QStringLiteral("non-action tool '%1' allowed under read-only permission")
                    .arg(tool)};
    }

    if (risk == Risk::Safe)
        return {Decision::Allow,
                QStringLiteral("safe tool call '%1' allowed").arg(tool)};

    if (permission == Permission::Cautious)
        return {Decision::NeedsApproval,
                QStringLiteral("sensitive tool call '%1' requires approval under cautious "
                               "permission")
                    .arg(tool)};

    const QString word = paymentWord(args);
    if (!word.isEmpty())
        return {Decision::NeedsApproval,
                QStringLiteral("args mention '%1'; sensitive tool call '%2' requires approval "
                               "under autonomous permission")
                    .arg(word, tool)};
    return {Decision::Allow,
            QStringLiteral("sensitive tool call '%1' allowed under autonomous permission")
                .arg(tool)};
}

Decision decide(Permission permission, const QString &toolName, Risk risk, const QJsonObject &args)
{
    return decideDetailed(permission, toolName, risk, args).decision;
}

}
