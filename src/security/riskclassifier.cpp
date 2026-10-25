#include "riskclassifier.hpp"

#include <QRegularExpression>
#include <QUrl>

namespace Security {
namespace {

const QStringList kClickKeywords = {
    QStringLiteral("submit"), QStringLiteral("purchase"), QStringLiteral("pay"),
    QStringLiteral("buy"),    QStringLiteral("checkout"), QStringLiteral("login"),
    QStringLiteral("sign-in"), QStringLiteral("post"),    QStringLiteral("comment"),
    QStringLiteral("send"),   QStringLiteral("donate"),
};

const QStringList kFillFieldKeywords = {
    QStringLiteral("password"), QStringLiteral("passwd"), QStringLiteral("otp"),
    QStringLiteral("card"),     QStringLiteral("cvv"),    QStringLiteral("expiry"),
};

QString argString(const QJsonObject &args, const QString &key)
{
    const QJsonValue v = args.value(key);
    return v.isString() ? v.toString() : QString();
}

QString matchedWord(const QString &haystack, const QStringList &words)
{
    const QString lower = haystack.toLower();
    for (const QString &w : words) {
        if (lower.contains(w, Qt::CaseInsensitive))
            return w;
    }
    return QString();
}

QString matchedArgWord(const QJsonObject &args, const QStringList &keys, const QStringList &words)
{
    for (const QString &key : keys) {
        const QString hit = matchedWord(argString(args, key), words);
        if (!hit.isEmpty())
            return QStringLiteral("%1 mentions '%2'").arg(key, hit);
    }
    return QString();
}

ClassifyDetail sensitive(const QString &reason)
{
    return {Risk::Sensitive, reason};
}

}

ClassifyDetail classifyDetailed(const QString &toolName, const QJsonObject &args)
{
    if (args.value(QLatin1String("sensitive")).toBool())
        return sensitive(QStringLiteral("args are explicitly marked sensitive"));

    const QString tool = toolName.toLower();

    if (tool == QLatin1String("click_element")) {
        const QString hit = matchedArgWord(
            args, {"text", "selector", "description", "value"}, kClickKeywords);
        if (!hit.isEmpty())
            return sensitive(QStringLiteral("click_element %1").arg(hit));
    }

    if (tool == QLatin1String("fill_input")) {
        const QString hit = matchedArgWord(args, {"field", "selector"}, kFillFieldKeywords);
        if (!hit.isEmpty())
            return sensitive(QStringLiteral("fill_input %1").arg(hit));
        const QString value = argString(args, "value");
        if (QRegularExpression(QStringLiteral("\\d{12,}")).match(value).hasMatch())
            return sensitive(QStringLiteral("fill_input value contains a run of 12+ digits"));
    }

    if (tool == QLatin1String("navigate_to_url")) {
        const QString rawUrl = argString(args, "url");
        const QString scheme = QUrl(rawUrl).scheme().toLower();
        const QStringList allowed = {QStringLiteral("http"), QStringLiteral("https"),
                                     QStringLiteral("moch")};
        if (!rawUrl.isEmpty() && !allowed.contains(scheme))
            return sensitive(QStringLiteral("navigate_to_url scheme '%1' is not http/https/moch")
                                 .arg(scheme.isEmpty() ? QStringLiteral("(none)") : scheme));
    }

    return {Risk::Safe, QStringLiteral("no sensitive indicators")};
}

Risk classify(const QString &toolName, const QJsonObject &args)
{
    return classifyDetailed(toolName, args).risk;
}

}
