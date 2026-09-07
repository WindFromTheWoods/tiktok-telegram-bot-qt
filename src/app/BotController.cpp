/******************************************************************************
 * @file    BotController.cpp
 * @brief   Implements the central download and publication workflow coordinator.
 *
 * @author  Zheludchenko Yehor
 * @date    2026-08-30
 * @version 2.1.0
 *
 * @copyright Copyright (c) 2026 TikTok Telegram Bot.
 *
 * Target: Qt 6.11.2 (MSVC 2022 64-bit, C++23)
 ******************************************************************************/

#include "app/BotController.h"

#include "Logging.h"
#include "app/ScheduleCalculator.h"
#include "tiktok/TikTokUrlValidator.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>
#include <chrono>
#include <utility>

namespace
{

constexpr qint64 TelegramVideoUploadLimit = 50LL * 1024LL * 1024LL;
constexpr qint64 TdLibVideoUploadLimit = 2LL * 1024LL * 1024LL * 1024LL;
constexpr int MaximumAutomaticRetries = 3;

const QString StartText =
    QStringLiteral("Send a TikTok video URL. I will download it in advance and publish it to the "
                   "configured channel at the next available schedule slot.");
const QString HelpText =
    QStringLiteral("Supported links:\n"
                   "https://www.tiktok.com/@username/video/123456789\n"
                   "https://vm.tiktok.com/ABC123/\n\n"
                   "Commands: /status, /help. Queue order, channels and publication times can be "
                   "managed in the desktop application.");

QString localDisplayTime(const QDateTime& utcTime)
{
    return utcTime.isValid() ? utcTime.toLocalTime().toString(QStringLiteral("dd.MM.yyyy HH:mm"))
                             : QStringLiteral("available now");
}

} // namespace

BotController::BotController(AppConfig config, AppDatabase* database, TdPublisher* tdPublisher,
                             QObject* parent)
    : QObject(parent)
    , m_config(std::move(config))
    , m_database(database)
    , m_tdPublisher(tdPublisher)
    , m_serverScheduling(tdPublisher != nullptr)
    , m_publicationInterval(m_config.publicationInterval())
    , m_downloader(m_config.ytDlpExecutable())
    , m_api(m_config.botToken())
    , m_bot(&m_api)
    , m_cacheRoot(QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                      .filePath(QStringLiteral("video-cache")))
    , m_cacheManager(m_cacheRoot)
{
    Q_ASSERT(m_database != nullptr);

    connect(&m_bot, &TelegramBot::messageReceived, this, &BotController::onMessageReceived);
    connect(&m_bot, &TelegramBot::pollingStarted, this, &BotController::onTelegramReady);
    connect(&m_bot, &TelegramBot::pollingIssue, this, &BotController::telegramConnectionIssue);
    connect(&m_downloader, &TikTokDownloader::metadataAvailable, this,
            &BotController::onMetadataAvailable);
    connect(&m_downloader, &TikTokDownloader::downloadProgress, this,
            &BotController::onDownloadProgress);
    connect(&m_downloader, &TikTokDownloader::downloadFinished, this,
            &BotController::onDownloadFinished);
    connect(&m_downloader, &TikTokDownloader::downloadFailed, this,
            &BotController::onDownloadFailed);
    connect(&m_api, &TelegramApi::videoCompleted, this, &BotController::onVideoSent);
    connect(&m_api, &TelegramApi::messageSent, this, &BotController::onMessageSent);
    if (m_tdPublisher != nullptr)
    {
        connect(m_tdPublisher, &TdPublisher::readyChanged, this,
                [this](const bool ready)
                {
                    logActivity(
                        ready ? QStringLiteral("success") : QStringLiteral("warning"),
                        ready ? QStringLiteral("Telegram server scheduling is ready.")
                              : QStringLiteral(
                                    "Telegram server scheduling is waiting for authorization."));
                    processPublishing();
                    reconcileServerVideos();
                });
        connect(m_tdPublisher, &TdPublisher::videoAccepted, this,
                &BotController::onTdVideoAccepted);
        connect(m_tdPublisher, &TdPublisher::videoFailureDetailed, this,
                &BotController::onTdVideoFailed);
        connect(m_tdPublisher, &TdPublisher::videoReconciled, this,
                &BotController::onVideoReconciled);
        connect(m_tdPublisher, &TdPublisher::remoteOperationFinished, this,
                &BotController::onRemoteOperationFinished);
    }

    m_publicationTimer.setSingleShot(true);
    connect(&m_publicationTimer, &QTimer::timeout, this, &BotController::processPublishing);
    m_metricsTimer.setInterval(std::chrono::seconds(5));
    connect(&m_metricsTimer, &QTimer::timeout, this, &BotController::publishMetrics);
    m_queueTimer.setInterval(std::chrono::seconds(30));
    connect(&m_queueTimer, &QTimer::timeout, this,
            [this]
            {
                wakeQueue();
                reconcileServerVideos();
                m_cacheManager.refresh();
            });
    connect(&m_cacheManager, &CacheManager::snapshotChanged, this,
            [this]
            {
                processDownloads();
                publishMetrics();
            });
}

