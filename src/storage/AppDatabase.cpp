/******************************************************************************
 * @file    AppDatabase.cpp
 * @brief   Implements SQLite persistence and publication-catalog state transitions.
 *
 * @author  Zheludchenko Yehor
 * @date    2026-08-30
 * @version 2.1.0
 *
 * @copyright Copyright (c) 2026 TikTok Telegram Bot.
 *
 * Target: Qt 6.11.2 (MSVC 2022 64-bit, C++23)
 ******************************************************************************/

#include "storage/AppDatabase.h"

#include "Logging.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStandardPaths>
#include <QTime>
#include <QTimeZone>
#include <QUrl>
#include <QUuid>

#include <algorithm>
#include <utility>

namespace
{

const QString VideoSelect =
    QStringLiteral("SELECT v.id, v.url, v.channel_id, c.name, c.telegram_id, v.status, "
                   "v.request_chat_id, v.request_user_id, v.requested_at_utc, v.scheduled_at_utc, "
                   "v.published_at_utc, v.title, v.author, v.thumbnail_path, v.local_file_path, "
                   "v.error, v.duration_seconds, v.file_size, v.retry_count, v.queue_order, "
                   "v.progress, v.force_publish, v.publisher_mode, v.td_chat_id, v.td_message_id, "
                   "v.caption,v.approved,v.source_video_id,v.allow_duplicate,v.next_retry_at_utc,"
                   "v.error_category,c.time_zone_id "
                   "FROM videos v JOIN channels c ON c.id=v.channel_id ");

const QString ActiveStatusSql =
    QStringLiteral("('queued','downloading','ready','uploading','server_scheduled')");

QString isoTime(const QDateTime& dateTime)
{
    return dateTime.isValid() ? dateTime.toUTC().toString(Qt::ISODateWithMs) : QString{};
}

QDateTime dateTimeFromDatabase(const QVariant& value)
{
    return QDateTime::fromString(value.toString(), Qt::ISODateWithMs).toUTC();
}

QString queryError(const QSqlQuery& query)
{
    return query.lastError().text().trimmed();
}

QString canonicalSourceId(const QString& value)
{
    static const QRegularExpression numeric(QStringLiteral("^[0-9]{1,30}$"));
    static const QRegularExpression path(QStringLiteral("/(?:video|photo)/([0-9]{1,30})(?:/|$)"));
    if (numeric.match(value.trimmed()).hasMatch())
        return value.trimmed();
    return path.match(QUrl(value).path()).captured(1);
}

QString canonicalSourceUrl(const QString& value)
{
    QUrl url(value.trimmed());
    url.setQuery(QString());
    url.setFragment(QString());
    return url.toString(QUrl::NormalizePathSegments | QUrl::StripTrailingSlash);
}

bool completeUpdate(QSqlQuery& query, QString* error,
                    const QString& missing = QStringLiteral("The selected item cannot be changed."))
{
    if (query.exec() && query.numRowsAffected() == 1)
        return true;
    if (error)
        *error = query.lastError().isValid() ? queryError(query) : missing;
    return false;
}

QJsonObject queryRowToJson(const QSqlQuery& query)
{
    QJsonObject object;
    const QSqlRecord record = query.record();
    for (int index = 0; index < record.count(); ++index)
    {
        const QVariant value = query.value(index);
        const QString name = record.fieldName(index);
        if (value.metaType().id() == QMetaType::LongLong ||
            value.metaType().id() == QMetaType::ULongLong)
        {
            object.insert(name, value.toString());
        }
        else
        {
            object.insert(name, QJsonValue::fromVariant(value));
        }
    }
    return object;
}

} // namespace

AppDatabase::AppDatabase(QString databasePath)
    : m_databasePath(std::move(databasePath))
    , m_connectionName(
          QStringLiteral("TikTokBot-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
{
}

AppDatabase::~AppDatabase()
{
    if (m_database.isValid())
    {
        m_database.close();
    }
    m_database = {};
    QSqlDatabase::removeDatabase(m_connectionName);
}

bool AppDatabase::open(QString* error)
{
    if (isOpen())
    {
        return true;
    }

    if (m_databasePath.isEmpty())
    {
        const QString dataDirectory =
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        if (!QDir().mkpath(dataDirectory))
        {
            if (error != nullptr)
            {
                *error = QStringLiteral("Could not create the application data directory.");
            }
            return false;
        }
        m_databasePath = QDir(dataDirectory).filePath(QStringLiteral("tiktok-bot.sqlite"));
    }
    else if (!QDir().mkpath(QFileInfo(m_databasePath).absolutePath()))
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Could not create the database directory.");
        }
        return false;
    }

    m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_database.setDatabaseName(m_databasePath);
    if (!m_database.open())
    {
        if (error != nullptr)
        {
            *error = m_database.lastError().text();
        }
        return false;
    }
    if (initializeSchema(error))
        return true;
    m_database.close();
    return false;
}

bool AppDatabase::isOpen() const noexcept
{
    return m_database.isValid() && m_database.isOpen();
}

QString AppDatabase::databasePath() const
{
    return m_databasePath;
}

