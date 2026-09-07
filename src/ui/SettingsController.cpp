/******************************************************************************
 * @file    SettingsController.cpp
 * @brief   Implements the QML-facing settings, catalog, and lifecycle facade.
 *
 * @author  Zheludchenko Yehor
 * @date    2026-08-30
 * @version 2.1.0
 *
 * @copyright Copyright (c) 2026 TikTok Telegram Bot.
 *
 * Target: Qt 6.11.2 (MSVC 2022 64-bit, C++23)
 ******************************************************************************/

#include "ui/SettingsController.h"

#include "Logging.h"
#include "app/AutostartManager.h"
#include "config/AppConfig.h"
#include "config/CredentialStore.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTimeZone>

#include <algorithm>
#include <chrono>
#include <utility>

namespace
{

constexpr auto ChannelIdSettingsKey = "telegram/channelId";
constexpr auto AdminUserIdSettingsKey = "telegram/adminUserId";
constexpr auto YtDlpSettingsKey = "downloader/ytDlpExecutable";
constexpr auto PublicationIntervalSettingsKey = "scheduler/publicationIntervalMinutes";
constexpr auto PublicationModeSettingsKey = "publisher/mode";
constexpr auto TdLibPathSettingsKey = "tdlib/libraryPath";
constexpr auto TdApiIdSettingsKey = "tdlib/apiId";
constexpr auto TdPhoneSettingsKey = "tdlib/phoneNumber";
constexpr auto LegacyPendingJobsKey = "catalog/pendingJobs";
constexpr auto LegacySentVideosKey = "catalog/sentVideos";
constexpr qsizetype MaximumLogEntries = 500;

QString environmentOrDefault(const char* name, const QString& fallback = {})
{
    const QString value = qEnvironmentVariable(name).trimmed();
    return value.isEmpty() ? fallback : value;
}

} // namespace

SettingsController::SettingsController(QObject* parent, const QString& databasePath)
    : QObject(parent)
    , m_database(std::make_unique<AppDatabase>(databasePath))
    , m_tdPublisher(std::make_unique<TdPublisher>())
{
    loadSettings();
    connect(&m_diagnostics, &DiagnosticsService::changed, this,
            &SettingsController::diagnosticsChanged);

    connect(m_tdPublisher.get(), &TdPublisher::authorizationChanged, this,
            [this]
            {
                emit tdAuthorizationChanged();
                if (m_publicationMode == QStringLiteral("telegram_server"))
                {
                    setStatus(m_tdPublisher->statusText(),
                              m_tdPublisher->authorizationState() == QStringLiteral("error"));
                }
            });
    connect(m_tdPublisher.get(), &TdPublisher::readyChanged, this,
            [this](const bool) { emit tdAuthorizationChanged(); });
    connect(m_tdPublisher.get(), &TdPublisher::activity, this, &SettingsController::appendLog);

    QString databaseError;
    if (!m_database->open(&databaseError))
    {
        setStatus(QStringLiteral("Could not open SQLite database: %1").arg(databaseError), true);
    }
    else
    {
        if (!m_channelId.trimmed().isEmpty())
        {
            const qint64 channelId = m_database->ensureDefaultChannel(m_channelId, &databaseError);
            if (channelId <= 0 && !databaseError.isEmpty())
            {
                setStatus(QStringLiteral("Could not migrate the channel: %1").arg(databaseError),
                          true);
            }
        }
        migrateLegacyCatalog();
        refreshDataModels();
        m_logs = m_database->recentEvents();
    }

    m_updateManager.setYtDlpExecutable(m_ytDlpExecutable);
    connect(&m_updateManager, &UpdateManager::activity, this, &SettingsController::appendLog);
    connect(&m_updateManager, &UpdateManager::applicationRestartRequested, this,
            &SettingsController::applicationQuitRequested);
    if (!QCoreApplication::arguments().contains(QStringLiteral("--ui-smoke-test")))
        m_updateManager.startAutomaticChecks();
}

SettingsController::~SettingsController()
{
    stopBot();
}

