/******************************************************************************
 * @file    TelegramTypes.h
 * @brief   Defines value types shared by the Telegram Bot API components.
 *
 * @author  Zheludchenko Yehor
 * @date    2026-08-30
 * @version 2.1.0
 *
 * @copyright Copyright (c) 2026 TikTok Telegram Bot.
 *
 * Target: Qt 6.11.2 (MSVC 2022 64-bit, C++23)
 ******************************************************************************/

#ifndef TIKTOK_TELEGRAM_BOT_TELEGRAM_TYPES_H
#define TIKTOK_TELEGRAM_BOT_TELEGRAM_TYPES_H

#include <QDateTime>
#include <QJsonValue>
#include <QString>
#include <QUrl>

#include <optional>

/** @brief A text message relevant to the bot's command and URL workflow. */
struct TelegramMessage
{
    qint64 chatId{}; /**< Chat to which the application can send a response. */
    qint64 userId{}; /**< Author identifier used for authorization. */
    QString text;    /**< Message text received from Telegram. */
};

/** @brief One Telegram update with an optional supported message payload. */
struct TelegramUpdate
{
    qint64 updateId{};                      /**< Monotonic Bot API update identifier. */
    std::optional<TelegramMessage> message; /**< Parsed message, when supported. */
};

/** @brief Normalized result of a Telegram Bot API request. */
struct TelegramApiResponse
{
    bool ok{false};           /**< Whether Telegram accepted the request. */
    bool explicitRejection{}; /**< Well-formed API 4xx rejection, not an inferred HTTP error. */
    QJsonValue result;        /**< Successful Bot API result payload. */
    int errorCode{};          /**< Telegram error code, or zero on success. */
    QString description;      /**< Sanitized Telegram or transport diagnostic. */
    int retryAfterSeconds{};  /**< Server-requested retry delay, or zero. */
};

/** @brief Legacy in-memory description of one requested download. */
struct DownloadJob
{
    qint64 requestChatId{};   /**< Chat to notify about progress. */
    qint64 requestUserId{};   /**< User that submitted the URL. */
    QUrl sourceUrl;           /**< Validated TikTok source URL. */
    QDateTime requestedAtUtc; /**< Submission time in UTC. */
};

#endif // TIKTOK_TELEGRAM_BOT_TELEGRAM_TYPES_H
