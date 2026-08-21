#pragma once

#include <QString>
#include <QUrl>

class TikTokUrlValidator final
{
public:
    [[nodiscard]] static bool isValid(const QUrl &url);
    [[nodiscard]] static bool isValid(const QString &urlText);
};