void BotController::start()
{
    if (m_shuttingDown)
    {
        return;
    }
    QString databaseError;
    if (!m_database->recoverInterruptedVideos(&databaseError))
    {
        logActivity(QStringLiteral("error"),
                    QStringLiteral("Could not recover interrupted tasks: %1").arg(databaseError));
    }
    QDir().mkpath(m_cacheRoot);
    pruneOrphanedCache();
    qCInfo(logApp) << "TikTok Telegram bot starting";
    logActivity(QStringLiteral("info"), QStringLiteral("Bot is starting."));
    refreshCatalog();
    m_metricsTimer.start();
    m_queueTimer.start();
    m_bot.start();
    wakeQueue();
    reconcileServerVideos();
}

void BotController::shutdown()
{
    if (m_shuttingDown)
    {
        return;
    }
    m_shuttingDown = true;
    qCInfo(logApp) << "TikTok Telegram bot shutting down";
    logActivity(QStringLiteral("info"), QStringLiteral("Bot stopped."));
    m_bot.stop();
    m_publicationTimer.stop();
    m_metricsTimer.stop();
    m_queueTimer.stop();
    m_downloader.cancel();
    m_api.shutdown();
}

void BotController::wakeQueue()
{
    if (m_shuttingDown)
    {
        return;
    }
    QString error;
    if (!m_database->retryDueVideos(QDateTime::currentDateTimeUtc(), &error))
    {
        logActivity(QStringLiteral("error"), error);
    }
    refreshCatalog();
    processDownloads();
    processPublishing();
}

void BotController::cleanCache()
{
    pruneOrphanedCache();
}

void BotController::reloadCachePolicy()
{
    m_cacheManager.reloadPolicy();
    m_cacheManager.refresh();
}

void BotController::onTelegramReady()
{
    if (m_shuttingDown)
    {
        return;
    }
    m_telegramReady = true;
    logActivity(QStringLiteral("success"), QStringLiteral("Telegram polling is active."));
    emit telegramReady();
    processPublishing();
}

void BotController::onMessageReceived(const TelegramMessage& message)
{
    if (m_shuttingDown)
    {
        return;
    }
    if (message.userId != m_config.adminUserId())
    {
        qCWarning(logApp) << "Rejected an unauthorized request from user" << message.userId;
        m_api.sendMessage(message.chatId, QStringLiteral("Unauthorized."));
        return;
    }

    const QString text = message.text.trimmed();
    const QString command = commandFromText(text);
    if (command == QStringLiteral("/start"))
    {
        m_api.sendMessage(message.chatId, StartText);
        return;
    }
    if (command == QStringLiteral("/help"))
    {
        m_api.sendMessage(message.chatId, HelpText);
        return;
    }
    if (command == QStringLiteral("/status"))
    {
        m_api.sendMessage(message.chatId, statusText());
        return;
    }
    if (!TikTokUrlValidator::isValid(text))
    {
        m_api.sendMessage(message.chatId,
                          QStringLiteral("Please send one supported TikTok HTTPS URL."));
        return;
    }

    const std::optional<ChannelRecord> channel = m_database->defaultChannel();
    if (!channel.has_value())
    {
        m_api.sendMessage(message.chatId, QStringLiteral("No destination channel is configured."));
        return;
    }
    const QDateTime scheduledAtUtc = publicationTimeForChannel(channel->id);
    QString databaseError;
    const qint64 videoId =
        m_database->addVideo(text, message.chatId, message.userId, channel->id, scheduledAtUtc,
                             &databaseError, true, false, channel->captionTemplate);
    if (videoId <= 0)
    {
        m_api.sendMessage(message.chatId,
                          QStringLiteral("Could not add the video: %1").arg(databaseError));
        return;
    }

    const int position = m_database->activeVideos().size();
    m_api.sendMessage(
        message.chatId,
        QStringLiteral("Video added to the queue at position %1. Planned publication: %2")
            .arg(position)
            .arg(localDisplayTime(scheduledAtUtc)));
    logActivity(QStringLiteral("info"),
                QStringLiteral("Video #%1 added to the queue.").arg(videoId));
    refreshCatalog();
    processDownloads();
    processPublishing();
}