bool AppDatabase::initializeSchema(QString* error)
{
    if (!execute(QStringLiteral("PRAGMA foreign_keys=ON"), error) ||
        !execute(QStringLiteral("PRAGMA journal_mode=WAL"), error) ||
        !execute(QStringLiteral("PRAGMA synchronous=NORMAL"), error))
    {
        return false;
    }

    QSqlQuery versionQuery(m_database);
    if (!versionQuery.exec(QStringLiteral("PRAGMA user_version")) || !versionQuery.next())
        return false;
    const int version = versionQuery.value(0).toInt();
    versionQuery.finish();
    if (version > 3)
    {
        if (error)
            *error = QStringLiteral("This database was created by a newer application version.");
        return false;
    }
    if (!m_database.transaction())
    {
        if (error)
            *error = m_database.lastError().text();
        return false;
    }
    const QStringList statements{
        QStringLiteral("CREATE TABLE IF NOT EXISTS channels("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, "
                       "telegram_id TEXT NOT NULL UNIQUE, is_default INTEGER NOT NULL DEFAULT 0, "
                       "enabled INTEGER NOT NULL DEFAULT 1)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS schedule_slots("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT, channel_id INTEGER NOT NULL, "
                       "day_of_week INTEGER NOT NULL DEFAULT 0, time_text TEXT NOT NULL, "
                       "enabled INTEGER NOT NULL DEFAULT 1, "
                       "FOREIGN KEY(channel_id) REFERENCES channels(id) ON DELETE CASCADE)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS videos("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, url TEXT NOT NULL, "
            "channel_id INTEGER NOT NULL, status TEXT NOT NULL, "
            "request_chat_id TEXT NOT NULL, request_user_id TEXT NOT NULL, "
            "requested_at_utc TEXT NOT NULL, scheduled_at_utc TEXT, published_at_utc TEXT, "
            "title TEXT, author TEXT, thumbnail_path TEXT, local_file_path TEXT, error TEXT, "
            "duration_seconds INTEGER NOT NULL DEFAULT 0, file_size INTEGER NOT NULL DEFAULT 0, "
            "retry_count INTEGER NOT NULL DEFAULT 0, queue_order INTEGER NOT NULL, "
            "progress REAL NOT NULL DEFAULT 0, force_publish INTEGER NOT NULL DEFAULT 0, "
            "publisher_mode TEXT NOT NULL DEFAULT 'local', td_chat_id TEXT, "
            "td_message_id TEXT, "
            "FOREIGN KEY(channel_id) REFERENCES channels(id) ON DELETE RESTRICT)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS videos_status_order_idx "
                       "ON videos(status, queue_order)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS videos_schedule_idx "
                       "ON videos(status, scheduled_at_utc)"),
    };
    for (const QString& statement : statements)
    {
        if (!execute(statement, error))
        {
            m_database.rollback();
            return false;
        }
    }

    const auto ensureColumn =
        [this, error](const QString& table, const QString& name, const QString& definition)
    {
        QSqlQuery columns(m_database);
        if (!columns.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table)))
        {
            if (error != nullptr)
            {
                *error = queryError(columns);
            }
            return false;
        }
        while (columns.next())
        {
            if (columns.value(1).toString() == name)
            {
                return true;
            }
        }
        columns.finish();
        return execute(
            QStringLiteral("ALTER TABLE %1 ADD COLUMN %2 %3").arg(table, name, definition), error);
    };
    if (!ensureColumn(QStringLiteral("videos"), QStringLiteral("publisher_mode"),
                      QStringLiteral("TEXT NOT NULL DEFAULT 'local'")) ||
        !ensureColumn(QStringLiteral("videos"), QStringLiteral("td_chat_id"),
                      QStringLiteral("TEXT")) ||
        !ensureColumn(QStringLiteral("videos"), QStringLiteral("td_message_id"),
                      QStringLiteral("TEXT")) ||
        !ensureColumn(QStringLiteral("videos"), QStringLiteral("caption"),
                      QStringLiteral("TEXT NOT NULL DEFAULT ''")) ||
        !ensureColumn(QStringLiteral("videos"), QStringLiteral("approved"),
                      QStringLiteral("INTEGER NOT NULL DEFAULT 1")) ||
        !ensureColumn(QStringLiteral("videos"), QStringLiteral("source_video_id"),
                      QStringLiteral("TEXT NOT NULL DEFAULT ''")) ||
        !ensureColumn(QStringLiteral("videos"), QStringLiteral("allow_duplicate"),
                      QStringLiteral("INTEGER NOT NULL DEFAULT 0")) ||
        !ensureColumn(QStringLiteral("videos"), QStringLiteral("next_retry_at_utc"),
                      QStringLiteral("TEXT NOT NULL DEFAULT ''")) ||
        !ensureColumn(QStringLiteral("videos"), QStringLiteral("error_category"),
                      QStringLiteral("TEXT NOT NULL DEFAULT ''")) ||
        !ensureColumn(QStringLiteral("channels"), QStringLiteral("time_zone_id"),
                      QStringLiteral("TEXT NOT NULL DEFAULT ''")) ||
        !ensureColumn(QStringLiteral("channels"), QStringLiteral("caption_template"),
                      QStringLiteral("TEXT NOT NULL DEFAULT ''")) ||
        !execute(QStringLiteral("DROP INDEX IF EXISTS videos_active_url_idx"), error) ||
        !execute(
            QStringLiteral(
                "CREATE UNIQUE INDEX videos_active_url_idx ON videos(channel_id,url) "
                "WHERE allow_duplicate=0 AND status IN ('queued','downloading','ready','uploading',"
                "'server_scheduled','delivery_unknown')"),
            error) ||
        !execute(QStringLiteral("CREATE INDEX IF NOT EXISTS videos_source_idx ON "
                                "videos(channel_id,source_video_id)"),
                 error) ||
        !execute(
            QStringLiteral(
                "CREATE INDEX IF NOT EXISTS videos_retry_idx ON videos(status,next_retry_at_utc)"),
            error) ||
        !execute(QStringLiteral(
                     "CREATE TABLE IF NOT EXISTS video_events(id INTEGER PRIMARY KEY AUTOINCREMENT,"
                     "video_id INTEGER,created_at_utc TEXT NOT NULL,category TEXT NOT NULL,message "
                     "TEXT NOT NULL,"
                     "FOREIGN KEY(video_id) REFERENCES videos(id) ON DELETE CASCADE)"),
                 error) ||
        !execute(
            QStringLiteral(
                "CREATE INDEX IF NOT EXISTS video_events_video_idx ON video_events(video_id,id)"),
            error) ||
        !execute(
            QStringLiteral(
                "CREATE TRIGGER IF NOT EXISTS videos_status_event AFTER UPDATE OF status ON videos "
                "WHEN OLD.status<>NEW.status BEGIN INSERT INTO "
                "video_events(video_id,created_at_utc,category,message) "
                "VALUES(NEW.id,strftime('%Y-%m-%dT%H:%M:%fZ','now'),NEW.status,COALESCE(NEW.error,'"
                "')); END"),
            error) ||
        !execute(QStringLiteral(
                     "CREATE TRIGGER IF NOT EXISTS videos_created_event AFTER INSERT ON videos "
                     "BEGIN INSERT INTO video_events(video_id,created_at_utc,category,message) "
                     "VALUES(NEW.id,strftime('%Y-%m-%dT%H:%M:%fZ','now'),'created',''); END"),
                 error) ||
        !execute(QStringLiteral(
                     "CREATE TRIGGER IF NOT EXISTS videos_review_event AFTER UPDATE OF "
                     "caption,approved,scheduled_at_utc,channel_id,allow_duplicate,next_retry_at_"
                     "utc ON videos "
                     "BEGIN INSERT INTO video_events(video_id,created_at_utc,category,message) "
                     "VALUES(NEW.id,strftime('%Y-%m-%dT%H:%M:%fZ','now'),'edited','Caption, "
                     "approval, destination, schedule or retry settings updated.'); END"),
                 error) ||
        !execute(
            QStringLiteral(
                "CREATE TRIGGER IF NOT EXISTS video_events_retention AFTER INSERT ON video_events "
                "BEGIN DELETE FROM video_events WHERE id<=NEW.id-50000; END"),
            error) ||
        !execute(QStringLiteral("PRAGMA user_version=3"), error))
    {
        m_database.rollback();
        return false;
    }
    if (!m_database.commit())
    {
        if (error)
            *error = m_database.lastError().text();
        m_database.rollback();
        return false;
    }
    return true;
}

bool AppDatabase::execute(const QString& sql, QString* error) const
{
    QSqlQuery query(m_database);
    if (query.exec(sql))
    {
        return true;
    }
    if (error != nullptr)
    {
        *error = queryError(query);
    }
    return false;
}

qint64 AppDatabase::ensureDefaultChannel(const QString& telegramId, QString* error)
{
    const QString normalizedId = telegramId.trimmed();
    if (!validTelegramChannelId(normalizedId))
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Invalid Telegram channel ID.");
        }
        return 0;
    }

    QSqlQuery find(m_database);
    find.prepare(QStringLiteral("SELECT id, is_default FROM channels WHERE telegram_id=?"));
    find.addBindValue(normalizedId);
    if (!find.exec())
    {
        if (error != nullptr)
        {
            *error = queryError(find);
        }
        return 0;
    }
    if (find.next())
    {
        const qint64 id = find.value(0).toLongLong();
        if (!find.value(1).toBool() && !defaultChannel().has_value())
        {
            if (!setDefaultChannel(id, error))
            {
                return 0;
            }
        }
        return id;
    }

    const qint64 id = addChannel(normalizedChannelName({}, normalizedId), normalizedId, error);
    if (id > 0 && !setDefaultChannel(id, error))
    {
        return 0;
    }
    return id;
}

qint64 AppDatabase::addChannel(const QString& name, const QString& telegramId, QString* error)
{
    const QString normalizedId = telegramId.trimmed();
    if (!validTelegramChannelId(normalizedId))
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Use @channel_name or a negative numeric channel ID.");
        }
        return 0;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO channels(name, telegram_id, is_default, enabled) VALUES(?,?,0,1) "
        "ON CONFLICT(telegram_id) DO UPDATE SET name=excluded.name, enabled=1 "
        "RETURNING id"));
    query.addBindValue(normalizedChannelName(name, normalizedId));
    query.addBindValue(normalizedId);
    if (!query.exec() || !query.next())
    {
        if (error != nullptr)
        {
            *error = queryError(query);
        }
        return 0;
    }
    return query.value(0).toLongLong();
}

bool AppDatabase::setDefaultChannel(const qint64 channelId, QString* error)
{
    if (!m_database.transaction())
    {
        if (error != nullptr)
        {
            *error = m_database.lastError().text();
        }
        return false;
    }
    QSqlQuery reset(m_database);
    const bool resetOk = reset.exec(QStringLiteral("UPDATE channels SET is_default=0"));
    QSqlQuery update(m_database);
    update.prepare(QStringLiteral("UPDATE channels SET is_default=1, enabled=1 WHERE id=?"));
    update.addBindValue(channelId);
    const bool updateOk = update.exec() && update.numRowsAffected() == 1;
    if (!resetOk || !updateOk || !m_database.commit())
    {
        m_database.rollback();
        if (error != nullptr)
        {
            *error = updateOk ? m_database.lastError().text() : queryError(update);
        }
        return false;
    }
    return true;
}

bool AppDatabase::disableChannel(const qint64 channelId, QString* error)
{
    QSqlQuery query(m_database);
    query.prepare(
        QStringLiteral("UPDATE channels SET enabled=0 WHERE id=? AND is_default=0 AND NOT EXISTS("
                       "SELECT 1 FROM videos WHERE channel_id=? AND status IN "
                       "('queued','downloading','ready','uploading','server_scheduled'))"));
    query.addBindValue(channelId);
    query.addBindValue(channelId);
    if (!query.exec() || query.numRowsAffected() != 1)
    {
        if (error != nullptr)
        {
            *error =
                query.lastError().isValid()
                    ? queryError(query)
                    : QStringLiteral(
                          "The default channel or a channel with active videos cannot be removed.");
        }
        return false;
    }
    return true;
}

QList<ChannelRecord> AppDatabase::channels(const bool includeDisabled) const
{
    QList<ChannelRecord> result;
    QSqlQuery query(m_database);
    QString sql = QStringLiteral(
        "SELECT id,name,telegram_id,is_default,enabled,time_zone_id,caption_template FROM "
        "channels");
    if (!includeDisabled)
    {
        sql += QStringLiteral(" WHERE enabled=1");
    }
    sql += QStringLiteral(" ORDER BY is_default DESC,name COLLATE NOCASE");
    if (!query.exec(sql))
    {
        qCWarning(logApp) << "Could not query channels:" << queryError(query);
        return result;
    }
    while (query.next())
    {
        result.append(ChannelRecord{query.value(0).toLongLong(), query.value(1).toString(),
                                    query.value(2).toString(), query.value(3).toBool(),
                                    query.value(4).toBool(), query.value(5).toString(),
                                    query.value(6).toString()});
    }
    return result;
}

