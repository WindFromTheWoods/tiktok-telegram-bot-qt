/******************************************************************************
 * @file    BotController.h
 * @brief   Declares the central download and publication workflow coordinator.
 *
 * @author  Zheludchenko Yehor
 * @date    2026-08-30
 * @version 2.1.0
 *
 * @copyright Copyright (c) 2026 TikTok Telegram Bot.
 *
 * Target: Qt 6.11.2 (MSVC 2022 64-bit, C++23)
 ******************************************************************************/

#ifndef TIKTOK_TELEGRAM_BOT_APP_BOT_CONTROLLER_H
#define TIKTOK_TELEGRAM_BOT_APP_BOT_CONTROLLER_H

#include "app/CacheManager.h"
#include "config/AppConfig.h"
#include "storage/AppDatabase.h"
#include "telegram/TdPublisher.h"
#include "telegram/TelegramApi.h"
#include "telegram/TelegramBot.h"
#include "telegram/TelegramTypes.h"
#include "tiktok/TikTokDownloader.h"

#include <QDateTime>
#include <QObject>
#include <QTimer>
#include <QVariantMap>

#include <QHash>
#include <chrono>
#include <optional>

/**
 * @brief Coordinates Telegram input, downloads, persistent state, and publication.
 *
 * The database and optional TDLib publisher are non-owning and must outlive this
 * controller. All members are used on the controller's Qt thread.
 */
class BotController final : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Constructs the running workflow.
     *
     * @param[in] config Valid Bot API runtime configuration.
     * @param[in] database Open database that outlives this controller; never nullptr.
     * @param[in] tdPublisher Optional ready-capable server publisher; may be nullptr.
     * @param[in] parent Parent object that owns this instance; may be nullptr.
     */
    explicit BotController(AppConfig config, AppDatabase* database,
                           TdPublisher* tdPublisher = nullptr, QObject* parent = nullptr);

    /** @brief Starts Telegram polling and queue processing. */
    void start();
    /** @brief Stops timers, polling, downloads, and active network requests. */
    void shutdown();
    /** @brief Wakes durable retries, downloads and publication after desktop queue edits. */
    void wakeQueue();
    /** @brief Cleans expired terminal media and refreshes the storage snapshot. */
    void cleanCache();
    /** @brief Applies changed persistent cache limits. */
    void reloadCachePolicy();

    /**
     * @brief Cancels an eligible local or server-scheduled video.
     * @param[in] videoId Local catalog identifier.
     * @param[out] error Receives a diagnostic; may be nullptr.
     * @return true if cancellation started or completed; otherwise false.
     */
    [[nodiscard]] bool cancelVideo(qint64 videoId, QString* error = nullptr);
    /**
     * @brief Retries an eligible failed video.
     * @param[in] videoId Local catalog identifier.
     * @param[out] error Receives a diagnostic; may be nullptr.
     * @return true if the video was requeued; otherwise false.
     */
    [[nodiscard]] bool retryVideo(qint64 videoId, QString* error = nullptr);
    /**
     * @brief Requests publication at the next safe processing opportunity.
     * @param[in] videoId Local catalog identifier.
     * @param[out] error Receives a diagnostic; may be nullptr.
     * @return true if the request was accepted; otherwise false.
     */
    [[nodiscard]] bool publishVideoNow(qint64 videoId, QString* error = nullptr);
    /**
     * @brief Moves an eligible local video one position in the queue.
     * @param[in] videoId Local catalog identifier.
     * @param[in] direction Negative to move earlier, positive to move later.
     * @param[out] error Receives a diagnostic; may be nullptr.
     * @return true if the queue changed; otherwise false.
     */
    [[nodiscard]] bool moveVideo(qint64 videoId, int direction, QString* error = nullptr);
    /**
     * @brief Replaces the local or Telegram server publication time.
     * @param[in] videoId Local catalog identifier.
     * @param[in] scheduledAtUtc New valid future UTC time.
     * @param[out] error Receives a diagnostic; may be nullptr.
     * @return true if the update completed or started; otherwise false.
     */
    [[nodiscard]] bool setVideoSchedule(qint64 videoId, const QDateTime& scheduledAtUtc,
                                        QString* error = nullptr);
    /**
     * @brief Assigns an eligible local video to a different channel.
     * @param[in] videoId Local catalog identifier.
     * @param[in] channelId Enabled destination channel key.
     * @param[out] error Receives a diagnostic; may be nullptr.
     * @return true if the destination changed; otherwise false.
     */
    [[nodiscard]] bool setVideoChannel(qint64 videoId, qint64 channelId, QString* error = nullptr);
    /** @return A QML-ready snapshot of queue and worker metrics. */
    [[nodiscard]] QVariantMap metrics() const;

