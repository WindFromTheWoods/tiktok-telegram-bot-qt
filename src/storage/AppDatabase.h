/******************************************************************************
 * @file    AppDatabase.h
 * @brief   Declares SQLite persistence and publication-catalog state transitions.
 *
 * @author  Zheludchenko Yehor
 * @date    2026-08-30
 * @version 2.1.0
 *
 * @copyright Copyright (c) 2026 TikTok Telegram Bot.
 *
 * Target: Qt 6.11.2 (MSVC 2022 64-bit, C++23)
 ******************************************************************************/

#ifndef TIKTOK_TELEGRAM_BOT_STORAGE_APP_DATABASE_H
#define TIKTOK_TELEGRAM_BOT_STORAGE_APP_DATABASE_H

#include <QDateTime>
#include <QList>
#include <QSqlDatabase>
#include <QString>
#include <QVariantList>

#include <optional>

/** @brief Persistent Telegram destination configuration. */
struct ChannelRecord
{
    qint64 id{};             /**< SQLite primary key. */
    QString name;            /**< User-facing channel name. */
    QString telegramId;      /**< `@username` or numeric Telegram identifier. */
    bool isDefault{false};   /**< Whether new videos use this destination. */
    bool enabled{true};      /**< Whether the destination remains selectable. */
    QString timeZoneId;      /**< IANA time zone; empty uses the system time zone. */
    QString captionTemplate; /**< Default caption with optional author/title placeholders. */
};

/** @brief One local-time publication slot assigned to a channel. */
struct ScheduleSlotRecord
{
    qint64 id{};        /**< SQLite primary key. */
    qint64 channelId{}; /**< Owning channel key. */
    int dayOfWeek{};    /**< Zero for every day, otherwise Qt weekday one through seven. */
    QString time;       /**< Local wall time in `HH:mm` form. */
    bool enabled{true}; /**< Whether scheduling considers this slot. */
};

/** @brief Complete persisted state of one requested TikTok publication. */
struct VideoRecord
{
    qint64 id{};                                    /**< SQLite primary key. */
    QString url;                                    /**< Original validated TikTok URL. */
    qint64 channelId{};                             /**< Destination channel key. */
    QString channelName;                            /**< Joined user-facing channel name. */
    QString telegramChannelId;                      /**< Joined Telegram destination. */
    QString status;                                 /**< Current queue state-machine value. */
    qint64 requestChatId{};                         /**< Bot chat that receives status replies. */
    qint64 requestUserId{};                         /**< User that submitted the URL. */
    QDateTime requestedAtUtc;                       /**< Submission time in UTC. */
    QDateTime scheduledAtUtc;                       /**< Planned publication time in UTC. */
    QDateTime publishedAtUtc;                       /**< Publication time, when completed. */
    QString title;                                  /**< Downloaded media title. */
    QString author;                                 /**< Downloaded creator name. */
    QString thumbnailPath;                          /**< Cached thumbnail path. */
    QString localFilePath;                          /**< Cached video path while locally owned. */
    QString error;                                  /**< Last sanitized failure diagnostic. */
    int durationSeconds{};                          /**< Video duration in seconds. */
    qint64 fileSize{};                              /**< Downloaded file size in bytes. */
    int retryCount{};                               /**< Number of automatic retry attempts. */
    qint64 queueOrder{};                            /**< Stable manual queue ordering value. */
    double progress{};                              /**< Download progress from 0 through 100. */
    bool forcePublish{false};                       /**< Whether to bypass the planned time. */
    QString publisherMode{QStringLiteral("local")}; /**< `local` or `telegram_server`. */
    QString tdChatId;                               /**< TDLib chat identifier, when uploaded. */
    QString tdMessageId;                            /**< TDLib message identifier, when uploaded. */
    QString caption;                                /**< User-approved Telegram caption. */
    bool approved{true};        /**< False keeps the downloaded item as a draft. */
    QString sourceVideoId;      /**< Canonical TikTok video identifier. */
    bool allowDuplicate{false}; /**< Explicit permission to repeat this video. */
    QDateTime nextRetryAtUtc;   /**< Durable automatic retry deadline. */
    QString errorCategory;      /**< Actionable failure classification. */
    QString channelTimeZoneId;  /**< Joined channel time zone. */
};

/**
 * @brief Owns one SQLite connection and provides transactional catalog operations.
 *
 * Instances are non-copyable and must only be used from the thread that opened them.
 */
class AppDatabase final
{
public:
    /**
     * @brief Constructs a database owner without opening the connection.
     *
     * @param[in] databasePath Explicit path, or empty for the application data path.
     */
    explicit AppDatabase(QString databasePath = {});
    /** @brief Closes and removes the owned Qt SQL connection. */
    ~AppDatabase();