std::optional<ChannelRecord> AppDatabase::defaultChannel() const
{
    const QList<ChannelRecord> records = channels();
    const auto iterator =
        std::find_if(records.cbegin(), records.cend(),
                     [](const ChannelRecord& channel) { return channel.isDefault; });
    return iterator == records.cend() ? std::nullopt : std::optional<ChannelRecord>(*iterator);
}

bool AppDatabase::setChannelTimeZone(const qint64 channelId, const QString& timeZoneId,
                                     QString* error)
{
    const QString zone = timeZoneId.trimmed();
    if (!zone.isEmpty() && !QTimeZone(zone.toUtf8()).isValid())
    {
        if (error)
            *error = QStringLiteral("Select a valid IANA time zone, for example Europe/Kyiv.");
        return false;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("UPDATE channels SET time_zone_id=? WHERE id=? AND enabled=1"));
    query.addBindValue(zone.isNull() ? QStringLiteral("") : zone);
    query.addBindValue(channelId);
    return completeUpdate(query, error);
}

bool AppDatabase::setChannelCaptionTemplate(const qint64 channelId, const QString& captionTemplate,
                                            QString* error)
{
    if (captionTemplate.size() > 1024)
    {
        if (error)
            *error = QStringLiteral("Caption templates cannot exceed 1024 characters.");
        return false;
    }
    QSqlQuery query(m_database);
    query.prepare(
        QStringLiteral("UPDATE channels SET caption_template=? WHERE id=? AND enabled=1"));
    query.addBindValue(captionTemplate.isNull() ? QStringLiteral("") : captionTemplate);
    query.addBindValue(channelId);
    return completeUpdate(query, error);
}

qint64 AppDatabase::addScheduleSlot(const qint64 channelId, const int dayOfWeek,
                                    const QString& time, QString* error)
{
    const QTime parsedTime = QTime::fromString(time.trimmed(), QStringLiteral("HH:mm"));
    if (dayOfWeek < 0 || dayOfWeek > 7 || !parsedTime.isValid())
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Day must be 0-7 and time must use HH:mm format.");
        }
        return 0;
    }
    QSqlQuery query(m_database);
    query.prepare(
        QStringLiteral("INSERT INTO schedule_slots(channel_id,day_of_week,time_text,enabled) "
                       "VALUES(?,?,?,1)"));
    query.addBindValue(channelId);
    query.addBindValue(dayOfWeek);
    query.addBindValue(parsedTime.toString(QStringLiteral("HH:mm")));
    if (!query.exec())
    {
        if (error != nullptr)
        {
            *error = queryError(query);
        }
        return 0;
    }
    return query.lastInsertId().toLongLong();
}

bool AppDatabase::removeScheduleSlot(const qint64 slotId, QString* error)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("DELETE FROM schedule_slots WHERE id=?"));
    query.addBindValue(slotId);
    if (!query.exec() || query.numRowsAffected() != 1)
    {
        if (error != nullptr)
        {
            *error = queryError(query);
        }
        return false;
    }
    return true;
}

QList<ScheduleSlotRecord> AppDatabase::scheduleSlots(const qint64 channelId) const
{
    QList<ScheduleSlotRecord> result;
    QSqlQuery query(m_database);
    QString sql =
        QStringLiteral("SELECT id,channel_id,day_of_week,time_text,enabled FROM schedule_slots ");
    if (channelId > 0)
    {
        sql += QStringLiteral("WHERE channel_id=? ");
    }
    sql += QStringLiteral("ORDER BY day_of_week,time_text");
    query.prepare(sql);
    if (channelId > 0)
    {
        query.addBindValue(channelId);
    }
    if (!query.exec())
    {
        qCWarning(logApp) << "Could not query schedule slots:" << queryError(query);
        return result;
    }
    while (query.next())
    {
        result.append(ScheduleSlotRecord{query.value(0).toLongLong(), query.value(1).toLongLong(),
                                         query.value(2).toInt(), query.value(3).toString(),
                                         query.value(4).toBool()});
    }
    return result;
}

qint64 AppDatabase::addVideo(const QString& url, const qint64 requestChatId,
                             const qint64 requestUserId, const qint64 channelId,
                             const QDateTime& scheduledAtUtc, QString* error, const bool approved,
                             const bool allowDuplicate, const QString& caption)
{
    if (caption.size() > 1024 || url.trimmed().isEmpty() || url.size() > 4096)
    {
        if (error)
            *error =
                QStringLiteral("Enter a valid video URL and a caption of at most 1024 characters.");
        return 0;
    }
    if (!allowDuplicate && duplicateVideo(url, channelId))
    {
        if (error)
            *error = QStringLiteral("This video is already queued or was published in the selected "
                                    "channel. Enable repeat publication to add it again.");
        return 0;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO videos(url,channel_id,status,request_chat_id,request_user_id,"
        "requested_at_utc,scheduled_at_utc,queue_order,approved,allow_duplicate,caption,source_"
        "video_id) "
        "SELECT ?,?,'queued',?,?,?,?,COALESCE((SELECT MAX(queue_order)+1 FROM videos),1),?,?,?,? "
        "WHERE EXISTS(SELECT 1 FROM channels WHERE id=? AND enabled=1)"));
    query.addBindValue(canonicalSourceUrl(url));
    query.addBindValue(channelId);
    query.addBindValue(QString::number(requestChatId));
    query.addBindValue(QString::number(requestUserId));
    query.addBindValue(isoTime(QDateTime::currentDateTimeUtc()));
    query.addBindValue(isoTime(scheduledAtUtc));
    query.addBindValue(approved ? 1 : 0);
    query.addBindValue(allowDuplicate ? 1 : 0);
    query.addBindValue(caption.isNull() ? QStringLiteral("") : caption);
    query.addBindValue(canonicalSourceId(url).isEmpty() ? QStringLiteral("")
                                                        : canonicalSourceId(url));
    query.addBindValue(channelId);
    if (!query.exec() || query.numRowsAffected() != 1)
    {
        if (error != nullptr)
        {
            *error = query.lastError().nativeErrorCode() == QStringLiteral("2067")
                         ? QStringLiteral("This video is already present in the active queue.")
                         : queryError(query);
        }
        return 0;
    }
    return query.lastInsertId().toLongLong();
}

std::optional<VideoRecord> AppDatabase::duplicateVideo(const QString& sourceUrlOrId,
                                                       const qint64 channelId) const
{
    const QString identity = canonicalSourceId(sourceUrlOrId);
    const QString normalizedUrl = canonicalSourceUrl(sourceUrlOrId);
    const auto records = videosForQuery(
        QStringLiteral("WHERE v.channel_id=? AND v.status<>'cancelled'"), {channelId});
    const auto found =
        std::find_if(records.cbegin(), records.cend(),
                     [&](const VideoRecord& item)
                     {
                         return canonicalSourceUrl(item.url) == normalizedUrl ||
                                (!identity.isEmpty() && (item.sourceVideoId == identity ||
                                                         canonicalSourceId(item.url) == identity));
                     });
    return found == records.cend() ? std::nullopt : std::optional<VideoRecord>(*found);
}

bool AppDatabase::hasDuplicateSourceVideo(const qint64 videoId) const
{
    const auto current = video(videoId);
    if (!current || current->allowDuplicate)
        return false;
    const QString identity =
        current->sourceVideoId.isEmpty() ? canonicalSourceId(current->url) : current->sourceVideoId;
    const auto records =
        videosForQuery(QStringLiteral("WHERE v.channel_id=? AND v.id<>? AND v.status<>'cancelled'"),
                       {current->channelId, videoId});
    return std::any_of(
        records.cbegin(), records.cend(),
        [&](const VideoRecord& item)
        {
            return (canonicalSourceUrl(item.url) == canonicalSourceUrl(current->url) ||
                    (!identity.isEmpty() && (item.sourceVideoId == identity ||
                                             canonicalSourceId(item.url) == identity))) &&
                   (item.id < videoId || item.status == QStringLiteral("sent") ||
                    item.status == QStringLiteral("server_scheduled") ||
                    item.status == QStringLiteral("uploading") ||
                    item.status == QStringLiteral("delivery_unknown"));
        });
}

bool AppDatabase::setVideoCaption(const qint64 videoId, const QString& caption, QString* error)
{
    if (caption.size() > 1024)
    {
        if (error)
            *error = QStringLiteral("Telegram video captions cannot exceed 1024 characters.");
        return false;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("UPDATE videos SET caption=? WHERE id=? AND status IN "
                                 "('queued','downloading','ready','failed')"));
    query.addBindValue(caption.isNull() ? QStringLiteral("") : caption);
    query.addBindValue(videoId);
    return completeUpdate(query, error);
}

bool AppDatabase::approveVideo(const qint64 videoId, QString* error)
{
    return setVideoDraft(videoId, false, error);
}

bool AppDatabase::setVideoDraft(const qint64 videoId, const bool draft, QString* error)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("UPDATE videos SET approved=?,force_publish=0 WHERE id=? AND "
                                 "status IN ('queued','downloading','ready','failed')"));
    query.addBindValue(draft ? 0 : 1);
    query.addBindValue(videoId);
    return completeUpdate(query, error);
}