void BotController::processDownloads()
{
    if (m_shuttingDown || m_downloadVideo.has_value() || m_downloader.isBusy())
    {
        return;
    }
    const std::optional<VideoRecord> candidate = m_database->nextVideoForDownload();
    if (!candidate.has_value())
    {
        publishMetrics();
        return;
    }

    if (!hasCacheCapacity())
    {
        logActivity(QStringLiteral("warning"),
                    QStringLiteral("Download paused: the video cache reached its safety limit. "
                                   "Publish, cancel, or remove queued videos to free disk space."));
        publishMetrics();
        return;
    }

    QString error;
    if (!m_database->markDownloading(candidate->id, &error))
    {
        logActivity(QStringLiteral("error"),
                    QStringLiteral("Could not start video #%1: %2").arg(candidate->id).arg(error));
        return;
    }
    m_downloadVideo = m_database->video(candidate->id);
    m_downloadProgress = 0;
    m_downloadDetail.clear();
    m_lastPersistedProgress = -1;
    logActivity(QStringLiteral("info"),
                QStringLiteral("Downloading video #%1 in advance.").arg(candidate->id));
    refreshCatalog();
    m_downloader.download(QUrl(candidate->url, QUrl::StrictMode),
                          cacheDirectoryForVideo(candidate->id),
                          m_serverScheduling ? TdLibVideoUploadLimit : TelegramVideoUploadLimit);
}

void BotController::onMetadataAvailable(const TikTokMetadata& metadata)
{
    if (!m_downloadVideo.has_value() || m_shuttingDown)
    {
        return;
    }
    QString error;
    if (!m_database->updateMetadata(m_downloadVideo->id, metadata.title, metadata.author,
                                    metadata.durationSeconds, metadata.thumbnailPath, &error))
    {
        logActivity(QStringLiteral("warning"),
                    QStringLiteral("Could not save metadata: %1").arg(error));
    }
    if (!metadata.videoId.isEmpty() &&
        !m_database->setSourceVideoId(m_downloadVideo->id, metadata.videoId, &error))
    {
        logActivity(QStringLiteral("warning"), error);
    }
    refreshCatalog();
}

void BotController::onDownloadProgress(const double percent, const QString& detail)
{
    if (!m_downloadVideo.has_value() || m_shuttingDown)
    {
        return;
    }
    m_downloadProgress = std::clamp(percent, 0.0, 100.0);
    m_downloadDetail = detail;
    const int wholePercent = static_cast<int>(m_downloadProgress);
    if (wholePercent >= m_lastPersistedProgress + 2 || wholePercent == 100)
    {
        static_cast<void>(
            m_database->updateDownloadProgress(m_downloadVideo->id, m_downloadProgress));
        m_lastPersistedProgress = wholePercent;
        refreshCatalog();
    }
    publishMetrics();
}

void BotController::onDownloadFinished(const QString& filePath)
{
    if (!m_downloadVideo.has_value() || m_shuttingDown)
    {
        return;
    }
    const VideoRecord completed = *m_downloadVideo;
    const QFileInfo fileInfo(filePath);
    QString error;
    if (!fileInfo.isFile() || fileInfo.size() <= 0)
    {
        static_cast<void>(m_database->markFailed(
            completed.id, QStringLiteral("Downloaded file is missing or empty."), &error));
    }
    else if (fileInfo.size() >
             (m_serverScheduling ? TdLibVideoUploadLimit : TelegramVideoUploadLimit))
    {
        const QString limitError =
            m_serverScheduling
                ? QStringLiteral("Video is larger than the configured 2 GB TDLib safety limit.")
                : QStringLiteral("Video is larger than Telegram's 50 MB Bot API limit.");
        static_cast<void>(m_database->markFailed(completed.id, limitError, &error));
        sendToRequester(completed, limitError);
    }
    else if (m_database->hasDuplicateSourceVideo(completed.id))
    {
        static_cast<void>(m_database->markFailed(
            completed.id,
            QStringLiteral("This TikTok video is already queued or published in this channel. "
                           "Enable the duplicate override to publish again."),
            &error));
        static_cast<void>(
            m_database->setFailureCategory(completed.id, QStringLiteral("duplicate")));
    }
    else if (!m_database->markReady(completed.id, filePath, fileInfo.size(), &error))
    {
        logActivity(QStringLiteral("error"),
                    QStringLiteral("Could not mark video as ready: %1").arg(error));
    }
    else
    {
        logActivity(QStringLiteral("success"),
                    QStringLiteral("Video #%1 is downloaded and ready.").arg(completed.id));
    }

    m_downloadVideo.reset();
    m_downloader.cleanup();
    m_downloadProgress = 0;
    m_downloadDetail.clear();
    refreshCatalog();
    m_cacheManager.refresh();
    processDownloads();
    processPublishing();
}

