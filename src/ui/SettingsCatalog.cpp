#include "ui/SettingsController.h"

#include "app/ScheduleCalculator.h"
#include "tiktok/TikTokUrlValidator.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QTimeZone>

namespace
{
constexpr int MaximumBatchSize = 200;
QStringList batchLines(const QString& text)
{
    return text.split(QRegularExpression(QStringLiteral("[\\r\\n\\t ]+")), Qt::SkipEmptyParts);
}
} // namespace

QStringList SettingsController::availableTimeZones() const
{
    QStringList zones;
    for (const QByteArray& zone : QTimeZone::availableTimeZoneIds())
        zones.append(QString::fromUtf8(zone));
    return zones;
}

QDateTime SettingsController::parseChannelDateTime(const qint64 channelId,
                                                   const QString& text) const
{
    QDateTime parsed = QDateTime::fromString(text.trimmed(), Qt::ISODate);
    if (!parsed.isValid())
        return {};
    if (parsed.timeSpec() != Qt::LocalTime)
        return parsed.toUTC();
    QTimeZone zone = QTimeZone::systemTimeZone();
    for (const auto& channel : m_database->channels())
        if (channel.id == channelId && !channel.timeZoneId.isEmpty())
            zone = QTimeZone(channel.timeZoneId.toUtf8());
    // Reject DST gaps rather than silently shifting the user's requested time.
    return QDateTime(parsed.date(), parsed.time(), zone, QDateTime::TransitionResolution::Reject)
        .toUTC();
}

QVariantList SettingsController::previewUrls(const QString& text, const qint64 channelId,
                                             const bool allowDuplicate)
{
    QVariantList result;
    if (text.size() > 256000)
        return {QVariantMap{{QStringLiteral("valid"), false},
                            {QStringLiteral("message"), tr("Batch text is too large.")}}};
    const QStringList lines = batchLines(text);
    if (lines.size() > MaximumBatchSize)
        return {QVariantMap{{QStringLiteral("valid"), false},
                            {QStringLiteral("message"), tr("Add at most 200 links at once.")}}};
    bool channelFound = false;
    for (const auto& channel : m_database->channels())
        if (channel.id == channelId)
            channelFound = true;
    QSet<QString> seen;
    for (const QString& raw : lines)
    {
        const QString url = raw.trimmed();
        const bool validUrl = TikTokUrlValidator::isValid(url);
        const auto duplicate = validUrl ? m_database->duplicateVideo(url, channelId) : std::nullopt;
        const bool repeated = seen.contains(url);
        const bool valid = channelFound && validUrl && !repeated && (!duplicate || allowDuplicate);
        QString message;
        if (!channelFound)
            message = tr("Select an enabled destination channel.");
        else if (!validUrl)
            message = tr("Unsupported TikTok HTTPS URL.");
        else if (repeated)
            message = tr("Repeated link in this batch.");
        else if (duplicate)
            message = tr("Already in this channel's catalog: #%1 (%2).")
                          .arg(duplicate->id)
                          .arg(duplicate->status);
        else
            message = tr("Ready; short-link duplicates will be checked after download.");
        result.append(QVariantMap{{QStringLiteral("url"), url},
                                  {QStringLiteral("valid"), valid},
                                  {QStringLiteral("duplicate"), duplicate.has_value() || repeated},
                                  {QStringLiteral("message"), message}});
        seen.insert(url);
    }
    return result;
}

QVariantMap SettingsController::addVideos(const QString& text, const qint64 channelId,
                                          const QString& localDateTime, const bool draft,
                                          const bool allowDuplicate)
{
    QStringList errors;
    int added = 0;
    std::optional<ChannelRecord> destination;
    for (const auto& channel : m_database->channels())
        if (channel.id == channelId)
            destination = channel;
    QDateTime explicitTime;
    if (!localDateTime.trimmed().isEmpty())
        explicitTime = parseChannelDateTime(channelId, localDateTime);
    if (!destination ||
        (!localDateTime.trimmed().isEmpty() &&
         (!explicitTime.isValid() || explicitTime <= QDateTime::currentDateTimeUtc())))
    {
        const QString error = tr("Select a channel and a valid future date in its time zone.");
        setStatus(error, true);
        return {{QStringLiteral("added"), 0},
                {QStringLiteral("errors"), QStringList{error}},
                {QStringLiteral("message"), error}};
    }
    QDateTime boundary = m_database->latestPlannedTime(channelId);
    const auto published = m_database->latestPublishedTime(channelId);
    if (!boundary.isValid() || published > boundary)
        boundary = published;
    const auto preview = previewUrls(text, channelId, allowDuplicate);
    for (const QVariant& item : preview)
    {
        const auto entry = item.toMap();
        if (!entry.value(QStringLiteral("valid")).toBool())
        {
            errors.append(entry.value(QStringLiteral("url")).toString() + QStringLiteral(": ") +
                          entry.value(QStringLiteral("message")).toString());
            continue;
        }
        const QDateTime scheduled =
            explicitTime.isValid() && added == 0
                ? explicitTime
                : ScheduleCalculator::nextPublication(
                      QDateTime::currentDateTimeUtc(), boundary,
                      m_database->scheduleSlots(channelId),
                      std::chrono::minutes(m_publicationIntervalMinutes), destination->timeZoneId);
        QString error;
        const qint64 id = m_database->addVideo(
            entry.value(QStringLiteral("url")).toString(), 0, m_adminUserId.toLongLong(), channelId,
            scheduled, &error, !draft, allowDuplicate, destination->captionTemplate);
        if (id > 0)
        {
            ++added;
            boundary = scheduled;
        }
        else
            errors.append(error);
    }
    const QString message = tr("Added %1 video(s); %2 rejected.").arg(added).arg(errors.size());
    setStatus(message, !errors.isEmpty());
    refreshDataModels();
    wakeQueue();
    return {{QStringLiteral("added"), added},
            {QStringLiteral("errors"), errors},
            {QStringLiteral("message"), message}};
}