bool AppDatabase::setVideoSourceId(const qint64 videoId, const QString& sourceVideoId,
                                   QString* error)
{
    const QString identity = canonicalSourceId(sourceVideoId);
    if (identity.isEmpty())
    {
        if (error)
            *error = QStringLiteral("TikTok did not provide a valid video identifier.");
        return false;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("UPDATE videos SET source_video_id=? WHERE id=?"));
    query.addBindValue(identity);
    query.addBindValue(videoId);
    return completeUpdate(query, error);
}

bool AppDatabase::setSourceVideoId(const qint64 videoId, const QString& sourceVideoId,
                                   QString* error)
{
    return setVideoSourceId(videoId, sourceVideoId, error);
}

bool AppDatabase::setVideoAllowDuplicate(const qint64 videoId, const bool allow, QString* error)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("UPDATE videos SET allow_duplicate=? WHERE id=? AND status IN "
                                 "('queued','downloading','ready','failed','cancelled')"));
    query.addBindValue(allow ? 1 : 0);
    query.addBindValue(videoId);
    return completeUpdate(query, error);
}

std::optional<VideoRecord> AppDatabase::video(const qint64 videoId) const
{
    return firstVideoForQuery(QStringLiteral("WHERE v.id=?"), {videoId});
}

std::optional<VideoRecord> AppDatabase::nextVideoForDownload() const
{
    return firstVideoForQuery(QStringLiteral("WHERE v.status='queued'"));
}

std::optional<VideoRecord> AppDatabase::nextVideoForUpload(const QDateTime& nowUtc) const
{
    return firstVideoForQuery(
        QStringLiteral("WHERE v.status='ready' AND v.approved=1 AND (v.force_publish=1 OR "
                       "v.scheduled_at_utc='' OR v.scheduled_at_utc<=?)"),
        {isoTime(nowUtc)});
}

std::optional<VideoRecord> AppDatabase::nextVideoForServerSubmission() const
{
    return firstVideoForQuery(QStringLiteral("WHERE v.status='ready' AND v.approved=1"));
}

QList<VideoRecord> AppDatabase::activeVideos() const
{
    return videosForQuery(QStringLiteral("WHERE v.status IN %1").arg(ActiveStatusSql));
}

QList<VideoRecord> AppDatabase::failedVideos() const
{
    return videosForQuery(QStringLiteral("WHERE v.status IN ('failed','delivery_unknown')"), {},
                          QStringLiteral("requested_at_utc DESC"));
}

QList<VideoRecord> AppDatabase::sentVideos(const int limit) const
{
    return videosForQuery(QStringLiteral("WHERE v.status='sent'"), {},
                          QStringLiteral("published_at_utc DESC"), limit);
}

QDateTime AppDatabase::latestPlannedTime(const qint64 channelId) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT MAX(scheduled_at_utc) FROM videos WHERE channel_id=? AND approved=1 AND status IN "
        "('queued','downloading','ready','uploading','server_scheduled')"));
    query.addBindValue(channelId);
    return query.exec() && query.next() ? dateTimeFromDatabase(query.value(0)) : QDateTime{};
}

QDateTime AppDatabase::latestPublishedTime(const qint64 channelId) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT MAX(published_at_utc) FROM videos WHERE channel_id=? AND status='sent'"));
    query.addBindValue(channelId);
    return query.exec() && query.next() ? dateTimeFromDatabase(query.value(0)) : QDateTime{};
}

QDateTime AppDatabase::nextReadyPublicationTime() const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT MIN(scheduled_at_utc) FROM videos WHERE status='ready' AND approved=1 AND "
        "force_publish=0 AND scheduled_at_utc<>''"));
    return query.exec() && query.next() ? dateTimeFromDatabase(query.value(0)) : QDateTime{};
}

bool AppDatabase::recoverInterruptedVideos(QString* error)
{
    if (!m_database.transaction())
        return false;
    if (!execute(QStringLiteral("UPDATE videos SET status='delivery_unknown',next_retry_at_utc='',"
                                "error_category='delivery_unknown',error='The application stopped "
                                "during upload. Check Telegram before retrying.' "
                                "WHERE status='uploading'"),
                 error) ||
        !execute(QStringLiteral(
                     "UPDATE videos SET status='queued',progress=0,error='',local_file_path='' "
                     "WHERE status='downloading'"),
                 error) ||
        !m_database.commit())
    {
        m_database.rollback();
        return false;
    }
    return true;
}

bool AppDatabase::markDownloading(const qint64 videoId, QString* error)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE videos SET status='downloading',progress=0,error='',next_retry_at_utc='' WHERE "
        "id=? AND status='queued'"));
    query.addBindValue(videoId);
    if (!query.exec() || query.numRowsAffected() != 1)
    {
        if (error != nullptr)
        {
            *error = queryError(query);
        }
        return false;
    }
    return true;
}

bool AppDatabase::updateDownloadProgress(const qint64 videoId, const double progress,
                                         QString* error)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("UPDATE videos SET progress=? WHERE id=?"));
    query.addBindValue(std::clamp(progress, 0.0, 100.0));
    query.addBindValue(videoId);
    if (!query.exec())
    {
        if (error != nullptr)
        {
            *error = queryError(query);
        }
        return false;
    }
    return true;
}

bool AppDatabase::updateMetadata(const qint64 videoId, const QString& title, const QString& author,
                                 const int durationSeconds, const QString& thumbnailPath,
                                 QString* error)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE videos SET title=?,author=?,duration_seconds=?,thumbnail_path=? WHERE id=?"));
    query.addBindValue(title.left(500));
    query.addBindValue(author.left(250));
    query.addBindValue(std::max(0, durationSeconds));
    query.addBindValue(thumbnailPath);
    query.addBindValue(videoId);
    if (!query.exec())
    {
        if (error != nullptr)
        {
            *error = queryError(query);
        }
        return false;
    }
    return true;
}

bool AppDatabase::markReady(const qint64 videoId, const QString& localFilePath,
                            const qint64 fileSize, QString* error)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE videos SET status='ready',local_file_path=?,file_size=?,progress=100,error='' "
        "WHERE id=? AND status IN ('queued','downloading','ready')"));
    query.addBindValue(localFilePath);
    query.addBindValue(fileSize);
    query.addBindValue(videoId);
    if (!query.exec() || query.numRowsAffected() != 1)
    {
        if (error != nullptr)
        {
            *error = queryError(query);
        }
        return false;
    }
    return true;
}

bool AppDatabase::markUploading(const qint64 videoId, QString* error)
{
    if (hasDuplicateSourceVideo(videoId))
    {
        if (error)
            *error =
                QStringLiteral("This video has already been queued or published in this channel.");
        return false;
    }
    QSqlQuery query(m_database);
    query.prepare(
        QStringLiteral("UPDATE videos SET status='uploading',error='',next_retry_at_utc='' WHERE "
                       "id=? AND status='ready' AND approved=1"));
    query.addBindValue(videoId);
    return completeUpdate(query, error);
}

bool AppDatabase::markSent(const qint64 videoId, const QDateTime& publishedAtUtc, QString* error)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("UPDATE videos SET "
                                 "status='sent',published_at_utc=?,force_publish=0,error='',next_"
                                 "retry_at_utc='',error_category='',"
                                 "local_file_path='' WHERE id=? AND status IN "
                                 "('uploading','delivery_unknown','server_scheduled')"));
    query.addBindValue(isoTime(publishedAtUtc));
    query.addBindValue(videoId);
    if (!query.exec() || query.numRowsAffected() != 1)
    {
        if (error != nullptr)
        {
            *error = queryError(query);
        }
        return false;
    }
    return true;
}

bool AppDatabase::markServerScheduled(const qint64 videoId, const QString& tdChatId,
                                      const QString& tdMessageId, QString* error)
{
    if (tdChatId.trimmed().isEmpty() || tdMessageId.trimmed().isEmpty())
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("TDLib did not return the scheduled message identifiers.");
        }
        return false;
    }
    QSqlQuery query(m_database);
    query.prepare(
        QStringLiteral("UPDATE videos SET status='server_scheduled',publisher_mode='tdlib',"
                       "td_chat_id=?,td_message_id=?,force_publish=0,error='',progress=100,"
                       "local_file_path='',next_retry_at_utc='',error_category='' WHERE id=? AND "
                       "status IN ('uploading','delivery_unknown')"));
    query.addBindValue(tdChatId);
    query.addBindValue(tdMessageId);
    query.addBindValue(videoId);
    if (!query.exec() || query.numRowsAffected() != 1)
    {
        if (error != nullptr)
        {
            *error = query.lastError().isValid()
                         ? queryError(query)
                         : QStringLiteral("The video is no longer being uploaded.");
        }
        return false;
    }
    return true;
}