void BotController::onDownloadFailed(const QString& errorMessage)
{
    if (m_shuttingDown)
    {
        return;
    }
    if (!m_downloadVideo.has_value())
    {
        m_downloader.cleanup();
        processDownloads();
        return;
    }
    const VideoRecord failed = *m_downloadVideo;
    QString databaseError;
    static_cast<void>(m_database->markFailed(failed.id, errorMessage, &databaseError));
    const std::optional<VideoRecord> updated = m_database->video(failed.id);
    sendToRequester(failed, QStringLiteral("Download failed: %1").arg(errorMessage));
    logActivity(QStringLiteral("error"),
                QStringLiteral("Video #%1 download failed: %2").arg(failed.id).arg(errorMessage));
    m_downloadVideo.reset();
    m_downloader.cleanup();
    refreshCatalog();
    if (updated.has_value() && updated->retryCount <= MaximumAutomaticRetries)
    {
        scheduleAutomaticRetry(updated->id, updated->retryCount);
    }
    m_cacheManager.refresh();
    processDownloads();
}

void BotController::processPublishing()
{
    const bool publisherReady =
        m_serverScheduling ? m_tdPublisher != nullptr && m_tdPublisher->isReady() : m_telegramReady;
    if (m_shuttingDown || !publisherReady || m_uploadVideo.has_value() ||
        (m_serverScheduling && m_tdPublisher->isBusy()))
    {
        return;
    }
    const std::optional<VideoRecord> candidate =
        m_serverScheduling ? m_database->nextVideoForServerSubmission()
                           : m_database->nextVideoForUpload(QDateTime::currentDateTimeUtc());
    if (!candidate.has_value())
    {
        armPublicationTimer();
        publishMetrics();
        return;
    }
    const QFileInfo fileInfo(candidate->localFilePath);
    if (m_database->hasDuplicateSourceVideo(candidate->id))
    {
        static_cast<void>(m_database->markFailed(
            candidate->id,
            QStringLiteral("Duplicate TikTok video in the selected channel; enable the override "
                           "before retrying.")));
        static_cast<void>(
            m_database->setFailureCategory(candidate->id, QStringLiteral("duplicate")));
        refreshCatalog();
        QTimer::singleShot(0, this, &BotController::processPublishing);
        return;
    }
    if (!fileInfo.isFile() || fileInfo.size() <= 0)
    {
        QString error;
        if (!m_database->requeueForDownload(candidate->id, &error))
        {
            logActivity(QStringLiteral("error"), error);
            return;
        }
        logActivity(QStringLiteral("warning"),
                    QStringLiteral("Cached file for video #%1 is missing; downloading it again.")
                        .arg(candidate->id));
        refreshCatalog();
        processDownloads();
        return;
    }
    QString error;
    QString caption = candidate->caption;
    caption.replace(QStringLiteral("{title}"), candidate->title);
    caption.replace(QStringLiteral("{author}"), candidate->author);
    caption.replace(QStringLiteral("{url}"), candidate->url);
    if (caption.size() > 1024)
    {
        static_cast<void>(m_database->markFailed(
            candidate->id,
            QStringLiteral(
                "Expanded caption exceeds 1024 characters. Edit the caption before retrying.")));
        static_cast<void>(m_database->setFailureCategory(candidate->id, QStringLiteral("caption")));
        refreshCatalog();
        QTimer::singleShot(0, this, &BotController::processPublishing);
        return;
    }
    if (!m_database->markUploading(candidate->id, &error))
    {
        logActivity(QStringLiteral("error"), error);
        return;
    }
    m_uploadVideo = m_database->video(candidate->id);
    logActivity(QStringLiteral("info"),
                (m_serverScheduling
                     ? QStringLiteral("Submitting video #%1 to Telegram's server queue for %2.")
                     : QStringLiteral("Publishing video #%1 to %2."))
                    .arg(candidate->id)
                    .arg(candidate->channelName));
    refreshCatalog();
    if (m_serverScheduling)
    {
        QString submissionError;
        const bool accepted = m_tdPublisher->submitVideo(
            candidate->id, candidate->telegramChannelId, candidate->localFilePath,
            candidate->scheduledAtUtc, candidate->durationSeconds,
            candidate->forcePublish ||
                candidate->scheduledAtUtc <= QDateTime::currentDateTimeUtc().addSecs(10),
            &submissionError, caption);
        if (!accepted)
        {
            onTdVideoFailed(candidate->id, submissionError);
        }
    }
    else
    {
        m_api.sendVideo(candidate->telegramChannelId, candidate->localFilePath, caption);
    }
}