    AppDatabase(const AppDatabase&) = delete;
    AppDatabase& operator=(const AppDatabase&) = delete;

    /** @brief Opens the database, migrates its schema, and recovers interrupted work. */
    [[nodiscard]] bool open(QString* error = nullptr);
    /** @return `true` when the underlying Qt SQL connection is open. */
    [[nodiscard]] bool isOpen() const noexcept;
    /** @return Absolute path of the SQLite database file. */
    [[nodiscard]] QString databasePath() const;

    /** @brief Returns an existing default channel or creates one for `telegramId`. */
    [[nodiscard]] qint64 ensureDefaultChannel(const QString& telegramId, QString* error = nullptr);
    /** @brief Adds an enabled channel and returns its key, or zero on failure. */
    [[nodiscard]] qint64 addChannel(const QString& name, const QString& telegramId,
                                    QString* error = nullptr);
    /** @brief Selects an enabled channel as the single default destination. */
    [[nodiscard]] bool setDefaultChannel(qint64 channelId, QString* error = nullptr);
    /** @brief Soft-deletes a non-required channel from future selection. */
    [[nodiscard]] bool disableChannel(qint64 channelId, QString* error = nullptr);
    /** @return Channels ordered for presentation, optionally including disabled rows. */
    [[nodiscard]] QList<ChannelRecord> channels(bool includeDisabled = false) const;
    /** @return The enabled default channel, when one exists. */
    [[nodiscard]] std::optional<ChannelRecord> defaultChannel() const;
    /** @brief Changes the channel's validated publication time zone. */
    [[nodiscard]] bool setChannelTimeZone(qint64 channelId, const QString& timeZoneId,
                                          QString* error = nullptr);
    /** @brief Saves the channel's default caption template. */
    [[nodiscard]] bool setChannelCaptionTemplate(qint64 channelId, const QString& captionTemplate,
                                                 QString* error = nullptr);

    /** @brief Adds a validated local-time schedule slot and returns its key. */
    [[nodiscard]] qint64 addScheduleSlot(qint64 channelId, int dayOfWeek, const QString& time,
                                         QString* error = nullptr);
    /** @brief Removes one schedule slot by primary key. */
    [[nodiscard]] bool removeScheduleSlot(qint64 slotId, QString* error = nullptr);
    /** @return Enabled schedule slots, optionally filtered by channel key. */
    [[nodiscard]] QList<ScheduleSlotRecord> scheduleSlots(qint64 channelId = 0) const;

    /** @brief Inserts a new queued video and returns its key, or zero on failure. */
    [[nodiscard]] qint64 addVideo(const QString& url, qint64 requestChatId, qint64 requestUserId,
                                  qint64 channelId, const QDateTime& scheduledAtUtc,
                                  QString* error = nullptr, bool approved = true,
                                  bool allowDuplicate = false, const QString& caption = {});
    /** @return Existing non-cancelled publication of a URL or canonical ID in a channel. */
    [[nodiscard]] std::optional<VideoRecord> duplicateVideo(const QString& sourceUrlOrId,
                                                            qint64 channelId) const;
    /** @brief Edits a local video's caption before upload. */
    [[nodiscard]] bool setVideoCaption(qint64 videoId, const QString& caption,
                                       QString* error = nullptr);
    /** @brief Approves a draft for publication. */
    [[nodiscard]] bool approveVideo(qint64 videoId, QString* error = nullptr);
    /** @brief Enables or clears local draft status. */
    [[nodiscard]] bool setVideoDraft(qint64 videoId, bool draft, QString* error = nullptr);
    /** @brief Saves the canonical source identity used by the duplicate guard. */
    [[nodiscard]] bool setVideoSourceId(qint64 videoId, const QString& sourceVideoId,
                                        QString* error = nullptr);
    /** @brief Alias used by downloader metadata handling. */
    [[nodiscard]] bool setSourceVideoId(qint64 videoId, const QString& sourceVideoId,
                                        QString* error = nullptr);
    /** @return Whether another non-cancelled item has the same channel and identity. */
    [[nodiscard]] bool hasDuplicateSourceVideo(qint64 videoId) const;
    /** @brief Records the user's explicit repeat-publication decision. */
    [[nodiscard]] bool setVideoAllowDuplicate(qint64 videoId, bool allow, QString* error = nullptr);
    /** @return The requested video, when it exists. */
    [[nodiscard]] std::optional<VideoRecord> video(qint64 videoId) const;
    /** @return The next queued item requiring download. */
    [[nodiscard]] std::optional<VideoRecord> nextVideoForDownload() const;
    /** @return The next locally cached item due for Bot API upload. */
    [[nodiscard]] std::optional<VideoRecord> nextVideoForUpload(const QDateTime& nowUtc) const;
    /** @return The next cached item ready for immediate TDLib submission. */
    [[nodiscard]] std::optional<VideoRecord> nextVideoForServerSubmission() const;
    /** @return All active queue states in publication order. */
    [[nodiscard]] QList<VideoRecord> activeVideos() const;
    /** @return Failed items eligible for manual retry or cancellation. */
    [[nodiscard]] QList<VideoRecord> failedVideos() const;
    /** @return Up to `limit` completed items, newest first. */
    [[nodiscard]] QList<VideoRecord> sentVideos(int limit = 200) const;
    /** @return Latest planned active timestamp for a channel. */
    [[nodiscard]] QDateTime latestPlannedTime(qint64 channelId) const;
    /** @return Latest completed publication timestamp for a channel. */
    [[nodiscard]] QDateTime latestPublishedTime(qint64 channelId) const;
    /** @return Earliest ready local publication timestamp across all channels. */
    [[nodiscard]] QDateTime nextReadyPublicationTime() const;
    /** @brief Restores interrupted transient states to safe restartable states. */
    [[nodiscard]] bool recoverInterruptedVideos(QString* error = nullptr);