void SettingsController::wakeQueue()
{
    if (m_botController)
        m_botController->wakeQueue();
}

void SettingsController::setVideoCaption(const qint64 id, const QString& caption)
{
    QString error;
    applyDatabaseAction(m_database->setVideoCaption(id, caption, &error), tr("Caption saved."),
                        error);
}
void SettingsController::approveVideo(const qint64 id)
{
    QString error;
    applyDatabaseAction(m_database->approveVideo(id, &error), tr("Video approved."), error);
    wakeQueue();
}
void SettingsController::setVideoDraft(const qint64 id, const bool draft)
{
    QString error;
    applyDatabaseAction(m_database->setVideoDraft(id, draft, &error), tr("Review state updated."),
                        error);
    wakeQueue();
}
void SettingsController::allowVideoRepeat(const qint64 id)
{
    QString error;
    applyDatabaseAction(m_database->setVideoAllowDuplicate(id, true, &error),
                        tr("Repeat permitted; retry when ready."), error);
}
void SettingsController::resolveDeliveryUnknown(const qint64 id, const bool published)
{
    QString error;
    applyDatabaseAction(m_database->resolveDeliveryUnknown(id, published, &error),
                        published ? tr("Marked as published after your verification.")
                                  : tr("Marked as not delivered. You can now retry manually."),
                        error);
}

QVariantMap SettingsController::bulkVideoAction(const QVariantList& ids, const QString& action,
                                                const QString& value)
{
    QStringList errors;
    int changed = 0;
    QSet<qint64> unique;
    for (const QVariant& raw : ids)
    {
        const qint64 id = raw.toLongLong();
        if (id <= 0 || unique.contains(id))
            continue;
        unique.insert(id);
        QString error;
        bool ok = false;
        if (action == QStringLiteral("approve"))
            ok = m_database->approveVideo(id, &error);
        else if (action == QStringLiteral("draft"))
            ok = m_database->setVideoDraft(id, true, &error);
        else if (action == QStringLiteral("retry"))
            ok = m_botController ? m_botController->retryVideo(id, &error)
                                 : m_database->retryVideo(id, &error);
        else if (action == QStringLiteral("cancel"))
        {
            const auto video = m_database->video(id);
            if (!m_botController && video && video->status == QStringLiteral("server_scheduled"))
                error = tr("Start the bot to cancel a Telegram scheduled post.");
            else
                ok = m_botController ? m_botController->cancelVideo(id, &error)
                                     : m_database->cancelVideo(id, &error);
        }
        else if (action == QStringLiteral("channel"))
            ok = m_botController ? m_botController->setVideoChannel(id, value.toLongLong(), &error)
                                 : m_database->setVideoChannel(id, value.toLongLong(), &error);
        else if (action == QStringLiteral("schedule"))
        {
            const auto video = m_database->video(id);
            const auto when = video ? parseChannelDateTime(video->channelId, value) : QDateTime{};
            if (!m_botController && video && video->status == QStringLiteral("server_scheduled"))
                error = tr("Start the bot to reschedule a Telegram post.");
            else
                ok = m_botController ? m_botController->setVideoSchedule(id, when, &error)
                                     : m_database->setVideoSchedule(id, when, &error);
        }
        else
            error = tr("Unknown bulk operation.");
        if (ok)
            ++changed;
        else
            errors.append(tr("#%1: %2").arg(id).arg(error));
    }
    const QString message =
        tr("Accepted %1 operation(s); %2 failed. Remote actions finish asynchronously.")
            .arg(changed)
            .arg(errors.size());
    refreshDataModels();
    wakeQueue();
    setStatus(message, !errors.isEmpty());
    return {{QStringLiteral("changed"), changed},
            {QStringLiteral("errors"), errors},
            {QStringLiteral("message"), message}};
}