bool AppDatabase::markServerPublished(const qint64 videoId, const QString& tdChatId,
                                      const QString& tdMessageId, const QDateTime& publishedAtUtc,
                                      QString* error)
{
    if (tdChatId.trimmed().isEmpty() || tdMessageId.trimmed().isEmpty() ||
        !publishedAtUtc.isValid())
    {
        if (error)
            *error = QStringLiteral(
                "Confirmed publication requires valid remote identifiers and a timestamp.");
        return false;
    }
    QSqlQuery query(m_database);
    query.prepare(
        QStringLiteral("UPDATE videos SET status='sent',publisher_mode='tdlib',td_chat_id=?,"
                       "td_message_id=?,published_at_utc=?,force_publish=0,error='',progress=100,"
                       "local_file_path='',next_retry_at_utc='',error_category='' WHERE id=? AND "
                       "status IN ('uploading','server_scheduled','delivery_unknown')"));
    query.addBindValue(tdChatId);
    query.addBindValue(tdMessageId);
    query.addBindValue(isoTime(publishedAtUtc));
    query.addBindValue(videoId);
    if (!query.exec() || query.numRowsAffected() != 1)
    {
        if (error != nullptr)
        {
            *error = queryError(query);
        }
        return false;
    }
    return true;
}

bool AppDatabase::markBotPublished(const qint64 videoId, const QString& chatId,
                                   const QString& messageId, const QDateTime& publishedAtUtc,
                                   QString* error)
{
    if (chatId.trimmed().isEmpty() || messageId.trimmed().isEmpty() || !publishedAtUtc.isValid())
    {
        if (error)
            *error = QStringLiteral(
                "Confirmed publication requires valid remote identifiers and a timestamp.");
        return false;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE videos SET status='sent',publisher_mode='local',td_chat_id=?,td_message_id=?,"
        "published_at_utc=?,force_publish=0,error='',progress=100,local_file_path='',"
        "next_retry_at_utc='',error_category='' WHERE id=? AND status IN "
        "('uploading','delivery_unknown')"));
    query.addBindValue(chatId);
    query.addBindValue(messageId);
    query.addBindValue(isoTime(publishedAtUtc));
    query.addBindValue(videoId);
    return completeUpdate(query, error);
}

QList<VideoRecord> AppDatabase::serverVideosForReconciliation() const
{
    return videosForQuery(QStringLiteral(
        "WHERE v.publisher_mode='tdlib' AND v.status IN ('server_scheduled','delivery_unknown') "
        "AND v.td_chat_id<>'' AND v.td_message_id<>''"));
}

bool AppDatabase::updateServerSchedule(const qint64 videoId, const QDateTime& scheduledAtUtc,
                                       QString* error)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("UPDATE videos SET scheduled_at_utc=?,force_publish=0,error='' "
                                 "WHERE id=? AND status='server_scheduled'"));
    query.addBindValue(isoTime(scheduledAtUtc));
    query.addBindValue(videoId);
    if (!query.exec() || query.numRowsAffected() != 1)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("The server-scheduled video cannot be updated.");
        }
        return false;
    }
    return true;
}

bool AppDatabase::finalizeElapsedServerSchedules(const QDateTime& nowUtc, QString* error)
{
    Q_UNUSED(nowUtc)
    Q_UNUSED(error)
    return isOpen();
}

bool AppDatabase::markFailed(const qint64 videoId, const QString& message, QString* error)
{
    return updateVideoStatus(videoId, QStringLiteral("failed"), message, true, error);
}

bool AppDatabase::markDeliveryUnknown(const qint64 videoId, const QString& message, QString* error)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE videos SET status='delivery_unknown',error=?,error_category='delivery_unknown',"
        "next_retry_at_utc='',force_publish=0 WHERE id=? AND status IN "
        "('uploading','server_scheduled','delivery_unknown')"));
    query.addBindValue(message.left(2000));
    query.addBindValue(videoId);
    return completeUpdate(query, error);
}

bool AppDatabase::resolveDeliveryUnknown(const qint64 videoId, const bool published, QString* error)
{
    QSqlQuery query(m_database);
    query.prepare(
        published
            ? QStringLiteral(
                  "UPDATE videos SET "
                  "status='sent',published_at_utc=?,error='',error_category='',next_retry_at_utc=''"
                  ","
                  "local_file_path='',force_publish=0 WHERE id=? AND status='delivery_unknown'")
            : QStringLiteral("UPDATE videos SET status='failed',error='User confirmed the message "
                             "was not published.',error_category='manual_retry',"
                             "next_retry_at_utc='',publisher_mode='local',td_chat_id='',td_message_"
                             "id='',force_publish=0 WHERE id=? AND status='delivery_unknown'"));
    if (published)
        query.addBindValue(isoTime(QDateTime::currentDateTimeUtc()));
    query.addBindValue(videoId);
    return completeUpdate(query, error);
}

bool AppDatabase::scheduleRetry(const qint64 videoId, const QDateTime& whenUtc,
                                const QString& category, QString* error)
{
    if (!whenUtc.isValid())
    {
        if (error)
            *error = QStringLiteral("Invalid retry date and time.");
        return false;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE videos SET next_retry_at_utc=?,error_category=? WHERE id=? AND status='failed'"));
    query.addBindValue(isoTime(whenUtc));
    query.addBindValue(category.isNull() ? QStringLiteral("") : category.left(80));
    query.addBindValue(videoId);
    return completeUpdate(query, error);
}

bool AppDatabase::retryDueVideos(const QDateTime& nowUtc, QString* error)
{
    if (!nowUtc.isValid())
        return false;
    // Read failures must not turn into successful, empty recovery passes.
    QSqlQuery due(m_database);
    due.prepare(QStringLiteral("SELECT id FROM videos WHERE status='failed' AND "
                               "next_retry_at_utc<>'' AND next_retry_at_utc<=?"));
    due.addBindValue(isoTime(nowUtc));
    if (!due.exec())
    {
        if (error)
            *error = queryError(due);
        return false;
    }
    QList<qint64> ids;
    while (due.next())
        ids.append(due.value(0).toLongLong());
    due.finish();
    for (const qint64 id : ids)
    {
        if (!retryVideo(id, error))
            return false;
    }
    return true;
}

bool AppDatabase::setFailureCategory(const qint64 videoId, const QString& category, QString* error)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("UPDATE videos SET error_category=? WHERE id=?"));
    query.addBindValue(category.isNull() ? QStringLiteral("") : category.left(80));
    query.addBindValue(videoId);
    return completeUpdate(query, error);
}

bool AppDatabase::appendVideoEvent(const qint64 videoId, const QString& category,
                                   const QString& message, QString* error)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO video_events(video_id,created_at_utc,category,message) VALUES(?,?,?,?)"));
    query.addBindValue(videoId > 0 ? QVariant(videoId) : QVariant(QMetaType::fromType<qint64>()));
    query.addBindValue(isoTime(QDateTime::currentDateTimeUtc()));
    query.addBindValue(category.isNull() ? QStringLiteral("") : category.left(80));
    query.addBindValue(message.isNull() ? QStringLiteral("") : message.left(2000));
    return completeUpdate(query, error);
}

QVariantList AppDatabase::videoEvents(const qint64 videoId) const
{
    QVariantList result;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT id,created_at_utc,category,message FROM video_events "
                                 "WHERE video_id=? ORDER BY id DESC LIMIT 200"));
    query.addBindValue(videoId);
    if (!query.exec())
        return {};
    while (query.next())
        result.append(QVariantMap{
            {QStringLiteral("id"), query.value(0)},
            {QStringLiteral("videoId"), videoId},
            {QStringLiteral("createdAtIso"), query.value(1)},
            {QStringLiteral("createdAt"), displayTime(dateTimeFromDatabase(query.value(1)))},
            {QStringLiteral("time"), displayTime(dateTimeFromDatabase(query.value(1)))},
            {QStringLiteral("level"), query.value(2)},
            {QStringLiteral("category"), query.value(2)},
            {QStringLiteral("message"), query.value(3)}});
    return result;
}

QVariantList AppDatabase::recentEvents(const int limit) const
{
    QVariantList result;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT id,video_id,created_at_utc,category,message FROM "
                                 "video_events ORDER BY id DESC LIMIT ?"));
    query.addBindValue(std::clamp(limit, 1, 5000));
    if (!query.exec())
        return {};
    while (query.next())
        result.append(QVariantMap{
            {QStringLiteral("id"), query.value(0)},
            {QStringLiteral("videoId"), query.value(1)},
            {QStringLiteral("createdAtIso"), query.value(2)},
            {QStringLiteral("createdAt"), displayTime(dateTimeFromDatabase(query.value(2)))},
            {QStringLiteral("time"), displayTime(dateTimeFromDatabase(query.value(2)))},
            {QStringLiteral("level"), query.value(3)},
            {QStringLiteral("category"), query.value(3)},
            {QStringLiteral("message"), query.value(4)}});
    return result;
}

bool AppDatabase::clearEvents(QString* error)
{
    return execute(QStringLiteral("DELETE FROM video_events"), error);
}

QList<qint64> AppDatabase::cacheCleanupCandidates() const
{
    QList<qint64> result;
    QSqlQuery query(m_database);
    if (!query.exec(
            QStringLiteral("SELECT id FROM videos WHERE status IN ('sent','cancelled') OR "
                           "(status='server_scheduled' AND td_chat_id<>'' AND td_message_id<>'')")))
        return {};
    while (query.next())
        result.append(query.value(0).toLongLong());
    return result;
}