QString SettingsController::botToken() const
{
    return m_botToken;
}
QString SettingsController::channelId() const
{
    return m_channelId;
}
QString SettingsController::adminUserId() const
{
    return m_adminUserId;
}
QString SettingsController::ytDlpExecutable() const
{
    return m_ytDlpExecutable;
}
int SettingsController::publicationIntervalMinutes() const noexcept
{
    return m_publicationIntervalMinutes;
}
QString SettingsController::publicationMode() const
{
    return m_publicationMode;
}
QString SettingsController::tdLibPath() const
{
    return m_tdLibPath;
}
QString SettingsController::tdApiId() const
{
    return m_tdApiId;
}
QString SettingsController::tdApiHash() const
{
    return m_tdApiHash;
}
QString SettingsController::tdPhoneNumber() const
{
    return m_tdPhoneNumber;
}
QString SettingsController::tdAuthorizationState() const
{
    return m_tdPublisher ? m_tdPublisher->authorizationState() : QStringLiteral("stopped");
}
QString SettingsController::tdAuthorizationStatus() const
{
    return m_tdPublisher ? m_tdPublisher->statusText() : QStringLiteral("TDLib is stopped.");
}
bool SettingsController::tdReady() const noexcept
{
    return m_tdPublisher && m_tdPublisher->isReady();
}
bool SettingsController::botRunning() const noexcept
{
    return m_botRunning;
}
QString SettingsController::statusMessage() const
{
    return m_statusMessage;
}
bool SettingsController::statusIsError() const noexcept
{
    return m_statusIsError;
}
bool SettingsController::secureSecretStorage() const noexcept
{
    return CredentialStore::usesSecureStorage();
}
QString SettingsController::secretStorageDescription() const
{
    return CredentialStore::storageDescription();
}
QVariantList SettingsController::scheduledVideos() const
{
    return m_scheduledVideos;
}
QVariantList SettingsController::sentVideos() const
{
    return m_sentVideos;
}
QVariantList SettingsController::failedVideos() const
{
    return m_failedVideos;
}
QVariantList SettingsController::channels() const
{
    return m_channels;
}
QVariantList SettingsController::scheduleSlots() const
{
    return m_scheduleSlots;
}
QVariantList SettingsController::logs() const
{
    return m_logs;
}
QVariantMap SettingsController::metrics() const
{
    return m_metrics;
}
bool SettingsController::autostartEnabled() const
{
    return AutostartManager::isEnabled();
}
QString SettingsController::databasePath() const
{
    return m_database ? m_database->databasePath() : QString{};
}
QObject* SettingsController::updateManager()
{
    return &m_updateManager;
}

void SettingsController::setBotToken(const QString& value)
{
    if (m_botToken != value)
    {
        m_botToken = value;
        emit settingsChanged();
    }
}

void SettingsController::setChannelId(const QString& value)
{
    if (m_channelId != value)
    {
        m_channelId = value;
        emit settingsChanged();
    }
}

void SettingsController::setAdminUserId(const QString& value)
{
    if (m_adminUserId != value)
    {
        m_adminUserId = value;
        emit settingsChanged();
    }
}

void SettingsController::setYtDlpExecutable(const QString& value)
{
    if (m_ytDlpExecutable != value)
    {
        m_ytDlpExecutable = value;
        m_updateManager.setYtDlpExecutable(value);
        emit settingsChanged();
    }
}

void SettingsController::setPublicationIntervalMinutes(const int value)
{
    if (m_publicationIntervalMinutes != value)
    {
        m_publicationIntervalMinutes = value;
        emit settingsChanged();
    }
}

void SettingsController::setPublicationMode(const QString& value)
{
    const QString normalized = value == QStringLiteral("telegram_server")
                                   ? QStringLiteral("telegram_server")
                                   : QStringLiteral("local");
    if (m_publicationMode != normalized)
    {
        m_publicationMode = normalized;
        emit settingsChanged();
    }
}

void SettingsController::setTdLibPath(const QString& value)
{
    const QString trimmed = value.trimmed();
    const QString normalized = trimmed.isEmpty() ? QString{} : QDir::cleanPath(trimmed);
    if (m_tdLibPath != normalized)
    {
        m_tdLibPath = normalized;
        emit settingsChanged();
    }
}

void SettingsController::setTdApiId(const QString& value)
{
    if (m_tdApiId != value)
    {
        m_tdApiId = value.trimmed();
        emit settingsChanged();
    }
}

void SettingsController::setTdApiHash(const QString& value)
{
    if (m_tdApiHash != value)
    {
        m_tdApiHash = value.trimmed();
        emit settingsChanged();
    }
}

