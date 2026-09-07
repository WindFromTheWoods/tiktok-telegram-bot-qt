/******************************************************************************
 * @file    TelegramBot.h
 * @brief   Declares resilient Telegram update polling.
 *
 * @author  Zheludchenko Yehor
 * @date    2026-08-30
 * @version 2.1.0
 *
 * @copyright Copyright (c) 2026 TikTok Telegram Bot.
 *
 * Target: Qt 6.11.2 (MSVC 2022 64-bit, C++23)
 ******************************************************************************/

#ifndef TIKTOK_TELEGRAM_BOT_TELEGRAM_BOT_H
#define TIKTOK_TELEGRAM_BOT_TELEGRAM_BOT_H

#include "telegram/TelegramTypes.h"

#include <QObject>
#include <QTimer>

class TelegramApi;

/** @brief Owns Bot API polling state, offsets, and bounded retry backoff. */
class TelegramBot final : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Constructs a poller over an externally owned API transport.
     *
     * @param[in] api Non-null API instance that must outlive this object.
     * @param[in] parent Parent object that owns this instance; may be nullptr.
     */
    explicit TelegramBot(TelegramApi* api, QObject* parent = nullptr);

    /** @brief Validates credentials and starts long polling. */
    void start();
    /** @brief Stops future polls without destroying the API transport. */
    void stop();

signals:
    /**
     * @brief Delivers a supported text message extracted from an update.
     * @param[in] message Parsed Telegram message.
     */
    void messageReceived(const TelegramMessage& message);
    /** @brief Emitted once credentials are valid and polling begins. */
    void pollingStarted();
    /**
     * @brief Reports a recoverable polling problem and selected retry delay.
     * @param[in] error Sanitized diagnostic.
     * @param[in] retryAfterSeconds Selected retry delay.
     */
    void pollingIssue(const QString& error, int retryAfterSeconds);

private slots:
    void poll();
    void onGetMeFinished(bool success, const QString& error, int retryAfterSeconds);
    void onUpdatesReceived(const QList<TelegramUpdate>& updates);
    void onUpdatesFailed(const QString& error, int retryAfterSeconds);

private:
    void schedulePoll(int delaySeconds);
    [[nodiscard]] int nextBackoffSeconds();

    TelegramApi* m_api{};
    QTimer m_pollTimer;
    qint64 m_offset{};
    int m_consecutiveFailures{};
    bool m_running{false};
    bool m_waitingForIdentity{false};
};

#endif // TIKTOK_TELEGRAM_BOT_TELEGRAM_BOT_H