void BotController::onTdVideoAccepted(const qint64 videoId, const QString& tdChatId,
                                      const QString& tdMessageId, const bool scheduled)
{
    if (!m_uploadVideo.has_value() || m_uploadVideo->id != videoId || m_shuttingDown)
    {
        return;
    }
    const VideoRecord uploaded = *m_uploadVideo;
    QString databaseError;
    const bool saved =
        scheduled
            ? m_database->markServerScheduled(videoId, tdChatId, tdMessageId, &databaseError)
            : m_database->markServerPublished(videoId, tdChatId, tdMessageId,
                                              QDateTime::currentDateTimeUtc(), &databaseError);
    if (saved)
    {
        pruneOrphanedCache();
        const QString result = scheduled
                                   ? QStringLiteral("Uploaded to Telegram and scheduled for %1.")
                                         .arg(localDisplayTime(uploaded.scheduledAtUtc))
                                   : QStringLiteral("Published successfully through TDLib.");
        sendToRequester(uploaded, result);
        logActivity(
            QStringLiteral("success"),
            scheduled
                ? QStringLiteral("Video #%1 is now stored in Telegram's server queue.").arg(videoId)
                : QStringLiteral("Video #%1 was published through TDLib.").arg(videoId));
    }
    else
    {
        logActivity(QStringLiteral("error"), databaseError);
    }
    m_uploadVideo.reset();
    refreshCatalog();
    processPublishing();
}

void BotController::onTdVideoFailed(const qint64 videoId, const QString& errorMessage,
                                    const bool deliveryUnknown, const int retryAfterSeconds)
{
    if (!m_uploadVideo.has_value() || m_uploadVideo->id != videoId || m_shuttingDown)
    {
        return;
    }
    const VideoRecord failed = *m_uploadVideo;
    QString databaseError;
    if (deliveryUnknown)
    {
        static_cast<void>(m_database->markDeliveryUnknown(videoId, errorMessage, &databaseError));
    }
    else
    {
        static_cast<void>(m_database->markFailed(videoId, errorMessage, &databaseError));
        static_cast<void>(m_database->setFailureCategory(
            videoId, retryAfterSeconds > 0 ? QStringLiteral("rate_limit")
                                           : QStringLiteral("telegram_rejected")));
    }
    sendToRequester(
        failed,
        deliveryUnknown
            ? QStringLiteral("Telegram delivery could not be confirmed. Check the channel and "
                             "resolve the item in the desktop app before retrying: %1")
                  .arg(errorMessage)
            : QStringLiteral("TDLib upload failed: %1").arg(errorMessage));
    logActivity(QStringLiteral("error"),
                QStringLiteral("Video #%1 TDLib upload failed: %2").arg(videoId).arg(errorMessage));
    const std::optional<VideoRecord> updated = m_database->video(videoId);
    m_uploadVideo.reset();
    refreshCatalog();
    if (!deliveryUnknown && retryAfterSeconds > 0 && updated.has_value() &&
        updated->retryCount <= MaximumAutomaticRetries)
    {
        scheduleAutomaticRetry(videoId, updated->retryCount, retryAfterSeconds,
                               QStringLiteral("rate_limit"));
    }
    processPublishing();
}

void BotController::onRemoteOperationFinished(const qint64 videoId, const QString& operation,
                                              const bool success, const QString& errorMessage)
{
    QString databaseError;
    if (!success)
    {
        m_pendingRemoteSchedules.remove(videoId);
        logActivity(QStringLiteral("error"),
                    QStringLiteral("Telegram server operation for video #%1 failed: %2")
                        .arg(videoId)
                        .arg(errorMessage));
        refreshCatalog();
        return;
    }

    bool saved = false;
    if (operation == QStringLiteral("cancel"))
    {
        saved = m_database->cancelVideo(videoId, &databaseError);
    }
    else if (operation == QStringLiteral("publish_now"))
    {
        saved = true;
        const auto record = m_database->video(videoId);
        if (record && m_tdPublisher)
        {
            static_cast<void>(
                m_tdPublisher->reconcileVideo(videoId, record->tdChatId, record->tdMessageId));
        }
    }
    else if (operation == QStringLiteral("reschedule"))
    {
        const QDateTime dateTime = m_pendingRemoteSchedules.take(videoId);
        saved = m_database->updateServerSchedule(videoId, dateTime, &databaseError);
    }
    logActivity(saved ? QStringLiteral("success") : QStringLiteral("error"),
                saved ? QStringLiteral("Telegram server queue updated for video #%1.").arg(videoId)
                      : databaseError);
    refreshCatalog();
}

