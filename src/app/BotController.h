#pragma once

#include "config/AppConfig.h"
#include "telegram/TelegramApi.h"
#include "telegram/TelegramBot.h"
#include "telegram/TelegramTypes.h"
#include "tiktok/TikTokDownloader.h"

#include <QDateTime>
#include <QObject>
#include <QQueue>
#include <QTimer>
#include <QVariantList>

#include <chrono>
#include <optional>

class BotController final : public QObject
{
    Q_OBJECT

public:
    explicit BotController(AppConfig config, QObject *parent = nullptr);

    void start();
    void shutdown();

signals:
    void telegramReady();
    void telegramConnectionIssue(const QString &error, int retryAfterSeconds);
    void scheduledVideosChanged(const QVariantList &videos);
    void videoPublished(const QString &sourceUrl, const QDateTime &publishedAtUtc);

private slots:
    void onMessageReceived(const TelegramMessage &message);
    void onDownloadFinished(const QString &filePath);
    void onDownloadFailed(const QString &error);
    void onVideoSent(bool success, const QString &error);
    void onMessageSent(qint64 chatId, bool success, const QString &error);

private:
    enum class State
    {
        Idle,
        Downloading,
        Uploading
    };

    void processNext();
    void finishCurrent();
    void sendToRequester(const QString &text);
    void loadPublicationSchedule();
    void loadPendingJobs();
    void savePendingJobs() const;
    void publishScheduledVideos();
    void recordSuccessfulPublication(const QDateTime &publishedAtUtc);
    [[nodiscard]] bool publicationIsDelayed() const;
    [[nodiscard]] QVariantList scheduledVideoEntries() const;
    [[nodiscard]] QString statusText() const;
    [[nodiscard]] static QString commandFromText(const QString &text);

    AppConfig m_config;
    std::chrono::minutes m_publicationInterval;
    TikTokDownloader m_downloader;
    TelegramApi m_api;
    TelegramBot m_bot;
    QTimer m_publicationTimer;
    QQueue<DownloadJob> m_pendingJobs;
    std::optional<DownloadJob> m_currentJob;
    QDateTime m_nextPublicationAtUtc;
    State m_state{State::Idle};
    bool m_shuttingDown{false};
};