void SettingsController::setTdPhoneNumber(const QString& value)
{
    if (m_tdPhoneNumber != value)
    {
        m_tdPhoneNumber = value.trimmed();
        emit settingsChanged();
    }
}

void SettingsController::saveAndStart()
{
    if (m_idleCache && m_idleCache->isBusy())
    {
        setStatus(tr("Wait for cache cleanup to finish before starting."), true);
        return;
    }
    if (m_publicationMode == QStringLiteral("telegram_server"))
    {
        QString tdError;
        if (!validateTdSettings(&tdError))
        {
            setStatus(QStringLiteral("Invalid TDLib settings: %1").arg(tdError), true);
            return;
        }
    }
    QString validationError;
    std::optional<AppConfig> config =
        AppConfig::fromValues(m_botToken, m_channelId, m_adminUserId, m_ytDlpExecutable,
                              QString::number(m_publicationIntervalMinutes), &validationError);
    if (!config.has_value())
    {
        setStatus(QStringLiteral("Invalid settings: %1").arg(validationError), true);
        return;
    }
    QString persistenceError;
    if (!persistSettings(*config, &persistenceError))
    {
        setStatus(QStringLiteral("Could not save settings: %1").arg(persistenceError), true);
        return;
    }
    startWithConfiguration(std::move(*config));
}

void SettingsController::stopBot()
{
    if (m_botController)
    {
        m_botController->shutdown();
        m_botController.reset();
    }
    if (m_tdPublisher)
    {
        m_tdPublisher->stop();
    }
    setBotRunning(false);
    setStatus(QStringLiteral("Bot stopped. Queued tasks remain in SQLite."), false);
    refreshDataModels();
}

void SettingsController::startIfConfigured()
{
    if (m_botToken.trimmed().isEmpty() || m_channelId.trimmed().isEmpty() ||
        m_adminUserId.trimmed().isEmpty() || !m_database || !m_database->isOpen())
    {
        return;
    }
    QString validationError;
    std::optional<AppConfig> config =
        AppConfig::fromValues(m_botToken, m_channelId, m_adminUserId, m_ytDlpExecutable,
                              QString::number(m_publicationIntervalMinutes), &validationError);
    if (!config.has_value())
    {
        setStatus(QStringLiteral("Saved settings are invalid: %1").arg(validationError), true);
        return;
    }
    startWithConfiguration(std::move(*config));
}

void SettingsController::setYtDlpFromUrl(const QUrl& url)
{
    if (url.isLocalFile())
    {
        setYtDlpExecutable(url.toLocalFile());
    }
}

void SettingsController::setTdLibFromUrl(const QUrl& url)
{
    if (url.isLocalFile())
    {
        setTdLibPath(url.toLocalFile());
    }
}

void SettingsController::connectTdPublisher()
{
    QString error;
    if (!validateTdSettings(&error))
    {
        setStatus(QStringLiteral("Invalid TDLib settings: %1").arg(error), true);
        return;
    }
    if (!startTdPublisher(&error))
    {
        setStatus(QStringLiteral("Could not start TDLib: %1").arg(error), true);
        return;
    }
    setStatus(QStringLiteral("TDLib started. Follow the authorization prompt below."), false);
}

void SettingsController::submitTdAuthorization(const QString& value, const QString& secondValue)
{
    if (!m_tdPublisher)
    {
        return;
    }
    const QString state = m_tdPublisher->authorizationState();
    if (state == QStringLiteral("wait_phone"))
    {
        setTdPhoneNumber(value);
        m_tdPublisher->submitPhoneNumber(value);
    }
    else if (state == QStringLiteral("wait_code"))
    {
        m_tdPublisher->submitAuthenticationCode(value);
    }
    else if (state == QStringLiteral("wait_password"))
    {
        m_tdPublisher->submitPassword(value);
    }
    else if (state == QStringLiteral("wait_email"))
    {
        m_tdPublisher->submitEmailAddress(value);
    }
    else if (state == QStringLiteral("wait_email_code"))
    {
        m_tdPublisher->submitEmailCode(value);
    }
    else if (state == QStringLiteral("wait_registration"))
    {
        m_tdPublisher->submitRegistration(value, secondValue);
    }
    else
    {
        setStatus(QStringLiteral("TDLib is not waiting for user input."), true);
    }
}

void SettingsController::logOutTdPublisher()
{
    if (m_tdPublisher)
    {
        m_tdPublisher->logOut();
    }
}

