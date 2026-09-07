/******************************************************************************
 * @file    SettingsController.h
 * @brief   Declares the QML-facing application settings and catalog facade.
 *
 * @author  Zheludchenko Yehor
 * @date    2026-08-30
 * @version 2.1.0
 *
 * @copyright Copyright (c) 2026 TikTok Telegram Bot.
 *
 * Target: Qt 6.11.2 (MSVC 2022 64-bit, C++23)
 ******************************************************************************/

#ifndef TIKTOK_TELEGRAM_BOT_UI_SETTINGS_CONTROLLER_H
#define TIKTOK_TELEGRAM_BOT_UI_SETTINGS_CONTROLLER_H

#include "app/BotController.h"
#include "app/CacheManager.h"
#include "app/DiagnosticsService.h"
#include "app/UpdateManager.h"
#include "storage/AppDatabase.h"

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

class AppConfig;

/**
 * @brief Exposes persisted settings, lifecycle commands, and catalog models to QML.
 *
 * This facade owns the database, running workflow, optional TDLib publisher, and
 * update manager. Public invokables execute on the main Qt thread.
 */
class SettingsController final : public QObject
{
    Q_OBJECT
    /** @brief Editable Telegram bot token. */
    Q_PROPERTY(QString botToken READ botToken WRITE setBotToken NOTIFY settingsChanged)
    /** @brief Editable default Telegram destination. */
    Q_PROPERTY(QString channelId READ channelId WRITE setChannelId NOTIFY settingsChanged)
    /** @brief Editable authorized Telegram user identifier. */
    Q_PROPERTY(QString adminUserId READ adminUserId WRITE setAdminUserId NOTIFY settingsChanged)
    /** @brief Editable yt-dlp executable name or path. */
    Q_PROPERTY(QString ytDlpExecutable READ ytDlpExecutable WRITE setYtDlpExecutable NOTIFY
                   settingsChanged)
    /** @brief Fixed fallback interval in minutes. */
    Q_PROPERTY(int publicationIntervalMinutes READ publicationIntervalMinutes WRITE
                   setPublicationIntervalMinutes NOTIFY settingsChanged)
    /** @brief Active local or Telegram-server mode selection. */
    Q_PROPERTY(QString publicationMode READ publicationMode WRITE setPublicationMode NOTIFY
                   settingsChanged)
    /** @brief Editable tdjson dynamic-library path. */
    Q_PROPERTY(QString tdLibPath READ tdLibPath WRITE setTdLibPath NOTIFY settingsChanged)
    /** @brief Editable Telegram application identifier. */
    Q_PROPERTY(QString tdApiId READ tdApiId WRITE setTdApiId NOTIFY settingsChanged)
    /** @brief Editable Telegram application secret hash. */
    Q_PROPERTY(QString tdApiHash READ tdApiHash WRITE setTdApiHash NOTIFY settingsChanged)
    /** @brief Editable TDLib user account phone number. */
    Q_PROPERTY(
        QString tdPhoneNumber READ tdPhoneNumber WRITE setTdPhoneNumber NOTIFY settingsChanged)
    /** @brief Machine-readable TDLib authorization state. */
    Q_PROPERTY(QString tdAuthorizationState READ tdAuthorizationState NOTIFY tdAuthorizationChanged)
    /** @brief User-facing TDLib authorization status. */
    Q_PROPERTY(
        QString tdAuthorizationStatus READ tdAuthorizationStatus NOTIFY tdAuthorizationChanged)
    /** @brief Whether TDLib can publish messages. */
    Q_PROPERTY(bool tdReady READ tdReady NOTIFY tdAuthorizationChanged)
    /** @brief Whether polling and queue processing are active. */
    Q_PROPERTY(bool botRunning READ botRunning NOTIFY botRunningChanged)
    /** @brief Latest user-facing application status. */
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusChanged)
    /** @brief Whether the current status is an error. */
    Q_PROPERTY(bool statusIsError READ statusIsError NOTIFY statusChanged)
    /** @brief Whether native protected storage is active. */
    Q_PROPERTY(bool secureSecretStorage READ secureSecretStorage CONSTANT)
    /** @brief Description of secret persistence. */
    Q_PROPERTY(QString secretStorageDescription READ secretStorageDescription CONSTANT)
    /** @brief Active and server-scheduled QML catalog entries. */
    Q_PROPERTY(QVariantList scheduledVideos READ scheduledVideos NOTIFY dataModelsChanged)
    /** @brief Completed QML catalog entries. */
    Q_PROPERTY(QVariantList sentVideos READ sentVideos NOTIFY dataModelsChanged)
    /** @brief Failed QML catalog entries. */
    Q_PROPERTY(QVariantList failedVideos READ failedVideos NOTIFY dataModelsChanged)
    /** @brief Configured QML channel entries. */
    Q_PROPERTY(QVariantList channels READ channels NOTIFY dataModelsChanged)
    /** @brief Configured QML schedule-slot entries. */
    Q_PROPERTY(QVariantList scheduleSlots READ scheduleSlots NOTIFY dataModelsChanged)
    /** @brief Recent sanitized QML activity entries. */
    Q_PROPERTY(QVariantList logs READ logs NOTIFY logsChanged)
    /** @brief Current dashboard metrics snapshot. */
    Q_PROPERTY(QVariantMap metrics READ metrics NOTIFY metricsChanged)
    /** @brief Whether user-level autostart is enabled. */
    Q_PROPERTY(bool autostartEnabled READ autostartEnabled NOTIFY autostartChanged)
    /** @brief Absolute path of the SQLite database. */
    Q_PROPERTY(QString databasePath READ databasePath CONSTANT)
    /** @brief Owned update manager exposed to QML. */
    Q_PROPERTY(QObject* updateManager READ updateManager CONSTANT)
    /** @brief IANA time-zone identifiers available for channel scheduling. */
    Q_PROPERTY(QStringList availableTimeZones READ availableTimeZones CONSTANT)
    /** @brief Latest asynchronous dependency and Telegram rights checks. */
    Q_PROPERTY(QVariantList diagnostics READ diagnostics NOTIFY diagnosticsChanged)
    /** @brief Configured maximum cache size in mebibytes. */
    Q_PROPERTY(int cacheLimitMiB READ cacheLimitMiB WRITE setCacheLimitMiB NOTIFY settingsChanged)
    /** @brief Disk space in mebibytes reserved outside the cache. */
    Q_PROPERTY(
        int minFreeDiskMiB READ minFreeDiskMiB WRITE setMinFreeDiskMiB NOTIFY settingsChanged)
    /** @brief Retention period for completed cache files. */
    Q_PROPERTY(int cacheRetentionDays READ cacheRetentionDays WRITE setCacheRetentionDays NOTIFY
                   settingsChanged)
    /** @brief Reserved runtime language setting. */
    Q_PROPERTY(QString uiLanguage READ uiLanguage WRITE setUiLanguage NOTIFY settingsChanged)