bool AppDatabase::requeueForDownload(const qint64 videoId, QString* error)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE videos SET status='queued',local_file_path='',file_size=0,progress=0,"
        "error='',force_publish=0,next_retry_at_utc='' WHERE id=? AND status IN "
        "('queued','downloading','ready','failed','cancelled')"));
    query.addBindValue(videoId);
    if (!query.exec() || query.numRowsAffected() != 1)
    {
        if (error != nullptr)
        {
            *error = query.lastError().isValid()
                         ? queryError(query)
                         : QStringLiteral("The video cannot be downloaded again.");
        }
        return false;
    }
    return true;
}

bool AppDatabase::cancelVideo(const qint64 videoId, QString* error)
{
    return updateVideoStatus(videoId, QStringLiteral("cancelled"), {}, false, error);
}

bool AppDatabase::retryVideo(const qint64 videoId, QString* error)
{
    if (hasDuplicateSourceVideo(videoId))
    {
        if (error)
            *error =
                QStringLiteral("Enable repeat publication before retrying this duplicate video.");
        return false;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE videos SET status=CASE WHEN local_file_path<>'' THEN 'ready' ELSE 'queued' END,"
        "error='',next_retry_at_utc='',error_category='',progress=CASE WHEN local_file_path<>'' "
        "THEN 100 ELSE 0 END,force_publish=0,"
        "queue_order=COALESCE((SELECT MAX(queue_order)+1 FROM videos),1) WHERE id=? "
        "AND status IN ('failed','cancelled')"));
    query.addBindValue(videoId);
    if (!query.exec() || query.numRowsAffected() != 1)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Only failed or cancelled videos can be retried.");
        }
        return false;
    }
    return true;
}

bool AppDatabase::publishVideoNow(const qint64 videoId, QString* error)
{
    QSqlQuery query(m_database);
    query.prepare(
        QStringLiteral("UPDATE videos SET force_publish=1,scheduled_at_utc=?,"
                       "status=CASE WHEN status='failed' AND local_file_path<>'' THEN 'ready' "
                       "WHEN status IN ('failed','cancelled') THEN 'queued' ELSE status "
                       "END,error='',next_retry_at_utc='' "
                       "WHERE id=? AND approved=1 AND status IN "
                       "('queued','downloading','ready','failed','cancelled')"));
    query.addBindValue(isoTime(QDateTime::currentDateTimeUtc()));
    query.addBindValue(videoId);
    if (!query.exec() || query.numRowsAffected() != 1)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("The selected video cannot be published now.");
        }
        return false;
    }
    return true;
}

bool AppDatabase::moveVideo(const qint64 videoId, const int direction, QString* error)
{
    if (direction != -1 && direction != 1)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Direction must be -1 or 1.");
        }
        return false;
    }
    const auto current = video(videoId);
    static const QStringList activeStatuses{QStringLiteral("queued"), QStringLiteral("downloading"),
                                            QStringLiteral("ready"), QStringLiteral("uploading")};
    if (!current.has_value() || !activeStatuses.contains(current->status))
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Video is not in the active queue.");
        }
        return false;
    }

    QSqlQuery neighbor(m_database);
    neighbor.prepare(
        direction < 0
            ? QStringLiteral("SELECT id,queue_order FROM videos WHERE status IN "
                             "('queued','downloading','ready','uploading') AND queue_order<? "
                             "ORDER BY queue_order DESC LIMIT 1")
            : QStringLiteral("SELECT id,queue_order FROM videos WHERE status IN "
                             "('queued','downloading','ready','uploading') AND queue_order>? "
                             "ORDER BY queue_order ASC LIMIT 1"));
    neighbor.addBindValue(current->queueOrder);
    if (!neighbor.exec() || !neighbor.next())
    {
        return true;
    }
    const qint64 neighborId = neighbor.value(0).toLongLong();
    const qint64 neighborOrder = neighbor.value(1).toLongLong();

    if (!m_database.transaction())
    {
        return false;
    }
    QSqlQuery first(m_database);
    first.prepare(QStringLiteral("UPDATE videos SET queue_order=? WHERE id=?"));
    first.addBindValue(neighborOrder);
    first.addBindValue(videoId);
    QSqlQuery second(m_database);
    second.prepare(QStringLiteral("UPDATE videos SET queue_order=? WHERE id=?"));
    second.addBindValue(current->queueOrder);
    second.addBindValue(neighborId);
    if (!first.exec() || !second.exec() || !m_database.commit())
    {
        m_database.rollback();
        if (error != nullptr)
        {
            *error = first.lastError().isValid() ? queryError(first) : queryError(second);
        }
        return false;
    }
    return true;
}

bool AppDatabase::setVideoSchedule(const qint64 videoId, const QDateTime& scheduledAtUtc,
                                   QString* error)
{
    if (!scheduledAtUtc.isValid())
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Invalid publication date and time.");
        }
        return false;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("UPDATE videos SET scheduled_at_utc=?,force_publish=0 WHERE id=? "
                                 "AND status IN ('queued','downloading','ready')"));
    query.addBindValue(isoTime(scheduledAtUtc));
    query.addBindValue(videoId);
    if (!query.exec() || query.numRowsAffected() != 1)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("This video's publication time cannot be changed.");
        }
        return false;
    }
    return true;
}

bool AppDatabase::setVideoChannel(const qint64 videoId, const qint64 channelId, QString* error)
{
    const auto current = video(videoId);
    if (!current)
        return false;
    const auto duplicate = duplicateVideo(
        current->sourceVideoId.isEmpty() ? current->url : current->sourceVideoId, channelId);
    if (!current->allowDuplicate && duplicate && duplicate->id != videoId)
    {
        if (error)
            *error = QStringLiteral(
                "This video is already queued or published in the destination channel.");
        return false;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("UPDATE videos SET channel_id=? WHERE id=? AND status IN "
                                 "('queued','downloading','ready') AND EXISTS(SELECT 1 FROM "
                                 "channels WHERE id=? AND enabled=1)"));
    query.addBindValue(channelId);
    query.addBindValue(videoId);
    query.addBindValue(channelId);
    if (!query.exec() || query.numRowsAffected() != 1)
    {
        if (error != nullptr)
        {
            *error = queryError(query);
        }
        return false;
    }
    return true;
}

bool AppDatabase::updateVideoStatus(const qint64 videoId, const QString& status,
                                    const QString& errorMessage, const bool incrementRetry,
                                    QString* error)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE videos SET "
        "status=?,error=?,retry_count=retry_count+?,force_publish=0,next_retry_at_utc='' "
        "WHERE id=? AND status NOT IN ('sent','delivery_unknown')"));
    query.addBindValue(status);
    query.addBindValue(errorMessage.left(2000));
    query.addBindValue(incrementRetry ? 1 : 0);
    query.addBindValue(videoId);
    if (!query.exec() || query.numRowsAffected() != 1)
    {
        if (error != nullptr)
        {
            *error = queryError(query);
        }
        return false;
    }
    return true;
}

std::optional<VideoRecord> AppDatabase::firstVideoForQuery(const QString& whereClause,
                                                           const QVariantList& bindings) const
{
    const QList<VideoRecord> records = videosForQuery(
        whereClause, bindings, QStringLiteral("force_publish DESC, queue_order ASC"), 1);
    return records.isEmpty() ? std::nullopt : std::optional<VideoRecord>(records.first());
}

QList<VideoRecord> AppDatabase::videosForQuery(const QString& whereClause,
                                               const QVariantList& bindings, const QString& orderBy,
                                               const int limit) const
{
    QList<VideoRecord> result;
    QString sql = VideoSelect + whereClause;
    if (!orderBy.isEmpty())
    {
        sql += QStringLiteral(" ORDER BY ") + orderBy;
    }
    if (limit > 0)
    {
        sql += QStringLiteral(" LIMIT %1").arg(limit);
    }
    QSqlQuery query(m_database);
    query.prepare(sql);
    for (const QVariant& binding : bindings)
    {
        query.addBindValue(binding);
    }
    if (!query.exec())
    {
        qCWarning(logApp) << "Could not query videos:" << queryError(query);
        return result;
    }
    while (query.next())
    {
        result.append(videoFromQuery(query));
    }
    return result;
}

VideoRecord AppDatabase::videoFromQuery(const QSqlQuery& query)
{
    VideoRecord video;
    video.id = query.value(0).toLongLong();
    video.url = query.value(1).toString();
    video.channelId = query.value(2).toLongLong();
    video.channelName = query.value(3).toString();
    video.telegramChannelId = query.value(4).toString();
    video.status = query.value(5).toString();
    video.requestChatId = query.value(6).toString().toLongLong();
    video.requestUserId = query.value(7).toString().toLongLong();
    video.requestedAtUtc = dateTimeFromDatabase(query.value(8));
    video.scheduledAtUtc = dateTimeFromDatabase(query.value(9));
    video.publishedAtUtc = dateTimeFromDatabase(query.value(10));
    video.title = query.value(11).toString();
    video.author = query.value(12).toString();
    video.thumbnailPath = query.value(13).toString();
    video.localFilePath = query.value(14).toString();
    video.error = query.value(15).toString();
    video.durationSeconds = query.value(16).toInt();
    video.fileSize = query.value(17).toLongLong();
    video.retryCount = query.value(18).toInt();
    video.queueOrder = query.value(19).toLongLong();
    video.progress = query.value(20).toDouble();
    video.forcePublish = query.value(21).toBool();
    video.publisherMode = query.value(22).toString();
    video.tdChatId = query.value(23).toString();
    video.tdMessageId = query.value(24).toString();
    video.caption = query.value(25).toString();
    video.approved = query.value(26).toBool();
    video.sourceVideoId = query.value(27).toString();
    video.allowDuplicate = query.value(28).toBool();
    video.nextRetryAtUtc = dateTimeFromDatabase(query.value(29));
    video.errorCategory = query.value(30).toString();
    video.channelTimeZoneId = query.value(31).toString();
    return video;
}