void SettingsController::cancelVideo(const qint64 videoId)
{
    const std::optional<VideoRecord> record = m_database->video(videoId);
    const bool remote = record.has_value() && record->status == QStringLiteral("server_scheduled");
    if (remote && !m_botController)
    {
        setStatus(
            QStringLiteral("Start the bot in Telegram server mode before cancelling this post."),
            true);
        return;
    }
    QString error;
    const bool success = m_botController ? m_botController->cancelVideo(videoId, &error)
                                         : m_database->cancelVideo(videoId, &error);
    applyDatabaseAction(success,
                        remote ? QStringLiteral("Cancellation request sent to Telegram.")
                               : QStringLiteral("Video cancelled."),
                        error);
}

void SettingsController::retryVideo(const qint64 videoId)
{
    QString error;
    const bool success = m_botController ? m_botController->retryVideo(videoId, &error)
                                         : m_database->retryVideo(videoId, &error);
    applyDatabaseAction(success, QStringLiteral("Video queued for retry."), error);
}

void SettingsController::publishVideoNow(const qint64 videoId)
{
    const std::optional<VideoRecord> record = m_database->video(videoId);
    const bool remote = record.has_value() && record->status == QStringLiteral("server_scheduled");
    if (remote && !m_botController)
    {
        setStatus(
            QStringLiteral("Start the bot in Telegram server mode before publishing this post."),
            true);
        return;
    }
    QString error;
    const bool success = m_botController ? m_botController->publishVideoNow(videoId, &error)
                                         : m_database->publishVideoNow(videoId, &error);
    applyDatabaseAction(success,
                        remote ? QStringLiteral("Immediate publication request sent to Telegram.")
                               : QStringLiteral("Immediate publication requested."),
                        error);
}

void SettingsController::moveVideo(const qint64 videoId, const int direction)
{
    QString error;
    const bool success = m_botController ? m_botController->moveVideo(videoId, direction, &error)
                                         : m_database->moveVideo(videoId, direction, &error);
    applyDatabaseAction(success, QStringLiteral("Queue order updated."), error);
}

void SettingsController::setVideoSchedule(const qint64 videoId, const QString& localIsoDateTime)
{
    const auto selected = m_database->video(videoId);
    QDateTime dateTime =
        selected ? parseChannelDateTime(selected->channelId, localIsoDateTime) : QDateTime{};
    if (!dateTime.isValid())
    {
        setStatus(QStringLiteral("Use an ISO date and time such as 2026-08-25T18:00."), true);
        return;
    }
    if (dateTime.timeSpec() == Qt::LocalTime)
    {
        dateTime.setTimeZone(QTimeZone::systemTimeZone());
    }
    const std::optional<VideoRecord> record = m_database->video(videoId);
    const bool remote = record.has_value() && record->status == QStringLiteral("server_scheduled");
    if (remote && !m_botController)
    {
        setStatus(
            QStringLiteral("Start the bot in Telegram server mode before rescheduling this post."),
            true);
        return;
    }
    QString error;
    const bool success = m_botController
                             ? m_botController->setVideoSchedule(videoId, dateTime.toUTC(), &error)
                             : m_database->setVideoSchedule(videoId, dateTime.toUTC(), &error);
    applyDatabaseAction(success,
                        remote ? QStringLiteral("New time sent to Telegram.")
                               : QStringLiteral("Publication time updated."),
                        error);
}

void SettingsController::setVideoChannel(const qint64 videoId, const qint64 channelId)
{
    QString error;
    const bool success = m_botController
                             ? m_botController->setVideoChannel(videoId, channelId, &error)
                             : m_database->setVideoChannel(videoId, channelId, &error);
    applyDatabaseAction(success, QStringLiteral("Destination channel updated."), error);
}

void SettingsController::addChannel(const QString& name, const QString& telegramId)
{
    QString error;
    const qint64 id = m_database->addChannel(name, telegramId, &error);
    if (id <= 0)
    {
        setStatus(QStringLiteral("Could not add channel: %1").arg(error), true);
        return;
    }
    if (!m_database->defaultChannel().has_value())
    {
        static_cast<void>(m_database->setDefaultChannel(id, &error));
    }
    refreshDataModels();
    setStatus(QStringLiteral("Channel added."), false);
}