    /** @brief Atomically moves a queued video into `downloading`. */
    [[nodiscard]] bool markDownloading(qint64 videoId, QString* error = nullptr);
    /** @brief Persists bounded download progress for UI recovery. */
    [[nodiscard]] bool updateDownloadProgress(qint64 videoId, double progress,
                                              QString* error = nullptr);
    /** @brief Persists metadata extracted by `yt-dlp`. */
    [[nodiscard]] bool updateMetadata(qint64 videoId, const QString& title, const QString& author,
                                      int durationSeconds, const QString& thumbnailPath,
                                      QString* error = nullptr);
    /** @brief Marks a download ready and records its owned local file. */
    [[nodiscard]] bool markReady(qint64 videoId, const QString& localFilePath, qint64 fileSize,
                                 QString* error = nullptr);
    /** @brief Atomically moves a ready video into `uploading`. */
    [[nodiscard]] bool markUploading(qint64 videoId, QString* error = nullptr);
    /** @brief Completes a local publication and clears transient queue state. */
    [[nodiscard]] bool markSent(qint64 videoId, const QDateTime& publishedAtUtc,
                                QString* error = nullptr);
    /** @brief Records Telegram ownership of a server-scheduled message. */
    [[nodiscard]] bool markServerScheduled(qint64 videoId, const QString& tdChatId,
                                           const QString& tdMessageId, QString* error = nullptr);
    /** @brief Completes an immediate TDLib publication with remote identifiers. */
    [[nodiscard]] bool markServerPublished(qint64 videoId, const QString& tdChatId,
                                           const QString& tdMessageId,
                                           const QDateTime& publishedAtUtc,
                                           QString* error = nullptr);
    /** @brief Persists a confirmed Bot API publication and its remote identifiers. */
    [[nodiscard]] bool markBotPublished(qint64 videoId, const QString& chatId,
                                        const QString& messageId, const QDateTime& publishedAtUtc,
                                        QString* error = nullptr);
    /** @return Remotely owned messages that require Telegram reconciliation. */
    [[nodiscard]] QList<VideoRecord> serverVideosForReconciliation() const;
    /** @brief Updates the catalog date after Telegram accepts a remote reschedule. */
    [[nodiscard]] bool updateServerSchedule(qint64 videoId, const QDateTime& scheduledAtUtc,
                                            QString* error = nullptr);
    /** @brief Compatibility no-op: elapsed time never proves remote publication. */
    [[nodiscard]] bool finalizeElapsedServerSchedules(const QDateTime& nowUtc,
                                                      QString* error = nullptr);
    /** @brief Moves an active item to `failed` with a sanitized diagnostic. */
    [[nodiscard]] bool markFailed(qint64 videoId, const QString& message, QString* error = nullptr);
    /** @brief Persists ambiguous delivery for explicit remote or manual confirmation. */
    [[nodiscard]] bool markDeliveryUnknown(qint64 videoId, const QString& message,
                                           QString* error = nullptr);
    /** @brief Resolves ambiguity after the user verifies whether Telegram published it. */
    [[nodiscard]] bool resolveDeliveryUnknown(qint64 videoId, bool published,
                                              QString* error = nullptr);
    /** @brief Saves an automatic retry deadline on a failed item. */
    [[nodiscard]] bool scheduleRetry(qint64 videoId, const QDateTime& whenUtc,
                                     const QString& category, QString* error = nullptr);
    /** @brief Restores due failed items to their restartable queue stage. */
    [[nodiscard]] bool retryDueVideos(const QDateTime& nowUtc, QString* error = nullptr);
    /** @brief Saves the failure category displayed with recovery actions. */
    [[nodiscard]] bool setFailureCategory(qint64 videoId, const QString& category,
                                          QString* error = nullptr);
    /** @brief Adds a bounded diagnostic entry to a video's persistent history. */
    [[nodiscard]] bool appendVideoEvent(qint64 videoId, const QString& category,
                                        const QString& message, QString* error = nullptr);
    /** @return The latest 200 persistent diagnostic entries for one video. */
    [[nodiscard]] QVariantList videoEvents(qint64 videoId) const;
    /** @return Recent persistent events, including application-level entries. */
    [[nodiscard]] QVariantList recentEvents(int limit = 500) const;
    /** @brief Clears the persistent diagnostic log on explicit user request. */
    [[nodiscard]] bool clearEvents(QString* error = nullptr);
    /** @return IDs whose cached media is safe to remove; SQL failures return no candidates. */
    [[nodiscard]] QList<qint64> cacheCleanupCandidates() const;
    /** @brief Clears local download state and returns an item to `queued`. */
    [[nodiscard]] bool requeueForDownload(qint64 videoId, QString* error = nullptr);
    /** @brief Cancels an item after any required remote cancellation succeeds. */
    [[nodiscard]] bool cancelVideo(qint64 videoId, QString* error = nullptr);
    /** @brief Returns a failed item to the appropriate queue stage. */
    [[nodiscard]] bool retryVideo(qint64 videoId, QString* error = nullptr);
    /** @brief Flags an active item to publish at the next processing opportunity. */
    [[nodiscard]] bool publishVideoNow(qint64 videoId, QString* error = nullptr);
    /** @brief Moves an eligible local item by one queue position. */
    [[nodiscard]] bool moveVideo(qint64 videoId, int direction, QString* error = nullptr);
    /** @brief Replaces the planned UTC time of an eligible item. */
    [[nodiscard]] bool setVideoSchedule(qint64 videoId, const QDateTime& scheduledAtUtc,
                                        QString* error = nullptr);
    /** @brief Assigns an eligible item to another enabled channel. */
    [[nodiscard]] bool setVideoChannel(qint64 videoId, qint64 channelId, QString* error = nullptr);