void BotController::onVideoSent(const TelegramVideoResult& result)
{
    if (!m_uploadVideo.has_value() || m_shuttingDown)
    {
        return;
    }
    const VideoRecord uploaded = *m_uploadVideo;
    QString databaseError;
    const QString errorMessage = result.error;
    if (result.success)
    {
        const QDateTime publishedAtUtc = QDateTime::currentDateTimeUtc();
        if (m_database->markBotPublished(uploaded.id, result.chatId, result.messageId,
                                         publishedAtUtc, &databaseError))
        {
            pruneOrphanedCache();
            sendToRequester(uploaded, QStringLiteral("Published successfully."));
            logActivity(QStringLiteral("success"), QStringLiteral("Video #%1 was published to %2.")
                                                       .arg(uploaded.id)
                                                       .arg(uploaded.channelName));
        }
    }
    else
    {
        if (result.deliveryUnknown)
        {
            static_cast<void>(
                m_database->markDeliveryUnknown(uploaded.id, errorMessage, &databaseError));
        }
        else
        {
            static_cast<void>(m_database->markFailed(uploaded.id, errorMessage, &databaseError));
            static_cast<void>(m_database->setFailureCategory(uploaded.id, result.category));
        }
        sendToRequester(
            uploaded,
            result.deliveryUnknown
                ? QStringLiteral("Telegram delivery could not be confirmed. Check the channel and "
                                 "resolve the item in the desktop app before retrying: %1")
                      .arg(errorMessage)
                : QStringLiteral("Upload failed: %1").arg(errorMessage));
        logActivity(
            QStringLiteral("error"),
            QStringLiteral("Video #%1 upload failed: %2").arg(uploaded.id).arg(errorMessage));
        const std::optional<VideoRecord> updated = m_database->video(uploaded.id);
        if (!result.deliveryUnknown && result.category == QStringLiteral("rate_limit") &&
            updated.has_value() && updated->retryCount <= MaximumAutomaticRetries)
        {
            scheduleAutomaticRetry(updated->id, updated->retryCount, result.retryAfterSeconds,
                                   result.category);
        }
    }
    if (!databaseError.isEmpty())
    {
        logActivity(QStringLiteral("error"), databaseError);
    }
    m_uploadVideo.reset();
    refreshCatalog();
    processPublishing();
}

void BotController::onMessageSent(const qint64 chatId, const bool success, const QString& error)
{
    if (!success && !m_shuttingDown)
    {
        qCWarning(logTelegram) << "Could not send status message to chat" << chatId << error;
    }
}

bool BotController::cancelVideo(const qint64 videoId, QString* error)
{
    if (m_uploadVideo.has_value() && m_uploadVideo->id == videoId)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("A video that is currently uploading cannot be cancelled.");
        }
        return false;
    }
    if (m_downloadVideo.has_value() && m_downloadVideo->id == videoId)
    {
        m_downloader.cancel();
        m_downloadVideo.reset();
        QTimer::singleShot(std::chrono::milliseconds(2200), this, &BotController::processDownloads);
    }
    const std::optional<VideoRecord> record = m_database->video(videoId);
    if (record.has_value() && record->status == QStringLiteral("server_scheduled"))
    {
        if (m_tdPublisher == nullptr)
        {
            if (error != nullptr)
            {
                *error = QStringLiteral("Start the TDLib server mode to cancel this post.");
            }
            return false;
        }
        return m_tdPublisher->cancelScheduledVideo(videoId, record->tdChatId, record->tdMessageId,
                                                   error);
    }
    if (!m_database->cancelVideo(videoId, error))
    {
        return false;
    }
    logActivity(QStringLiteral("info"), QStringLiteral("Video #%1 was cancelled.").arg(videoId));
    refreshCatalog();
    processDownloads();
    armPublicationTimer();
    return true;
}

bool BotController::retryVideo(const qint64 videoId, QString* error)
{
    if (!m_database->retryVideo(videoId, error))
    {
        return false;
    }
    logActivity(QStringLiteral("info"),
                QStringLiteral("Video #%1 was queued for retry.").arg(videoId));
    refreshCatalog();
    processDownloads();
    processPublishing();
    return true;
}

bool BotController::publishVideoNow(const qint64 videoId, QString* error)
{
    const std::optional<VideoRecord> record = m_database->video(videoId);
    if (record.has_value() && record->status == QStringLiteral("server_scheduled"))
    {
        if (m_tdPublisher == nullptr)
        {
            if (error != nullptr)
            {
                *error = QStringLiteral("Start the TDLib server mode to publish this post.");
            }
            return false;
        }
        return m_tdPublisher->publishScheduledVideoNow(videoId, record->tdChatId,
                                                       record->tdMessageId, error);
    }
    if (!m_database->publishVideoNow(videoId, error))
    {
        return false;
    }
    logActivity(QStringLiteral("info"),
                QStringLiteral("Video #%1 was requested for immediate publication.").arg(videoId));
    refreshCatalog();
    processDownloads();
    processPublishing();
    return true;
}