void SettingsController::setDefaultChannel(const qint64 channelId)
{
    QString error;
    if (!m_database->setDefaultChannel(channelId, &error))
    {
        setStatus(QStringLiteral("Could not set default channel: %1").arg(error), true);
        return;
    }
    const std::optional<ChannelRecord> channel = m_database->defaultChannel();
    if (channel.has_value())
    {
        setChannelId(channel->telegramId);
        QSettings settings;
        settings.setValue(QString::fromLatin1(ChannelIdSettingsKey), channel->telegramId);
    }
    refreshDataModels();
    setStatus(QStringLiteral("Default channel changed."), false);
}

void SettingsController::removeChannel(const qint64 channelId)
{
    QString error;
    const bool success = m_database->disableChannel(channelId, &error);
    applyDatabaseAction(success, QStringLiteral("Channel removed."), error);
}

void SettingsController::addScheduleSlot(const qint64 channelId, const int dayOfWeek,
                                         const QString& time)
{
    QString error;
    const qint64 id = m_database->addScheduleSlot(channelId, dayOfWeek, time, &error);
    if (id <= 0)
    {
        setStatus(QStringLiteral("Could not add schedule slot: %1").arg(error), true);
        return;
    }
    refreshDataModels();
    setStatus(QStringLiteral("Schedule slot added."), false);
}

void SettingsController::removeScheduleSlot(const qint64 slotId)
{
    QString error;
    const bool success = m_database->removeScheduleSlot(slotId, &error);
    applyDatabaseAction(success, QStringLiteral("Schedule slot removed."), error);
}

void SettingsController::setAutostartEnabled(const bool enabled)
{
    QString error;
    if (!AutostartManager::setEnabled(enabled, &error))
    {
        setStatus(QStringLiteral("Could not update autostart: %1").arg(error), true);
        return;
    }
    emit autostartChanged();
    setStatus(enabled ? QStringLiteral("Windows autostart enabled.")
                      : QStringLiteral("Windows autostart disabled."),
              false);
}

void SettingsController::exportBackup(const QUrl& fileUrl)
{
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : QString{};
    QString error;
    if (path.isEmpty() || !m_database->exportBackup(path, &error))
    {
        setStatus(QStringLiteral("Could not export backup: %1").arg(error), true);
        return;
    }
    setStatus(QStringLiteral("Backup exported. The Telegram token was not included."), false);
}

void SettingsController::importBackup(const QUrl& fileUrl)
{
    if ((m_idleCache && m_idleCache->isBusy()) || m_botController)
    {
        setStatus(
            tr("Stop the bot and wait for cache operations to finish before restoring a backup."),
            true);
        return;
    }
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : QString{};
    if (path.isEmpty())
    {
        setStatus(QStringLiteral("Select a local backup file."), true);
        return;
    }
    stopBot();
    QString error;
    if (!m_database->importBackup(path, &error))
    {
        setStatus(QStringLiteral("Could not import backup: %1").arg(error), true);
        return;
    }
    const std::optional<ChannelRecord> channel = m_database->defaultChannel();
    if (channel.has_value())
    {
        setChannelId(channel->telegramId);
    }
    refreshDataModels();
    setStatus(QStringLiteral("Backup imported. Review settings and start the bot."), false);
}

void SettingsController::clearLogs()
{
    QString error;
    if (!m_database->clearEvents(&error))
    {
        setStatus(error, true);
        return;
    }
    m_logs.clear();
    emit logsChanged();
}

void SettingsController::refreshDataModels()
{
    if (!m_database || !m_database->isOpen())
    {
        return;
    }
    m_scheduledVideos = m_database->activeVideoEntries();
    m_failedVideos = m_database->failedVideoEntries();
    m_sentVideos = m_database->sentVideoEntries();
    m_channels = m_database->channelEntries();
    m_scheduleSlots = m_database->scheduleEntries();
    emit dataModelsChanged();
}

