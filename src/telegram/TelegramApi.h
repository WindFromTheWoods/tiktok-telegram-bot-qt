/******************************************************************************
 * @file    TelegramApi.h
 * @brief   Declares the asynchronous Telegram Bot API transport.
 *
 * @author  Zheludchenko Yehor
 * @date    2026-08-30
 * @version 2.1.0
 *
 * @copyright Copyright (c) 2026 TikTok Telegram Bot.
 *
 * Target: Qt 6.11.2 (MSVC 2022 64-bit, C++23)
 ******************************************************************************/

#ifndef TIKTOK_TELEGRAM_BOT_TELEGRAM_API_H
#define TIKTOK_TELEGRAM_BOT_TELEGRAM_API_H

#include "telegram/TelegramTypes.h"

#include <QByteArray>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSet>

class QNetworkReply;

/** @brief Evidence retained after one upload, including an ambiguous delivery outcome. */
struct TelegramVideoResult
{
    bool success{};          /**< Whether Telegram returned usable delivery evidence. */
    bool deliveryUnknown{};  /**< Whether retrying could create a duplicate message. */
    QString error;           /**< Sanitized transport or API diagnostic. */
    QString category;        /**< Stable failure classification used by recovery UI. */
    int retryAfterSeconds{}; /**< Telegram-requested delay, or zero. */
    QString chatId;          /**< Confirmed destination identifier. */
    QString messageId;       /**< Confirmed Telegram message identifier. */
};

Q_DECLARE_METATYPE(TelegramVideoResult)

/** @brief Performs non-blocking Bot API calls and normalizes Telegram responses. */
class TelegramApi final : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Constructs a Bot API client.
     *
     * @param[in] botToken Secret BotFather token used only to construct request URLs.
     * @param[in] parent Parent object that owns this instance; may be nullptr.
     */
    explicit TelegramApi(QString botToken, QObject* parent = nullptr);

    /** @brief Validates the configured token with `getMe`. */
    void getMe();
    /**
     * @brief Starts a long-poll request.
     *
     * @param[in] offset First update identifier to return.
     * @param[in] timeoutSeconds Telegram long-poll timeout.
     */
    void getUpdates(qint64 offset, int timeoutSeconds = 30);
    /**
     * @brief Sends a plain-text message.
     *
     * @param[in] chatId Destination chat identifier.
     * @param[in] text Message text.
     */
    void sendMessage(qint64 chatId, const QString& text);
    /**
     * @brief Uploads and sends a local MP4 video.
     *
     * @param[in] chatId Destination username or numeric identifier.
     * @param[in] filePath Local file path.
     * @param[in] caption Optional caption; empty sends no text below the video.
     */
    void sendVideo(const QString& chatId, const QString& filePath, const QString& caption);
    /** @brief Aborts active replies and prevents new completion work. */
    void shutdown();

    /**
     * @brief Parses a response independently of the network transport.
     *
     * @param[in] payload Raw JSON response bytes.
     * @param[in] httpStatusCode HTTP response status.
     * @param[in] networkError Optional Qt network error string.
     * @return Normalized response including Telegram retry information.
     */
    [[nodiscard]] static TelegramApiResponse
    parseResponse(const QByteArray& payload, int httpStatusCode, const QString& networkError = {});
    /** @return Conservative delivery evidence for a normalized upload response. */
    [[nodiscard]] static TelegramVideoResult uploadResult(const TelegramApiResponse& response);

signals:
    /**
     * @brief Reports completion of bot identity validation.
     * @param[in] success Whether Telegram accepted the token.
     * @param[in] error Sanitized diagnostic, or empty on success.
     * @param[in] retryAfterSeconds Server-requested retry delay, or zero.
     */
    void getMeFinished(bool success, const QString& error, int retryAfterSeconds);
    /**
     * @brief Delivers successfully parsed updates in server order.
     * @param[in] updates Parsed Bot API updates.
     */
    void updatesReceived(const QList<TelegramUpdate>& updates);
    /**
     * @brief Reports a polling error and suggested retry delay.
     * @param[in] error Sanitized diagnostic.
     * @param[in] retryAfterSeconds Server-requested retry delay, or zero.
     */
    void updatesFailed(const QString& error, int retryAfterSeconds);
    /**
     * @brief Reports completion of a text message request.
     * @param[in] chatId Destination chat identifier.
     * @param[in] success Whether Telegram accepted the message.
     * @param[in] error Sanitized diagnostic, or empty on success.
     */
    void messageSent(qint64 chatId, bool success, const QString& error);
    /**
     * @brief Reports completion of a video upload request.
     * @param[in] success Whether Telegram accepted the video.
     * @param[in] error Sanitized diagnostic, or empty on success.
     */
    void videoSent(bool success, const QString& error);
    /** @brief Reports identifiers, retry delay and uncertainty for a video upload. */
    void videoCompleted(const TelegramVideoResult& result);

private:
    [[nodiscard]] QNetworkRequest makeRequest(const QString& method) const;
    [[nodiscard]] TelegramApiResponse finishReply(QNetworkReply* reply);
    [[nodiscard]] static QList<TelegramUpdate> parseUpdates(const QJsonValue& value,
                                                            QString* error);
    void trackReply(QNetworkReply* reply);

    QString m_baseUrl;
    QNetworkAccessManager m_network;
    QSet<QNetworkReply*> m_activeReplies;
    bool m_shuttingDown{false};
};

#endif // TIKTOK_TELEGRAM_BOT_TELEGRAM_API_H
