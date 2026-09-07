/******************************************************************************
 * @file    Logging.cpp
 * @brief   Defines the application's structured Qt logging categories.
 *
 * @author  Zheludchenko Yehor
 * @date    2026-08-30
 * @version 2.1.0
 *
 * @copyright Copyright (c) 2026 TikTok Telegram Bot.
 *
 * Target: Qt 6.11.2 (MSVC 2022 64-bit, C++23)
 ******************************************************************************/

#include "Logging.h"

#include <QRegularExpression>

Q_LOGGING_CATEGORY(logApp, "bot.app")
Q_LOGGING_CATEGORY(logTelegram, "bot.telegram")
Q_LOGGING_CATEGORY(logTikTok, "bot.tiktok")
Q_LOGGING_CATEGORY(logNetwork, "bot.network")

QString redactSensitiveLogText(QString message, const QStringList& secrets)
{
    constexpr qsizetype MinimumExactSecretLength = 4;
    constexpr auto Redacted = "[redacted]";

    for (const QString& secret : secrets)
    {
        if (secret.size() >= MinimumExactSecretLength)
            message.replace(secret, QString::fromLatin1(Redacted), Qt::CaseSensitive);
    }

    static const QRegularExpression TelegramBotToken(
        QStringLiteral(R"(\b(?:bot)?\d{6,}:[A-Za-z0-9_-]{20,}\b)"));
    message.replace(TelegramBotToken, QString::fromLatin1(Redacted));
    return message;
}