void SettingsController::loadSettings()
{
    if (QCoreApplication::arguments().contains(QStringLiteral("--ui-smoke-test")))
        return;
    QString credentialError;
    m_botToken = CredentialStore::readTelegramBotToken(&credentialError);
    if (m_botToken.isEmpty())
    {
        m_botToken = environmentOrDefault("TELEGRAM_BOT_TOKEN");
    }
    QSettings settings;
    m_channelId = settings
                      .value(QString::fromLatin1(ChannelIdSettingsKey),
                             environmentOrDefault("TELEGRAM_CHANNEL_ID"))
                      .toString();
    m_adminUserId = settings
                        .value(QString::fromLatin1(AdminUserIdSettingsKey),
                               environmentOrDefault("TELEGRAM_ADMIN_USER_ID"))
                        .toString();
    m_ytDlpExecutable = settings
                            .value(QString::fromLatin1(YtDlpSettingsKey),
                                   environmentOrDefault("YTDLP_PATH", QStringLiteral("yt-dlp")))
                            .toString();
    m_publicationIntervalMinutes =
        settings
            .value(QString::fromLatin1(PublicationIntervalSettingsKey),
                   environmentOrDefault("TELEGRAM_POST_INTERVAL_MINUTES", QStringLiteral("120")))
            .toInt();
    m_publicationMode =
        settings.value(QString::fromLatin1(PublicationModeSettingsKey), QStringLiteral("local"))
            .toString();
    if (m_publicationMode != QStringLiteral("telegram_server"))
    {
        m_publicationMode = QStringLiteral("local");
    }
    m_tdLibPath = settings.value(QString::fromLatin1(TdLibPathSettingsKey)).toString();
    m_tdApiId = settings.value(QString::fromLatin1(TdApiIdSettingsKey)).toString();
    m_tdPhoneNumber = settings.value(QString::fromLatin1(TdPhoneSettingsKey)).toString();
    QString tdCredentialError;
    m_tdApiHash = CredentialStore::readTdApiHash(&tdCredentialError);
    m_tdDatabaseEncryptionKey = CredentialStore::readTdDatabaseEncryptionKey(&tdCredentialError);
    if (!credentialError.isEmpty())
    {
        setStatus(QStringLiteral("Could not read saved bot token: %1").arg(credentialError), true);
    }
    if (!tdCredentialError.isEmpty())
    {
        setStatus(
            QStringLiteral("Could not read saved TDLib credentials: %1").arg(tdCredentialError),
            true);
    }
}

void SettingsController::migrateLegacyCatalog()
{
    QSettings settings;
    const QByteArray pendingData =
        settings.value(QString::fromLatin1(LegacyPendingJobsKey)).toByteArray();
    const QByteArray sentData =
        settings.value(QString::fromLatin1(LegacySentVideosKey)).toByteArray();
    if (pendingData.isEmpty() && sentData.isEmpty())
    {
        return;
    }
    const std::optional<ChannelRecord> channel = m_database->defaultChannel();
    if (!channel.has_value())
    {
        return;
    }

    QDateTime planned = QDateTime::currentDateTimeUtc();
    const qint64 intervalSeconds = std::chrono::duration_cast<std::chrono::seconds>(
                                       std::chrono::minutes(m_publicationIntervalMinutes))
                                       .count();
    const QJsonArray pending = QJsonDocument::fromJson(pendingData).array();
    for (const QJsonValue& value : pending)
    {
        const QJsonObject item = value.toObject();
        bool chatValid = false;
        bool userValid = false;
        const qint64 chatId =
            item.value(QStringLiteral("requestChatId")).toString().toLongLong(&chatValid);
        const qint64 userId =
            item.value(QStringLiteral("requestUserId")).toString().toLongLong(&userValid);
        if (!chatValid || !userValid)
        {
            continue;
        }
        QString error;
        static_cast<void>(m_database->addVideo(item.value(QStringLiteral("sourceUrl")).toString(),
                                               chatId, userId, channel->id, planned, &error));
        planned = planned.addSecs(intervalSeconds);
    }
    const QJsonArray sent = QJsonDocument::fromJson(sentData).array();
    for (const QJsonValue& value : sent)
    {
        const QJsonObject item = value.toObject();
        const QString url = item.value(QStringLiteral("sourceUrl")).toString();
        const QDateTime publishedAt = QDateTime::fromString(
            item.value(QStringLiteral("publishedAtUtc")).toString(), Qt::ISODateWithMs);
        if (url.isEmpty() || !publishedAt.isValid())
        {
            continue;
        }
        QString error;
        const qint64 id = m_database->addVideo(url, 0, 0, channel->id, publishedAt, &error);
        if (id > 0)
        {
            static_cast<void>(m_database->markSent(id, publishedAt, &error));
        }
    }
    settings.remove(QString::fromLatin1(LegacyPendingJobsKey));
    settings.remove(QString::fromLatin1(LegacySentVideosKey));
    settings.sync();
    appendLog(QStringLiteral("info"),
              QStringLiteral("Legacy queue and history migrated to SQLite."));
}