QVariantMap AppDatabase::videoEntry(const VideoRecord& video)
{
    const QTimeZone zone = video.channelTimeZoneId.isEmpty()
                               ? QTimeZone::systemTimeZone()
                               : QTimeZone(video.channelTimeZoneId.toUtf8());
    const QDateTime channelTime = video.scheduledAtUtc.toTimeZone(zone);
    const int minutes = video.durationSeconds / 60;
    const int seconds = video.durationSeconds % 60;
    return {
        {QStringLiteral("id"), video.id},
        {QStringLiteral("url"), video.url},
        {QStringLiteral("channelId"), video.channelId},
        {QStringLiteral("channelName"), video.channelName},
        {QStringLiteral("telegramChannelId"), video.telegramChannelId},
        {QStringLiteral("status"), video.status},
        {QStringLiteral("requestedAt"), displayTime(video.requestedAtUtc)},
        {QStringLiteral("scheduledAt"), displayTime(video.scheduledAtUtc)},
        {QStringLiteral("scheduledAtIso"), isoTime(video.scheduledAtUtc)},
        {QStringLiteral("publishedAt"), displayTime(video.publishedAtUtc)},
        {QStringLiteral("title"),
         video.title.isEmpty() ? QStringLiteral("TikTok video") : video.title},
        {QStringLiteral("author"), video.author},
        {QStringLiteral("duration"),
         video.durationSeconds > 0
             ? QStringLiteral("%1:%2").arg(minutes).arg(seconds, 2, 10, QLatin1Char('0'))
             : QString{}},
        {QStringLiteral("fileSize"), video.fileSize},
        {QStringLiteral("fileSizeMiB"),
         video.fileSize > 0
             ? QString::number(static_cast<double>(video.fileSize) / 1048576.0, 'f', 1)
             : QString{}},
        {QStringLiteral("thumbnail"), video.thumbnailPath.isEmpty()
                                          ? QString{}
                                          : QUrl::fromLocalFile(video.thumbnailPath).toString()},
        {QStringLiteral("localFilePath"), video.localFilePath},
        {QStringLiteral("localFileAvailable"),
         !video.localFilePath.isEmpty() && QFileInfo::exists(video.localFilePath)},
        {QStringLiteral("error"), video.error},
        {QStringLiteral("retryCount"), video.retryCount},
        {QStringLiteral("progress"), video.progress},
        {QStringLiteral("forcePublish"), video.forcePublish},
        {QStringLiteral("publisherMode"), video.publisherMode},
        {QStringLiteral("tdChatId"), video.tdChatId},
        {QStringLiteral("tdMessageId"), video.tdMessageId},
        {QStringLiteral("caption"), video.caption},
        {QStringLiteral("approved"), video.approved},
        {QStringLiteral("sourceVideoId"), video.sourceVideoId},
        {QStringLiteral("allowDuplicate"), video.allowDuplicate},
        {QStringLiteral("nextRetryAtIso"), isoTime(video.nextRetryAtUtc)},
        {QStringLiteral("errorCategory"), video.errorCategory},
        {QStringLiteral("scheduledAtChannelIso"), channelTime.toString(Qt::ISODate)},
        {QStringLiteral("scheduledDate"), channelTime.date().toString(Qt::ISODate)},
        {QStringLiteral("scheduledTime"), channelTime.time().toString(QStringLiteral("HH:mm"))},
    };
}

QVariantList AppDatabase::channelEntries() const
{
    QVariantList result;
    for (const ChannelRecord& channel : channels())
    {
        result.append(QVariantMap{
            {QStringLiteral("id"), channel.id},
            {QStringLiteral("name"), channel.name},
            {QStringLiteral("telegramId"), channel.telegramId},
            {QStringLiteral("isDefault"), channel.isDefault},
            {QStringLiteral("timeZoneId"), channel.timeZoneId},
            {QStringLiteral("captionTemplate"), channel.captionTemplate},
        });
    }
    return result;
}

QVariantList AppDatabase::scheduleEntries() const
{
    QVariantList result;
    const QList<ChannelRecord> channelRecords = channels(true);
    for (const ScheduleSlotRecord& slot : scheduleSlots())
    {
        const auto channel =
            std::find_if(channelRecords.cbegin(), channelRecords.cend(),
                         [slot](const ChannelRecord& item) { return item.id == slot.channelId; });
        result.append(QVariantMap{
            {QStringLiteral("id"), slot.id},
            {QStringLiteral("channelId"), slot.channelId},
            {QStringLiteral("channelName"),
             channel == channelRecords.cend() ? QString{} : channel->name},
            {QStringLiteral("dayOfWeek"), slot.dayOfWeek},
            {QStringLiteral("time"), slot.time},
        });
    }
    return result;
}

QVariantList AppDatabase::activeVideoEntries() const
{
    QVariantList result;
    for (const VideoRecord& record : activeVideos())
    {
        result.append(videoEntry(record));
    }
    return result;
}

QVariantList AppDatabase::failedVideoEntries() const
{
    QVariantList result;
    for (const VideoRecord& record : failedVideos())
    {
        result.append(videoEntry(record));
    }
    return result;
}

QVariantList AppDatabase::sentVideoEntries(const int limit) const
{
    QVariantList result;
    for (const VideoRecord& record : sentVideos(limit))
    {
        result.append(videoEntry(record));
    }
    return result;
}

bool AppDatabase::exportBackup(const QString& filePath, QString* error) const
{
    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("TikTokTelegramBotBackup"));
    root.insert(QStringLiteral("version"), 3);
    root.insert(QStringLiteral("createdAtUtc"), isoTime(QDateTime::currentDateTimeUtc()));

    const auto exportTable = [this, error](const QString& sql, QJsonArray* rows)
    {
        QSqlQuery query(m_database);
        if (!query.exec(sql))
        {
            if (error != nullptr)
            {
                *error = queryError(query);
            }
            return false;
        }
        while (query.next())
        {
            rows->append(queryRowToJson(query));
        }
        return true;
    };
    QJsonArray channels;
    QJsonArray scheduleSlots;
    QJsonArray videos;
    if (!exportTable(
            QStringLiteral(
                "SELECT id,name,telegram_id,is_default,enabled,time_zone_id,caption_template FROM "
                "channels"),
            &channels) ||
        !exportTable(QStringLiteral("SELECT id,channel_id,day_of_week,time_text,enabled "
                                    "FROM schedule_slots"),
                     &scheduleSlots) ||
        !exportTable(
            QStringLiteral(
                "SELECT id,url,channel_id,status,request_chat_id,request_user_id,"
                "requested_at_utc,scheduled_at_utc,published_at_utc,title,author,"
                "thumbnail_path,local_file_path,error,duration_seconds,file_size,"
                "retry_count,queue_order,progress,force_publish,publisher_mode,"
                "td_chat_id,td_message_id,caption,approved,source_video_id,allow_duplicate,"
                "next_retry_at_utc,error_category FROM videos"),
            &videos))
    {
        return false;
    }
    root.insert(QStringLiteral("channels"), channels);
    root.insert(QStringLiteral("scheduleSlots"), scheduleSlots);
    root.insert(QStringLiteral("videos"), videos);

    QSaveFile output(filePath);
    if (!output.open(QIODevice::WriteOnly) ||
        output.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0 || !output.commit())
    {
        if (error != nullptr)
        {
            *error = output.errorString();
        }
        return false;
    }
    return true;
}

