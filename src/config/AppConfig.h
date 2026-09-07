/******************************************************************************
 * @file    AppConfig.h
 * @brief   Declares validated runtime configuration for the Telegram bot pipeline.
 *
 * @author  Zheludchenko Yehor
 * @date    2026-08-30
 * @version 2.1.0
 *
 * @copyright Copyright (c) 2026 TikTok Telegram Bot.
 *
 * Target: Qt 6.11.2 (MSVC 2022 64-bit, C++23)
 ******************************************************************************/

#ifndef TIKTOK_TELEGRAM_BOT_CONFIG_APP_CONFIG_H
#define TIKTOK_TELEGRAM_BOT_CONFIG_APP_CONFIG_H

#include <QString>

#include <chrono>
#include <optional>

/** @brief Immutable, validated configuration required to run the Bot API workflow. */
class AppConfig final
{
public:
    /**
     * @brief Creates configuration from supported legacy environment variables.
     *
     * @param[out] error Receives all validation failures; may be nullptr.
     * @return Valid configuration, or `std::nullopt` on validation failure.
     */
    [[nodiscard]] static std::optional<AppConfig> fromEnvironment(QString* error = nullptr);

    /**
     * @brief Validates explicit UI or persisted values and creates configuration.
     *
     * @param[in] botToken Telegram bot token.
     * @param[in] channelId Default `@username` or negative numeric channel identifier.
     * @param[in] adminUserId Positive Telegram user identifier allowed to submit URLs.
     * @param[in] ytDlpExecutable Executable name or path; empty selects `yt-dlp`.
     * @param[in] publicationIntervalMinutes Fallback interval; empty selects 120 minutes.
     * @param[out] error Receives all validation failures; may be nullptr.
     * @return Valid configuration, or `std::nullopt` on validation failure.
     */
    [[nodiscard]] static std::optional<AppConfig>
    fromValues(QString botToken, QString channelId, QString adminUserId, QString ytDlpExecutable,
               QString publicationIntervalMinutes, QString* error = nullptr);

    /** @return The validated Telegram bot token. */
    [[nodiscard]] const QString& botToken() const noexcept;
    /** @return The validated default Telegram destination. */
    [[nodiscard]] const QString& channelId() const noexcept;
    /** @return The only Telegram user identifier allowed to submit work. */
    [[nodiscard]] qint64 adminUserId() const noexcept;
    /** @return The configured `yt-dlp` executable name or path. */
    [[nodiscard]] const QString& ytDlpExecutable() const noexcept;
    /** @return The fixed fallback interval between publications. */
    [[nodiscard]] std::chrono::minutes publicationInterval() const noexcept;

private:
    AppConfig(QString botToken, QString channelId, qint64 adminUserId, QString ytDlpExecutable,
              std::chrono::minutes publicationInterval);

    QString m_botToken;
    QString m_channelId;
    qint64 m_adminUserId{};
    QString m_ytDlpExecutable;
    std::chrono::minutes m_publicationInterval;
};

#endif // TIKTOK_TELEGRAM_BOT_CONFIG_APP_CONFIG_H