bool SettingsController::persistSettings(const AppConfig& config, QString* error)
{
    if (!CredentialStore::writeTelegramBotToken(config.botToken(), error))
    {
        return false;
    }
    if (m_publicationMode == QStringLiteral("telegram_server"))
    {
        if (m_tdDatabaseEncryptionKey.isEmpty())
        {
            m_tdDatabaseEncryptionKey.resize(32);
            for (char& byte : m_tdDatabaseEncryptionKey)
            {
                byte = static_cast<char>(QRandomGenerator::system()->generate() & 0xffU);
            }
        }
        if (!CredentialStore::writeTdApiHash(m_tdApiHash, error) ||
            !CredentialStore::writeTdDatabaseEncryptionKey(m_tdDatabaseEncryptionKey, error))
        {
            return false;
        }
    }
    const qint64 databaseChannelId = m_database->ensureDefaultChannel(config.channelId(), error);
    if (databaseChannelId <= 0 || !m_database->setDefaultChannel(databaseChannelId, error))
    {
        return false;
    }

    QSettings settings;
    settings.setValue(QString::fromLatin1(ChannelIdSettingsKey), config.channelId());
    settings.setValue(QString::fromLatin1(AdminUserIdSettingsKey),
                      QString::number(config.adminUserId()));
    settings.setValue(QString::fromLatin1(YtDlpSettingsKey), config.ytDlpExecutable());
    settings.setValue(QString::fromLatin1(PublicationIntervalSettingsKey),
                      config.publicationInterval().count());
    settings.setValue(QString::fromLatin1(PublicationModeSettingsKey), m_publicationMode);
    settings.setValue(QString::fromLatin1(TdLibPathSettingsKey), m_tdLibPath);
    settings.setValue(QString::fromLatin1(TdApiIdSettingsKey), m_tdApiId);
    settings.setValue(QString::fromLatin1(TdPhoneSettingsKey), m_tdPhoneNumber);
    settings.sync();
    if (settings.status() != QSettings::NoError)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Local settings storage error.");
        }
        return false;
    }

    m_botToken = config.botToken();
    m_channelId = config.channelId();
    m_adminUserId = QString::number(config.adminUserId());
    m_ytDlpExecutable = config.ytDlpExecutable();
    m_publicationIntervalMinutes = static_cast<int>(config.publicationInterval().count());
    m_updateManager.setYtDlpExecutable(m_ytDlpExecutable);
    emit settingsChanged();
    refreshDataModels();
    return true;
}

void SettingsController::startWithConfiguration(AppConfig config)
{
    if (m_botController)
    {
        m_botController->shutdown();
        m_botController.reset();
    }
    QString tdStartError;
    TdPublisher* publisher = nullptr;
    if (m_publicationMode == QStringLiteral("telegram_server"))
    {
        publisher = m_tdPublisher.get();
        static_cast<void>(startTdPublisher(&tdStartError));
    }
    else if (m_tdPublisher)
    {
        m_tdPublisher->stop();
    }
    m_botController =
        std::make_unique<BotController>(std::move(config), m_database.get(), publisher);
    connect(
        m_botController.get(), &BotController::telegramReady, this,
        [this]
        {
            setStatus(
                m_publicationMode == QStringLiteral("telegram_server")
                    ? QStringLiteral("Bot polling is active. %1").arg(m_tdPublisher->statusText())
                    : QStringLiteral("Bot is running. Telegram polling is active."),
                false);
        });
    connect(m_botController.get(), &BotController::telegramConnectionIssue, this,
            [this](const QString& error, const int retryAfterSeconds)
            {
                setStatus(QStringLiteral("Telegram error: %1 Retrying in %2 seconds.")
                              .arg(error)
                              .arg(retryAfterSeconds),
                          true);
                appendLog(QStringLiteral("warning"), error);
            });
    connect(m_botController.get(), &BotController::catalogChanged, this,
            &SettingsController::refreshDataModels);
    connect(m_botController.get(), &BotController::metricsChanged, this,
            [this](const QVariantMap& metrics)
            {
                m_metrics = metrics;
                emit metricsChanged();
            });
    connect(m_botController.get(), &BotController::activityLogged, this,
            &SettingsController::appendLog);
    m_botController->start();
    setBotRunning(true);
    setStatus(tdStartError.isEmpty()
                  ? QStringLiteral("Settings saved. Connecting to Telegram…")
                  : QStringLiteral("Bot started, but TDLib needs attention: %1").arg(tdStartError),
              !tdStartError.isEmpty());
}

