/******************************************************************************
 * @file    TikTokUrlValidator.cpp
 * @brief   Implements strict allow-list validation for TikTok URLs.
 *
 * @author  Zheludchenko Yehor
 * @date    2026-08-30
 * @version 2.1.0
 *
 * @copyright Copyright (c) 2026 TikTok Telegram Bot.
 *
 * Target: Qt 6.11.2 (MSVC 2022 64-bit, C++23)
 ******************************************************************************/

#include "tiktok/TikTokUrlValidator.h"

#include <QRegularExpression>
#include <QStringList>

namespace
{

bool isValidMainTikTokPath(const QStringList& segments)
{
    static const QRegularExpression videoIdPattern(QStringLiteral(R"(^[0-9]+$)"));

    return segments.size() == 3 && segments.at(0).startsWith(QLatin1Char('@')) &&
           segments.at(0).size() > 1 && segments.at(1) == QStringLiteral("video") &&
           videoIdPattern.match(segments.at(2)).hasMatch();
}

bool isValidShortTikTokPath(const QStringList& segments)
{
    static const QRegularExpression shortCodePattern(QStringLiteral(R"(^[A-Za-z0-9_-]+$)"));
    return segments.size() == 1 && shortCodePattern.match(segments.constFirst()).hasMatch();
}

} // namespace

bool TikTokUrlValidator::isValid(const QUrl& url)
{
    static const QRegularExpression encodedPathSeparator(QStringLiteral(R"(%(?:2f|5c))"),
                                                         QRegularExpression::CaseInsensitiveOption);

    if (!url.isValid() || url.isRelative() ||
        url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0 ||
        !url.userInfo().isEmpty())
    {
        return false;
    }

    if (url.port(-1) != -1 && url.port() != 443)
    {
        return false;
    }

    if (encodedPathSeparator.match(url.path(QUrl::FullyEncoded)).hasMatch())
    {
        return false;
    }

    const QString host = url.host(QUrl::FullyDecoded).toLower();
    const QStringList segments =
        url.path(QUrl::FullyDecoded).split(QLatin1Char('/'), Qt::SkipEmptyParts);

    if (host == QStringLiteral("tiktok.com") || host == QStringLiteral("www.tiktok.com"))
    {
        return isValidMainTikTokPath(segments);
    }

    if (host == QStringLiteral("vm.tiktok.com") || host == QStringLiteral("vt.tiktok.com"))
    {
        return isValidShortTikTokPath(segments);
    }

    return false;
}

bool TikTokUrlValidator::isValid(const QString& urlText)
{
    if (urlText != urlText.trimmed() || urlText.isEmpty())
    {
        return false;
    }
    return isValid(QUrl(urlText, QUrl::StrictMode));
}
