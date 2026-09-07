/******************************************************************************
 * @file    CredentialStore.h
 * @brief   Declares secure persistence for application secrets.
 *
 * @author  Zheludchenko Yehor
 * @date    2026-08-30
 * @version 2.1.0
 *
 * @copyright Copyright (c) 2026 TikTok Telegram Bot.
 *
 * Target: Qt 6.11.2 (MSVC 2022 64-bit, C++23)
 ******************************************************************************/

#ifndef TIKTOK_TELEGRAM_BOT_CONFIG_CREDENTIAL_STORE_H
#define TIKTOK_TELEGRAM_BOT_CONFIG_CREDENTIAL_STORE_H

#include <QByteArray>
#include <QString>

/** @brief Stores named secrets in the native credential facility when available. */
class CredentialStore final
{
public:
    /**
     * @brief Reads the Telegram bot token.
     *
     * @param[out] error Receives a storage diagnostic; may be nullptr.
     * @return The token, or an empty string when it is absent or cannot be read.
     */
    [[nodiscard]] static QString readTelegramBotToken(QString* error = nullptr);

    /**
     * @brief Writes or replaces the Telegram bot token.
     *
     * @param[in] token Secret value to store.
     * @param[out] error Receives a storage diagnostic; may be nullptr.
     * @return `true` on success.
     */
    [[nodiscard]] static bool writeTelegramBotToken(const QString& token, QString* error = nullptr);

    /**
     * @brief Reads the Telegram application API hash used by TDLib.
     *
     * @param[out] error Receives a storage diagnostic; may be nullptr.
     * @return The API hash, or an empty string when unavailable.
     */
    [[nodiscard]] static QString readTdApiHash(QString* error = nullptr);

    /**
     * @brief Writes or replaces the Telegram application API hash.
     *
     * @param[in] apiHash Secret value to store.
     * @param[out] error Receives a storage diagnostic; may be nullptr.
     * @return `true` on success.
     */
    [[nodiscard]] static bool writeTdApiHash(const QString& apiHash, QString* error = nullptr);

    /**
     * @brief Reads the binary key that encrypts the local TDLib database.
     *
     * @param[out] error Receives a storage diagnostic; may be nullptr.
     * @return The encryption key, or an empty array when unavailable.
     */
    [[nodiscard]] static QByteArray readTdDatabaseEncryptionKey(QString* error = nullptr);

    /**
     * @brief Writes or replaces the TDLib database encryption key.
     *
     * @param[in] key Binary encryption key to store.
     * @param[out] error Receives a storage diagnostic; may be nullptr.
     * @return `true` on success.
     */
    [[nodiscard]] static bool writeTdDatabaseEncryptionKey(const QByteArray& key,
                                                           QString* error = nullptr);

    /** @return `true` when secrets use an operating-system credential facility. */
    [[nodiscard]] static bool usesSecureStorage() noexcept;
    /** @return A user-facing description of the active storage backend. */
    [[nodiscard]] static QString storageDescription();
};

#endif // TIKTOK_TELEGRAM_BOT_CONFIG_CREDENTIAL_STORE_H
