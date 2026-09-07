/******************************************************************************
 * @file    TikTokUrlValidator.h
 * @brief   Declares strict allow-list validation for supported TikTok URLs.
 *
 * @author  Zheludchenko Yehor
 * @date    2026-08-30
 * @version 2.1.0
 *
 * @copyright Copyright (c) 2026 TikTok Telegram Bot.
 *
 * Target: Qt 6.11.2 (MSVC 2022 64-bit, C++23)
 ******************************************************************************/

#ifndef TIKTOK_TELEGRAM_BOT_TIKTOK_URL_VALIDATOR_H
#define TIKTOK_TELEGRAM_BOT_TIKTOK_URL_VALIDATOR_H

#include <QString>
#include <QUrl>

/** @brief Validates canonical and short TikTok HTTPS URLs before invoking `yt-dlp`. */
class TikTokUrlValidator final
{
public:
    /**
     * @brief Validates a parsed URL without performing network access.
     *
     * @param[in] url Candidate URL.
     * @return `true` only for a supported HTTPS TikTok host and path.
     */
    [[nodiscard]] static bool isValid(const QUrl& url);

    /**
     * @brief Parses and validates a complete URL string.
     *
     * @param[in] urlText Candidate URL text.
     * @return `true` only when the complete text is a supported TikTok URL.
     */
    [[nodiscard]] static bool isValid(const QString& urlText);
};

#endif // TIKTOK_TELEGRAM_BOT_TIKTOK_URL_VALIDATOR_H
