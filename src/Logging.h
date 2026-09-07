/******************************************************************************
 * @file    Logging.h
 * @brief   Declares the application's structured Qt logging categories.
 *
 * @author  Zheludchenko Yehor
 * @date    2026-08-30
 * @version 2.1.0
 *
 * @copyright Copyright (c) 2026 TikTok Telegram Bot.
 *
 * Target: Qt 6.11.2 (MSVC 2022 64-bit, C++23)
 ******************************************************************************/

#ifndef TIKTOK_TELEGRAM_BOT_LOGGING_H
#define TIKTOK_TELEGRAM_BOT_LOGGING_H

#include <QLoggingCategory>
#include <QStringList>

/** @brief Logs application lifecycle and orchestration events. */
Q_DECLARE_LOGGING_CATEGORY(logApp)
/** @brief Logs Bot API polling and TDLib publication events. */
Q_DECLARE_LOGGING_CATEGORY(logTelegram)
/** @brief Logs TikTok URL validation and download activity. */
Q_DECLARE_LOGGING_CATEGORY(logTikTok)
/** @brief Logs network failures and retry activity. */
Q_DECLARE_LOGGING_CATEGORY(logNetwork)

/**
 * @brief Redacts credentials before text is persisted or shown as a notification.
 * @param[in] message Log message that may contain sensitive values.
 * @param[in] secrets Additional exact values to redact.
 * @return A safe copy of the message.
 */
[[nodiscard]] QString redactSensitiveLogText(QString message, const QStringList& secrets = {});

#endif // TIKTOK_TELEGRAM_BOT_LOGGING_H