bool BotController::moveVideo(const qint64 videoId, const int direction, QString* error)
{
    const std::optional<VideoRecord> record = m_database->video(videoId);
    if (record.has_value() && record->status == QStringLiteral("server_scheduled"))
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Server-scheduled posts are ordered by Telegram time.");
        }
        return false;
    }
    if (!m_database->moveVideo(videoId, direction, error))
    {
        return false;
    }
    refreshCatalog();
    return true;
}

bool BotController::setVideoSchedule(const qint64 videoId, const QDateTime& scheduledAtUtc,
                                     QString* error)
{
    const std::optional<VideoRecord> record = m_database->video(videoId);
    if (record.has_value() && record->status == QStringLiteral("server_scheduled"))
    {
        if (m_tdPublisher == nullptr)
        {
            if (error != nullptr)
            {
                *error = QStringLiteral("Start the TDLib server mode to reschedule this post.");
            }
            return false;
        }
        if (!m_tdPublisher->rescheduleVideo(videoId, record->tdChatId, record->tdMessageId,
                                            scheduledAtUtc, error))
        {
            return false;
        }
        m_pendingRemoteSchedules.insert(videoId, scheduledAtUtc.toUTC());
        return true;
    }
    if (!m_database->setVideoSchedule(videoId, scheduledAtUtc, error))
    {
        return false;
    }
    refreshCatalog();
    armPublicationTimer();
    return true;
}

bool BotController::setVideoChannel(const qint64 videoId, const qint64 channelId, QString* error)
{
    const std::optional<VideoRecord> record = m_database->video(videoId);
    if (record.has_value() && record->status == QStringLiteral("server_scheduled"))
    {
        if (error != nullptr)
        {
            *error =
                QStringLiteral("Cancel the Telegram scheduled post before changing its channel.");
        }
        return false;
    }
    if (!m_database->setVideoChannel(videoId, channelId, error))
    {
        return false;
    }
    refreshCatalog();
    return true;
}

void BotController::reconcileServerVideos()
{
    if (m_shuttingDown || !m_tdPublisher || !m_tdPublisher->isReady())
        return;
    for (const VideoRecord& record : m_database->serverVideosForReconciliation())
        static_cast<void>(
            m_tdPublisher->reconcileVideo(record.id, record.tdChatId, record.tdMessageId));
}

void BotController::onVideoReconciled(const qint64 videoId, const QString& state,
                                      const QString& tdChatId, const QString& tdMessageId,
                                      const QDateTime& publishedAtUtc, const QString& error)
{
    if (m_shuttingDown)
        return;
    const auto record = m_database->video(videoId);
    if (!record || (record->status != QStringLiteral("server_scheduled") &&
                    record->status != QStringLiteral("delivery_unknown")))
        return;
    QString databaseError;
    bool changed = false;
    if (state == QStringLiteral("published"))
        changed = m_database->markServerPublished(videoId, tdChatId, tdMessageId, publishedAtUtc,
                                                  &databaseError);
    else if (state == QStringLiteral("scheduled") &&
             record->status == QStringLiteral("delivery_unknown"))
        changed = m_database->markServerScheduled(videoId, tdChatId, tdMessageId, &databaseError);
    else if (state == QStringLiteral("unknown") &&
             record->status != QStringLiteral("delivery_unknown"))
        changed = m_database->markDeliveryUnknown(
            videoId,
            error.isEmpty() ? QStringLiteral("Telegram has not confirmed the message state. Verify "
                                             "the channel before retrying.")
                            : error,
            &databaseError);
    if (!databaseError.isEmpty())
        logActivity(QStringLiteral("error"), databaseError);
    if (changed)
    {
        logActivity(
            state == QStringLiteral("published") ? QStringLiteral("success")
                                                 : QStringLiteral("info"),
            QStringLiteral("Telegram verification for video #%1: %2.").arg(videoId).arg(state));
        refreshCatalog();
    }
}

QDateTime BotController::publicationTimeForChannel(const qint64 channelId) const
{
    QDateTime latest = m_database->latestPlannedTime(channelId);
    const QDateTime latestPublished = m_database->latestPublishedTime(channelId);
    if (!latest.isValid() || latestPublished > latest)
    {
        latest = latestPublished;
    }
    QString zone;
    for (const auto& channel : m_database->channels())
        if (channel.id == channelId)
            zone = channel.timeZoneId;
    return ScheduleCalculator::nextPublication(QDateTime::currentDateTimeUtc(), latest,
                                               m_database->scheduleSlots(channelId),
                                               m_publicationInterval, zone);
}