void SettingsController::openVideoFile(const qint64 id)
{
    const auto video = m_database->video(id);
    if (!video)
        return;
    const QFileInfo file(video->localFilePath);
    const QString root =
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
            .filePath(QStringLiteral("video-cache"));
    const QString canonicalRoot = QFileInfo(root).canonicalFilePath();
    const QString relative = QDir(canonicalRoot).relativeFilePath(file.canonicalFilePath());
    if (canonicalRoot.isEmpty() || !file.isFile() || file.isSymbolicLink() ||
        relative.startsWith(QStringLiteral("../")) || QDir::isAbsolutePath(relative) ||
        file.suffix().compare(QStringLiteral("mp4"), Qt::CaseInsensitive) != 0)
    {
        setStatus(tr("The cached MP4 is unavailable. Download it before previewing."), true);
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(file.canonicalFilePath())))
        setStatus(tr("No video player could open this file."), true);
}
QVariantList SettingsController::videoEvents(const qint64 id) const
{
    return m_database->videoEvents(id);
}
void SettingsController::setChannelTimeZone(const qint64 id, const QString& zone)
{
    QString error;
    applyDatabaseAction(m_database->setChannelTimeZone(id, zone, &error),
                        tr("Channel time zone saved. Existing publication instants are unchanged."),
                        error);
}
void SettingsController::setChannelCaptionTemplate(const qint64 id, const QString& text)
{
    QString error;
    applyDatabaseAction(m_database->setChannelCaptionTemplate(id, text, &error),
                        tr("Caption template saved."), error);
}
void SettingsController::moveVideoToDate(const qint64 id, const QString& date)
{
    const auto record = m_database->video(id);
    if (!record)
        return;
    QTimeZone zone = QTimeZone::systemTimeZone();
    for (const auto& channel : m_database->channels())
        if (channel.id == record->channelId && !channel.timeZoneId.isEmpty())
            zone = QTimeZone(channel.timeZoneId.toUtf8());
    const QDate day = QDate::fromString(date, Qt::ISODate);
    const QTime time = record->scheduledAtUtc.isValid()
                           ? record->scheduledAtUtc.toTimeZone(zone).time()
                           : QTime(12, 0);
    const QDateTime target(day, time, zone, QDateTime::TransitionResolution::Reject);
    if (!target.isValid())
    {
        setStatus(tr("That local date/time does not exist in the channel time zone."), true);
        return;
    }
    setVideoSchedule(id, target.toUTC().toString(Qt::ISODate));
}

int SettingsController::cacheLimitMiB() const
{
    return static_cast<int>(CachePolicy::fromSettings().limitBytes / 1048576);
}
int SettingsController::minFreeDiskMiB() const
{
    return static_cast<int>(CachePolicy::fromSettings().minimumFreeBytes / 1048576);
}
int SettingsController::cacheRetentionDays() const
{
    return CachePolicy::fromSettings().retentionDays;
}
void SettingsController::updateCachePolicy(const QString& key, const int value)
{
    QSettings settings;
    settings.setValue(key, value);
    settings.sync();
    if (settings.status() != QSettings::NoError)
    {
        setStatus(tr("Could not save cache settings."), true);
        return;
    }
    if (m_botController)
        m_botController->reloadCachePolicy();
    if (m_idleCache)
        m_idleCache->reloadPolicy();
    emit settingsChanged();
}
void SettingsController::setCacheLimitMiB(const int value)
{
    updateCachePolicy(QStringLiteral("cache/limitMiB"), std::clamp(value, 256, 1048576));
}
void SettingsController::setMinFreeDiskMiB(const int value)
{
    updateCachePolicy(QStringLiteral("cache/minFreeMiB"), std::clamp(value, 128, 1048576));
}
void SettingsController::setCacheRetentionDays(const int value)
{
    updateCachePolicy(QStringLiteral("cache/retentionDays"), std::clamp(value, 0, 365));
}
void SettingsController::cleanCache()
{
    if (m_botController)
    {
        m_botController->cleanCache();
        return;
    }
    if (!m_idleCache)
    {
        m_idleCache = std::make_unique<CacheManager>(
            QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                .filePath(QStringLiteral("video-cache")));
        connect(m_idleCache.get(), &CacheManager::snapshotChanged, this,
                [this]
                {
                    const auto snapshot = m_idleCache->snapshot();
                    setStatus(snapshot.error.isEmpty()
                                  ? tr("Cache checked; %1 expired file(s) removed.")
                                        .arg(snapshot.removedFiles)
                                  : snapshot.error,
                              !snapshot.error.isEmpty());
                    refreshDataModels();
                });
    }
    m_idleCache->refresh(m_database->cacheCleanupCandidates(), true);
    setStatus(tr("Cleaning completed/cancelled media in the background…"), false);
}
QVariantList SettingsController::diagnostics() const
{
    return m_diagnostics.results();
}
void SettingsController::runDiagnostics()
{
    m_diagnostics.run(m_ytDlpExecutable, m_tdLibPath, m_botToken, m_database->channels(),
                      m_publicationMode == QStringLiteral("telegram_server"));
}
QString SettingsController::uiLanguage() const
{
    return QSettings().value(QStringLiteral("ui/language"), QStringLiteral("system")).toString();
}
void SettingsController::setUiLanguage(const QString& value)
{
    if (value != QStringLiteral("system") && value != QStringLiteral("en") &&
        value != QStringLiteral("ru"))
        return;
    QSettings settings;
    settings.setValue(QStringLiteral("ui/language"), value);
    emit settingsChanged();
    emit languageChanged();
}