public:
    /**
     * @brief Constructs the QML-facing application facade.
     *
     * @param[in] parent Parent object that owns this instance; may be nullptr.
     * @param[in] databasePath Optional isolated database path used by verification tools.
     */
    explicit SettingsController(QObject* parent = nullptr, const QString& databasePath = {});
    /** @brief Stops owned services before destroying persistent resources. */
    ~SettingsController() override;

    /** @return The currently entered bot token. */
    [[nodiscard]] QString botToken() const;
    /** @return The default Telegram destination. */
    [[nodiscard]] QString channelId() const;
    /** @return The authorized submitter's Telegram user identifier as text. */
    [[nodiscard]] QString adminUserId() const;
    /** @return The configured yt-dlp executable name or path. */
    [[nodiscard]] QString ytDlpExecutable() const;
    /** @return The fallback interval between posts, in minutes. */
    [[nodiscard]] int publicationIntervalMinutes() const noexcept;
    /** @return `local` or `telegram_server`. */
    [[nodiscard]] QString publicationMode() const;
    /** @return The configured tdjson dynamic-library path. */
    [[nodiscard]] QString tdLibPath() const;
    /** @return The Telegram application identifier as text. */
    [[nodiscard]] QString tdApiId() const;
    /** @return The transiently loaded Telegram application API hash. */
    [[nodiscard]] QString tdApiHash() const;
    /** @return The TDLib user account phone number. */
    [[nodiscard]] QString tdPhoneNumber() const;
    /** @return Stable TDLib authorization state used to select the input form. */
    [[nodiscard]] QString tdAuthorizationState() const;
    /** @return User-facing TDLib authorization status. */
    [[nodiscard]] QString tdAuthorizationStatus() const;
    /** @return `true` when TDLib is authorized. */
    [[nodiscard]] bool tdReady() const noexcept;
    /** @return `true` while Telegram polling and queue processing are active. */
    [[nodiscard]] bool botRunning() const noexcept;
    /** @return Latest user-facing lifecycle or validation status. */
    [[nodiscard]] QString statusMessage() const;
    /** @return `true` when the current status represents an error. */
    [[nodiscard]] bool statusIsError() const noexcept;
    /** @return `true` when secrets use native protected storage. */
    [[nodiscard]] bool secureSecretStorage() const noexcept;
    /** @return User-facing description of secret persistence. */
    [[nodiscard]] QString secretStorageDescription() const;
    /** @return Active and server-scheduled video entries for QML. */
    [[nodiscard]] QVariantList scheduledVideos() const;
    /** @return Completed video entries for QML. */
    [[nodiscard]] QVariantList sentVideos() const;
    /** @return Failed video entries for QML. */
    [[nodiscard]] QVariantList failedVideos() const;
    /** @return Configured channel entries for QML. */
    [[nodiscard]] QVariantList channels() const;
    /** @return Configured calendar slot entries for QML. */
    [[nodiscard]] QVariantList scheduleSlots() const;
    /** @return Recent sanitized activity entries for QML. */
    [[nodiscard]] QVariantList logs() const;
    /** @return Current dashboard metrics. */
    [[nodiscard]] QVariantMap metrics() const;
    /** @return `true` when user-level application autostart is enabled. */
    [[nodiscard]] bool autostartEnabled() const;
    /** @return Absolute SQLite database path. */
    [[nodiscard]] QString databasePath() const;
    /** @return Non-owning QML access to the owned update manager. */
    [[nodiscard]] QObject* updateManager();

    /** @return Installed time zone identifiers for channel schedules. */
    QStringList availableTimeZones() const;
    /** @return Latest dependency and channel-rights diagnostics. */
    QVariantList diagnostics() const;
    /** @return Maximum cache size in MiB. */
    int cacheLimitMiB() const;
    /** @return Reserved free disk space in MiB. */
    int minFreeDiskMiB() const;
    /** @return Retention in days for completed media. */
    int cacheRetentionDays() const;
    /** @return Current language selection (system, en, ru). */
    QString uiLanguage() const;
    /** @brief Persists the maximum cache size. */
    void setCacheLimitMiB(int value);
    /** @brief Persists the free-space reserve. */
    void setMinFreeDiskMiB(int value);
    /** @brief Persists terminal-media retention. */
    void setCacheRetentionDays(int value);
    /** @brief Persists the language and requests runtime translation refresh. */
    void setUiLanguage(const QString& value);

    /** @brief Validates a batch without inserting anything. */
    Q_INVOKABLE QVariantList previewUrls(const QString& text, qint64 channelId,
                                         bool allowDuplicate);
    /** @brief Inserts validated batch items; returns individual errors and accepted count. */
    Q_INVOKABLE QVariantMap addVideos(const QString& text, qint64 channelId,
                                      const QString& localDateTime, bool draft,
                                      bool allowDuplicate);
    /** @brief Applies a caption to an editable local video. */
    Q_INVOKABLE void setVideoCaption(qint64 videoId, const QString& caption);
    /** @brief Approves a draft for automatic publication. */
    Q_INVOKABLE void approveVideo(qint64 videoId);
    /** @brief Holds or releases a local video for review. */
    Q_INVOKABLE void setVideoDraft(qint64 videoId, bool draft);
    /** @brief Allows a deliberate duplicate after user review. */
    Q_INVOKABLE void allowVideoRepeat(qint64 videoId);
    /** @brief Records the user's verification of an ambiguous delivery. */
    Q_INVOKABLE void resolveDeliveryUnknown(qint64 videoId, bool published);
    /** @brief Applies one operation to a selection and reports partial failures. */
    Q_INVOKABLE QVariantMap bulkVideoAction(const QVariantList& ids, const QString& action,
                                            const QString& value);
    /** @brief Opens only an owned, existing cached media file in the default player. */
    Q_INVOKABLE void openVideoFile(qint64 videoId);
    /** @return Persistent activity for one video. */
    Q_INVOKABLE QVariantList videoEvents(qint64 videoId) const;
    /** @brief Changes a channel's schedule time zone. */
    Q_INVOKABLE void setChannelTimeZone(qint64 channelId, const QString& zone);
    /** @brief Changes a channel's default caption template. */
    Q_INVOKABLE void setChannelCaptionTemplate(qint64 channelId, const QString& text);
    /** @brief Moves a calendar entry, preserving its wall time in the channel's zone. */
    Q_INVOKABLE void moveVideoToDate(qint64 videoId, const QString& date);
    /** @brief Starts asynchronous tool and Telegram permission checks. */
    Q_INVOKABLE void runDiagnostics();
    /** @brief Cleans eligible media using configured retention. */
    Q_INVOKABLE void cleanCache();

    /** @brief Replaces the unsaved bot token field. @param[in] value New token. */
    void setBotToken(const QString& value);
    /** @brief Replaces the default channel field. @param[in] value New destination. */
    void setChannelId(const QString& value);
    /** @brief Replaces the administrator field. @param[in] value New user ID text. */
    void setAdminUserId(const QString& value);
    /** @brief Replaces the yt-dlp field. @param[in] value New executable name or path. */
    void setYtDlpExecutable(const QString& value);
    /** @brief Replaces the fallback interval. @param[in] value New interval in minutes. */
    void setPublicationIntervalMinutes(int value);
    /** @brief Selects publication mode. @param[in] value `local` or `telegram_server`. */
    void setPublicationMode(const QString& value);
    /** @brief Replaces the tdjson field. @param[in] value New dynamic-library path. */
    void setTdLibPath(const QString& value);
    /** @brief Replaces the API ID field. @param[in] value New identifier text. */
    void setTdApiId(const QString& value);
    /** @brief Replaces the API hash. @param[in] value New transient secret. */
    void setTdApiHash(const QString& value);
    /** @brief Replaces the account phone. @param[in] value New international number. */
    void setTdPhoneNumber(const QString& value);

    /** @brief Validates, persists, and starts the selected publication workflow. */
    Q_INVOKABLE void saveAndStart();
    /** @brief Stops queue processing and Telegram polling. */
    Q_INVOKABLE void stopBot();
    /** @brief Starts automatically when complete saved settings exist. */
    Q_INVOKABLE void startIfConfigured();
    /** @brief Applies a selected yt-dlp file. @param[in] url Local file URL. */
    Q_INVOKABLE void setYtDlpFromUrl(const QUrl& url);
    /** @brief Applies a selected tdjson file. @param[in] url Local file URL. */
    Q_INVOKABLE void setTdLibFromUrl(const QUrl& url);
    /** @brief Starts or resumes TDLib authorization without starting Bot API polling. */
    Q_INVOKABLE void connectTdPublisher();
    /**
     * @brief Submits the value currently requested by TDLib authorization.
     * @param[in] value Primary credential or registration first name.
     * @param[in] secondValue Registration last name, when requested.
     */
    Q_INVOKABLE void submitTdAuthorization(const QString& value, const QString& secondValue = {});
    /** @brief Logs out the authorized TDLib user session. */
    Q_INVOKABLE void logOutTdPublisher();

    /** @brief Cancels a local or remote video. @param[in] videoId Catalog identifier. */
    Q_INVOKABLE void cancelVideo(qint64 videoId);
    /** @brief Retries a failed video. @param[in] videoId Catalog identifier. */
    Q_INVOKABLE void retryVideo(qint64 videoId);
    /** @brief Publishes a video now. @param[in] videoId Catalog identifier. */
    Q_INVOKABLE void publishVideoNow(qint64 videoId);
    /**
     * @brief Moves an eligible video one queue position.
     * @param[in] videoId Catalog identifier.
     * @param[in] direction Negative to move earlier, positive to move later.
     */
    Q_INVOKABLE void moveVideo(qint64 videoId, int direction);
    /**
     * @brief Applies a local ISO timestamp selected by QML.
     * @param[in] videoId Catalog identifier.
     * @param[in] localIsoDateTime Local ISO 8601 timestamp.
     */
    Q_INVOKABLE void setVideoSchedule(qint64 videoId, const QString& localIsoDateTime);
    /**
     * @brief Assigns an eligible video to another channel.
     * @param[in] videoId Catalog identifier.
     * @param[in] channelId Destination database key.
     */
    Q_INVOKABLE void setVideoChannel(qint64 videoId, qint64 channelId);

    /**
     * @brief Adds a Telegram destination.
     * @param[in] name User-facing channel name.
     * @param[in] telegramId Channel username or numeric identifier.
     */
    Q_INVOKABLE void addChannel(const QString& name, const QString& telegramId);
    /** @brief Selects the default destination. @param[in] channelId Channel key. */
    Q_INVOKABLE void setDefaultChannel(qint64 channelId);
    /** @brief Disables a channel. @param[in] channelId Channel key. */
    Q_INVOKABLE void removeChannel(qint64 channelId);
    /**
     * @brief Adds a local-time calendar slot for a channel.
     * @param[in] channelId Destination channel key.
     * @param[in] dayOfWeek Zero for daily, otherwise Qt weekday one through seven.
     * @param[in] time Local wall time in `HH:mm` form.
     */
    Q_INVOKABLE void addScheduleSlot(qint64 channelId, int dayOfWeek, const QString& time);
    /** @brief Removes a calendar slot. @param[in] slotId Schedule slot key. */
    Q_INVOKABLE void removeScheduleSlot(qint64 slotId);

    /** @brief Changes application autostart. @param[in] enabled Desired state. */
    Q_INVOKABLE void setAutostartEnabled(bool enabled);
    /** @brief Exports a non-secret JSON backup. @param[in] fileUrl Destination URL. */
    Q_INVOKABLE void exportBackup(const QUrl& fileUrl);
    /** @brief Imports a validated JSON backup. @param[in] fileUrl Source URL. */
    Q_INVOKABLE void importBackup(const QUrl& fileUrl);
    /** @brief Deletes persisted activity-log entries. */
    Q_INVOKABLE void clearLogs();
    /** @brief Rebuilds QML-facing models from the current database state. */
    Q_INVOKABLE void refreshDataModels();