QString BotController::cacheDirectoryForVideo(const qint64 videoId) const
{
    return QDir(m_cacheRoot).filePath(QString::number(videoId));
}

qint64 BotController::cacheSizeBytes() const
{
    return m_cacheManager.bytesUsed();
}

void BotController::pruneOrphanedCache()
{
    m_cacheManager.refresh(m_database->cacheCleanupCandidates(), true);
}

bool BotController::hasCacheCapacity()
{
    return m_cacheManager.hasCapacity(
        (m_serverScheduling ? TdLibVideoUploadLimit : TelegramVideoUploadLimit) * 2);
}

void BotController::armPublicationTimer()
{
    m_publicationTimer.stop();
    if (m_serverScheduling)
    {
        return;
    }
    if (!m_telegramReady || m_uploadVideo.has_value())
    {
        return;
    }
    const QDateTime nextPublication = m_database->nextReadyPublicationTime();
    if (!nextPublication.isValid())
    {
        return;
    }
    const qint64 delay =
        std::max<qint64>(0, QDateTime::currentDateTimeUtc().msecsTo(nextPublication));
    m_publicationTimer.start(std::chrono::milliseconds(delay));
}

void BotController::refreshCatalog()
{
    emit catalogChanged();
    publishMetrics();
}

void BotController::logActivity(const QString& level, const QString& message)
{
    emit activityLogged(level, message);
}

void BotController::sendToRequester(const VideoRecord& video, const QString& text)
{
    if (video.requestChatId != 0)
    {
        m_api.sendMessage(video.requestChatId, text);
    }
}

void BotController::scheduleAutomaticRetry(const qint64 videoId, const int retryCount,
                                           const int retryAfterSeconds, const QString& category)
{
    const int delaySeconds = std::max(
        retryAfterSeconds, std::min(60 * (1 << std::clamp(retryCount - 1, 0, 4)), 15 * 60));
    QString error;
    if (!m_database->scheduleRetry(videoId, QDateTime::currentDateTimeUtc().addSecs(delaySeconds),
                                   category, &error))
    {
        logActivity(QStringLiteral("error"), error);
        return;
    }
    logActivity(
        QStringLiteral("warning"),
        QStringLiteral("Video #%1 will retry in %2 seconds.").arg(videoId).arg(delaySeconds));
    refreshCatalog();
}

QVariantMap BotController::metrics() const
{
    const qint64 cacheBytes = cacheSizeBytes();
    const QList<VideoRecord> active = m_database->activeVideos();
    const QList<VideoRecord> failed = m_database->failedVideos();
    const QDateTime nextPublication = m_database->nextReadyPublicationTime();
    return {
        {QStringLiteral("activeCount"), active.size()},
        {QStringLiteral("failedCount"), failed.size()},
        {QStringLiteral("sentCount"), m_database->sentVideos(1000000).size()},
        {QStringLiteral("downloadProgress"), m_downloadProgress},
        {QStringLiteral("downloadDetail"), m_downloadDetail},
        {QStringLiteral("downloading"), m_downloadVideo.has_value()},
        {QStringLiteral("uploading"), m_uploadVideo.has_value()},
        {QStringLiteral("cacheMiB"),
         QString::number(static_cast<double>(cacheBytes) / 1048576.0, 'f', 1)},
        {QStringLiteral("freeDiskGiB"),
         m_cacheManager.isReady()
             ? QString::number(static_cast<double>(m_cacheManager.bytesAvailable()) /
                                   (1024.0 * 1024.0 * 1024.0),
                               'f', 1)
             : QStringLiteral("—")},
        {QStringLiteral("nextPublication"), localDisplayTime(nextPublication)},
    };
}

void BotController::publishMetrics()
{
    QString error;
    const QList<VideoRecord> before = m_database->activeVideos();
    static_cast<void>(
        m_database->finalizeElapsedServerSchedules(QDateTime::currentDateTimeUtc(), &error));
    if (m_database->activeVideos().size() != before.size())
    {
        emit catalogChanged();
    }
    emit metricsChanged(metrics());
}

QString BotController::statusText() const
{
    const QVariantMap currentMetrics = metrics();
    return QStringLiteral("Active: %1\nFailed: %2\nSent: %3\nNext publication: %4")
        .arg(currentMetrics.value(QStringLiteral("activeCount")).toInt())
        .arg(currentMetrics.value(QStringLiteral("failedCount")).toInt())
        .arg(currentMetrics.value(QStringLiteral("sentCount")).toInt())
        .arg(currentMetrics.value(QStringLiteral("nextPublication")).toString());
}

QString BotController::commandFromText(const QString& text)
{
    const QString firstToken = text.section(QLatin1Char(' '), 0, 0);
    return firstToken.section(QLatin1Char('@'), 0, 0).toLower();
}