bool SettingsController::startTdPublisher(QString* error)
{
    if (!validateTdSettings(error))
    {
        return false;
    }
    if (m_tdDatabaseEncryptionKey.isEmpty())
    {
        m_tdDatabaseEncryptionKey.resize(32);
        for (char& byte : m_tdDatabaseEncryptionKey)
        {
            byte = static_cast<char>(QRandomGenerator::system()->generate() & 0xffU);
        }
    }
    if (!CredentialStore::writeTdApiHash(m_tdApiHash, error) ||
        !CredentialStore::writeTdDatabaseEncryptionKey(m_tdDatabaseEncryptionKey, error))
    {
        return false;
    }
    QSettings settings;
    settings.setValue(QString::fromLatin1(TdLibPathSettingsKey), m_tdLibPath);
    settings.setValue(QString::fromLatin1(TdApiIdSettingsKey), m_tdApiId);
    settings.setValue(QString::fromLatin1(TdPhoneSettingsKey), m_tdPhoneNumber);
    settings.sync();

    TdPublisherConfig tdConfig;
    tdConfig.libraryPath = m_tdLibPath;
    tdConfig.apiId = m_tdApiId.toInt();
    tdConfig.apiHash = m_tdApiHash;
    tdConfig.phoneNumber = m_tdPhoneNumber;
    tdConfig.databaseEncryptionKey = m_tdDatabaseEncryptionKey;
    tdConfig.databaseDirectory =
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
            .filePath(QStringLiteral("tdlib-user"));
    return m_tdPublisher->start(std::move(tdConfig), error);
}

bool SettingsController::validateTdSettings(QString* error) const
{
    bool apiIdValid = false;
    const int apiId = m_tdApiId.toInt(&apiIdValid);
    if (!apiIdValid || apiId <= 0)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("API ID must be a positive number from my.telegram.org.");
        }
        return false;
    }
    static const QRegularExpression ApiHashPattern(QStringLiteral("^[0-9a-fA-F]{32}$"));
    if (!ApiHashPattern.match(m_tdApiHash.trimmed()).hasMatch())
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("API hash must contain 32 hexadecimal characters.");
        }
        return false;
    }
    if (!m_tdLibPath.trimmed().isEmpty() && !QFileInfo(m_tdLibPath).isFile())
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("The selected tdjson.dll file does not exist.");
        }
        return false;
    }
    return true;
}

void SettingsController::setBotRunning(const bool running)
{
    if (m_botRunning != running)
    {
        m_botRunning = running;
        emit botRunningChanged();
    }
}

void SettingsController::setStatus(QString message, const bool isError)
{
    if (m_statusMessage == message && m_statusIsError == isError)
    {
        return;
    }
    m_statusMessage = std::move(message);
    m_statusIsError = isError;
    emit statusChanged();
}

void SettingsController::appendLog(const QString& level, const QString& message)
{
    const QString safeMessage =
        redactSensitiveLogText(message, {m_botToken, m_tdApiHash, m_tdDatabaseEncryptionKey});
    if (m_database && m_database->isOpen())
        static_cast<void>(m_database->appendVideoEvent(0, level, safeMessage));
    m_logs.prepend(QVariantMap{
        {QStringLiteral("time"),
         QDateTime::currentDateTime().toString(QStringLiteral("dd.MM.yyyy HH:mm:ss"))},
        {QStringLiteral("level"), level},
        {QStringLiteral("message"), safeMessage},
    });
    while (m_logs.size() > MaximumLogEntries)
    {
        m_logs.removeLast();
    }
    emit logsChanged();
    if (level == QStringLiteral("success") &&
        safeMessage.contains(QStringLiteral("published"), Qt::CaseInsensitive))
    {
        emit notificationRequested(QStringLiteral("Video published"), safeMessage);
    }
    else if (level == QStringLiteral("error"))
    {
        emit notificationRequested(QStringLiteral("Bot error"), safeMessage);
    }
}

void SettingsController::applyDatabaseAction(const bool success, const QString& successMessage,
                                             const QString& errorMessage)
{
    refreshDataModels();
    setStatus(success ? successMessage : QStringLiteral("Operation failed: %1").arg(errorMessage),
              !success);
}