signals:
    /** @brief Indicates that one or more editable settings changed. */
    void settingsChanged();
    /** @brief Indicates that the running state changed. */
    void botRunningChanged();
    /** @brief Indicates that lifecycle status text or severity changed. */
    void statusChanged();
    /** @brief Indicates that catalog, channel, or schedule models changed. */
    void dataModelsChanged();
    /** @brief Indicates that the activity-log model changed. */
    void logsChanged();
    /** @brief Indicates that dashboard metrics changed. */
    void metricsChanged();
    /** @brief Indicates that the autostart state changed. */
    void autostartChanged();
    /** @brief Indicates that TDLib authorization state or text changed. */
    void tdAuthorizationChanged();
    /** @brief Diagnostic results changed. */
    void diagnosticsChanged();
    /** @brief Runtime translation must be reloaded. */
    void languageChanged();
    /**
     * @brief Requests display of a native user notification.
     * @param[in] title Notification title.
     * @param[in] message Notification body.
     */
    void notificationRequested(const QString& title, const QString& message);
    /** @brief Requests a clean application exit. */
    void applicationQuitRequested();

private:
    void loadSettings();
    void migrateLegacyCatalog();
    [[nodiscard]] bool persistSettings(const AppConfig& config, QString* error);
    void startWithConfiguration(AppConfig config);
    [[nodiscard]] bool startTdPublisher(QString* error);
    [[nodiscard]] bool validateTdSettings(QString* error) const;
    void setBotRunning(bool running);
    void setStatus(QString message, bool isError);
    void appendLog(const QString& level, const QString& message);
    void applyDatabaseAction(bool success, const QString& successMessage,
                             const QString& errorMessage);
    void wakeQueue();
    void updateCachePolicy(const QString& key, int value);
    [[nodiscard]] QDateTime parseChannelDateTime(qint64 channelId, const QString& text) const;

    QString m_botToken;
    QString m_channelId;
    QString m_adminUserId;
    QString m_ytDlpExecutable{QStringLiteral("yt-dlp")};
    int m_publicationIntervalMinutes{120};
    QString m_publicationMode{QStringLiteral("local")};
    QString m_tdLibPath;
    QString m_tdApiId;
    QString m_tdApiHash;
    QString m_tdPhoneNumber;
    QByteArray m_tdDatabaseEncryptionKey;
    std::unique_ptr<AppDatabase> m_database;
    std::unique_ptr<BotController> m_botController;
    std::unique_ptr<TdPublisher> m_tdPublisher;
    UpdateManager m_updateManager;
    DiagnosticsService m_diagnostics;
    std::unique_ptr<CacheManager> m_idleCache;
    QVariantList m_scheduledVideos;
    QVariantList m_sentVideos;
    QVariantList m_failedVideos;
    QVariantList m_channels;
    QVariantList m_scheduleSlots;
    QVariantList m_logs;
    QVariantMap m_metrics;
    QString m_statusMessage{QStringLiteral("Enter the settings and select Save & Start.")};
    bool m_botRunning{false};
    bool m_statusIsError{false};
};

#endif // TIKTOK_TELEGRAM_BOT_UI_SETTINGS_CONTROLLER_H
