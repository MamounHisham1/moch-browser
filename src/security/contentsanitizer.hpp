#pragma once

#include <QString>

namespace Security {

struct SanitizeResult
{
    QString text;
    int originalLength = 0;
    int truncated = 0;
};

SanitizeResult sanitize(const QString &rawHtml, const QString &sourceUrl, int maxChars = 24000);

}