signals:
    /** @brief Indicates that Bot API credentials were accepted and polling started. */
    void telegramReady();
    /**
     * @brief Reports a recoverable Bot API connection issue.
     * @param[in] error Sanitized connection diagnostic.
     * @param[in] retryAfterSeconds Selected retry delay.
     */
    void telegramConnectionIssue(const QString& error, int retryAfterSeconds);
    /** @brief Indicates that a catalog model must be refreshed. */
    void catalogChanged();
    /**
     * @brief Delivers a fresh metrics snapshot.
     * @param[in] metrics QML-ready metrics map.
     */
    void metricsChanged(const QVariantMap& metrics);
    /**
     * @brief Delivers a sanitized application activity entry.
     * @param[in] level Log severity name.
     * @param[in] message Human-readable event text.
     */
    void activityLogged(const QString& level, const QString& message);

private slots:
    void onTelegramReady();
    void onMessageReceived(const TelegramMessage& message);
    void onMetadataAvailable(const TikTokMetadata& metadata);
    void onDownloadProgress(double percent, const QString& detail);
    void onDownloadFinished(const QString& filePath);
    void onDownloadFailed(const QString& error);
    void onVideoSent(const TelegramVideoResult& result);
    void onTdVideoAccepted(qint64 videoId, const QString& tdChatId, const QString& tdMessageId,
                           bool scheduled);
    void onTdVideoFailed(qint64 videoId, const QString& error, bool deliveryUnknown = false,
                         int retryAfterSeconds = 0);
    void onVideoReconciled(qint64 videoId, const QString& state, const QString& tdChatId,
                           const QString& tdMessageId, const QDateTime& publishedAtUtc,
                           const QString& error);
    void reconcileServerVideos();
    void onRemoteOperationFinished(qint64 videoId, const QString& operation, bool success,
                                   const QString& error);
    void onMessageSent(qint64 chatId, bool success, const QString& error);
    void processDownloads();
    void processPublishing();
    void publishMetrics();

private:
    [[nodiscard]] QDateTime publicationTimeForChannel(qint64 channelId) const;
    [[nodiscard]] QString cacheDirectoryForVideo(qint64 videoId) const;
    void pruneOrphanedCache();
    [[nodiscard]] bool hasCacheCapacity();
    [[nodiscard]] qint64 cacheSizeBytes() const;
    void armPublicationTimer();
    void refreshCatalog();
    void logActivity(const QString& level, const QString& message);
    void sendToRequester(const VideoRecord& video, const QString& text);
    void scheduleAutomaticRetry(qint64 videoId, int retryCount, int retryAfterSeconds = 0,
                                const QString& category = QStringLiteral("download"));
    [[nodiscard]] QString statusText() const;
    [[nodiscard]] static QString commandFromText(const QString& text);

    AppConfig m_config;
    AppDatabase* m_database{};
    TdPublisher* m_tdPublisher{};
    bool m_serverScheduling{false};
    std::chrono::minutes m_publicationInterval;
    TikTokDownloader m_downloader;
    TelegramApi m_api;
    TelegramBot m_bot;
    QTimer m_publicationTimer;
    QTimer m_metricsTimer;
    QTimer m_queueTimer;
    std::optional<VideoRecord> m_downloadVideo;
    std::optional<VideoRecord> m_uploadVideo;
    QString m_cacheRoot;
    CacheManager m_cacheManager;
    QString m_downloadDetail;
    double m_downloadProgress{};
    int m_lastPersistedProgress{-1};
    bool m_telegramReady{false};
    bool m_shuttingDown{false};
    QHash<qint64, QDateTime> m_pendingRemoteSchedules;
};

#endif // TIKTOK_TELEGRAM_BOT_APP_BOT_CONTROLLER_H