    /** @return QML-ready channel maps. */
    [[nodiscard]] QVariantList channelEntries() const;
    /** @return QML-ready schedule-slot maps. */
    [[nodiscard]] QVariantList scheduleEntries() const;
    /** @return QML-ready active-video maps. */
    [[nodiscard]] QVariantList activeVideoEntries() const;
    /** @return QML-ready failed-video maps. */
    [[nodiscard]] QVariantList failedVideoEntries() const;
    /** @return QML-ready sent-video maps, bounded by `limit`. */
    [[nodiscard]] QVariantList sentVideoEntries(int limit = 200) const;

    /** @brief Exports configuration and catalog data, excluding secrets, to JSON. */
    [[nodiscard]] bool exportBackup(const QString& filePath, QString* error = nullptr) const;
    /** @brief Validates and transactionally imports a supported JSON backup. */
    [[nodiscard]] bool importBackup(const QString& filePath, QString* error = nullptr);

private:
    [[nodiscard]] bool initializeSchema(QString* error);
    [[nodiscard]] bool execute(const QString& sql, QString* error = nullptr) const;
    [[nodiscard]] bool updateVideoStatus(qint64 videoId, const QString& status,
                                         const QString& errorMessage, bool incrementRetry,
                                         QString* error);
    [[nodiscard]] std::optional<VideoRecord>
    firstVideoForQuery(const QString& whereClause, const QVariantList& bindings = {}) const;
    [[nodiscard]] QList<VideoRecord>
    videosForQuery(const QString& whereClause, const QVariantList& bindings = {},
                   const QString& orderBy = QStringLiteral("queue_order ASC"),
                   int limit = -1) const;
    [[nodiscard]] static VideoRecord videoFromQuery(const class QSqlQuery& query);
    [[nodiscard]] static QVariantMap videoEntry(const VideoRecord& video);
    [[nodiscard]] static QString displayTime(const QDateTime& utcTime);
    [[nodiscard]] static QString normalizedChannelName(const QString& name,
                                                       const QString& telegramId);
    [[nodiscard]] static bool validTelegramChannelId(const QString& telegramId);

    QString m_databasePath;
    QString m_connectionName;
    QSqlDatabase m_database;
};

#endif // TIKTOK_TELEGRAM_BOT_STORAGE_APP_DATABASE_H