bool AppDatabase::importBackup(const QString& filePath, QString* error)
{
    QFile input(filePath);
    if (!input.open(QIODevice::ReadOnly))
    {
        if (error != nullptr)
        {
            *error = input.errorString();
        }
        return false;
    }
    if (input.size() > 64LL * 1024 * 1024)
    {
        if (error)
            *error = QStringLiteral("Backup exceeds the 64 MiB import limit.");
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(input.readAll(), &parseError);
    const QJsonObject root = document.object();
    if (parseError.error != QJsonParseError::NoError || !document.isObject() ||
        root.value(QStringLiteral("format")).toString() !=
            QStringLiteral("TikTokTelegramBotBackup") ||
        (root.value(QStringLiteral("version")).toInt() != 1 &&
         root.value(QStringLiteral("version")).toInt() != 2 &&
         root.value(QStringLiteral("version")).toInt() != 3) ||
        !root.value(QStringLiteral("channels")).isArray() ||
        !root.value(QStringLiteral("scheduleSlots")).isArray() ||
        !root.value(QStringLiteral("videos")).isArray())
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("The selected file is not a valid bot backup.");
        }
        return false;
    }

    if (!m_database.transaction())
    {
        if (error != nullptr)
        {
            *error = m_database.lastError().text();
        }
        return false;
    }
    const auto failImport = [this, error](const QString& message)
    {
        m_database.rollback();
        if (error != nullptr)
        {
            *error = message;
        }
        return false;
    };
    if (!execute(QStringLiteral("DELETE FROM videos"), error) ||
        !execute(QStringLiteral("DELETE FROM schedule_slots"), error) ||
        !execute(QStringLiteral("DELETE FROM channels"), error))
    {
        m_database.rollback();
        return false;
    }

    for (const QJsonValue& value : root.value(QStringLiteral("channels")).toArray())
    {
        const QJsonObject item = value.toObject();
        const QString telegramId = item.value(QStringLiteral("telegram_id")).toString();
        if (!validTelegramChannelId(telegramId))
        {
            return failImport(QStringLiteral("The backup contains an invalid channel ID."));
        }
        QSqlQuery query(m_database);
        query.prepare(QStringLiteral("INSERT INTO "
                                     "channels(id,name,telegram_id,is_default,enabled,time_zone_id,"
                                     "caption_template) VALUES(?,?,?,?,?,?,?)"));
        query.addBindValue(item.value(QStringLiteral("id")).toVariant());
        query.addBindValue(
            normalizedChannelName(item.value(QStringLiteral("name")).toString(), telegramId));
        query.addBindValue(telegramId);
        query.addBindValue(item.value(QStringLiteral("is_default")).toVariant().toInt());
        query.addBindValue(item.value(QStringLiteral("enabled")).toVariant().toInt());
        const QString zone =
            item.value(QStringLiteral("time_zone_id")).toString(QStringLiteral(""));
        if (!zone.isEmpty() && !QTimeZone(zone.toUtf8()).isValid())
            return failImport(QStringLiteral("The backup contains an invalid channel time zone."));
        query.addBindValue(zone);
        query.addBindValue(
            item.value(QStringLiteral("caption_template")).toString(QStringLiteral("")).left(1024));
        if (!query.exec())
        {
            return failImport(queryError(query));
        }
    }
    for (const QJsonValue& value : root.value(QStringLiteral("scheduleSlots")).toArray())
    {
        const QJsonObject item = value.toObject();
        bool dayValid = false;
        const int dayOfWeek =
            item.value(QStringLiteral("day_of_week")).toVariant().toInt(&dayValid);
        const QTime time = QTime::fromString(item.value(QStringLiteral("time_text")).toString(),
                                             QStringLiteral("HH:mm"));
        if (!dayValid || dayOfWeek < 0 || dayOfWeek > 7 || !time.isValid())
        {
            return failImport(QStringLiteral("The backup contains an invalid schedule slot."));
        }
        QSqlQuery query(m_database);
        query.prepare(QStringLiteral(
            "INSERT INTO schedule_slots(id,channel_id,day_of_week,time_text,enabled) "
            "VALUES(?,?,?,?,?)"));
        query.addBindValue(item.value(QStringLiteral("id")).toVariant());
        query.addBindValue(item.value(QStringLiteral("channel_id")).toVariant());
        query.addBindValue(dayOfWeek);
        query.addBindValue(time.toString(QStringLiteral("HH:mm")));
        query.addBindValue(item.value(QStringLiteral("enabled")).toVariant().toInt());
        if (!query.exec())
        {
            return failImport(queryError(query));
        }
    }
    const QString videoInsert = QStringLiteral(
        "INSERT INTO videos(id,url,channel_id,status,request_chat_id,request_user_id,"
        "requested_at_utc,scheduled_at_utc,published_at_utc,title,author,thumbnail_path,"
        "local_file_path,error,duration_seconds,file_size,retry_count,queue_order,progress,"
        "force_publish,caption,approved,source_video_id,allow_duplicate,next_retry_at_utc,error_"
        "category,"
        "publisher_mode,td_chat_id,td_message_id) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    for (const QJsonValue& value : root.value(QStringLiteral("videos")).toArray())
    {
        const QJsonObject item = value.toObject();
        const QString importedStatus = item.value(QStringLiteral("status")).toString();
        const QString status = (importedStatus == QStringLiteral("uploading") ||
                                importedStatus == QStringLiteral("delivery_unknown"))
                                   ? QStringLiteral("delivery_unknown")
                               : importedStatus == QStringLiteral("server_scheduled")
                                   ? QStringLiteral("server_scheduled")
                               : importedStatus == QStringLiteral("sent")
                                   ? QStringLiteral("sent")
                                   : (importedStatus == QStringLiteral("failed") ||
                                              importedStatus == QStringLiteral("cancelled")
                                          ? importedStatus
                                          : QStringLiteral("queued"));
        const QString url = item.value(QStringLiteral("url")).toString().trimmed();
        if (url.isEmpty() || url.size() > 4096)
        {
            return failImport(QStringLiteral("The backup contains an invalid video URL."));
        }
        QSqlQuery query(m_database);
        query.prepare(videoInsert);
        query.addBindValue(item.value(QStringLiteral("id")).toVariant());
        query.addBindValue(url);
        query.addBindValue(item.value(QStringLiteral("channel_id")).toVariant());
        query.addBindValue(status);
        query.addBindValue(item.value(QStringLiteral("request_chat_id")).toVariant());
        query.addBindValue(item.value(QStringLiteral("request_user_id")).toVariant());
        query.addBindValue(item.value(QStringLiteral("requested_at_utc")).toString());
        query.addBindValue(item.value(QStringLiteral("scheduled_at_utc")).toString());
        query.addBindValue(item.value(QStringLiteral("published_at_utc")).toString());
        query.addBindValue(item.value(QStringLiteral("title")).toString().left(500));
        query.addBindValue(item.value(QStringLiteral("author")).toString().left(250));
        // Cache paths are machine-local and must never be trusted from an imported file.
        query.addBindValue(QString{});
        query.addBindValue(QString{});
        query.addBindValue(item.value(QStringLiteral("error")).toString().left(2000));
        query.addBindValue(
            std::max(0, item.value(QStringLiteral("duration_seconds")).toVariant().toInt()));
        query.addBindValue(
            std::max<qint64>(0, item.value(QStringLiteral("file_size")).toVariant().toLongLong()));
        query.addBindValue(
            std::max(0, item.value(QStringLiteral("retry_count")).toVariant().toInt()));
        query.addBindValue(item.value(QStringLiteral("queue_order")).toVariant());
        query.addBindValue(status == QStringLiteral("sent") ? 100.0 : 0.0);
        query.addBindValue(0);
        query.addBindValue(
            item.value(QStringLiteral("caption")).toString(QStringLiteral("")).left(1024));
        query.addBindValue(item.contains(QStringLiteral("approved"))
                               ? item.value(QStringLiteral("approved")).toVariant().toBool()
                               : true);
        query.addBindValue(
            item.value(QStringLiteral("source_video_id")).toString(QStringLiteral("")).left(128));
        query.addBindValue(item.value(QStringLiteral("allow_duplicate")).toVariant().toBool());
        const QDateTime retryTime = QDateTime::fromString(
            item.value(QStringLiteral("next_retry_at_utc")).toString(), Qt::ISODateWithMs);
        query.addBindValue(status == QStringLiteral("failed") && retryTime.isValid()
                               ? isoTime(retryTime)
                               : QStringLiteral(""));
        query.addBindValue(
            item.value(QStringLiteral("error_category")).toString(QStringLiteral("")).left(80));
        query.addBindValue(
            item.value(QStringLiteral("publisher_mode")).toString(QStringLiteral("local")));
        query.addBindValue(
            item.value(QStringLiteral("td_chat_id")).toString(QStringLiteral("")).left(40));
        query.addBindValue(
            item.value(QStringLiteral("td_message_id")).toString(QStringLiteral("")).left(40));
        if (!query.exec())
        {
            return failImport(queryError(query));
        }
    }
    if (!m_database.commit())
    {
        return failImport(m_database.lastError().text());
    }
    return true;
}

QString AppDatabase::displayTime(const QDateTime& utcTime)
{
    return utcTime.isValid() ? utcTime.toLocalTime().toString(QStringLiteral("dd.MM.yyyy HH:mm"))
                             : QString{};
}

QString AppDatabase::normalizedChannelName(const QString& name, const QString& telegramId)
{
    const QString trimmedName = name.trimmed();
    return trimmedName.isEmpty() ? telegramId : trimmedName.left(120);
}

bool AppDatabase::validTelegramChannelId(const QString& telegramId)
{
    static const QRegularExpression usernamePattern(
        QStringLiteral(R"(^@[A-Za-z][A-Za-z0-9_]{4,31}$)"));
    if (usernamePattern.match(telegramId).hasMatch())
    {
        return true;
    }
    bool parsed = false;
    const qint64 numericId = telegramId.toLongLong(&parsed);
    return parsed && numericId < 0;
}
